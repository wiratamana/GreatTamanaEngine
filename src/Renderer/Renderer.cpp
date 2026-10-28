#include "Renderer.h"

#include "../Core/ISurfaceProvider.h"
#include "../Core/Logging.h"
#include "Compute/ComputeDispatch.h"
#include "Compute/ComputePipeline.h"
#include "DescriptorSet.h"
#include "GpuWaitFailure.h"
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

Renderer::Renderer(ISurfaceProvider& surfaceProvider, bool forceValidation, PresentPolicy presentPolicy)
    : m_instance("GreatTamanaEngine", surfaceProvider.VulkanInstanceExtensions(), kEnableValidation || forceValidation)
    , m_surface(m_instance.Native(), surfaceProvider)
    , m_device(m_instance.Native(), m_surface.Native())
    , m_depthFormat(m_device.PickDepthFormat())
    , m_presentPolicy(presentPolicy)
    , m_allocator(m_instance.Native(), m_device.Physical(), m_device.Native(), VK_API_VERSION_1_3)
    , m_presenter(m_device.Physical(), m_device.Native(), m_surface.Native(), m_device.GraphicsQueueFamily(),
          m_device.PresentQueueFamily(), m_device.PresentQueue(), surfaceProvider.Width(), surfaceProvider.Height(),
          m_allocator.Native(), m_depthFormat, m_memoryTracker, m_device, m_presentPolicy)
    , m_resources(m_device.Physical(), m_device.Native(), m_allocator.Native(), m_device.GraphicsQueueFamily(),
          m_depthFormat, m_memoryTracker, m_device, m_abandoned)
    , m_emptySetLayout(m_device.Native(), DescriptorSetLayoutBuilder(m_device.Native()).Build())
{
    m_emptySet = m_resources.AllocateDescriptorSet(m_emptySetLayout.Native()).Native();
    m_bufferSetLayoutHandle = RegisterDescriptorSetLayout(m_resources.BufferDescriptorSetLayout());
    m_emptySetLayoutHandle = RegisterDescriptorSetLayout(m_emptySetLayout.Native());
}

Renderer::~Renderer()
{
    if (m_device.Native() != VK_NULL_HANDLE) {
        const bool idle = WaitForGpuIdle(BlockSite::Shutdown, "~Renderer") == BlockOutcome::Completed;
        if (idle) {
            m_resources.FreeDescriptorSet(m_emptySet);
        }
        // Leaked parked objects keep their allocations live; destroying the allocator would assert.
        const std::uint32_t parked = m_abandoned.Count();
        if (ShouldAbandonAllocator(idle, parked)) {
            m_allocator.AbandonOnDestroy();
            GTE_LOG_WARNING("Vulkan", "device lost: allocator abandoned, parked objects: " + std::to_string(parked));
        }
        m_abandoned.Release(idle);
    }
    UnregisterDescriptorSetLayout(m_emptySetLayoutHandle);
    UnregisterDescriptorSetLayout(m_bufferSetLayoutHandle);
}

void Renderer::LogValidationState() const
{
    switch (m_instance.Validation()) {
    case ValidationState::On:
        GTE_LOG_INFO("Vulkan", "validation enabled");
        break;
    case ValidationState::LayerMissing:
        GTE_LOG_WARNING("Vulkan",
            "validation requested but layer not available; set VK_LAYER_PATH to the folder of VkLayer_khronos_validation.json");
        break;
    case ValidationState::MessengerFailed:
        GTE_LOG_WARNING("Vulkan", "validation layer loaded but debug messenger failed");
        break;
    case ValidationState::Off:
        GTE_LOG_INFO("Vulkan", "validation disabled");
        break;
    }
}

void Renderer::OnResize(int width, int height)
{
    m_presenter.OnResize(width, height);
}

DrawStats Renderer::RenderOffscreen(
    RenderTexture& target, const std::array<float, 4>& clearColor, const std::function<void(rg::CommandBuffer&)>& recordExtra)
{
    return m_presenter.RenderOffscreen(m_frameRecorder, target, clearColor, recordExtra);
}

OffscreenRecording Renderer::BeginOffscreenRecording()
{
    return OffscreenRecording(m_presenter, this);
}

