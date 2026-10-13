#include "Renderer.h"

#include "../Core/ISurfaceProvider.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/RenderGraphBarrierPlanner.h"
#include "UiTextureBackend.h"
#include "Vulkan/DescriptorSetLayoutBuilder.h"
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

// --- GAP2: gte::PipelineDesc <-> Vk* conversion (Renderer-internal only) ---
VkShaderStageFlags ToVkShaderStageFlags(ShaderStageMask mask) noexcept
{
    VkShaderStageFlags flags = 0;
    if (HasStage(mask, ShaderStageMask::Vertex)) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (HasStage(mask, ShaderStageMask::Fragment)) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (HasStage(mask, ShaderStageMask::Compute)) {
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return flags;
}

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

// One blend attachment state per BlendMode - applied identically to every
// color attachment a Pipeline built via PipelineDesc has (see
// Pipeline::Pipeline()'s configurable constructor).
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

// GAP5: the live set of descriptor sets GetUiTextureHandle() has minted,
// guarded by its own mutex - exists purely so ReleaseUiTextureHandle() can
// reject a double-release/foreign value instead of calling
// ImGui_ImplVulkan_RemoveTexture() on garbage. The handle VALUE itself is
// the real VkDescriptorSet bit pattern (see Renderer.h's own doc comment on
// GetUiTextureHandle()), never a separate generation-checked index.
std::mutex g_uiTextureMutex;
std::unordered_set<VkDescriptorSet> g_liveUiTextures;

} // namespace

Renderer::Renderer(ISurfaceProvider& surfaceProvider)
    : m_instance("GreatTamanaEngine", surfaceProvider.VulkanInstanceExtensions(), kEnableValidation)
    , m_surface(m_instance.Native(), surfaceProvider)
    , m_device(m_instance.Native(), m_surface.Native())
    , m_depthFormat(m_device.PickDepthFormat())
    // m_memoryTracker uses its default member initializer (see Renderer.h) -
    // constructed here implicitly, before m_presenter/m_resources below,
    // both of which receive a copy of the SAME shared_ptr.
    // m_gpuTiming (Phase 4B - PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md)
    // has no default member initializer (see Renderer.h) - it needs
    // m_device's already-resolved native handle/queried timestamp
    // capability, so it's constructed explicitly here, right after m_device
    // is ready, before m_presenter below (which receives a copy of the SAME
    // shared_ptr, mirroring m_memoryTracker's own sharing pattern
    // immediately above).
    , m_gpuTiming(std::make_shared<GpuTimingService>(m_device, m_device.TimestampCapability()))
    // VK_API_VERSION_1_3 matches VulkanInstance::CreateInstance's
    // VkApplicationInfo::apiVersion (see also GetVulkanContextInfo() below).
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
    }
    // m_frameRecorder, m_resources, m_presenter, m_allocator, m_device,
    // m_surface, m_instance clean themselves up automatically after this
    // destructor body, in reverse declaration order.
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

PixelFormat Renderer::ColorPixelFormat() const noexcept
{
    return FromVkFormat(ColorFormat());
}

PixelFormat Renderer::DepthPixelFormat() const noexcept
{
    return FromVkFormat(DepthFormat());
}

RenderTexture Renderer::CreateRenderTexture(int width, int height, VkFormat format, const char* debugName,
    const char* depthDebugName, bool allowStorageImageAccess, bool allowDepthSampledAccess,
    bool createDepthCompanion, bool createColorImage) const
{
    // VK_FORMAT_UNDEFINED (the default - see Renderer.h) means "match
    // ColorFormat() exactly", not "let Vulkan pick" - resolved here (the one
    // place that knows both ColorFormat() and the resource factory) rather
    // than baking a hardcoded literal into the default argument, so this
    // always tracks whatever the swapchain actually negotiated at runtime
    // (see VulkanSwapchain.cpp's ChooseSurfaceFormat), even if that differs
    // across GPUs/drivers. See AGENTS.md ("Render Target Format Matching").
    const VkFormat resolvedFormat = (format == VK_FORMAT_UNDEFINED) ? ColorFormat() : format;
    return m_resources.CreateRenderTexture(width, height, resolvedFormat, debugName, depthDebugName,
        allowStorageImageAccess, allowDepthSampledAccess, createDepthCompanion, createColorImage);
}

