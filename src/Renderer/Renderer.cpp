#include "Renderer.h"

#include "../Window/Window.h"
#include "RenderGraph/RenderGraph.h"

#include <memory>
#include <utility>

namespace gte {

namespace {

#ifdef NDEBUG
constexpr bool kEnableValidation = false;
#else
constexpr bool kEnableValidation = true;
#endif

} // namespace

Renderer::Renderer(Window& window)
    : m_instance("GreatTamanaEngine", Window::VulkanInstanceExtensions(), kEnableValidation)
    , m_surface(m_instance.Native(), window)
    , m_device(m_instance.Native(), m_surface.Native())
    , m_depthFormat(m_device.PickDepthFormat())
    // m_memoryTracker uses its default member initializer (see Renderer.h) -
    // constructed here implicitly, before m_presenter/m_resources below,
    // both of which receive a copy of the SAME shared_ptr.
    // m_gpuTiming (Phase 4B - PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md)
    // has no default member initializer (see Renderer.h) - it needs
    // m_device's already-resolved native handle/graphics queue+family/
    // queried timestamp capability, so it's constructed explicitly here,
    // right after m_device is ready, before m_presenter below (which
    // receives a copy of the SAME shared_ptr, mirroring m_memoryTracker's
    // own sharing pattern immediately above).
    , m_gpuTiming(std::make_shared<GpuTimingService>(
          m_device.Native(), m_device.GraphicsQueue(), m_device.GraphicsQueueFamily(), m_device.TimestampCapability()))
    // VK_API_VERSION_1_3 matches VulkanInstance::CreateInstance's
    // VkApplicationInfo::apiVersion (see also GetVulkanContextInfo() below).
    , m_allocator(m_instance.Native(), m_device.Physical(), m_device.Native(), VK_API_VERSION_1_3)
    , m_presenter(m_device.Physical(), m_device.Native(), m_surface.Native(), m_device.GraphicsQueueFamily(),
          m_device.PresentQueueFamily(), m_device.GraphicsQueue(), m_device.PresentQueue(), window.Width(),
          window.Height(), m_allocator.Native(), m_depthFormat, m_memoryTracker, m_gpuTiming)
    , m_resources(m_device.Physical(), m_device.Native(), m_allocator.Native(), m_device.GraphicsQueue(),
          m_device.GraphicsQueueFamily(), m_depthFormat, m_memoryTracker)
{
}

Renderer::~Renderer()
{
    if (m_device.Native() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device.Native());
    }
    // m_frameRecorder, m_resources, m_presenter, m_allocator, m_device,
    // m_surface, m_instance clean themselves up automatically after this
    // destructor body, in reverse declaration order.
}

Renderer& Renderer::operator=(Renderer&& other) noexcept
{
    if (this != &other) {
        // Wait for the GPU to finish with everything THIS Renderer
        // currently owns before any of it gets torn down below by the
        // move-assignments of m_presenter/m_resources (which will destroy
        // their old swapchain/command pools/etc.) - neither of those
        // collaborators performs this wait internally, so it must happen
        // exactly once, here, before touching any of them. See also
        // ~Renderer() above.
        if (m_device.Native() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_device.Native());
        }

        m_instance = std::move(other.m_instance);
        m_surface = std::move(other.m_surface);
        m_device = std::move(other.m_device);
        m_depthFormat = other.m_depthFormat;
        m_memoryTracker = std::move(other.m_memoryTracker);
        m_gpuTiming = std::move(other.m_gpuTiming);
        m_allocator = std::move(other.m_allocator);
        m_presenter = std::move(other.m_presenter);
        m_resources = std::move(other.m_resources);
        m_frameRecorder = std::move(other.m_frameRecorder);
    }
    return *this;
}

void Renderer::Clear(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    m_frameRecorder.Clear(r, g, b, a);
}

void Renderer::OnResize(int width, int height)
{
    m_presenter.OnResize(width, height);
}

DrawStats Renderer::RenderOffscreen(
    RenderTexture& target, std::optional<GpuTimingSlot> timingSlot, const std::function<void(VkCommandBuffer)>& recordExtra)
{
    return m_presenter.RenderOffscreen(m_frameRecorder, target, timingSlot, recordExtra);
}

VkCommandBuffer Renderer::BeginOffscreenRenderGraphRecording()
{
    return m_presenter.BeginOffscreenRecording();
}

