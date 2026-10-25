#pragma once

// Private state of ClusterViewRenderer, shared by its translation units.

#include "ClusterViewRenderer.h"

#include "Culling/ClusterCullMath.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "Renderer.h"
#include "RenderTexture.h"
#include "RenderGraph/ClusterDrawBindings.h"
#include "RenderGraph/GpuBuffer.h"
#include "RenderGraph/GpuParams.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/Shader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gte {

// Releases a UI texture handle on destruction.
class ClusterViewUiHandle {
public:
    explicit ClusterViewUiHandle(Renderer& renderer) noexcept
        : m_renderer(renderer)
    {
    }
    ~ClusterViewUiHandle() { Reset(); }
    ClusterViewUiHandle(const ClusterViewUiHandle&) = delete;
    ClusterViewUiHandle& operator=(const ClusterViewUiHandle&) = delete;

    void Reset() noexcept
    {
        if (m_handle != UiTextureHandle::Invalid) {
            m_renderer.ReleaseUiTextureHandle(m_handle);
            m_handle = UiTextureHandle::Invalid;
        }
    }
    void Set(UiTextureHandle handle) noexcept
    {
        Reset();
        m_handle = handle;
    }
    UiTextureHandle Get() const noexcept { return m_handle; }

private:
    Renderer& m_renderer;
    UiTextureHandle m_handle = UiTextureHandle::Invalid;
};

// Fragment shaders of the viewer, each built with and without back-face culling.
enum class ClusterViewFragment : std::uint8_t { Clay, Tint, Dim, Id };

struct ClusterViewRenderer::Impl {
    // One survivor list: device buffers written by the cull shader plus host mirrors for read-back.
    enum class SetId : std::uint8_t { Main, Preview, StageA, StageB, Ghost };
    static constexpr std::size_t kSetCount = 5;

    struct CullSet {
        rg::GpuBuffer visible;      // uvec2 (instance, cluster) per survivor
        rg::GpuBuffer commands;     // 5 uints per survivor
        rg::GpuBuffer count;        // survivor count
        rg::GpuBuffer countHost;    // HostRead mirror of `count` (only sets whose count is read back)
        rg::GpuBuffer commandsHost; // HostRead mirror of `commands` (Main and Preview only)
        rg::GpuBuffer visibleHost;  // HostRead mirror of `visible` (Main and Preview only)
        rg::GpuParams<ClusterCullParams> params;
        std::uint32_t capacity = 0; // clusters the buffers were sized for
        bool mirrors = false;
    };

    // Graph handles of one CullSet for one graph build.
    struct GraphSet {
        SetId id = SetId::Main;
        rg::BufferHandle visible, commands, count, countHost, commandsHost, visibleHost;
    };

    // Color + depth target of one slot and its UI handle. The handle is declared last: it is released first.
    struct SlotTarget {
        explicit SlotTarget(Renderer& owner)
            : ui(owner)
        {
        }
        std::optional<RenderTexture> texture;
        ClusterViewUiHandle ui;
        int width = 0;
        int height = 0;
    };

    // Flags a run applies after the Hi-Z decision.
    struct AppliedCull {
        ClusterCullFlags flags = ClusterCullFlags::None;
        bool occlusionApplied = false;
        bool hizCut = false;
    };

    // What a run draws; absent for a cull-only run.
    struct DrawPlan {
        SlotTarget* target = nullptr;
        const ClusterViewFrame* frame = nullptr;
        const Pipeline* survivors = nullptr;
        const Pipeline* ghost = nullptr; // null: no ghost draw
    };

    struct RunResult {
        bool ok = false;
        std::string note;
        ClusterViewCounts counts;
        std::vector<std::uint32_t> survivors;
        bool occlusionApplied = false;
        bool hizCut = false;
        double milliseconds = 0;
    };

    explicit Impl(Renderer& owner);

    // Idles the GPU (best effort), then members release in reverse declaration order.
    ~Impl() { static_cast<void>(renderer.WaitForGpuIdle(BlockSite::Shutdown, "cluster viewer")); }

    // True once the GPU is idle. Anything the GPU may read must stay alive when this is false.
    bool DrainGpu(const char* detail) const noexcept
    {
        return renderer.WaitForGpuIdle(BlockSite::ResourceRelease, detail) == BlockOutcome::Completed;
    }

    // Reads the shader files, queries limits and makes the stand-in atlas. Throws on failure.
    void Build();

    // ---- Validation ----
    bool CheckSize(int width, int height, std::string& note) const;
    bool CheckCullCamera(const ClusterViewCull& cull, std::string& note) const;
    // Logs `message` unless it equals the previous one.
    void LogOnce(const std::string& message);

    // ---- Mesh ----
    void ReleaseMesh();

    // ---- Targets ----
    bool EnsureTarget(SlotTarget& slot, const char* name, const char* depthName, int width, int height);
    void ReleaseSlot(SlotTarget& slot);

    // ---- Cull sets ----
    CullSet& Set(SetId id) noexcept { return sets[static_cast<std::size_t>(id)]; }
    // `mirrors`: host copies of commands and survivors. `countRead`: host copy of the count.
    bool EnsureSet(SetId id, bool mirrors, bool countRead);
    GraphSet ImportSet(rg::RenderGraphBuilder& builder, SetId id);

    // ---- Hi-Z decision, the one place that decides ----
    AppliedCull ResolveFlags(const ClusterViewCull& cull) const;
    ClusterCullParams MakeParams(const ClusterViewCull& cull, ClusterCullFlags flags, bool useHiz) const;
    // Real atlas when `useHiz`, the 1x1 stand-in otherwise. Both are tracked owners of their layout.
    rg::TextureHandle ImportHizAtlas(rg::RenderGraphBuilder& builder, bool useHiz);