Renderer::CapturedRawPixels Renderer::CaptureImagePixels(VkImage image, VkImageAspectFlags aspect, VkFormat format,
    Extent2D extentIn, const rg::ResourceState& previousState, std::uint32_t zOffset, std::uint32_t depth) const
{
    const VkExtent2D extent = ToVkExtent2D(extentIn);
    const int bytesPerPixel = BytesPerTexelForFormat(format);
    if (bytesPerPixel == 0) {
        return CapturedRawPixels{}; // Unsupported format - pixels stays empty, caller fails cleanly.
    }

    const VkDeviceSize size =
        VkDeviceSize(extent.width) * extent.height * depth * static_cast<VkDeviceSize>(bytesPerPixel);
    Buffer readback = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferMemoryUsage::GpuToCpu, "CaptureReadback");

    const VkImageSubresourceRange range{ aspect, 0, 1, 0, 1 };
    const rg::ResourceState transferSrcState = rg::RequiredStateFor(rg::ResourceAccess::TransferSrc, false);

    ImmediateSubmit([&](rg::CommandBuffer& cmd) {
        rg::EmitImageBarrier(cmd.Native(), image, range, previousState, transferSrcState);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = aspect;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, static_cast<std::int32_t>(zOffset) };
        region.imageExtent = { extent.width, extent.height, depth };
        vkCmdCopyImageToBuffer(cmd.Native(), image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.Native(), 1, &region);

        // Host-read visibility: a fence wait alone (ImmediateSubmit()'s own
        // wait, below) guarantees the GPU write finished EXECUTING, but the
        // Vulkan spec still requires an explicit memory dependency whose
        // destination stage/access includes HOST_BIT/HOST_READ_BIT before a
        // device write is guaranteed VISIBLE to a later host read - mirrors
        // CaptureRenderTexturePixels()'s original own reasoning for this step.
        const rg::ResourceState transferWriteState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT };
        const rg::ResourceState hostReadState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT };
        rg::EmitBufferBarrier(cmd.Native(), readback.Native(), 0, size, transferWriteState, hostReadState);

        rg::EmitImageBarrier(cmd.Native(), image, range, transferSrcState, previousState);
    });

    CapturedRawPixels result;
    result.pixels.resize(static_cast<std::size_t>(size));
    std::memcpy(result.pixels.data(), readback.MappedData(), static_cast<std::size_t>(size));
    result.width = static_cast<int>(extent.width);
    result.height = static_cast<int>(extent.height);
    result.format = format;
    return result;
}

Renderer::CapturedRawPixels Renderer::CaptureRenderTexturePixels(RenderTexture& texture) const
{
    const rg::ResourceState shaderReadState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    return CaptureImagePixels(
        texture.Image(), VK_IMAGE_ASPECT_COLOR_BIT, texture.Format(), texture.Extent(), shaderReadState);
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
    VkDeviceSize size, VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage, const char* debugName) const
{
    return m_resources.CreateBuffer(size, usage, memoryUsage, debugName);
}

Buffer Renderer::CreateStructuredBuffer(VkDeviceSize elementStride, std::uint32_t elementCount,
    BufferMemoryUsage memoryUsage, VkBufferUsageFlags extraUsage, const char* debugName) const
{
    return m_resources.CreateStructuredBuffer(elementStride, elementCount, memoryUsage, extraUsage, debugName);
}

Buffer Renderer::CreateGpuSkinningTargetBuffer(VkDeviceSize size, const char* debugName) const
{
    return m_resources.CreateGpuSkinningTargetBuffer(size, debugName);
}