bool Renderer::PresentViaRenderGraph(rg::RenderGraph& graph,
    const std::function<std::vector<rg::TextureHandle>(rg::RenderGraphBuilder&, rg::TextureHandle)>& build)
{
    return m_presenter.PresentViaRenderGraph(graph, build);
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

PixelFormat Renderer::ScenePixelFormat() const noexcept
{
    return PixelFormat::RGBA16_Float;
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

void Renderer::ResizeRenderTexture(RenderTexture& texture, int width, int height) const
{
    if (WaitForGpuIdle(BlockSite::TargetResize, "render texture resize") != BlockOutcome::Completed) {
        throw std::runtime_error("Renderer::ResizeRenderTexture: the GPU did not go idle");
    }
    texture.Resize(width, height);
}

CaptureResult Renderer::CaptureImagePixels(VkImage image, VkImageAspectFlags aspect, VkFormat format,
    Extent2D extentIn, const rg::ResourceState& previousState, std::uint32_t zOffset, std::uint32_t depth) const
{
    const VkExtent2D extent = ToVkExtent2D(extentIn);
    const int bytesPerPixel = BytesPerTexelForFormat(format);
    if (image == VK_NULL_HANDLE || bytesPerPixel == 0) {
        return { {}, CaptureStatus::NothingToCapture };
    }

    const uint64_t size =
        uint64_t(extent.width) * extent.height * depth * static_cast<uint64_t>(bytesPerPixel);
    try {
        const auto readback = std::make_shared<Buffer>(
            CreateBuffer(size, BufferUsage::TransferDst, BufferMemoryUsage::GpuToCpu, "CaptureReadback"));

        const VkImageSubresourceRange range{ aspect, 0, 1, 0, 1 };
        const rg::ResourceState transferSrcState = rg::RequiredStateFor(rg::ResourceAccess::TransferSrc, false);

        // The readback buffer is parked if the wait fails: the GPU may still write it.
        const std::array<std::shared_ptr<void>, 1> inFlight{ readback };
        ImmediateSubmit(
            [&](rg::CommandBuffer& cmd) {
                const VkCommandBuffer rawCmd = rg::RecordingAccess::Native(cmd);
                rg::EmitImageBarrier(rawCmd, image, range, previousState, transferSrcState);

                VkBufferImageCopy region{};
                region.imageSubresource.aspectMask = aspect;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = { 0, 0, static_cast<std::int32_t>(zOffset) };
                region.imageExtent = { extent.width, extent.height, depth };
                vkCmdCopyImageToBuffer(
                    rawCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->Native(), 1, &region);

                // Make the device copy visible to the host read.
                const rg::ResourceState transferWriteState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT };
                const rg::ResourceState hostReadState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT };
                rg::EmitBufferBarrier(rawCmd, readback->Native(), 0, size, transferWriteState, hostReadState);

                rg::EmitImageBarrier(rawCmd, image, range, transferSrcState, previousState);
            },
            BlockSite::CaptureReadback, "capture readback", inFlight);

        CaptureResult result;
        result.pixels.pixels.resize(static_cast<std::size_t>(size));
        std::memcpy(result.pixels.pixels.data(), readback->MappedData(), static_cast<std::size_t>(size));
        result.pixels.width = static_cast<int>(extent.width);
        result.pixels.height = static_cast<int>(extent.height);
        result.pixels.format = CapturedPixelFormatFromVk(format);
        result.status = CaptureStatus::Ok;
        return result;
    } catch (const GpuWaitFailure& failure) {
        return { {}, CaptureStatusFromBlockOutcome(failure.Outcome()) };
    } catch (const std::exception& e) {
        GTE_LOG_WARNING("Renderer", std::string("capture failed: ") + e.what());
        return { {}, DeviceLost() ? CaptureStatus::DeviceLost : CaptureStatus::Failed };
    } catch (...) {
        return { {}, DeviceLost() ? CaptureStatus::DeviceLost : CaptureStatus::Failed };
    }
}

CaptureResult Renderer::CaptureRenderTexturePixels(RenderTexture& texture) const
{
    const rg::ResourceState shaderReadState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    return CaptureImagePixels(
        texture.Image(), VK_IMAGE_ASPECT_COLOR_BIT, texture.NativeFormat(), texture.Extent(), shaderReadState);
}

