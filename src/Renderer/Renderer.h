#pragma once

#include "../Math/Mat4.h"
#include "Buffer.h"
#include "Compute/ComputeDispatch.h"
#include "FramePresenter.h"
#include "FrameRecorder.h"
#include "GpuResourceFactory.h"
#include "GpuTiming.h"
#include "GpuTimingService.h"
#include "Memory/GpuMemoryTracker.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderGraph/RenderGraphBarrierPlanner.h"
#include "RenderGraph/RenderGraphTypes.h"
#include "RenderTexture.h"
#include "Texture2D.h"
#include "TextureArray2D.h"
#include "VolumeTexture.h"
#include "Vulkan/VulkanAllocator.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanSurface.h"

#include "../Core/CaptureResult.h"
#include "../Core/PipelineDesc.h"
#include "../Core/UiTextureHandle.h"
#include "GraphicsHandleRegistry.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte::rg {
class CommandBuffer;
class RenderGraph;
class RenderGraphBuilder;
class RenderGraphPersistentResourceCache;
class RenderGraphComputeBindingCache;
} // namespace gte::rg

namespace gte {

class AtmosphereLutRenderer;
class GpuDrivenBatchCache;
class HeadlessRenderGraphFixture;
class ISurfaceProvider;
class RenderFeatureCompositor;
class SceneServicesDescriptorSet;
class VolumeTexturePreviewRenderer;

// High-level Vulkan renderer façade managing device lifecycle, frames, and resources.
class Renderer {
public:
    explicit Renderer(ISurfaceProvider& surfaceProvider);
    ~Renderer();

    // Non-copyable and non-movable.
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Sets the clear color for subsequent render operations.
    void Clear(std::uint8_t r = 0, std::uint8_t g = 0, std::uint8_t b = 0, std::uint8_t a = 255);

    // Synchronously renders into an offscreen render target.
    DrawStats RenderOffscreen(RenderTexture& target, std::optional<GpuTimingSlot> timingSlot,
        const std::function<void(rg::CommandBuffer&)>& recordExtra = {});

    // Begins recording into the dedicated offscreen command buffer.
    VkCommandBuffer BeginOffscreenRenderGraphRecording();

    // Ends recording and submits the offscreen command buffer, blocking until complete.
    void EndOffscreenRenderGraphRecording();

    // Executes swapchain presentation passes using the render graph.
    std::optional<DrawStats> PresentViaRenderGraph(rg::RenderGraph& graph, bool needsSwapchainDepth,
        const std::function<std::vector<rg::TextureHandle>(rg::RenderGraphBuilder&, rg::TextureHandle)>& build);

    // Binds an active command buffer for direct draw submissions during a graph pass.
    void BeginGraphPassRecording(
        VkCommandBuffer cmd, std::function<void(bool, std::uint32_t, std::uint32_t)> recordDrawStats);
    void EndGraphPassRecording() noexcept;

    // Formats used by the current swapchain and depth buffer.
    VkFormat ColorFormat() const noexcept;
    VkFormat DepthFormat() const noexcept;
    PixelFormat ColorPixelFormat() const noexcept;
    PixelFormat DepthPixelFormat() const noexcept;

    // Creates an offscreen render target.
    RenderTexture CreateRenderTexture(int width, int height, PixelFormat format = PixelFormat::Undefined,
        const char* debugName = nullptr, const char* depthDebugName = nullptr,
        bool allowStorageImageAccess = false, bool allowDepthSampledAccess = false,
        bool createDepthCompanion = true, bool createColorImage = true) const;

    // Holds raw image pixels read back from the GPU.
    struct CapturedRawPixels {
        std::vector<std::uint8_t> pixels;
        int width = 0;
        int height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    // Synchronously reads back color pixels from a RenderTexture.
    CapturedRawPixels CaptureRenderTexturePixels(RenderTexture& texture) const;