Buffer Renderer::CreateDeviceLocalBuffer(
    const void* data, VkDeviceSize size, VkBufferUsageFlags usage, const char* debugName) const
{
    return m_resources.CreateDeviceLocalBuffer(data, size, usage, debugName);
}

// Precondition: never called re-entrantly from inside an active graph-pass
// recording (CommandBuffer::Dispatch()/Draw() would otherwise clobber
// m_currentGraphPassCmd - see BeginGraphPassRecording()). Synchronization2
// submit path, one fence, zero leaks.
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

    rg::CommandBuffer cmd{ vkCmd, const_cast<Renderer*>(this), nullptr };
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

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(m_device.Native(), &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        m_resources.FreeTransientCommandBuffer(vkCmd);
        throw std::runtime_error("Renderer::ImmediateSubmit: vkCreateFence failed.");
    }

    if (m_device.SubmitToGraphicsQueue(submit, fence) != VK_SUCCESS) {
        vkDestroyFence(m_device.Native(), fence, nullptr);
        m_resources.FreeTransientCommandBuffer(vkCmd);
        throw std::runtime_error("Renderer::ImmediateSubmit: vkQueueSubmit2 failed.");
    }

    vkWaitForFences(m_device.Native(), 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    vkDestroyFence(m_device.Native(), fence, nullptr); // always reached: no early return above
    m_resources.FreeTransientCommandBuffer(vkCmd);
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
    VkDescriptorSet materialDescriptorSet, VkDescriptorSet sceneServicesSet)
{
    // Global Scene Services Descriptor Set campaign (better-render-pass-6),
    // PHASE5 - the ONE place that resolves pipeline.HasSceneServicesSet()'s
    // gate: a non-null sceneServicesSet is only ever actually forwarded
    // (down either path below) when `pipeline` itself was built with a real
    // sceneServicesSetLayout (PHASE3) - passing one against a Pipeline with
    // no set = 1 is exactly the backward-compatibility break this gate
    // exists to prevent (PHASE0's global rule 1).
    const VkDescriptorSet effectiveSceneServicesSet = pipeline.HasSceneServicesSet() ? sceneServicesSet : VK_NULL_HANDLE;

    // Never bind descriptor set 0 against a pipeline layout that declares no
    // material set - a depth-only pipeline (zero descriptor sets) paired
    // with a textured entity's real material texture would otherwise trip
    // VUID-vkCmdBindDescriptorSets-firstSet-00360.
    const VkDescriptorSet effectiveMaterialDescriptorSet =
        pipeline.HasMaterialSet() ? materialDescriptorSet : VK_NULL_HANDLE;

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
    VkDeviceSize indirectOffset, std::uint32_t maxDrawCount, VkBuffer countBuffer, VkDeviceSize countBufferOffset,
    VkDescriptorSet instanceBufferDescriptorSet, VkDescriptorSet sceneServicesSet, const Mat4& viewProjMatrix)
{
    // Deliberately NO legacy/queued fallback path, same "day-one render-
    // graph-only" precedent Renderer::Dispatch() already established for
    // compute - an indirect draw call has no pre-existing non-graph call
    // site to preserve backward compatibility with.
    assert(m_currentGraphPassCmd != VK_NULL_HANDLE
        && "Renderer::SubmitIndirect() called outside of a render-graph pass recording - see "
           "BeginGraphPassRecording()/EndGraphPassRecording().");
    if (m_currentGraphPassCmd == VK_NULL_HANDLE) {
        return;
    }

    // This campaign is VkDrawIndexedIndirectCommand-only - every Pipeline
    // built with VertexLayout::PositionNormalInstanced is only ever meant
    // to draw an INDEXED Mesh.
    assert(mesh.HasIndexBuffer()
        && "Renderer::SubmitIndirect(): every indirect draw is VkDrawIndexedIndirectCommand-only (indexed meshes "
           "only).");

    // Only ever actually forwarded when `pipeline` itself carries a real
    // sceneServicesSetLayout - mirrors Submit()'s own gate exactly.
    const VkDescriptorSet effectiveSceneServicesSet = pipeline.HasSceneServicesSet() ? sceneServicesSet : VK_NULL_HANDLE;

    FrameRecorder::IssueIndirectDrawCommand(m_currentGraphPassCmd, pipeline.Native(), pipeline.Layout(),
        mesh.VertexBuffer(), mesh.IndexBuffer(), instanceBufferDescriptorSet, effectiveSceneServicesSet, viewProjMatrix,
        indirectBuffer, indirectOffset, maxDrawCount, countBuffer, countBufferOffset,
        m_device.SupportsDrawIndirectCount());

    // Deliberately does NOT touch m_currentGraphPassRecordDrawStats (that
    // callback only ever mutates DrawStats::drawCallCount/triangleCount,
    // via Submit()'s own PassContext::recordDraw plumbing) - an indirect
    // draw's real object/triangle count is GPU-only knowledge this method
    // must never block to read back.
}

