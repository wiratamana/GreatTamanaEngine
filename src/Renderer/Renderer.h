#pragma once

#include "../Math/Mat4.h"
#include "Buffer.h"
#include "Culling/ClusterCullMath.h"
#include "FramePresenter.h"
#include "FrameRecorder.h"
#include "GpuResourceFactory.h"
#include "GpuTiming.h"
#include "GpuTimingService.h"
#include "Memory/GpuMemoryTracker.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderGraph/GpuBuffer.h"
#include "RenderGraph/RecordingContext.h"
#include "RenderGraph/RenderGraphBarrierPlanner.h"
#include "RenderGraph/RenderGraphTypes.h"
#include "RenderTexture.h"
#include "Texture2D.h"
#include "TextureArray2D.h"
#include "VolumeTexture.h"
#include "Vulkan/ReflectedSetLayouts.h"
#include "Vulkan/VulkanAllocator.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanSurface.h"

#include "../Core/CapturedPixels.h"
#include "../Core/PipelineDesc.h"
#include "../Core/UiTextureHandle.h"
#include "GraphicsHandleRegistry.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte::rg {
class ClusterDrawBindings;
class CommandBuffer;
class RenderGraph;
class RenderGraphBuilder;
class RenderGraphPersistentResourceCache;
class RenderGraphComputeBindingCache;
class OneShot;
struct DebugTextureSnapshot;
class RetainedEventSnapshot;
class SceneRecording;
class Shader;
class RenderGraphTimestampPool;
} // namespace gte::rg

namespace gte {

class ComputePipeline;
class DescriptorSet;
class HeadlessRenderGraphFixture;
class ISurfaceProvider;
class MeshPreviewRenderer;
class RenderFeatureCompositor;
class SceneServicesDescriptorSet;
class VolumeTexturePreviewRenderer;

// High-level Vulkan renderer façade managing device lifecycle, frames, and resources.
class Renderer {
public:
    // `forceValidation` turns API validation on in any build type (it is on by default in debug builds only).
    explicit Renderer(ISurfaceProvider& surfaceProvider, bool forceValidation);
    ~Renderer();

    // Logs once whether API validation is on, off, or was requested but is not installed.
    void LogValidationState() const;

    // Non-copyable and non-movable.
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Synchronously renders into an offscreen render target, cleared to `clearColor` (linear RGBA).
    DrawStats RenderOffscreen(RenderTexture& target, std::optional<GpuTimingSlot> timingSlot,
        const std::array<float, 4>& clearColor, const std::function<void(rg::CommandBuffer&)>& recordExtra = {});

    // Begins recording into the dedicated offscreen command buffer.
    rg::RecordingContext BeginOffscreenRenderGraphRecording();

    // Ends recording and submits the offscreen command buffer, blocking until complete.
    void EndOffscreenRenderGraphRecording();

    // Executes swapchain presentation passes using the render graph.
    std::optional<DrawStats> PresentViaRenderGraph(rg::RenderGraph& graph,
        const std::function<std::vector<rg::TextureHandle>(rg::RenderGraphBuilder&, rg::TextureHandle)>& build);

    // Formats used by the current swapchain and depth buffer.
    VkFormat ColorFormat() const noexcept;
    VkFormat DepthFormat() const noexcept;
    PixelFormat ColorPixelFormat() const noexcept;
    PixelFormat DepthPixelFormat() const noexcept;

    // Format of scene color: view targets and the pipelines that draw scene geometry into them.
    PixelFormat ScenePixelFormat() const noexcept;

    // Creates an offscreen render target. transferSrc/transferDst add the image usage bits copy, clear and blit need.
    RenderTexture CreateRenderTexture(int width, int height, PixelFormat format = PixelFormat::Undefined,
        const char* debugName = nullptr, const char* depthDebugName = nullptr,
        bool allowStorageImageAccess = false, bool allowDepthSampledAccess = false,
        bool createDepthCompanion = true, bool createColorImage = true, bool transferSrc = false,
        bool transferDst = false) const;

    // Resizes `texture` once the GPU no longer uses it. Its contents become undefined.
    void ResizeRenderTexture(RenderTexture& texture, int width, int height) const;

    // Raw image pixels read back from the GPU.
    using CapturedRawPixels = CapturedPixels;

    // Synchronously reads back color pixels from a RenderTexture.
    CapturedRawPixels CaptureRenderTexturePixels(RenderTexture& texture) const;

    // Waits for the GPU, then reads back a debug texture snapshot (its depth image when `depthAspect`).
    // Empty pixels when the image is missing or its format is unsupported.
    CapturedRawPixels CaptureDebugTexturePixels(const rg::DebugTextureSnapshot& snapshot, bool depthAspect) const;

