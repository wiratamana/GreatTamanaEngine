#pragma once

// Coordinates render graph compilation, physical resource allocation,
// barrier generation, and execution across frames.

#include "BoundedOnceSet.h"
#include "CommandBuffer.h"
#include "RecordingAccess.h"
#include "RenderGraphBarrierPlanner.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphCompiler.h"
#include "RenderGraphComputeBindingCache.h"
#include "RenderGraphDebugMetadataSink.h"
#include "RenderGraphDebugTextureRegistry.h"
#include "RenderGraphDebugVolumeTextureRegistry.h"
#include "RenderGraphEventSnapshotPool.h"
#include "../../Core/FrameDebuggerEventSink.h"
#include "RenderGraphPersistentResourceCache.h"
#include "RenderGraphResourcePool.h"
#include "RenderGraphTimestampPool.h"
#include "RenderGraphSnapshot.h"
#include "../RenderTimingObserver.h"
#include "RenderGraphTypes.h"
#include "../Culling/ClusterPassCounts.h"
#include "../DrawStats.h"
#include "../GpuTiming.h"

#include <volk.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {
class RenderPassToggleRegistry;

// Execution mode: synchronous offscreen rendering or pipelined presentation.
enum class ExecuteTimingMode : std::uint8_t {
    SynchronousImmediateReadback,
    PipelinedDeferredReadback,
};

// Whether a graph measures GPU time.
enum class TimingMode : std::uint8_t { Enabled, Disabled };

// Manages and executes the frame's render passes and resources.
class RenderGraph {
public:
    // A Disabled graph creates no query pool and writes no stamps; its GPU records read Inactive.
    explicit RenderGraph(Renderer& renderer, TimingMode timingMode = TimingMode::Enabled);

    // Builds and executes passes using the provided build callback.
    template <typename BuildFn>
    void Execute(const RecordingContext& cmd, ExecuteTimingMode timingMode, BuildFn&& build)
    {
        RenderGraphBuilder builder;
        builder.SetDebugMetadataSink(m_debugMetadataSink);
        builder.SetPassToggleRegistry(m_renderPassToggleRegistry);
        builder.SetOnceLog(&m_onceLog);
        builder.SetPersistentResourceCache(&m_persistentResourceCache, timingMode, m_persistentResourceFrameCounter);
        builder.SetRenderer(m_renderer);

        if (m_debugMetadataSink != nullptr) {
            m_debugMetadataSink->BeginFrame();
        }

        const std::vector<TextureHandle> finalOutputs = build(builder);
        ExecuteCompiledGraph(RecordingAccess::Native(cmd), timingMode, builder.Finish(), finalOutputs);
    }

    // Returns the debug snapshot for the specified execution mode.
    const RenderGraphSnapshot& LastSnapshot(ExecuteTimingMode mode) const noexcept;

    // Queries debug snapshots for textures registered during execution.
    std::optional<DebugTextureSnapshot> DebugTextureSnapshotFor(const std::string& name) const;
    std::vector<DebugTextureSnapshot> ListDebugTextures() const;

    // Queries debug snapshots for volume textures registered during execution.
    std::optional<DebugVolumeTextureSnapshot> DebugVolumeTextureSnapshotFor(const std::string& name) const;
    std::vector<DebugVolumeTextureSnapshot> ListDebugVolumeTextures() const;

    // Notifies the registry of an external layout transition for a debug texture.
    void NotifyDebugTextureStateOverride(std::string_view name, const ResourceState& newColorState);

    // Current frame counter used for debug texture freshness.
    std::uint64_t CurrentDebugTextureFrameCounter() const noexcept;

    // Advances the persistent resource cache frame counter and clears PersistentTexturesResizedThisFrame().
    void BeginPersistentResourceFrame() noexcept;

    // True when an Execute() since the last BeginPersistentResourceFrame() ran with persistent textures that had a
    // stale size: they are recreated at the right size only after that Execute(), so its output is incomplete.
    bool PersistentTexturesResizedThisFrame() const noexcept { return m_persistentTexturesResizedThisFrame; }