Pipeline Renderer::CreatePipeline(const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture,
    const char* debugName, bool useInstanceBuffer, VkDescriptorSetLayout sceneServicesSetLayout) const
{
    return m_resources.CreatePipeline(ColorFormat(), vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout,
        useMaterialTexture, debugName, useInstanceBuffer, sceneServicesSetLayout);
}

Pipeline Renderer::CreatePipeline(std::span<const VkFormat> colorFormats, const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture,
    const char* debugName, bool useInstanceBuffer, VkDescriptorSetLayout sceneServicesSetLayout) const
{
    return m_resources.CreatePipeline(colorFormats, vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout,
        useMaterialTexture, debugName, useInstanceBuffer, sceneServicesSetLayout);
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
    // Phase 4 (COMPUTE_PHASE4_DISPATCH_EXECUTION_STRATEGY_v2.md) - see this
    // method's own doc comment in Renderer.h for the full reasoning: unlike
    // Submit(), there is deliberately no legacy/queued fallback - a
    // dispatch outside an active render-graph pass recording has nothing
    // sensible to do at all.
    assert(m_currentGraphPassCmd != VK_NULL_HANDLE
        && "Renderer::Dispatch() called outside of a render-graph pass recording - see "
           "BeginGraphPassRecording()/EndGraphPassRecording()");
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

std::shared_ptr<Buffer> Renderer::CreateSharedMeshVertexBuffer(
    const void* vertexData, VkDeviceSize vertexDataSize, const char* debugName) const
{
    return m_resources.CreateSharedMeshVertexBuffer(vertexData, vertexDataSize, debugName);
}

std::shared_ptr<Buffer> Renderer::CreateSharedSkinnedMeshVertexBuffer(
    const void* vertexData, VkDeviceSize vertexDataSize, const char* debugName) const
{
    return m_resources.CreateSharedSkinnedMeshVertexBuffer(vertexData, vertexDataSize, debugName);
}

Mesh Renderer::CreateMeshFromSharedVertexBuffer(std::shared_ptr<Buffer> sharedVertexBuffer,
    std::uint32_t vertexCount, const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount,
    const char* debugName) const
{
    return m_resources.CreateMeshFromSharedVertexBuffer(
        std::move(sharedVertexBuffer), vertexCount, indexData, indexDataSize, indexCount, debugName);
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
    int width, int height, int depth, VkFormat format, const char* debugName) const
{
    return m_resources.CreateVolumeTexture(width, height, depth, format, debugName);
}

// better-render-pass-3 campaign, BLOCK5 - see this method's own declaration
// (Renderer.h) for the full reasoning on why this is NOT a trivial one-line
// forward like CreateVolumeTexture() immediately above.
TextureArray2D Renderer::CreateTextureArray(int width, int height, int arrayLayers, VkFormat format, bool hasDepth,
    bool isCubemap, bool allowStorageImageAccess, const char* debugName) const
{
    // VK_FORMAT_UNDEFINED resolves per hasDepth - mirrors CreateRenderTexture()'s
    // own resolution line but branches by hasDepth instead of always resolving
    // to ColorFormat(). This resolution MUST happen here, in Renderer, never
    // inside GpuResourceFactory::CreateTextureArray() - that class has no
    // ColorFormat() access and no color-format field of its own (confirmed:
    // GpuResourceFactory::CreateRenderTexture() does zero format resolution of
    // its own).
    const VkFormat resolvedFormat = (format == VK_FORMAT_UNDEFINED)
        ? (hasDepth ? DepthFormat() : ColorFormat())
        : format;
    return m_resources.CreateTextureArray(
        width, height, arrayLayers, resolvedFormat, hasDepth, isCubemap, allowStorageImageAccess, debugName);
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
    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE2 - mirrors timestampCapability immediately above.
    info.supportsDrawIndirectCount = m_device.SupportsDrawIndirectCount();
    return info;
}

Renderer::VulkanContextInfo Renderer::GetImGuiVulkanBackendInfo() const
{
    // private-to-private call, no friend needed here. gte_core (which this
    // translation unit is part of) builds with NO ImGui include path at
    // all - this method deliberately returns plain Vulkan values instead of
    // filling an ImGui_ImplVulkan_InitInfo directly, so the Editor-only
    // caller (which DOES have ImGui available) does that trivial field copy
    // itself.
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
    for (const DescriptorBindingDesc& binding : bindings) {
        const VkShaderStageFlags stages = ToVkShaderStageFlags(binding.stages);
        switch (binding.kind) {
            case DescriptorKind::CombinedImageSampler:
                builder.AddCombinedImageSampler(binding.binding, stages, binding.count);
                break;
            case DescriptorKind::UniformBuffer:
                builder.AddUniformBuffer(binding.binding, stages, binding.count);
                break;
            case DescriptorKind::StorageBuffer:
                builder.AddStorageBuffer(binding.binding, stages, binding.count);
                break;
            case DescriptorKind::StorageImage:
                builder.AddStorageImage(binding.binding, stages, binding.count);
                break;
        }
    }
    return RegisterDescriptorSetLayout(builder.Build());
}

void Renderer::DestroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) const noexcept
{
    const VkDescriptorSetLayout layout = Native(handle);
    UnregisterDescriptorSetLayout(handle);
    if (layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device.Native(), layout, nullptr);
    }
}

