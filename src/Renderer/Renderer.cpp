#include "Renderer.h"

#include "../Core/ISurfaceProvider.h"
#include "RenderGraph/GpuBufferInternal.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/RenderGraphBarrierPlanner.h"
#include "UiTextureBackend.h"
#include "Vulkan/DescriptorCost.h"
#include "Vulkan/DescriptorSetLayoutBuilder.h"
#include "Vulkan/ReflectedSetLayouts.h"
#include "Vulkan/ShaderStageMask.h"
#include "VulkanFormatInfo.h"

#include <cassert>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gte {

namespace {

#ifdef NDEBUG
constexpr bool kEnableValidation = false;
#else
constexpr bool kEnableValidation = true;
#endif

// Conversion helpers for PipelineDesc to Vulkan types.
VkPrimitiveTopology ToVkTopology(Topology topology) noexcept
{
    switch (topology) {
        case Topology::LineList:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case Topology::PointList:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case Topology::TriangleList:
        default:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkCullModeFlags ToVkCullMode(CullMode cullMode) noexcept
{
    switch (cullMode) {
        case CullMode::Front:
            return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:
            return VK_CULL_MODE_BACK_BIT;
        case CullMode::None:
        default:
            return VK_CULL_MODE_NONE;
    }
}

VkCompareOp ToVkCompareOp(CompareOp op) noexcept
{
    switch (op) {
        case CompareOp::Never:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:
            return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::GreaterEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:
        default:
            return VK_COMPARE_OP_ALWAYS;
    }
}

// Returns the color blend attachment state for a given BlendMode.
VkPipelineColorBlendAttachmentState ToVkBlendState(BlendMode mode) noexcept
{
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    switch (mode) {
        case BlendMode::AlphaBlend:
            state.blendEnable = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            state.colorBlendOp = VK_BLEND_OP_ADD;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            state.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BlendMode::Additive:
            state.blendEnable = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            state.colorBlendOp = VK_BLEND_OP_ADD;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BlendMode::Opaque:
        default:
            state.blendEnable = VK_FALSE;
            break;
    }
    return state;
}

// Tracks active UI descriptor sets to prevent double-releases.
std::mutex g_uiTextureMutex;
std::unordered_set<VkDescriptorSet> g_liveUiTextures;

} // namespace

Renderer::Renderer(ISurfaceProvider& surfaceProvider)
    : m_instance("GreatTamanaEngine", surfaceProvider.VulkanInstanceExtensions(), kEnableValidation)
    , m_surface(m_instance.Native(), surfaceProvider)
    , m_device(m_instance.Native(), m_surface.Native())
    , m_depthFormat(m_device.PickDepthFormat())
    , m_gpuTiming(std::make_shared<GpuTimingService>(m_device, m_device.TimestampCapability()))
    , m_allocator(m_instance.Native(), m_device.Physical(), m_device.Native(), VK_API_VERSION_1_3)
    , m_presenter(m_device.Physical(), m_device.Native(), m_surface.Native(), m_device.GraphicsQueueFamily(),
          m_device.PresentQueueFamily(), m_device.GraphicsQueue(), m_device.PresentQueue(), surfaceProvider.Width(),
          surfaceProvider.Height(), m_allocator.Native(), m_depthFormat, m_memoryTracker, m_gpuTiming, m_device)
    , m_resources(m_device.Physical(), m_device.Native(), m_allocator.Native(), m_device.GraphicsQueue(),
          m_device.GraphicsQueueFamily(), m_depthFormat, m_memoryTracker, m_device)
{
}

Renderer::~Renderer()
{
    if (m_device.Native() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device.Native());
        for (const VkFence fence : m_fencePool) {
            vkDestroyFence(m_device.Native(), fence, nullptr);
        }
    }
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
    RenderTexture& target, std::optional<GpuTimingSlot> timingSlot, const std::function<void(rg::CommandBuffer&)>& recordExtra)
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

PixelFormat Renderer::ColorPixelFormat() const noexcept
{
    return FromVkFormat(ColorFormat());
}

PixelFormat Renderer::DepthPixelFormat() const noexcept
{
    return FromVkFormat(DepthFormat());
}

RenderTexture Renderer::CreateRenderTexture(int width, int height, PixelFormat format, const char* debugName,
    const char* depthDebugName, bool allowStorageImageAccess, bool allowDepthSampledAccess,
    bool createDepthCompanion, bool createColorImage, bool transferSrc, bool transferDst) const
{
    // Defaults to the swapchain color format if format is undefined.
    const PixelFormat resolvedFormat = (format == PixelFormat::Undefined) ? ColorPixelFormat() : format;
    return m_resources.CreateRenderTexture(width, height, resolvedFormat, debugName, depthDebugName,
        allowStorageImageAccess, allowDepthSampledAccess, createDepthCompanion, createColorImage, transferSrc,
        transferDst);
}

Renderer::CapturedRawPixels Renderer::CaptureImagePixels(VkImage image, VkImageAspectFlags aspect, VkFormat format,
    Extent2D extentIn, const rg::ResourceState& previousState, std::uint32_t zOffset, std::uint32_t depth) const
{
    const VkExtent2D extent = ToVkExtent2D(extentIn);
    const int bytesPerPixel = BytesPerTexelForFormat(format);
    if (bytesPerPixel == 0) {
        return CapturedRawPixels{}; // Unsupported format.
    }

    const uint64_t size =
        uint64_t(extent.width) * extent.height * depth * static_cast<uint64_t>(bytesPerPixel);
    Buffer readback = CreateBuffer(size, BufferUsage::TransferDst, BufferMemoryUsage::GpuToCpu, "CaptureReadback");

    const VkImageSubresourceRange range{ aspect, 0, 1, 0, 1 };
    const rg::ResourceState transferSrcState = rg::RequiredStateFor(rg::ResourceAccess::TransferSrc, false);

    ImmediateSubmit([&](rg::CommandBuffer& cmd) {
        rg::EmitImageBarrier(cmd.RawCommandBuffer(), image, range, previousState, transferSrcState);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = aspect;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, static_cast<std::int32_t>(zOffset) };
        region.imageExtent = { extent.width, extent.height, depth };
        vkCmdCopyImageToBuffer(cmd.RawCommandBuffer(), image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.Native(), 1, &region);

        // Memory barrier to ensure device copy is visible to host read.
        const rg::ResourceState transferWriteState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT };
        const rg::ResourceState hostReadState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT };
        rg::EmitBufferBarrier(cmd.RawCommandBuffer(), readback.Native(), 0, size, transferWriteState, hostReadState);