    // Waits for the GPU, then reads back a retained per-event snapshot (a shader-read color or depth image).
    CapturedRawPixels CaptureEventSnapshotPixels(const rg::RetainedEventSnapshot& snapshot, bool depthAspect) const;

    // Synchronously reads back raw pixel data from any GPU image.
    CapturedRawPixels CaptureImagePixels(VkImage image, VkImageAspectFlags aspect, VkFormat format, Extent2D extent,
        const rg::ResourceState& previousState, std::uint32_t zOffset = 0, std::uint32_t depth = 1) const;

    // Blocks until all GPU execution finishes. Call only during teardown or manual captures.
    void WaitForGpuIdle() const;

    // Same wait as WaitForGpuIdle(), named as the one sanctioned barrier outside src/Renderer before unloading
    // code (a project assembly) whose callbacks, pipelines or buffers the GPU may still use.
    void DrainGpuForCodeUnload() const;

    // Same wait, named as the sanctioned barrier before destroying textures, buffers or sets the GPU may still read.
    void DrainGpuForResourceRelease() const;

    // The device reference rg::GpuBuffer is created from.
    rg::DeviceRef Device() noexcept { return rg::DeviceRef(this); }

    // Schedules an asynchronous swapchain readback.
    void RequestSwapchainCapture();

    // Retrieves and clears the most recently completed swapchain capture.
    std::optional<CapturedSwapchainPixels> TakeLastCompletedSwapchainCapture();

    // Buffer factory methods.
    Buffer CreateBuffer(uint64_t size, BufferUsage usage, BufferMemoryUsage memoryUsage,
        const char* debugName = nullptr) const;

    Buffer CreateDeviceLocalBuffer(
        const void* data, uint64_t size, BufferUsage usage, const char* debugName = nullptr) const;

    // Prepares the renderer for a new frame and clears queued draw commands.
    void BeginFrame();

    // Queues a mesh draw call for the active frame.
    void Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(),
        const Mat4& viewProjMatrix = Mat4::Identity(),
        DescriptorSetHandle materialDescriptorSet = DescriptorSetHandle::Invalid,
        DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid);

    // Issues a counted indexed indirect draw (count taken from `countBuffer`, capped at `maxDrawCount`). Needs the
    // indirect-draw device features. Set 0 = `bufferDescriptorSet`, set 1 = scene services when the pipeline has it.
    void SubmitIndirect(const Pipeline& pipeline, const Mesh& mesh, VkBuffer indirectBuffer,
        uint64_t indirectOffset, std::uint32_t maxDrawCount, VkBuffer countBuffer, uint64_t countBufferOffset,
        DescriptorSetHandle bufferDescriptorSet,
        DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid,
        const Mat4& viewProjMatrix = Mat4::Identity());

    // Scene geometry pipeline. Builds against ScenePixelFormat().
    Pipeline CreatePipeline(const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath,
        VertexLayout vertexLayout = VertexLayout::PositionColor, bool useMaterialTexture = false,
        const char* debugName = nullptr,
        DescriptorSetLayoutHandle sceneServicesSetLayout = DescriptorSetLayoutHandle::Invalid) const;

    // Vulkan-free overload taking PixelFormat. An empty span builds a depth-only pipeline.
    Pipeline CreatePipeline(std::span<const PixelFormat> colorFormats, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        bool useMaterialTexture = false, const char* debugName = nullptr,
        DescriptorSetLayoutHandle sceneServicesSetLayout = DescriptorSetLayoutHandle::Invalid) const;

    // Layout of the buffer set: binding 0 = visible list, binding 1 = instances.
    DescriptorSetLayoutHandle BufferSetLayout() const noexcept;

    // Cached cluster draw pipeline of `key` (PositionNormal vertices, set 0 = cluster buffer set). The lit and
    // tint variants need `sceneServicesSetLayout` as set 1; depth-only ignores it. Null when the pipeline cannot
    // be built (logged once per key). The pointer stays valid for the renderer's life. Main thread only.
    const Pipeline* CreateClusterPipeline(const ClusterPipelineKey& key, DescriptorSetLayoutHandle sceneServicesSetLayout);

    // Device indirect draw capabilities. maxDrawIndirectCount is 0 unless multi draw is enabled.
    IndirectDrawCapsView IndirectDrawCaps() const noexcept;

    // Mesh creation helpers.
    Mesh CreateMesh(const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount,
        const char* debugName = nullptr) const;