    // ---- Pass declaration (graph build time) ----
    bool DeclareFill(rg::RenderGraphBuilder& builder, const char* passName, rg::BufferHandle buffer);
    bool DeclareReadback(rg::RenderGraphBuilder& builder, const char* passName, rg::BufferHandle device,
        rg::BufferHandle host, std::uint64_t bytes);
    bool DeclareCull(rg::RenderGraphBuilder& builder, const GraphSet& set, rg::BufferHandle instances,
        rg::BufferHandle table, rg::TextureHandle hizAtlas, const ClusterCullParams& params);
    // Counted indirect draw of `set`. `bindingIndex` picks the descriptor set (one per draw of a graph).
    bool DeclareDraw(rg::RenderGraphBuilder& builder, const char* passName, const GraphSet& set,
        rg::BufferHandle instances, rg::TextureHandle target, bool depthOnly, const Pipeline& pipeline,
        const Mat4& viewProjection, std::size_t bindingIndex, std::optional<std::array<float, 4>> clearColor,
        std::optional<float> clearDepth);
    // Count reset and a flags-None cull of every cluster into the Ghost set. Needs a ready cull shader.
    bool DeclareCullAll(rg::RenderGraphBuilder& builder, const Mat4& viewProjection, const Vec3& eye,
        const char* resetName, GraphSet& set, rg::BufferHandle& instances);

    // ---- Execution ----
    // Runs `build` in the private graph, makes `finalize` samplable and waits. False and `note` on failure.
    template <typename BuildFn>
    bool Execute(BuildFn&& build, std::span<RenderTexture* const> finalize, std::string& note);

    const Pipeline* ViewPipeline(ClusterViewFragment fragment, bool cullBackFaces);

    // Cull (stage runs + final run), optional draw and read-back; fills counts and survivors.
    RunResult RunPlan(
        SetId finalSet, const ClusterViewCull& cull, const AppliedCull& applied, const DrawPlan* draw, bool readSurvivors);

    Renderer& renderer;

    // ---- Device facts ----
    std::uint32_t maxClusters = 0;
    std::uint32_t maxImageDimension = 0;

    // ---- Shaders ----
    rg::Shader cullShader{ "ClusterCull" };
    rg::Shader hizShader{ "HizBuild" };
    std::vector<std::byte> vertexSpirv;
    std::array<std::vector<std::byte>, 4> fragmentSpirv;
    std::array<std::optional<Pipeline>, 8> pipelines; // [fragment * 2 + cullBackFaces]
    std::array<bool, 8> pipelineFailed{};

    // ---- Mesh data (GPU only) ----
    std::optional<Mesh> mesh;
    std::optional<rg::GpuBuffer> table;
    std::optional<rg::GpuBuffer> instance;
    std::uint32_t tableClusters = 0; // clusters of `table`
    std::uint32_t realClusters = 0;  // 0 when `table` is the synthesized single cluster

    // ---- Cull sets ----
    std::array<CullSet, kSetCount> sets;

    // ---- Targets ----
    SlotTarget mainSlot;
    SlotTarget previewSlot;
    SlotTarget idSlot;                            // RGBA8 + depth; created by the first id pass
    std::optional<RenderTexture> hizDepth;        // depth-only, sampled depth
    std::optional<RenderTexture> hizAtlas;        // R32_Float storage image
    std::optional<RenderTexture> hizStandIn;      // 1x1 R32_Float bound while no capture exists
    std::optional<ClusterViewHizInfo> hizInfo;    // pose of the capture held in `hizAtlas`
    std::uint64_t hizCaptureCounter = 0;
    int hizSceneWidth = 0; // size `hizDepth` and `hizAtlas` were made for
    int hizSceneHeight = 0;

    std::string lastLogged;

    // Descriptor sets of the cluster draws; freed while the GPU is idle.
    rg::ClusterDrawBindings bindings;

    // Declared last: destroyed first, so its cached descriptor sets never outlive a buffer they use.
    rg::RenderGraph graph;
};

template <typename BuildFn>
bool ClusterViewRenderer::Impl::Execute(BuildFn&& build, std::span<RenderTexture* const> finalize, std::string& note)
{
    // Ends the offscreen recording on every exit path.
    struct Recording {
        explicit Recording(Renderer& owner)
            : renderer(owner)
            , context(owner.BeginOffscreenRenderGraphRecording())
        {
        }
        ~Recording()
        {
            if (open) {
                try {
                    renderer.EndOffscreenRenderGraphRecording();
                } catch (...) {
                }
            }
        }
        Recording(const Recording&) = delete;
        Recording& operator=(const Recording&) = delete;

        void End()
        {
            open = false;
            renderer.EndOffscreenRenderGraphRecording();
        }

        Renderer& renderer;
        rg::RecordingContext context;
        bool open = true;
    };

    try {
        graph.BeginPersistentResourceFrame();
        Recording recording(renderer);
        graph.Execute(recording.context, rg::ExecuteTimingMode::SynchronousImmediateReadback, std::forward<BuildFn>(build));
        for (RenderTexture* texture : finalize) {
            texture->FinalizeForExternalSampling(recording.context);
        }
        recording.End();
        return true;
    } catch (const std::exception& e) {
        note = std::string("the graph run failed: ") + e.what();
        LogOnce(note);
        return false;
    }
}

} // namespace gte