    // Synchronous regime: reads the stamps of the last Execute() once its submission completed, resolves them and
    // fills the synchronous snapshot's GPU fields.
    void FinalizeSynchronousGpuTiming();

    // Pipelined regime: the presenter picks the frame slot the next Execute() records its stamps into.
    void SetPipelinedGpuSlot(std::uint32_t slot) noexcept { m_pipelinedGpuSlot = slot % kGpuFramesInFlight; }

    // Reads the stamps of the frame that was submitted into `slot` as number `frameIndex` and applies them to the
    // pipelined snapshot. Call after the wait that proves the slot's submission finished.
    void ResolvePipelinedGpuFrame(std::uint32_t slot, std::uint64_t frameIndex);

    // The frame recorded into `slot` was never submitted or its data left the ring: it is counted and forgotten.
    void DropPipelinedGpuFrame(std::uint32_t slot) noexcept;

    // Frames whose GPU data was lost or never read, since the graph was made.
    std::uint32_t DroppedGpuFrames() const noexcept { return m_droppedGpuFrames; }

    // Enables or disables GPU timestamp capture.
    void SetGpuTimingCaptureEnabled(bool enabled) noexcept { m_timestampPool.SetCaptureEnabled(enabled); }

    // Fine stamps barriers and work apart; coarse writes one shared stamp per pass.
    void SetGpuStampMode(GpuStampMode mode) noexcept { m_gpuStampMode = mode; }

    // Sets optional sinks/providers for debug inspection.
    void SetDebugMetadataSink(IPassDebugMetadataSink* sink) noexcept { m_debugMetadataSink = sink; }
    void SetDebugMetadataProvider(IPassDebugMetadataProvider* provider) noexcept { m_debugMetadataProvider = provider; }

    // Sets the sink for frame debugger draw events.
    void SetFrameDebuggerEventSink(FrameDebuggerEventSink* sink) noexcept { m_frameDebuggerEventSink = sink; }

    // Sets the registry for toggling passes dynamically.
    void SetRenderPassToggleRegistry(RenderPassToggleRegistry* registry) noexcept
    {
        m_renderPassToggleRegistry = registry;
    }

    // Optional. Sees the compile, record and finish stages and every pass recording.
    void SetTimingObserver(IRenderTimingObserver* observer) noexcept { m_timingObserver = observer; }

    // The state a texture must be left in after RunOneShot; no state keeps whatever the passes left.
    struct OneShotFinal {
        TextureHandle texture;
        std::optional<ResourceState> state;
    };

    // Compiles, records, submits and waits for a graph of attachment-free passes (compute, transfer), with no
    // timing, snapshots or persistent resources. `states` holds the color state of every touched texture (nullopt
    // otherwise), set only after the wait succeeded. On failure it is untouched and one error is logged.
    bool RunOneShot(CompiledGraphInput input, std::span<const TextureHandle> outputs,
        std::span<const OneShotFinal> finals, std::vector<std::optional<ResourceState>>& states);

    OnceErrorLog* OnceLog() noexcept { return &m_onceLog; }
    std::uint64_t NextOneShotFrame() noexcept { return ++m_oneShotFrame; }

private:
    // PassContext needs access to internal physical resource types for resolution.
    friend struct PassContext;

    // One physical record per TextureHandle, for every dimension. `dimension` selects which fields apply.
    struct PhysicalTexture {
        bool resolved = false;
        bool isImported = false;
        bool hasDepth = false; // Tex2D only: carries a companion depth image (target.depth*).
        TextureDimension dimension = TextureDimension::Tex2D;
        // Every dimension: image/imageView/extent(w,h)/format. Tex2D additionally: depth image/view/format.
        RenderTarget target;
        std::uint32_t depthSlices = 1;  // Tex3D only.
        TextureArrayTarget arrayTarget; // Array2D/Cube only.
        VkSampler sampler = VK_NULL_HANDLE;
        VkSampler depthSampler = VK_NULL_HANDLE;
        ResourceState colorState;                 // Tex2D color image, or the Tex3D image.
        ResourceState depthState;                 // Tex2D companion depth image.
        std::vector<ResourceState> layerStates;   // Array2D/Cube: one state per layer.
    };