        rg::EmitImageBarrier(cmd.RawCommandBuffer(), image, range, transferSrcState, previousState);
    });

    CapturedRawPixels result;
    result.pixels.resize(static_cast<std::size_t>(size));
    std::memcpy(result.pixels.data(), readback.MappedData(), static_cast<std::size_t>(size));
    result.width = static_cast<int>(extent.width);
    result.height = static_cast<int>(extent.height);
    result.format = CapturedPixelFormatFromVk(format);
    return result;
}

Renderer::CapturedRawPixels Renderer::CaptureRenderTexturePixels(RenderTexture& texture) const
{
    const rg::ResourceState shaderReadState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    return CaptureImagePixels(
        texture.Image(), VK_IMAGE_ASPECT_COLOR_BIT, texture.NativeFormat(), texture.Extent(), shaderReadState);
}

void Renderer::WaitForGpuIdle() const
{
    vkDeviceWaitIdle(m_device.Native());
}

void Renderer::RequestSwapchainCapture()
{
    m_presenter.RequestSwapchainCapture();
}

std::optional<CapturedSwapchainPixels> Renderer::TakeLastCompletedSwapchainCapture()
{
    return m_presenter.TakeLastCompletedSwapchainCapture();
}

Buffer Renderer::CreateBuffer(
    uint64_t size, BufferUsage usage, BufferMemoryUsage memoryUsage, const char* debugName) const
{
    return m_resources.CreateBuffer(size, usage, memoryUsage, debugName);
}

Buffer Renderer::CreateStructuredBuffer(uint64_t elementStride, std::uint32_t elementCount,
    BufferMemoryUsage memoryUsage, BufferUsage extraUsage, const char* debugName) const
{
    return m_resources.CreateStructuredBuffer(elementStride, elementCount, memoryUsage, extraUsage, debugName);
}

Buffer Renderer::CreateGpuSkinningTargetBuffer(uint64_t size, const char* debugName) const
{
    return m_resources.CreateGpuSkinningTargetBuffer(size, debugName);
}