void Renderer::EndOffscreenRenderGraphRecording()
{
    m_presenter.EndOffscreenRecording();
}

std::optional<DrawStats> Renderer::PresentViaRenderGraph(rg::RenderGraph& graph, bool needsSwapchainDepth,
    const std::function<std::vector<rg::TextureHandle>(rg::RenderGraphBuilder&, rg::TextureHandle)>& build)
{
    const bool ran = m_presenter.PresentViaRenderGraph(graph, needsSwapchainDepth, build);
    if (!ran) {
        return std::nullopt;
    }
    // "Present" must match RenderPasses.cpp's AddPresentPass() pass name
    // literal exactly - see this method's own doc comment in Renderer.h.
    return graph.LastKnownStatsFor("Present").drawStats;
}

void Renderer::BeginGraphPassRecording(
    VkCommandBuffer cmd, std::function<void(bool, std::uint32_t, std::uint32_t)> recordDrawStats)
{
    m_currentGraphPassCmd = cmd;
    m_currentGraphPassRecordDrawStats = std::move(recordDrawStats);
}

void Renderer::EndGraphPassRecording() noexcept
{
    m_currentGraphPassCmd = VK_NULL_HANDLE;
    m_currentGraphPassRecordDrawStats = nullptr;
}

VkFormat Renderer::ColorFormat() const noexcept
{
    return m_presenter.ColorFormat();
}

VkFormat Renderer::DepthFormat() const noexcept
{
    return m_depthFormat;
}

RenderTexture Renderer::CreateRenderTexture(int width, int height, VkFormat format, const char* debugName,
    const char* depthDebugName, bool allowStorageImageAccess) const
{
    // VK_FORMAT_UNDEFINED (the default - see Renderer.h) means "match
    // ColorFormat() exactly", not "let Vulkan pick" - resolved here (the one
    // place that knows both ColorFormat() and the resource factory) rather
    // than baking a hardcoded literal into the default argument, so this
    // always tracks whatever the swapchain actually negotiated at runtime
    // (see VulkanSwapchain.cpp's ChooseSurfaceFormat), even if that differs
    // across GPUs/drivers. See AGENTS.md ("Render Target Format Matching").
    const VkFormat resolvedFormat = (format == VK_FORMAT_UNDEFINED) ? ColorFormat() : format;
    return m_resources.CreateRenderTexture(
        width, height, resolvedFormat, debugName, depthDebugName, allowStorageImageAccess);
}

Buffer Renderer::CreateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage, const char* debugName) const
{
    return m_resources.CreateBuffer(size, usage, memoryUsage, debugName);
}

Buffer Renderer::CreateStructuredBuffer(VkDeviceSize elementStride, std::uint32_t elementCount,
    BufferMemoryUsage memoryUsage, VkBufferUsageFlags extraUsage, const char* debugName) const
{
    return m_resources.CreateStructuredBuffer(elementStride, elementCount, memoryUsage, extraUsage, debugName);
}


Buffer Renderer::CreateDeviceLocalBuffer(
    const void* data, VkDeviceSize size, VkBufferUsageFlags usage, const char* debugName) const
{
    return m_resources.CreateDeviceLocalBuffer(data, size, usage, debugName);
}

void Renderer::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& recordFn) const
{
    m_resources.ImmediateSubmit(recordFn);
}

void Renderer::BeginFrame()
{
    m_frameRecorder.BeginFrame();
}

void Renderer::Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix, const Mat4& viewProjMatrix,
    VkDescriptorSet materialDescriptorSet)
{
    // Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md) -
    // while a render-graph pass is being recorded (BeginGraphPassRecording()
    // was called and hasn't been cleared yet - see Renderer.h), issue this
    // draw's real Vulkan commands DIRECTLY against that pass's own command
    // buffer instead of queuing into the legacy m_frameRecorder draw list -
    // this is the one surgical change that lets Game/RenderSystem keep
    // calling Renderer::Submit() completely unaware anything changed
    // underneath them.
    if (m_currentGraphPassCmd != VK_NULL_HANDLE) {
        const bool hasIndexBuffer = mesh.HasIndexBuffer();
        const std::uint32_t indexCount = hasIndexBuffer ? mesh.IndexCount() : 0;
        FrameRecorder::IssueDrawCommand(m_currentGraphPassCmd, pipeline.Native(), pipeline.Layout(),
            mesh.VertexBuffer(), mesh.VertexCount(), hasIndexBuffer ? mesh.IndexBuffer() : VK_NULL_HANDLE, indexCount,
            modelMatrix, viewProjMatrix, materialDescriptorSet);
        if (m_currentGraphPassRecordDrawStats) {
            m_currentGraphPassRecordDrawStats(hasIndexBuffer, mesh.VertexCount(), indexCount);
        }
        return;
    }

    m_frameRecorder.Submit(pipeline, mesh, modelMatrix, viewProjMatrix, materialDescriptorSet);
}