    struct PhysicalBuffer {
        bool resolved = false;
        VkBuffer buffer = VK_NULL_HANDLE;
        BufferObjectHandle object = BufferObjectHandle::Invalid; // Generational identity of `buffer`.
        uint64_t size = 0;
        ResourceState state;
        ResourceState lastWrite; // State of the last write usage; empty until the buffer is written.
    };

    void ExecuteCompiledGraph(VkCommandBuffer cmd, ExecuteTimingMode timingMode, CompiledGraphInput input,
        const std::vector<TextureHandle>& finalOutputs);

    void EnsureTextureResolved(
        std::uint32_t index, const CompiledGraphInput& input, std::vector<PhysicalTexture>& physicalTextures);
    void EnsureBufferResolved(
        std::uint32_t index, const CompiledGraphInput& input, std::vector<PhysicalBuffer>& physicalBuffers);

    // Tracks whether a barrier was emitted and its before/after layout.
    struct AppliedBarrierInfo {
        bool applied = false;
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    void ApplyUsageBarrierIfNeeded(VkCommandBuffer cmd, const ResourceUsage& usage, const CompiledGraphInput& input,
        std::vector<PhysicalTexture>& physicalTextures, std::vector<PhysicalBuffer>& physicalBuffers,
        AppliedBarrierInfo* outBarrierInfo = nullptr);

    // Returns Invalid if any declared binding cannot be resolved; never writes a partial descriptor set.
    DescriptorSetHandle ResolveComputeBindingDescriptorSet(const PassRecord& pass,
        const std::vector<PhysicalTexture>& physicalTextures, const std::vector<PhysicalBuffer>& physicalBuffers);

    // The descriptor writes of a pass's bindings; false if any binding cannot be resolved.
    bool BuildDescriptorWrites(const PassRecord& pass, const std::vector<PhysicalTexture>& physicalTextures,
        const std::vector<PhysicalBuffer>& physicalBuffers, std::vector<DescriptorWrite>& writes);

    // Logs a skipped binding (or the whole pass when `binding` is nullopt) once per distinct key.
    void ReportSkippedOnce(const PassRecord& pass, std::optional<std::uint32_t> binding, const char* reason);

    PassContext BuildPassContext(VkCommandBuffer cmd, std::vector<PhysicalTexture>& physicalTextures,
        std::vector<PhysicalBuffer>& physicalBuffers, DrawStats& passDrawStats);

    std::vector<VkRenderingAttachmentInfo> BuildColorAttachmentInfos(const PassRecord& pass,
        const std::vector<PhysicalTexture>& physicalTextures, std::vector<VkExtent2D>& outResolvedExtents) const;

    struct DepthAttachmentResult {
        std::optional<VkRenderingAttachmentInfo> info;
        VkExtent2D extent{};
    };

    DepthAttachmentResult BuildDepthAttachmentInfo(const PassRecord& pass,
        const std::vector<PhysicalTexture>& physicalTextures, TextureHandle depthHandle) const;

    // Writes one 2D texture's debug snapshot (the single place a snapshot entry is built).
    void UpsertDebugTextureSnapshot(ExecuteTimingMode timingMode, const char* name, const PhysicalTexture& tex);

    // Registers resolved 2D textures / 3D volumes with the debug inspector (other dimensions are not inspected).
    void RegisterDebugTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
        const std::vector<PhysicalTexture>& physicalTextures);
    void RegisterDebugVolumeTextureSnapshots(ExecuteTimingMode timingMode, const CompiledGraphInput& input,
        const std::vector<PhysicalTexture>& physicalTextures);