    // Synchronously reads back raw pixel data from any GPU image.
    CapturedRawPixels CaptureImagePixels(VkImage image, VkImageAspectFlags aspect, VkFormat format, Extent2D extent,
        const rg::ResourceState& previousState, std::uint32_t zOffset = 0, std::uint32_t depth = 1) const;

    // Blocks until all GPU execution finishes. Call only during teardown or manual captures.
    void WaitForGpuIdle() const;

    // Schedules an asynchronous swapchain readback.
    void RequestSwapchainCapture();

    // Retrieves and clears the most recently completed swapchain capture.
    std::optional<CapturedSwapchainPixels> TakeLastCompletedSwapchainCapture();

    // Buffer factory methods.
    Buffer CreateBuffer(uint64_t size, BufferUsage usage, BufferMemoryUsage memoryUsage,
        const char* debugName = nullptr) const;

    Buffer CreateDeviceLocalBuffer(
        const void* data, uint64_t size, BufferUsage usage, const char* debugName = nullptr) const;

    Buffer CreateStructuredBuffer(uint64_t elementStride, std::uint32_t elementCount,
        BufferMemoryUsage memoryUsage, BufferUsage extraUsage = BufferUsage::None, const char* debugName = nullptr) const;

    Buffer CreateGpuSkinningTargetBuffer(uint64_t size, const char* debugName = nullptr) const;

    // Executes a one-shot command buffer immediately on the graphics queue.
    void ImmediateSubmit(const std::function<void(rg::CommandBuffer&)>& recordFn) const;

    // Submits work directly to the graphics queue.
    VkResult SubmitToGraphicsQueue(const VkSubmitInfo2& submit, VkFence fence) const;

    // Prepares the renderer for a new frame and clears queued draw commands.
    void BeginFrame();

    // Queues a mesh draw call for the active frame.
    void Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(),
        const Mat4& viewProjMatrix = Mat4::Identity(),
        DescriptorSetHandle materialDescriptorSet = DescriptorSetHandle::Invalid,
        DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid);

    // Issues an indirect draw call from GPU-side command and count buffers.
    void SubmitIndirect(const Pipeline& pipeline, const Mesh& mesh, VkBuffer indirectBuffer,
        uint64_t indirectOffset, std::uint32_t maxDrawCount, VkBuffer countBuffer, uint64_t countBufferOffset,
        DescriptorSetHandle instanceBufferDescriptorSet,
        DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid,
        const Mat4& viewProjMatrix = Mat4::Identity());

    // Graphics pipeline factories.
    Pipeline CreatePipeline(const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath,
        VertexLayout vertexLayout = VertexLayout::PositionColor, bool useMaterialTexture = false,
        const char* debugName = nullptr, bool useInstanceBuffer = false,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE) const;

    Pipeline CreatePipeline(std::span<const VkFormat> colorFormats, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        bool useMaterialTexture = false, const char* debugName = nullptr, bool useInstanceBuffer = false,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE) const;

    // Vulkan-free overload taking PixelFormat. An empty span builds a depth-only pipeline.
    Pipeline CreatePipeline(std::span<const PixelFormat> colorFormats, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        bool useMaterialTexture = false, const char* debugName = nullptr, bool useInstanceBuffer = false,
        DescriptorSetLayoutHandle sceneServicesSetLayout = DescriptorSetLayoutHandle::Invalid) const;

    // Compute pipeline and descriptor factories.
    ComputePipeline CreateComputePipeline(const std::string& shaderSpirvPath,
        const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
        std::optional<VkPushConstantRange> pushConstantRange = std::nullopt) const;

    ComputeDescriptorSet AllocateComputeDescriptorSet(VkDescriptorSetLayout layout) const;
    ComputeDescriptorSet AllocateComputeDescriptorSet(DescriptorSetLayoutHandle layoutHandle) const;

    // Frees a set from AllocateComputeDescriptorSet(). The GPU must be done with it.
    void FreeComputeDescriptorSet(VkDescriptorSet descriptorSet) const noexcept;