Buffer Renderer::CreateDeviceLocalBuffer(
    const void* data, uint64_t size, BufferUsage usage, const char* debugName) const
{
    return m_resources.CreateDeviceLocalBuffer(data, size, usage, debugName);
}

// Executes a command buffer immediately on the graphics queue and waits for completion.
void Renderer::ImmediateSubmit(const std::function<void(rg::CommandBuffer&)>& recordFn) const
{
    assert(m_currentGraphPassCmd == VK_NULL_HANDLE
        && "ImmediateSubmit() called while a graph pass is already being recorded");

    VkCommandBuffer vkCmd = m_resources.AllocateTransientCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(vkCmd, &beginInfo) != VK_SUCCESS) {
        m_resources.FreeTransientCommandBuffer(vkCmd);
        throw std::runtime_error("Renderer::ImmediateSubmit: vkBeginCommandBuffer failed.");
    }

    rg::CommandBuffer cmd = rg::CommandBuffer::AdoptRaw(vkCmd, const_cast<Renderer*>(this));
    try {
        recordFn(cmd);
    } catch (...) {
        m_resources.FreeTransientCommandBuffer(vkCmd);
        throw;
    }

    if (vkEndCommandBuffer(vkCmd) != VK_SUCCESS) {
        m_resources.FreeTransientCommandBuffer(vkCmd);
        throw std::runtime_error("Renderer::ImmediateSubmit: vkEndCommandBuffer failed.");
    }

    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = vkCmd;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdSubmit;

    const VkFence fence = AcquireFence();
    if (fence == VK_NULL_HANDLE) {
        m_resources.FreeTransientCommandBuffer(vkCmd);
        throw std::runtime_error("Renderer::ImmediateSubmit: vkCreateFence failed.");
    }

    if (m_device.SubmitToGraphicsQueue(submit, fence) != VK_SUCCESS) {
        ReleaseFence(fence);
        m_resources.FreeTransientCommandBuffer(vkCmd);
        throw std::runtime_error("Renderer::ImmediateSubmit: vkQueueSubmit2 failed.");
    }

    vkWaitForFences(m_device.Native(), 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    ReleaseFence(fence);
    m_resources.FreeTransientCommandBuffer(vkCmd);
}

VkFence Renderer::AcquireFence() const
{
    {
        const std::lock_guard<std::mutex> lock(m_fencePoolMutex);
        if (!m_fencePool.empty()) {
            const VkFence fence = m_fencePool.back();
            m_fencePool.pop_back();
            return fence;
        }
    }
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    return vkCreateFence(m_device.Native(), &fenceInfo, nullptr, &fence) == VK_SUCCESS ? fence : VK_NULL_HANDLE;
}

void Renderer::ReleaseFence(VkFence fence) const noexcept
{
    vkResetFences(m_device.Native(), 1, &fence);
    try {
        const std::lock_guard<std::mutex> lock(m_fencePoolMutex);
        m_fencePool.push_back(fence);
    } catch (...) {
        vkDestroyFence(m_device.Native(), fence, nullptr);
    }
}

rg::RenderGraph& Renderer::OneShotGraph()
{
    if (!m_oneShotGraph) {
        m_oneShotGraph = std::make_unique<rg::RenderGraph>(*this);
    }
    return *m_oneShotGraph;
}

VkResult Renderer::SubmitToGraphicsQueue(const VkSubmitInfo2& submit, VkFence fence) const
{
    return m_device.SubmitToGraphicsQueue(submit, fence);
}

void Renderer::BeginFrame()
{
    m_frameRecorder.BeginFrame();
}