Pipeline Renderer::CreatePipeline(
    const PipelineDesc& desc, std::span<const std::byte> vertexSpirv, std::span<const std::byte> fragmentSpirv) const
{
    VkFormat colorFormats[PipelineDesc::kMaxColorAttachments]{};
    const std::span<const PixelFormat> colorFmts = desc.ColorFormats();
    for (std::size_t i = 0; i < colorFmts.size(); ++i) {
        colorFormats[i] = ToVkFormat(colorFmts[i]);
    }

    // desc.layout drives vkCreatePipelineLayout: sceneServicesSetLayout (if
    // any) first, then extraSetLayouts in order - the ONLY place these
    // opaque handles unwrap to a real VkDescriptorSetLayout value.
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
        return UiTextureHandle::Invalid; // No UI backend installed (e.g. a headless Player build).
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
            return; // Already released, or never a real handle - safe no-op.
        }
    }
    RemoveUiTexture(set);
}

CaptureResult Renderer::CaptureRenderTarget(const RenderTexture& target) const
{
    const rg::ResourceState shaderReadState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const CapturedRawPixels raw =
        CaptureImagePixels(target.Image(), VK_IMAGE_ASPECT_COLOR_BIT, target.Format(), target.Extent(), shaderReadState);

    CaptureResult result;
    result.extent = Extent2D{ static_cast<std::uint32_t>(raw.width), static_cast<std::uint32_t>(raw.height) };
    result.format = FromVkFormat(raw.format);
    result.pixels.resize(raw.pixels.size());
    std::memcpy(result.pixels.data(), raw.pixels.data(), raw.pixels.size());
    return result;
}

} // namespace gte