    // Asks the render graph to drop the cached descriptor sets built for a destroyed shader layout.
    void QueueBindingRelease(VkDescriptorSetLayout layout) noexcept;
    std::vector<VkDescriptorSetLayout> TakeQueuedBindingReleases() noexcept;

    // Layout shared by instanced indirect draw buffers.
    VkDescriptorSetLayout InstanceBufferDescriptorSetLayout() const noexcept;

    // Hardware capability checks.
    bool SupportsDrawIndirectCount() const noexcept;
    bool SupportsDepthBlit() const noexcept;

    // Dispatches a compute pipeline within an active render graph pass.
    void Dispatch(const ComputePipeline& pipeline, VkDescriptorSet descriptorSet, const void* pushConstants,
        std::uint32_t pushConstantBytes, std::uint32_t groupCountX, std::uint32_t groupCountY = 1,
        std::uint32_t groupCountZ = 1);

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

    // Texture creation factories.
    Texture2D CreateTexture2D(const void* pixelsRgba8, int width, int height, const char* debugName = nullptr,
        bool allowStorageImageAccess = false) const;

    MaterialTexture CreateMaterialTexture2D(
        const void* pixelsRgba8, int width, int height, const char* debugName = nullptr) const;

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

    // GPU timing queries.
    GpuTimingSample LastGpuTiming(GpuTimingSlot slot) const noexcept;
    void SetGpuTimingCaptureEnabled(bool enabled) noexcept;

    // Descriptor set layout helpers.
    DescriptorSetLayoutHandle CreateDescriptorSetLayout(std::span<const DescriptorBindingDesc> bindings) const;
    void DestroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) const noexcept;

    // One layout per reflected set; the result owns and destroys them.
    ReflectedSetLayouts CreateReflectedSetLayouts(
        const std::vector<ReflectedDescriptorBinding>& bindings, const std::string& label) const;

    // Builds a dynamic graphics pipeline from a descriptive configuration.
    Pipeline CreatePipeline(const PipelineDesc& desc, std::span<const std::byte> vertexSpirv,
        std::span<const std::byte> fragmentSpirv) const;

    // UI texture handles for immediate-mode GUI integration.
    UiTextureHandle GetUiTextureHandle(const Texture2D& texture);
    UiTextureHandle GetUiTextureHandle(const RenderTexture& target);
    void ReleaseUiTextureHandle(UiTextureHandle handle) noexcept;

    // Reads back a render target into engine pixel format for inspection.
    CaptureResult CaptureRenderTarget(const RenderTexture& target) const;

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
        bool supportsDrawIndirectCount = false;
    };

    // Resizes swapchain on window resize events.
    void OnResize(int width, int height);

    // Returns context information required by ImGui's Vulkan backend.
    VulkanContextInfo GetImGuiVulkanBackendInfo() const;

private:
    VulkanContextInfo GetVulkanContextInfo() const;

    friend class rg::RenderGraph;
    friend class rg::RenderGraphPersistentResourceCache;
    friend class rg::RenderGraphComputeBindingCache;
    friend class SceneServicesDescriptorSet;
    friend class GpuDrivenBatchCache;
    friend class VolumeTexturePreviewRenderer;
    friend class RenderFeatureCompositor;
    friend class AtmosphereLutRenderer;
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

    VkCommandBuffer m_currentGraphPassCmd = VK_NULL_HANDLE;
    std::function<void(bool, std::uint32_t, std::uint32_t)> m_currentGraphPassRecordDrawStats;

    mutable std::mutex m_bindingReleaseMutex;
    std::vector<VkDescriptorSetLayout> m_bindingReleaseQueue;
};

// Global helper to query context info for RenderGraph.
Renderer::VulkanContextInfo QueryVulkanContextInfoForRenderGraph(Renderer& renderer);

} // namespace gte