void Renderer::Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix, const Mat4& viewProjMatrix,
    DescriptorSetHandle materialDescriptorSet, DescriptorSetHandle sceneServicesSet)
{
    // Bind scene services set only if the pipeline supports it.
    const VkDescriptorSet effectiveSceneServicesSet =
        pipeline.HasSceneServicesSet() ? Native(sceneServicesSet) : VK_NULL_HANDLE;

    // Bind material set only if the pipeline has a material layout.
    const VkDescriptorSet effectiveMaterialDescriptorSet =
        pipeline.HasMaterialSet() ? Native(materialDescriptorSet) : VK_NULL_HANDLE;

    // If a render graph pass is active, record directly to its command buffer.
    if (m_currentGraphPassCmd != VK_NULL_HANDLE) {
        const bool hasIndexBuffer = mesh.HasIndexBuffer();
        const std::uint32_t indexCount = hasIndexBuffer ? mesh.IndexCount() : 0;
        FrameRecorder::IssueDrawCommand(m_currentGraphPassCmd, pipeline.Native(), pipeline.Layout(),
            mesh.VertexBuffer(), mesh.VertexCount(), hasIndexBuffer ? mesh.IndexBuffer() : VK_NULL_HANDLE, indexCount,
            modelMatrix, viewProjMatrix, effectiveMaterialDescriptorSet, effectiveSceneServicesSet);
        if (m_currentGraphPassRecordDrawStats) {
            m_currentGraphPassRecordDrawStats(hasIndexBuffer, mesh.VertexCount(), indexCount);
        }
        return;
    }

    m_frameRecorder.Submit(
        pipeline, mesh, modelMatrix, viewProjMatrix, effectiveMaterialDescriptorSet, effectiveSceneServicesSet);
}

void Renderer::SubmitIndirect(const Pipeline& pipeline, const Mesh& mesh, VkBuffer indirectBuffer,
    uint64_t indirectOffset, std::uint32_t maxDrawCount, VkBuffer countBuffer, uint64_t countBufferOffset,
    DescriptorSetHandle instanceBufferDescriptorSet, DescriptorSetHandle sceneServicesSet, const Mat4& viewProjMatrix)
{
    assert(m_currentGraphPassCmd != VK_NULL_HANDLE
        && "Renderer::SubmitIndirect() called outside of a render-graph pass recording.");
    if (m_currentGraphPassCmd == VK_NULL_HANDLE) {
        return;
    }

    assert(mesh.HasIndexBuffer()
        && "Renderer::SubmitIndirect(): indexed mesh required for indirect draw.");

    const VkDescriptorSet effectiveSceneServicesSet =
        pipeline.HasSceneServicesSet() ? Native(sceneServicesSet) : VK_NULL_HANDLE;

    FrameRecorder::IssueIndirectDrawCommand(m_currentGraphPassCmd, pipeline.Native(), pipeline.Layout(),
        mesh.VertexBuffer(), mesh.IndexBuffer(), Native(instanceBufferDescriptorSet), effectiveSceneServicesSet,
        viewProjMatrix, indirectBuffer, indirectOffset, maxDrawCount, countBuffer, countBufferOffset,
        m_device.SupportsDrawIndirectCount());
}

Pipeline Renderer::CreatePipeline(const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture,
    const char* debugName, bool useInstanceBuffer, DescriptorSetLayoutHandle sceneServicesSetLayout) const
{
    return m_resources.CreatePipeline(ColorFormat(), vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout,
        useMaterialTexture, debugName, useInstanceBuffer, Native(sceneServicesSetLayout));
}

Pipeline Renderer::CreatePipeline(std::span<const VkFormat> colorFormats, const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture,
    const char* debugName, bool useInstanceBuffer, VkDescriptorSetLayout sceneServicesSetLayout) const
{
    return m_resources.CreatePipeline(colorFormats, vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout,
        useMaterialTexture, debugName, useInstanceBuffer, sceneServicesSetLayout);
}

Pipeline Renderer::CreatePipeline(std::span<const PixelFormat> colorFormats, const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture,
    const char* debugName, bool useInstanceBuffer, DescriptorSetLayoutHandle sceneServicesSetLayout) const
{
    std::vector<VkFormat> vkColorFormats;
    vkColorFormats.reserve(colorFormats.size());
    for (const PixelFormat format : colorFormats) {
        vkColorFormats.push_back(ToVkFormat(format));
    }
    return m_resources.CreatePipeline(vkColorFormats, vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout,
        useMaterialTexture, debugName, useInstanceBuffer, Native(sceneServicesSetLayout));
}

ComputePipeline Renderer::CreateComputePipeline(const std::string& shaderSpirvPath,
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
    std::optional<VkPushConstantRange> pushConstantRange) const
{
    return m_resources.CreateComputePipeline(shaderSpirvPath, descriptorSetLayouts, pushConstantRange);
}