    RenderGraphResourcePool m_resourcePool;
    RenderGraphPersistentResourceCache m_persistentResourceCache;
    RenderGraphComputeBindingCache m_computeBindingCache;

    Renderer* m_renderer = nullptr;
    IPassDebugMetadataSink* m_debugMetadataSink = nullptr;
    IPassDebugMetadataProvider* m_debugMetadataProvider = nullptr;
    FrameDebuggerEventSink* m_frameDebuggerEventSink = nullptr;
    RenderPassToggleRegistry* m_renderPassToggleRegistry = nullptr;
    IRenderTimingObserver* m_timingObserver = nullptr;

    TimingMode m_timingMode = TimingMode::Enabled;
    RenderGraphTimestampPool m_timestampPool;
    GpuStampMode m_gpuStampMode = GpuStampMode::Fine;

    // Stamps recorded by one Execute() and not yet read: the plan, one assignment per executed pass, the records
    // with their identity filled in, and each pass's name hash. The pipelined regime holds one per frame slot.
    struct GpuFrameData {
        bool pending = false;
        bool captured = false; // Capture was on when the stamps were recorded.
        GpuStampPlan plan;
        std::vector<GpuStampAssignment> assignments;
        std::vector<GpuPassRecord> records;
        std::vector<std::uint32_t> nameHashes;
        std::vector<std::uint8_t> dynamicallySkipped; // The pass was refused at record time (no descriptor set).
    };

    // One frame's resolved GPU timing, expanded to one record per executed pass (folded passes share a record).
    struct ResolvedGpuFrame {
        bool valid = false;
        std::uint64_t frameIndex = 0;
        GpuFrameReport report;
        std::vector<std::uint32_t> nameHashes;
        std::vector<GpuPassRecord> perPass;
    };

    // Reads the pool region of `data` once and resolves it. Clears `data.pending`.
    ResolvedGpuFrame ResolveGpuFrame(GpuFrameData& data, bool pipelined, std::uint32_t bufferIndex,
        std::uint64_t frameIndex);

    // Copies `resolved` into `snapshot`; a pass takes its record only when its position and name hash match.
    static void ApplyResolvedGpu(const ResolvedGpuFrame& resolved, RenderGraphSnapshot& snapshot);

    GpuFrameData m_synchronousGpu;
    GpuFrameData m_pipelinedGpu[kGpuFramesInFlight];
    ResolvedGpuFrame m_pipelinedResolved; // The newest pipelined frame whose stamps were read back.
    std::uint32_t m_pipelinedGpuSlot = 0;
    std::uint32_t m_droppedGpuFrames = 0;
    std::vector<GpuStampPassInput> m_gpuPlanInputs;
    std::vector<std::uint64_t> m_gpuValues; // Scratch for one pool read.
    std::vector<std::uint8_t> m_gpuAvailable;

    static constexpr std::size_t kMaxReportedSkippedBindings = 256;
    BoundedOnceSet<kMaxReportedSkippedBindings> m_reportedSkippedBindings;
    OnceErrorLog m_onceLog;

    RenderGraphSnapshot m_synchronousSnapshot;
    RenderGraphSnapshot m_pipelinedSnapshot;

    RenderGraphDebugTextureRegistry m_debugTextures;
    RenderGraphDebugVolumeTextureRegistry m_debugVolumeTextures;

    std::uint64_t m_debugTextureFrameCounter = 0;
    std::uint64_t m_persistentResourceFrameCounter = 0;
    bool m_persistentTexturesResizedThisFrame = false;
    std::uint64_t m_oneShotFrame = 0;
};

// Execution context passed to each pass's execute callback.
// The command buffer is private: record through Cmd(), the pass kinds, or SceneRecording.
struct PassContext {
    VkExtent2D colorAttachmentExtent{};