CaptureResult Renderer::CaptureRenderTexturePixelsAfterIdle(RenderTexture& texture) const
{
    if (const BlockOutcome idle = WaitForGpuIdle(BlockSite::CaptureReadback, "render texture capture");
        idle != BlockOutcome::Completed) {
        return { {}, CaptureStatusFromBlockOutcome(idle) };
    }
    return CaptureRenderTexturePixels(texture);
}

CaptureResult Renderer::CaptureDebugTexturePixels(const rg::DebugTextureSnapshot& snapshot, bool depthAspect) const
{
    const VkImage image = depthAspect ? snapshot.target.depthImage : snapshot.target.image;
    if (image == VK_NULL_HANDLE) {
        return { {}, CaptureStatus::NothingToCapture };
    }
    const VkImageAspectFlags aspect = depthAspect ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    const VkFormat format = depthAspect ? snapshot.target.depthFormat : snapshot.target.format;
    const rg::ResourceState& state = depthAspect ? snapshot.depthState : snapshot.colorState;
    if (const BlockOutcome idle = WaitForGpuIdle(BlockSite::CaptureReadback, "debug texture capture");
        idle != BlockOutcome::Completed) {
        return { {}, CaptureStatusFromBlockOutcome(idle) };
    }
    return CaptureImagePixels(image, aspect, format, snapshot.ColorExtent(), state);
}

CaptureResult Renderer::CaptureEventSnapshotPixels(const rg::RetainedEventSnapshot& retained, bool depthAspect) const
{
    const rg::EventSnapshotResource* snapshot = retained.Get();
    if (snapshot == nullptr || snapshot->Image() == VK_NULL_HANDLE) {
        return { {}, CaptureStatus::NothingToCapture };
    }
    const VkImageAspectFlags aspect = depthAspect ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    const rg::ResourceState shaderRead{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT };
    if (const BlockOutcome idle = WaitForGpuIdle(BlockSite::CaptureReadback, "event snapshot capture");
        idle != BlockOutcome::Completed) {
        return { {}, CaptureStatusFromBlockOutcome(idle) };
    }
    return CaptureImagePixels(snapshot->Image(), aspect, snapshot->Format(), FromVkExtent2D(snapshot->Extent()), shaderRead);
}

BlockOutcome Renderer::WaitForGpuIdle(BlockSite site, const char* detail) const noexcept
{
    return m_device.Waits().WaitIdle(kIdleWaitTimeout, site, detail);
}

BlockOutcome Renderer::DrainGpuForCodeUnload() const noexcept
{
    return WaitForGpuIdle(BlockSite::CodeUnload, "code unload");
}

BlockOutcome Renderer::DrainGpuForResourceRelease(const char* detail) const noexcept
{
    return WaitForGpuIdle(BlockSite::ResourceRelease, detail);
}

void Renderer::IdleForTeardown(const char* what) const noexcept
{
    static_cast<void>(WaitForGpuIdle(BlockSite::Shutdown, what));
}

bool Renderer::DeviceLost() const noexcept
{
    return m_device.Waits().DeviceLost();
}

void Renderer::InjectDeviceLost() noexcept
{
    m_device.Waits().MarkDeviceLost();
}

std::uint32_t Renderer::GpuWaitConsecutiveFrameTimeouts() const noexcept
{
    return m_device.Waits().ConsecutiveFrameTimeouts();
}

ClockCalibration Renderer::GpuClockCalibration() const noexcept
{
    // The device setup enables no calibrated-timestamp extension, so there is nothing to fit.
    return MissingClockCalibration(ClockCalibrationReason::ExtensionNotEnabled);
}