    Mesh CreateMesh(const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount,
        const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    Mesh CreateSkinnedMesh(const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount,
        const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    // Shared vertex buffer helpers for multi-part meshes.
    std::shared_ptr<Buffer> CreateSharedMeshVertexBuffer(
        const void* vertexData, uint64_t vertexDataSize, const char* debugName = nullptr) const;

    std::shared_ptr<Buffer> CreateSharedSkinnedMeshVertexBuffer(
        const void* vertexData, uint64_t vertexDataSize, const char* debugName = nullptr) const;

    Mesh CreateMeshFromSharedVertexBuffer(std::shared_ptr<Buffer> sharedVertexBuffer, std::uint32_t vertexCount,
        const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    // The same, sharing the vertex buffer of a GpuBuffer (for example a skinning output).
    Mesh CreateMeshFromSharedVertexBuffer(const rg::GpuBuffer& sharedVertexBuffer, std::uint32_t vertexCount,
        const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    // Texture creation factories.
    Texture2D CreateTexture2D(const Texture2DUploadDesc& desc) const;

    // Batched upload with mip chains (sRGB for base color, UNORM otherwise); index-aligned with `uploads`,
    // a null entry for an invalid upload. Throws on a GPU failure.
    std::vector<std::shared_ptr<Texture2D>> CreateMaterialImages(std::span<const MaterialImageUpload> uploads) const;

    // Finished texture uploads since creation (submits, images, staging bytes).
    GpuUploadStats GetUploadStats() const noexcept;

    // A new material (own params block and set) over `images`; null entries use the default image of the slot.
    MaterialTexture CreateMaterialVariant(const MaterialImages& images, const MaterialParams& params) const;

    VolumeTexture CreateVolumeTexture(
        int width, int height, int depth, PixelFormat format, const char* debugName = nullptr) const;

    TextureArray2D CreateTextureArray(int width, int height, int arrayLayers, PixelFormat format, bool hasDepth,
        bool isCubemap, bool allowStorageImageAccess = false, const char* debugName = nullptr) const;

    // GPU memory tracking queries.
    GpuMemoryTracker::Totals GetMemoryTotals() const;
    std::vector<GpuMemoryTracker::Entry> GetMemoryResources() const;
    std::shared_ptr<GpuMemoryTracker> GetMemoryTracker() const noexcept { return m_resources.GetMemoryTracker(); }
    VmaAllocator GetVmaAllocator() const noexcept { return m_allocator.Native(); }
    std::vector<VmaBudget> GetVmaHeapBudgets() const;
    DescriptorPoolStats GetDescriptorPoolStats() const; // Sets and descriptors in use per type.

    // GPU timing capture.
    void SetGpuTimingCaptureEnabled(bool enabled) noexcept;

    // Builds a dynamic graphics pipeline from a descriptive configuration.
    Pipeline CreatePipeline(const PipelineDesc& desc, std::span<const std::byte> vertexSpirv,
        std::span<const std::byte> fragmentSpirv) const;

    // UI texture handles for immediate-mode GUI integration.
    UiTextureHandle GetUiTextureHandle(const Texture2D& texture);
    UiTextureHandle GetUiTextureHandle(const RenderTexture& target);

    // UI handle for a debug texture snapshot's color view; Invalid when it has none or no UI backend exists.
    UiTextureHandle GetUiTextureHandle(const rg::DebugTextureSnapshot& snapshot);
    void ReleaseUiTextureHandle(UiTextureHandle handle) noexcept;

    // Waits for the GPU, then releases `handle` and destroys `texture`. Either may be empty.
    void RetireUiTexture(UiTextureHandle handle, std::unique_ptr<Texture2D> texture) noexcept;

    // Context details needed by external backends (e.g. ImGui).
    struct VulkanContextInfo {
        std::uint32_t apiVersion = 0;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        std::uint32_t graphicsQueueFamily = 0;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        std::uint32_t imageCount = 0;
        std::uint32_t minImageCount = 0;
        GpuTimestampCapability timestampCapability;
    };

    // Resizes swapchain on window resize events.
    void OnResize(int width, int height);

    // Returns context information required by ImGui's Vulkan backend.
    VulkanContextInfo GetImGuiVulkanBackendInfo() const;

private:
    VulkanContextInfo GetVulkanContextInfo() const;

    // Raw GPU entry points for the src/Renderer internals named as friends below.
    // Binds a command buffer for direct draw submissions during a graph pass.
    void BeginGraphPassRecording(
        VkCommandBuffer cmd, std::function<void(bool, std::uint32_t, std::uint32_t)> recordDrawStats);
    void EndGraphPassRecording() noexcept;

    // Runs a one-shot command buffer on the graphics queue and waits for it.
    void ImmediateSubmit(const std::function<void(rg::CommandBuffer&)>& recordFn) const;
    VkResult SubmitToGraphicsQueue(const VkSubmitInfo2& submit, VkFence fence) const;

    ComputePipeline CreateComputePipeline(const std::string& shaderSpirvPath,
        const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
        std::optional<VkPushConstantRange> pushConstantRange = std::nullopt) const;

    DescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout) const;
    DescriptorSet AllocateDescriptorSet(DescriptorSetLayoutHandle layoutHandle) const;
    // Frees a set from AllocateDescriptorSet(). The GPU must be done with it.
    void FreeDescriptorSet(VkDescriptorSet descriptorSet) const noexcept;

    // Layout from explicit bindings; the caller destroys it once no set or pipeline uses it.
    DescriptorSetLayoutHandle CreateDescriptorSetLayout(std::span<const DescriptorBindingDesc> bindings) const;
    void DestroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) const noexcept;

    // Asks the render graph to drop the cached descriptor sets built for a destroyed shader layout.
    void QueueBindingRelease(VkDescriptorSetLayout layout) noexcept;
    std::vector<VkDescriptorSetLayout> TakeQueuedBindingReleases() noexcept;

    bool SupportsDepthBlit() const noexcept;

    // Dispatches a compute pipeline within an active graph pass.
    void Dispatch(const ComputePipeline& pipeline, VkDescriptorSet descriptorSet, const void* pushConstants,
        std::uint32_t pushConstantBytes, std::uint32_t groupCountX, std::uint32_t groupCountY = 1,
        std::uint32_t groupCountZ = 1);

    // One layout per reflected set; the result owns and destroys them.
    ReflectedSetLayouts CreateReflectedSetLayouts(
        const std::vector<ReflectedDescriptorBinding>& bindings, const std::string& label) const;

    friend class rg::ClusterDrawBindings;
    friend class rg::CommandBuffer;
    friend class rg::SceneRecording;
    friend class rg::Shader;
    friend class rg::RenderGraphTimestampPool;
    friend class MeshPreviewRenderer;

    friend class rg::RenderGraph;
    friend class rg::RenderGraphPersistentResourceCache;
    friend class rg::RenderGraphComputeBindingCache;
    friend class rg::OneShot;
    friend class SceneServicesDescriptorSet;
    friend class VolumeTexturePreviewRenderer;
    friend class RenderFeatureCompositor;
    friend class HeadlessRenderGraphFixture;

    // Helper for RenderGraph initialization before member lists are ready.
    friend VulkanContextInfo QueryVulkanContextInfoForRenderGraph(Renderer& renderer);

    VulkanInstance m_instance;
    VulkanSurface m_surface;
    VulkanDevice m_device;

    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

    std::shared_ptr<GpuMemoryTracker> m_memoryTracker = std::make_shared<GpuMemoryTracker>();
    std::shared_ptr<GpuTimingService> m_gpuTiming;

    VulkanAllocator m_allocator;
    FramePresenter m_presenter;
    GpuResourceFactory m_resources;
    FrameRecorder m_frameRecorder;

    // Zero-binding layout and its set; bound at set 0 of pipelines that have only a scene-services set.
    OwnedDescriptorSetLayout m_emptySetLayout;
    VkDescriptorSet m_emptySet = VK_NULL_HANDLE; // Freed in ~Renderer() before the pool goes away.

    VkCommandBuffer m_currentGraphPassCmd = VK_NULL_HANDLE;
    std::function<void(bool, std::uint32_t, std::uint32_t)> m_currentGraphPassRecordDrawStats;

    mutable std::mutex m_bindingReleaseMutex;
    std::vector<VkDescriptorSetLayout> m_bindingReleaseQueue;

    // Reusable fences for blocking submits.
    VkFence AcquireFence() const;
    void ReleaseFence(VkFence fence) const noexcept;
    mutable std::mutex m_fencePoolMutex;
    mutable std::vector<VkFence> m_fencePool;

    // Cluster draw pipelines by ClusterPipelineCacheIndex(); built on first use.
    struct ClusterPipelineSlot {
        std::unique_ptr<Pipeline> pipeline;
        DescriptorSetLayoutHandle sceneServicesSetLayout = DescriptorSetLayoutHandle::Invalid;
        bool failureLogged = false;
    };
    std::array<ClusterPipelineSlot, kClusterPipelineKeyCount> m_clusterPipelines;
    DescriptorSetLayoutHandle m_bufferSetLayoutHandle = DescriptorSetLayoutHandle::Invalid;

    // Runs one-shot graphs; created on first use. Declared last so it is destroyed first.
    rg::RenderGraph& OneShotGraph();
    std::unique_ptr<rg::RenderGraph> m_oneShotGraph;
};

// Global helper to query context info for RenderGraph.
Renderer::VulkanContextInfo QueryVulkanContextInfoForRenderGraph(Renderer& renderer);

} // namespace gte