    // Representative write targets for attachment 0 (for debugging).
    VkImage writeTargetColorImage = VK_NULL_HANDLE;
    VkFormat writeTargetColorFormat = VK_FORMAT_UNDEFINED;
    VkImage writeTargetDepthImage = VK_NULL_HANDLE;
    VkFormat writeTargetDepthFormat = VK_FORMAT_UNDEFINED;

    struct ResolvedTexture {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct ResolvedDepthTexture {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct ResolvedImage {
        VkImage image = VK_NULL_HANDLE;
        VkExtent2D extent{};
    };

    struct ResolvedVolumeTexture {
        VkImageView view = VK_NULL_HANDLE;
    };

    struct ResolvedTextureArray {
        VkImageView view = VK_NULL_HANDLE;
    };

    struct ResolvedTextureArrayLayer {
        VkImageView view = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
    };

    // Non-owning pointers to active physical resources during this pass.
    const std::vector<RenderGraph::PhysicalTexture>* textures = nullptr;
    const std::vector<RenderGraph::PhysicalBuffer>* buffers = nullptr;

    Renderer* renderer = nullptr;
    FrameDebuggerEventSink* eventSink = nullptr;

    // The graph's once-log; set for every pass the graph executes.
    OnceErrorLog* onceLog = nullptr;
    void ReportOnce(std::string_view category, std::string_view text) const
    {
        if (onceLog != nullptr) {
            onceLog->Error(category, text);
        }
    }

    DescriptorSetHandle computeDescriptorSet = DescriptorSetHandle::Invalid;
    DescriptorSetHandle BoundDescriptorSet() const noexcept { return computeDescriptorSet; }

    // Resource resolvers.
    ResolvedTexture resolveReadTexture(TextureHandle handle) const noexcept;
    ResolvedTexture resolveTexture(TextureHandle handle) const noexcept { return resolveReadTexture(handle); }
    ResolvedDepthTexture resolveDepthTexture(TextureHandle handle) const noexcept;
    VkBuffer resolveBuffer(BufferHandle handle) const noexcept;
    // Color image and extent of a Tex2D texture (transfer passes); null when unresolved.
    ResolvedImage resolveColorImage(TextureHandle handle) const noexcept;
    // Non-2D resolvers: `handle` must name a Tex3D / Array2D-Cube texture.
    ResolvedVolumeTexture resolveVolumeTexture(TextureHandle handle) const noexcept;
    ResolvedTextureArray resolveTextureArray(TextureHandle handle) const noexcept;
    ResolvedTextureArrayLayer resolveArrayLayer(TextureHandle handle, std::uint32_t layerIndex) const noexcept;

    // Callable object to accumulate draw call statistics.
    struct RecordDrawFn {
        DrawStats* drawStats = nullptr;
        void operator()(bool hasIndexBuffer, std::uint32_t vertexCount, std::uint32_t indexCount) const;
    };

    // Callable object to accumulate indirect draw statistics.
    struct RecordIndirectDrawFn {
        DrawStats* drawStats = nullptr;
        void operator()() const;
        explicit operator bool() const noexcept { return drawStats != nullptr; }
    };

    RecordDrawFn recordDraw;
    RecordIndirectDrawFn recordIndirectDraw;

    // CommandBuffer wrapper for recording draws in this pass.
    CommandBuffer Cmd() const noexcept
    {
        return CommandBuffer(m_cmd, renderer, recordDraw.drawStats,
            CommandBuffer::WriteTargetInfo{
                writeTargetColorImage, colorAttachmentExtent, writeTargetColorFormat, writeTargetDepthImage,
                writeTargetDepthFormat },
            eventSink);
    }

    // Vulkan-free handle for this pass's command buffer; recorders built from it count draws and notify the
    // frame debugger like Cmd().
    RecordingContext Recording() const noexcept { return RecordingAccess::Wrap(m_cmd, renderer, this); }

private:
    friend class RenderGraph;
    friend struct RecordingAccess;

    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
};

inline VkCommandBuffer RecordingAccess::Native(const PassContext& ctx) noexcept
{
    return ctx.m_cmd;
}

} // namespace gte::rg