ComputeDescriptorSet Renderer::AllocateComputeDescriptorSet(VkDescriptorSetLayout layout) const
{
    return m_resources.AllocateComputeDescriptorSet(layout);
}

ComputeDescriptorSet Renderer::AllocateComputeDescriptorSet(DescriptorSetLayoutHandle layoutHandle) const
{
    return m_resources.AllocateComputeDescriptorSet(Native(layoutHandle));
}

void Renderer::FreeComputeDescriptorSet(VkDescriptorSet descriptorSet) const noexcept
{
    m_resources.FreeComputeDescriptorSet(descriptorSet);
}

void Renderer::QueueBindingRelease(VkDescriptorSetLayout layout) noexcept
{
    m_resources.ForgetComputeLayout(layout);
    try {
        const std::lock_guard<std::mutex> lock(m_bindingReleaseMutex);
        m_bindingReleaseQueue.push_back(layout);
    } catch (...) {
        // Out of memory: the stale-set sweep still frees the entries later.
    }
}

std::vector<VkDescriptorSetLayout> Renderer::TakeQueuedBindingReleases() noexcept
{
    try {
        const std::lock_guard<std::mutex> lock(m_bindingReleaseMutex);
        return std::exchange(m_bindingReleaseQueue, {});
    } catch (...) {
        return {}; // The stale-set sweep still frees those sets later.
    }
}

VkDescriptorSetLayout Renderer::InstanceBufferDescriptorSetLayout() const noexcept
{
    return m_resources.InstanceBufferDescriptorSetLayout();
}

bool Renderer::SupportsDrawIndirectCount() const noexcept
{
    return m_device.SupportsDrawIndirectCount();
}

bool Renderer::SupportsDepthBlit() const noexcept
{
    return m_device.SupportsDepthBlit();
}