Pipeline Renderer::CreatePipeline(const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture) const
{
    return m_resources.CreatePipeline(
        ColorFormat(), vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout, useMaterialTexture);
}

ComputePipeline Renderer::CreateComputePipeline(const std::string& shaderSpirvPath,
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
    std::optional<VkPushConstantRange> pushConstantRange) const
{
    return m_resources.CreateComputePipeline(shaderSpirvPath, descriptorSetLayouts, pushConstantRange);
}

VkDescriptorSet Renderer::AllocateComputeDescriptorSet(VkDescriptorSetLayout layout) const
{
    return m_resources.AllocateComputeDescriptorSet(layout);
}

Mesh Renderer::CreateMesh(
    const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount, const char* debugName) const
{
    return m_resources.CreateMesh(vertexData, vertexDataSize, vertexCount, debugName);
}

Mesh Renderer::CreateMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
    const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount, const char* debugName) const
{
    return m_resources.CreateMesh(vertexData, vertexDataSize, vertexCount, indexData, indexDataSize, indexCount, debugName);
}

Mesh Renderer::CreateSkinnedMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
    const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount, const char* debugName) const
{
    return m_resources.CreateSkinnedMesh(
        vertexData, vertexDataSize, vertexCount, indexData, indexDataSize, indexCount, debugName);
}

Texture2D Renderer::CreateTexture2D(
    const void* pixelsRgba8, int width, int height, const char* debugName, bool allowStorageImageAccess) const
{
    return m_resources.CreateTexture2D(pixelsRgba8, width, height, debugName, allowStorageImageAccess);
}

MaterialTexture Renderer::CreateMaterialTexture2D(
    const void* pixelsRgba8, int width, int height, const char* debugName) const
{
    return m_resources.CreateMaterialTexture2D(pixelsRgba8, width, height, debugName);
}

Renderer::VulkanContextInfo Renderer::GetVulkanContextInfo() const
{
    VulkanContextInfo info;
    info.apiVersion = VK_API_VERSION_1_3; // matches VulkanInstance::CreateInstance's VkApplicationInfo::apiVersion
    info.instance = m_instance.Native();
    info.physicalDevice = m_device.Physical();
    info.device = m_device.Native();
    info.graphicsQueueFamily = m_device.GraphicsQueueFamily();
    info.graphicsQueue = m_device.GraphicsQueue();
    info.colorFormat = ColorFormat(); // single source of truth - see ColorFormat()'s comment in Renderer.h
    info.imageCount = m_presenter.ImageCount();
    info.minImageCount = m_presenter.FramesInFlight(); // matches what FramePresenter actually keeps in flight
    // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - the single source of truth
    // gte::rg::RenderGraph's own RenderGraphTimestampPool is built from.
    info.timestampCapability = m_device.TimestampCapability();
    return info;
}

GpuMemoryTracker::Totals Renderer::GetMemoryTotals() const
{
    return m_resources.GetMemoryTotals();
}

std::vector<GpuMemoryTracker::Entry> Renderer::GetMemoryResources() const
{
    return m_resources.GetMemoryResources();
}

#if GTE_ENABLE_EDITOR
const std::string& Renderer::GetMemoryDebugName(GpuResourceHandle handle) const
{
    return m_resources.GetMemoryDebugName(handle);
}
#endif

std::vector<VmaBudget> Renderer::GetVmaHeapBudgets() const
{
    return m_allocator.GetHeapBudgets();
}

GpuTimingSample Renderer::LastGpuTiming(GpuTimingSlot slot) const noexcept
{
    return m_gpuTiming->LastKnown(slot);
}

void Renderer::SetGpuTimingCaptureEnabled(bool enabled) noexcept
{
    m_gpuTiming->SetCaptureEnabled(enabled);
}

} // namespace gte
