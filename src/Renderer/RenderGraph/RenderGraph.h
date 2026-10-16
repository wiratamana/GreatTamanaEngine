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
#include "RenderGraphNameSlotTable.h"
#include "RenderGraphPersistentResourceCache.h"
#include "RenderGraphResourcePool.h"
#include "RenderGraphTimestampPool.h"
#include "RenderGraphSnapshot.h"
#include "RenderGraphTypes.h"
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

// Manages and executes the frame's render passes and resources.
class RenderGraph {
public:
    explicit RenderGraph(Renderer& renderer);

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

    // Returns the most recently recorded draw and GPU timing stats for a pass.
    PassGpuStats LastKnownStatsFor(const char* passName) const;

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

    // Advances the persistent resource cache frame counter.
    void BeginPersistentResourceFrame() noexcept;

    // Current frame counter for the persistent resource cache.
    std::uint64_t CurrentPersistentResourceFrameCounter() const noexcept { return m_persistentResourceFrameCounter; }

    // Reads back and resolves GPU timestamps for synchronous execution.
    void FinalizeSynchronousGpuTiming();

    // Enables or disables GPU timestamp capture.
    void SetGpuTimingCaptureEnabled(bool enabled) noexcept { m_timestampPool.SetCaptureEnabled(enabled); }

    // Sets optional sinks/providers for debug inspection.
    void SetDebugMetadataSink(IPassDebugMetadataSink* sink) noexcept { m_debugMetadataSink = sink; }
    void SetDebugMetadataProvider(IPassDebugMetadataProvider* provider) noexcept { m_debugMetadataProvider = provider; }

    // Sets the sink for frame debugger draw events.
    void SetFrameDebuggerEventSink(FrameDebuggerEventSink* sink) noexcept { m_frameDebuggerEventSink = sink; }

    // Enables or disables per-pass write target snapshot capture.
    void SetEventSnapshotCaptureEnabled(bool enabled) noexcept { m_eventSnapshotPool.SetCaptureEnabled(enabled); }

    // Sets the registry for toggling passes dynamically.
    void SetRenderPassToggleRegistry(RenderPassToggleRegistry* registry) noexcept
    {
        m_renderPassToggleRegistry = registry;
    }

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

    // Reference to a pass write target captured for debugging.
    struct FrameDebuggerEventSnapshotRef {
        const char* passName = nullptr;
        TextureHandle resource;
        std::int32_t slot = kNoNameSlot;
        std::uint32_t region = 0;
    };
    const std::vector<FrameDebuggerEventSnapshotRef>& EventSnapshotsThisFrame() const noexcept
    {
        return m_eventSnapshotsThisFrame;
    }

    const RenderGraphEventSnapshotPool& EventSnapshotPool() const noexcept { return m_eventSnapshotPool; }

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
        uint64_t size = 0;
        ResourceState state;
    };

    struct NamedStats {
        std::string name;
        PassGpuStats stats;
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
        const std::vector<PhysicalBuffer>& physicalBuffers, std::vector<ComputeDescriptorWrite>& writes);

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

    void UpdateDrawStatsFor(const char* name, const DrawStats& drawStats);
    void UpdateTimingFor(const char* name, const GpuTimingSample& timing);

    GpuTimingSample ResolveAndConvertTiming(const RenderGraphTimestampPool::RawTicks& raw) const;

    static constexpr std::uint32_t kSynchronousTimingSlotBudget = 16;
    static constexpr std::uint32_t kPipelinedTimingSlotBudget = 8;
    static constexpr std::uint32_t kEventSnapshotSlotBudget = 64;

    RenderGraphResourcePool m_resourcePool;
    RenderGraphPersistentResourceCache m_persistentResourceCache;
    RenderGraphComputeBindingCache m_computeBindingCache;

    Renderer* m_renderer = nullptr;
    IPassDebugMetadataSink* m_debugMetadataSink = nullptr;
    IPassDebugMetadataProvider* m_debugMetadataProvider = nullptr;
    FrameDebuggerEventSink* m_frameDebuggerEventSink = nullptr;
    RenderPassToggleRegistry* m_renderPassToggleRegistry = nullptr;

    RenderGraphTimestampPool m_timestampPool;
    RenderGraphNameSlotTable m_synchronousTimingSlots{ kSynchronousTimingSlotBudget };
    RenderGraphNameSlotTable m_pipelinedTimingSlots{ kPipelinedTimingSlotBudget };

    RenderGraphEventSnapshotPool m_eventSnapshotPool;
    std::vector<FrameDebuggerEventSnapshotRef> m_eventSnapshotsThisFrame;

    std::vector<std::string> m_reportedSynchronousOverflows;
    std::vector<std::string> m_reportedPipelinedOverflows;

    static constexpr std::size_t kMaxReportedSkippedBindings = 256;
    BoundedOnceSet<kMaxReportedSkippedBindings> m_reportedSkippedBindings;
    OnceErrorLog m_onceLog;

    std::uint32_t m_pipelinedFrameCounter = 0;
    bool m_pipelinedHasWritten[kPipelinedTimingSlotBudget][kGpuTimingFramesInFlight]{};

    std::vector<NamedStats> m_lastKnownStats;

    RenderGraphSnapshot m_synchronousSnapshot;
    RenderGraphSnapshot m_pipelinedSnapshot;

    RenderGraphDebugTextureRegistry m_debugTextures;
    RenderGraphDebugVolumeTextureRegistry m_debugVolumeTextures;

    std::uint64_t m_debugTextureFrameCounter = 0;
    std::uint64_t m_persistentResourceFrameCounter = 0;
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