void Renderer::Dispatch(const ComputePipeline& pipeline, VkDescriptorSet descriptorSet, const void* pushConstants,
    std::uint32_t pushConstantBytes, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ)
{
    assert(m_currentGraphPassCmd != VK_NULL_HANDLE
        && "Renderer::Dispatch() called outside of a render-graph pass recording.");
    if (m_currentGraphPassCmd == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(m_currentGraphPassCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Native());
    if (descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(
            m_currentGraphPassCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout(), 0, 1, &descriptorSet, 0, nullptr);
    }
    if (pushConstantBytes > 0 && pushConstants != nullptr) {
        vkCmdPushConstants(
            m_currentGraphPassCmd, pipeline.Layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantBytes, pushConstants);
    }
    vkCmdDispatch(m_currentGraphPassCmd, groupCountX, groupCountY, groupCountZ);
}

Mesh Renderer::CreateMesh(
    const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount, const char* debugName) const
{
    return m_resources.CreateMesh(vertexData, vertexDataSize, vertexCount, debugName);
}

Mesh Renderer::CreateMesh(const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount,
    const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount, const char* debugName) const
{
    return m_resources.CreateMesh(vertexData, vertexDataSize, vertexCount, indexData, indexDataSize, indexCount, debugName);
}

Mesh Renderer::CreateSkinnedMesh(const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount,
    const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount, const char* debugName) const
{
    return m_resources.CreateSkinnedMesh(
        vertexData, vertexDataSize, vertexCount, indexData, indexDataSize, indexCount, debugName);
}

std::shared_ptr<Buffer> Renderer::CreateSharedMeshVertexBuffer(
    const void* vertexData, uint64_t vertexDataSize, const char* debugName) const
{
    return m_resources.CreateSharedMeshVertexBuffer(vertexData, vertexDataSize, debugName);
}

std::shared_ptr<Buffer> Renderer::CreateSharedSkinnedMeshVertexBuffer(
    const void* vertexData, uint64_t vertexDataSize, const char* debugName) const
{
    return m_resources.CreateSharedSkinnedMeshVertexBuffer(vertexData, vertexDataSize, debugName);
}

Mesh Renderer::CreateMeshFromSharedVertexBuffer(std::shared_ptr<Buffer> sharedVertexBuffer,
    std::uint32_t vertexCount, const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount,
    const char* debugName) const
{
    return m_resources.CreateMeshFromSharedVertexBuffer(
        std::move(sharedVertexBuffer), vertexCount, indexData, indexDataSize, indexCount, debugName);
}

Mesh Renderer::CreateMeshFromSharedVertexBuffer(const rg::GpuBuffer& sharedVertexBuffer, std::uint32_t vertexCount,
    const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount, const char* debugName) const
{
    return m_resources.CreateMeshFromSharedVertexBuffer(rg::GpuBufferAccess::Shared(sharedVertexBuffer), vertexCount,
        indexData, indexDataSize, indexCount, debugName);
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

VolumeTexture Renderer::CreateVolumeTexture(
    int width, int height, int depth, PixelFormat format, const char* debugName) const
{
    return m_resources.CreateVolumeTexture(width, height, depth, format, debugName);
}

TextureArray2D Renderer::CreateTextureArray(int width, int height, int arrayLayers, PixelFormat format, bool hasDepth,
    bool isCubemap, bool allowStorageImageAccess, const char* debugName) const
{
    // Resolves undefined format to depth or color format based on hasDepth.
    const PixelFormat resolvedFormat = (format == PixelFormat::Undefined)
        ? (hasDepth ? DepthPixelFormat() : ColorPixelFormat())
        : format;
    return m_resources.CreateTextureArray(
        width, height, arrayLayers, resolvedFormat, hasDepth, isCubemap, allowStorageImageAccess, debugName);
}

Renderer::VulkanContextInfo Renderer::GetVulkanContextInfo() const
{
    VulkanContextInfo info;
    info.apiVersion = VK_API_VERSION_1_3;
    info.instance = m_instance.Native();
    info.physicalDevice = m_device.Physical();
    info.device = m_device.Native();
    info.graphicsQueueFamily = m_device.GraphicsQueueFamily();
    info.graphicsQueue = m_device.GraphicsQueue();
    info.colorFormat = ColorFormat();
    info.imageCount = m_presenter.ImageCount();
    info.minImageCount = m_presenter.FramesInFlight();
    info.timestampCapability = m_device.TimestampCapability();
    info.supportsDrawIndirectCount = m_device.SupportsDrawIndirectCount();
    return info;
}

Renderer::VulkanContextInfo Renderer::GetImGuiVulkanBackendInfo() const
{
    return GetVulkanContextInfo();
}

GpuMemoryTracker::Totals Renderer::GetMemoryTotals() const
{
    return m_resources.GetMemoryTotals();
}

std::vector<GpuMemoryTracker::Entry> Renderer::GetMemoryResources() const
{
    return m_resources.GetMemoryResources();
}

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

DescriptorSetLayoutHandle Renderer::CreateDescriptorSetLayout(std::span<const DescriptorBindingDesc> bindings) const
{
    DescriptorSetLayoutBuilder builder(m_device.Native());
    PoolCapacity cost{ .sets = 1 };
    for (const DescriptorBindingDesc& binding : bindings) {
        const VkShaderStageFlags stages = ToVkShaderStageFlags(binding.stages);
        switch (binding.kind) {
            case DescriptorKind::CombinedImageSampler:
                builder.AddCombinedImageSampler(binding.binding, stages, binding.count);
                AddDescriptorCost(cost, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, binding.count);
                break;
            case DescriptorKind::UniformBuffer:
                builder.AddUniformBuffer(binding.binding, stages, binding.count);
                AddDescriptorCost(cost, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, binding.count);
                break;
            case DescriptorKind::StorageBuffer:
                builder.AddStorageBuffer(binding.binding, stages, binding.count);
                AddDescriptorCost(cost, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, binding.count);
                break;
            case DescriptorKind::StorageImage:
                builder.AddStorageImage(binding.binding, stages, binding.count);
                AddDescriptorCost(cost, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, binding.count);
                break;
        }
    }
    const VkDescriptorSetLayout layout = builder.Build();
    m_resources.RegisterLayoutCost(layout, cost);
    return RegisterDescriptorSetLayout(layout);
}

void Renderer::DestroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) const noexcept
{
    const VkDescriptorSetLayout layout = Native(handle);
    UnregisterDescriptorSetLayout(handle);
    if (layout != VK_NULL_HANDLE) {
        m_resources.ForgetComputeLayout(layout);
        vkDestroyDescriptorSetLayout(m_device.Native(), layout, nullptr);
    }
}

ReflectedSetLayouts Renderer::CreateReflectedSetLayouts(
    const std::vector<ReflectedDescriptorBinding>& bindings, const std::string& label) const
{
    ReflectedSetLayouts layouts(m_device.Native(), bindings, label);
    for (const DescriptorBindingSetGroup& group : GroupDescriptorBindingsBySet(bindings)) {
        m_resources.RegisterLayoutCost(layouts.Native(group.set), DescriptorCostOf(group.bindings));
    }
    return layouts;
}

DescriptorPoolStats Renderer::GetDescriptorPoolStats() const
{
    return m_resources.DescriptorStats();
}

Pipeline Renderer::CreatePipeline(
    const PipelineDesc& desc, std::span<const std::byte> vertexSpirv, std::span<const std::byte> fragmentSpirv) const
{
    VkFormat colorFormats[PipelineDesc::kMaxColorAttachments]{};
    const std::span<const PixelFormat> colorFmts = desc.ColorFormats();
    for (std::size_t i = 0; i < colorFmts.size(); ++i) {
        colorFormats[i] = ToVkFormat(colorFmts[i]);
    }

    // Unwrap descriptor set layouts in order.
    std::vector<VkDescriptorSetLayout> setLayouts;
    if (desc.layout.sceneServicesSetLayout != DescriptorSetLayoutHandle::Invalid) {
        setLayouts.push_back(Native(desc.layout.sceneServicesSetLayout));
    }
    for (DescriptorSetLayoutHandle handle : desc.layout.extraSetLayouts) {
        setLayouts.push_back(Native(handle));
    }

    return Pipeline(m_device.Native(), std::span<const VkFormat>(colorFormats, colorFmts.size()),
        ToVkFormat(desc.depthAttachmentFormat), vertexSpirv, fragmentSpirv, desc.vertexLayout,
        ToVkTopology(desc.topology), ToVkCullMode(desc.cullMode), desc.depthTest, desc.depthWrite,
        ToVkCompareOp(desc.depthCompare), ToVkBlendState(desc.blendMode), setLayouts,
        ToVkShaderStageFlags(desc.layout.pushConstantStages), desc.layout.pushConstantByteSize, desc.debugName);
}

UiTextureHandle Renderer::GetUiTextureHandle(const Texture2D& texture)
{
    const VkDescriptorSet set = AddUiTexture(texture.Sampler(), texture.View());
    if (set == VK_NULL_HANDLE) {
        return UiTextureHandle::Invalid; // No UI backend installed.
    }
    std::lock_guard lock(g_uiTextureMutex);
    g_liveUiTextures.insert(set);
    return static_cast<UiTextureHandle>(reinterpret_cast<std::uintptr_t>(set));
}

UiTextureHandle Renderer::GetUiTextureHandle(const RenderTexture& target)
{
    const VkDescriptorSet set = AddUiTexture(target.Sampler(), target.View());
    if (set == VK_NULL_HANDLE) {
        return UiTextureHandle::Invalid;
    }
    std::lock_guard lock(g_uiTextureMutex);
    g_liveUiTextures.insert(set);
    return static_cast<UiTextureHandle>(reinterpret_cast<std::uintptr_t>(set));
}

void Renderer::ReleaseUiTextureHandle(UiTextureHandle handle) noexcept
{
    const auto set = reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(static_cast<std::uint64_t>(handle)));
    {
        std::lock_guard lock(g_uiTextureMutex);
        if (g_liveUiTextures.erase(set) == 0) {
            return; // Already released or invalid handle.
        }
    }
    RemoveUiTexture(set);
}

CaptureResult Renderer::CaptureRenderTarget(const RenderTexture& target) const
{
    const rg::ResourceState shaderReadState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const CapturedRawPixels raw =
        CaptureImagePixels(target.Image(), VK_IMAGE_ASPECT_COLOR_BIT, target.NativeFormat(), target.Extent(), shaderReadState);

    CaptureResult result;
    result.extent = Extent2D{ static_cast<std::uint32_t>(raw.width), static_cast<std::uint32_t>(raw.height) };
    result.format = raw.format;
    result.pixels.resize(raw.pixels.size());
    std::memcpy(result.pixels.data(), raw.pixels.data(), raw.pixels.size());
    return result;
}

} // namespace gte