void Renderer::SetTimingObserver(IRenderTimingObserver* observer) noexcept
{
    m_device.Waits().SetObserver(observer);
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

Buffer Renderer::CreateDeviceLocalBuffer(
    const void* data, uint64_t size, BufferUsage usage, const char* debugName) const
{
    return m_resources.CreateDeviceLocalBuffer(data, size, usage, debugName);
}

void Renderer::ImmediateSubmit(const std::function<void(rg::CommandBuffer&)>& recordFn, BlockSite site,
    const char* detail, std::span<const std::shared_ptr<void>> inFlight) const
{
    assert(m_currentGraphPassCmd == VK_NULL_HANDLE
        && "ImmediateSubmit() called while a graph pass is already being recorded");

    m_resources.SubmitOneShot(
        site, detail,
        [&](VkCommandBuffer raw) {
            rg::CommandBuffer cmd = rg::RecordingAccess::Adopt(raw, const_cast<Renderer*>(this));
            recordFn(cmd);
        },
        inFlight);
}

rg::RenderGraph& Renderer::OneShotGraph()
{
    if (!m_oneShotGraph) {
        m_oneShotGraph = std::make_unique<rg::RenderGraph>(*this, rg::TimingMode::Disabled);
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

    // Set 0: the material set, or the empty set when the pipeline only has a filler there.
    // Either way set 0 matches the layout before set 1 is bound.
    VkDescriptorSet effectiveMaterialDescriptorSet = VK_NULL_HANDLE;
    if (pipeline.HasMaterialSet()) {
        effectiveMaterialDescriptorSet = Native(materialDescriptorSet);
    } else if (pipeline.HasSyntheticSetZero()) {
        effectiveMaterialDescriptorSet = m_emptySet;
    }

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
    DescriptorSetHandle bufferDescriptorSet, DescriptorSetHandle sceneServicesSet, const Mat4& viewProjMatrix,
    DescriptorSetHandle materialDescriptorSet, std::uint32_t drawTag)
{
    assert(m_currentGraphPassCmd != VK_NULL_HANDLE
        && "Renderer::SubmitIndirect() called outside of a render-graph pass recording.");
    if (m_currentGraphPassCmd == VK_NULL_HANDLE) {
        return;
    }

    assert(mesh.HasIndexBuffer()
        && "Renderer::SubmitIndirect(): indexed mesh required for indirect draw.");
    assert(ClusterPathSupported(IndirectDrawCaps())
        && "Renderer::SubmitIndirect(): the device lacks the indirect-draw features.");

    const VkDescriptorSet effectiveSceneServicesSet =
        pipeline.HasSceneServicesSet() ? Native(sceneServicesSet) : VK_NULL_HANDLE;
    const VkDescriptorSet effectiveMaterialSet =
        pipeline.HasMaterialSet() ? Native(materialDescriptorSet) : VK_NULL_HANDLE;
    assert((!pipeline.HasMaterialSet() || effectiveMaterialSet != VK_NULL_HANDLE)
        && "Renderer::SubmitIndirect(): the pipeline needs a material set.");

    FrameRecorder::IssueIndirectDrawCommand(m_currentGraphPassCmd, pipeline.Native(), pipeline.Layout(),
        mesh.VertexBuffer(), mesh.IndexBuffer(), Native(bufferDescriptorSet), effectiveMaterialSet,
        effectiveSceneServicesSet, viewProjMatrix, indirectBuffer, indirectOffset, maxDrawCount, countBuffer,
        countBufferOffset, drawTag);
}

Pipeline Renderer::CreatePipeline(const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture,
    const char* debugName, DescriptorSetLayoutHandle sceneServicesSetLayout) const
{
    return m_resources.CreatePipeline(ToVkFormat(ScenePixelFormat()), vertexShaderSpirvPath, fragmentShaderSpirvPath,
        vertexLayout, useMaterialTexture, debugName, Native(sceneServicesSetLayout));
}

Pipeline Renderer::CreatePipeline(std::span<const PixelFormat> colorFormats, const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture,
    const char* debugName, DescriptorSetLayoutHandle sceneServicesSetLayout) const
{
    std::vector<VkFormat> vkColorFormats;
    vkColorFormats.reserve(colorFormats.size());
    for (const PixelFormat format : colorFormats) {
        vkColorFormats.push_back(ToVkFormat(format));
    }
    return m_resources.CreatePipeline(vkColorFormats, vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout,
        useMaterialTexture, debugName, Native(sceneServicesSetLayout));
}

ComputePipeline Renderer::CreateComputePipeline(const std::string& shaderSpirvPath,
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
    std::optional<VkPushConstantRange> pushConstantRange) const
{
    return m_resources.CreateComputePipeline(shaderSpirvPath, descriptorSetLayouts, pushConstantRange);
}

DescriptorSet Renderer::AllocateDescriptorSet(VkDescriptorSetLayout layout) const
{
    return m_resources.AllocateDescriptorSet(layout);
}

DescriptorSet Renderer::AllocateDescriptorSet(DescriptorSetLayoutHandle layoutHandle) const
{
    return m_resources.AllocateDescriptorSet(Native(layoutHandle));
}

void Renderer::FreeDescriptorSet(VkDescriptorSet descriptorSet) const noexcept
{
    m_resources.FreeDescriptorSet(descriptorSet);
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

DescriptorSetLayoutHandle Renderer::BufferSetLayout() const noexcept
{
    return m_bufferSetLayoutHandle;
}

DescriptorSetLayoutHandle Renderer::EmptySetLayout() const noexcept
{
    return m_emptySetLayoutHandle;
}

const Pipeline* Renderer::CreateClusterPipeline(
    const ClusterPipelineKey& key, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    const bool depthOnly = key.variant == ClusterPipelineVariant::DepthOnly;
    ClusterPipelineSlot& slot = m_clusterPipelines[ClusterPipelineCacheIndex(key)];
    if (slot.pipeline != nullptr) {
        // A depth-only pipeline has no scene set, so its caller's layout never matters.
        const bool sameLayout = depthOnly || slot.sceneServicesSetLayout == sceneServicesSetLayout;
        assert(sameLayout && "Renderer::CreateClusterPipeline: one scene services layout per renderer");
        return sameLayout ? slot.pipeline.get() : nullptr;
    }

    if (!depthOnly && sceneServicesSetLayout == DescriptorSetLayoutHandle::Invalid) {
        return nullptr;
    }

    const ClusterShaderSet shaders = ClusterShadersFor(key);
    try {
        const VkFormat colorFormat = ToVkFormat(ScenePixelFormat());
        const std::span<const VkFormat> colorFormats =
            depthOnly ? std::span<const VkFormat>() : std::span<const VkFormat>(&colorFormat, 1);
        slot.pipeline = std::make_unique<Pipeline>(m_resources.CreateClusterPipeline(colorFormats, shaders.vertex,
            shaders.fragment, shaders.vertexLayout, ClusterKeyUsesMaterialSet(key), key.cullBackFaces,
            shaders.debugName, depthOnly ? VK_NULL_HANDLE : Native(sceneServicesSetLayout)));
        slot.sceneServicesSetLayout = sceneServicesSetLayout;
    } catch (const std::exception& e) {
        if (!slot.failureLogged) {
            slot.failureLogged = true;
            GTE_LOG_ERROR("Renderer", std::string("cluster pipeline '") + shaders.debugName + "' failed: " + e.what());
        }
        return nullptr;
    }
    return slot.pipeline.get();
}

IndirectDrawCapsView Renderer::IndirectDrawCaps() const noexcept
{
    return m_device.IndirectDraw();
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

Texture2D Renderer::CreateTexture2D(const Texture2DUploadDesc& desc) const
{
    return m_resources.CreateTexture2D(desc);
}

std::vector<std::shared_ptr<Texture2D>> Renderer::CreateMaterialImages(
    std::span<const MaterialImageUpload> uploads) const
{
    return m_resources.CreateMaterialImages(uploads);
}

GpuUploadStats Renderer::GetUploadStats() const noexcept
{
    return m_resources.UploadStats();
}

MaterialTexture Renderer::CreateMaterialVariant(const MaterialImages& images, const MaterialParams& params) const
{
    return m_resources.CreateMaterialVariant(images, params);
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
    return info;
}

Renderer::VulkanContextInfo Renderer::GetImGuiVulkanBackendInfo() const
{
    return GetVulkanContextInfo();
}

std::uint32_t Renderer::MaxImageDimension2D() const noexcept
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_device.Physical(), &properties);
    return properties.limits.maxImageDimension2D;
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

UiTextureHandle Renderer::GetUiTextureHandle(const rg::DebugTextureSnapshot& snapshot)
{
    if (snapshot.target.imageView == VK_NULL_HANDLE || snapshot.sampler == VK_NULL_HANDLE) {
        return UiTextureHandle::Invalid;
    }
    const VkDescriptorSet set = AddUiTexture(snapshot.sampler, snapshot.target.imageView);
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

void Renderer::RetireUiTexture(UiTextureHandle handle, std::unique_ptr<Texture2D> texture) noexcept
{
    if (WaitForGpuIdle(BlockSite::ResourceRelease, "retire UI texture") != BlockOutcome::Completed) {
        // The GPU may still sample the texture through the descriptor, so keep it alive.
        m_abandoned.Park(std::shared_ptr<Texture2D>(std::move(texture)));
        return;
    }
    ReleaseUiTextureHandle(handle);
    texture.reset();
}

} // namespace gte
