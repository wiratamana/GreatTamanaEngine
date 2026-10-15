#include "FramePresenter.h"

#include "RenderGraph/CommandBuffer.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/RenderGraphBarrierPlanner.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "Vulkan/VulkanDevice.h"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace gte {

FramePresenter::FramePresenter(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
    std::uint32_t graphicsQueueFamily, std::uint32_t presentQueueFamily, VkQueue graphicsQueue,
    VkQueue presentQueue, int width, int height, VmaAllocator allocator, VkFormat depthFormat,
    std::shared_ptr<GpuMemoryTracker> memoryTracker, std::shared_ptr<GpuTimingService> gpuTiming,
    VulkanDevice& vulkanDevice)
    : m_device(device)
    , m_graphicsQueue(graphicsQueue)
    , m_presentQueue(presentQueue)
    , m_graphicsQueueFamily(graphicsQueueFamily)
    , m_vulkanDevice(&vulkanDevice)
    , m_allocator(allocator)
    , m_depthFormat(depthFormat)
    , m_memoryTracker(std::move(memoryTracker))
    , m_gpuTiming(std::move(gpuTiming))
    , m_swapchain(physicalDevice, device, surface, graphicsQueueFamily, presentQueueFamily, width, height)
    , m_frameSync(device, kFramesInFlight, m_swapchain.ImageCount())
    , m_swapchainCapture(allocator, m_memoryTracker, kFramesInFlight)
{
    m_pendingWidth = width;
    m_pendingHeight = height;

    CreateCommandObjects();
    // m_depthBuffers are created lazily (EnsureDepthBuffersForSwapchain()), not here.
}

FramePresenter::~FramePresenter()
{
    Destroy();
}

void FramePresenter::Destroy() noexcept
{
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    // The depth buffers, frame sync and swapchain clean themselves up in reverse declaration order.
}

void FramePresenter::CreateCommandObjects()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("FramePresenter: vkCreateCommandPool failed");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("FramePresenter: vkAllocateCommandBuffers failed");
    }

    // Separate from the per-frame buffers so offscreen work never contends with them.
    VkCommandBufferAllocateInfo offscreenAllocInfo{};
    offscreenAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    offscreenAllocInfo.commandPool = m_commandPool;
    offscreenAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    offscreenAllocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_device, &offscreenAllocInfo, &m_offscreenCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("FramePresenter: vkAllocateCommandBuffers failed (offscreen)");
    }
}

void FramePresenter::CreateDepthBuffers()
{
    // Named so they show up in the Memory panel. debugName needs static storage, so a fixed table covers the
    // realistic image counts; an index beyond it stays unnamed.
    static const char* kSwapchainDepthNames[] = {
        "SwapchainDepth[0]",
        "SwapchainDepth[1]",
        "SwapchainDepth[2]",
        "SwapchainDepth[3]",
        "SwapchainDepth[4]",
        "SwapchainDepth[5]",
        "SwapchainDepth[6]",
        "SwapchainDepth[7]",
    };
    constexpr std::size_t kNamedCount = sizeof(kSwapchainDepthNames) / sizeof(kSwapchainDepthNames[0]);

    const VkExtent2D extent = m_swapchain.Extent();
    m_depthBuffers.clear();
    m_depthBuffers.reserve(m_swapchain.ImageCount());
    for (std::uint32_t i = 0; i < m_swapchain.ImageCount(); ++i) {
        const char* debugName = (i < kNamedCount) ? kSwapchainDepthNames[i] : nullptr;
        m_depthBuffers.emplace_back(m_allocator, m_memoryTracker, m_device, static_cast<int>(extent.width),
            static_cast<int>(extent.height), FromVkFormat(m_depthFormat), debugName);
    }
}

void FramePresenter::EnsureDepthBuffersForSwapchain()
{
    // Created lazily, only on a frame that needs swapchain depth.
    if (m_depthBuffers.empty()) {
        CreateDepthBuffers();
    }
}

void FramePresenter::OnResize(int width, int height)
{
    m_pendingWidth = width;
    m_pendingHeight = height;
    m_resizeRequested = true;
}

void FramePresenter::RecreateSwapchain()
{
    if (m_pendingWidth <= 0 || m_pendingHeight <= 0) {
        // Minimized or zero-sized: keep m_resizeRequested set and retry each frame.
        return;
    }

    vkDeviceWaitIdle(m_device);

    m_swapchain.Recreate(m_pendingWidth, m_pendingHeight);

    // The image count can change, so the per-image semaphores are rebuilt too.
    m_frameSync.RecreateRenderFinishedSemaphores(m_swapchain.ImageCount());

    // A real resize invalidates every readback buffer and any pending capture; this point is past the
    // minimized-window early return, so it only fires for a resize that happened.
    m_swapchainCapture.NotifySwapchainRecreated();

    // New size (and maybe image count) needs new depth images. Rebuilt only if they already existed.
    if (!m_depthBuffers.empty()) {
        CreateDepthBuffers();
    }


    m_resizeRequested = false;
}

DrawStats FramePresenter::RenderOffscreen(FrameRecorder& frameRecorder, RenderTexture& target,
    std::optional<GpuTimingSlot> timingSlot, const std::function<void(rg::CommandBuffer&)>& recordExtra)
{
    const VkFence offscreenFence = m_frameSync.OffscreenFence();
    vkWaitForFences(m_device, 1, &offscreenFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    vkResetFences(m_device, 1, &offscreenFence);

    vkResetCommandBuffer(m_offscreenCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(m_offscreenCommandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("vkBeginCommandBuffer failed (offscreen)");
    }

    // The query reset and start timestamp are recorded before RecordFrame() because a reset must not happen
    // inside a rendering instance. The fence wait above proves this slot pair's last use is complete.
    // Nothing is recorded when timingSlot is nullopt.
    if (timingSlot.has_value()) {
        m_gpuTiming->RecordOffscreenPassStart(m_offscreenCommandBuffer, *timingSlot);
    }

    // RecordFrame() speaks raw VkCommandBuffer; wrap `recordExtra` so callers never need a raw handle.
    // recordExtra callbacks only use the raw escape hatch, never Draw()/Dispatch(), which need a Renderer.
    const std::function<void(VkCommandBuffer)> rawRecordExtra = [&recordExtra](VkCommandBuffer cmd) {
        if (recordExtra) {
            rg::CommandBuffer wrapped = rg::CommandBuffer::AdoptRaw(cmd);
            recordExtra(wrapped);
        }
    };
    const DrawStats drawStats = frameRecorder.RecordFrame(m_offscreenCommandBuffer, target.Target(), ColorFormat(),
        m_depthFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, rawRecordExtra);

    // The end timestamp brackets the whole pass including recordExtra, never inside RecordFrame().
    if (timingSlot.has_value()) {
        m_gpuTiming->RecordOffscreenPassEnd(m_offscreenCommandBuffer, *timingSlot);
    }

    if (vkEndCommandBuffer(m_offscreenCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed (offscreen)");
    }

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = m_offscreenCommandBuffer;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    if (m_vulkanDevice->SubmitToGraphicsQueue(submitInfo, offscreenFence) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit2 failed (offscreen)");
    }

    // Synchronous for now.
    vkWaitForFences(m_device, 1, &offscreenFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    // The fence wait proves the submission (and its timestamp writes) completed, so reading back is safe.
    if (timingSlot.has_value()) {
        m_gpuTiming->ReadOffscreenResultNow(*timingSlot);
    }

    return drawStats;
}

VkCommandBuffer FramePresenter::BeginOffscreenRecording()
{
    const VkFence offscreenFence = m_frameSync.OffscreenFence();
    vkWaitForFences(m_device, 1, &offscreenFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    vkResetFences(m_device, 1, &offscreenFence);

    vkResetCommandBuffer(m_offscreenCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(m_offscreenCommandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("vkBeginCommandBuffer failed (offscreen render graph)");
    }

    // GPU timestamps are not wired into the render-graph offscreen and present paths; pass timing reads Absent.
    return m_offscreenCommandBuffer;
}

void FramePresenter::EndOffscreenRecording()
{
    if (vkEndCommandBuffer(m_offscreenCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed (offscreen render graph)");
    }

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = m_offscreenCommandBuffer;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    const VkFence offscreenFence = m_frameSync.OffscreenFence();
    if (m_vulkanDevice->SubmitToGraphicsQueue(submitInfo, offscreenFence) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit2 failed (offscreen render graph)");
    }

    // Synchronous, like RenderOffscreen().
    vkWaitForFences(m_device, 1, &offscreenFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
}

bool FramePresenter::PresentViaRenderGraph(rg::RenderGraph& graph, bool needsSwapchainDepth,
    const std::function<std::vector<rg::TextureHandle>(rg::RenderGraphBuilder&, rg::TextureHandle)>& build)
{
    if (m_pendingWidth <= 0 || m_pendingHeight <= 0) {
        return false; // Minimized - nothing to draw this frame.
    }

    if (m_resizeRequested) {
        RecreateSwapchain();
        if (m_resizeRequested) {
            return false; // Still pending (stayed minimized) - try again next frame.
        }
    }

    const VkFence fence = m_frameSync.InFlightFence(m_currentFrame);
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    // The fence wait proved the GPU work from this slot's previous use is done, including any capture copy, so
    // the earlier capture can be read now with no extra wait. Read before any swapchain recreation clears slots.
    m_lastCompletedCapture = m_swapchainCapture.TryTakeCompletedCapture(m_currentFrame);

    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        m_device, m_swapchain.Native(), std::numeric_limits<std::uint64_t>::max(),
        m_frameSync.ImageAvailableSemaphore(m_currentFrame), VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return false;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed (VkResult=" + std::to_string(acquireResult) + ")");
    }

    vkResetFences(m_device, 1, &fence);

    if (needsSwapchainDepth) {
        EnsureDepthBuffersForSwapchain();
    }

    const VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("vkBeginCommandBuffer failed (render graph present)");
    }

    RenderTarget target;
    target.image = m_swapchain.Image(imageIndex);
    target.imageView = m_swapchain.ImageView(imageIndex);
    target.extent = m_swapchain.Extent();
    target.format = m_swapchain.ImageFormat();
    if (needsSwapchainDepth) {
        // Indexed by image index (not frame slot), which is what makes this safe without extra sync.
        const DepthBuffer& depthBuffer = m_depthBuffers[imageIndex];
        target.depthImage = depthBuffer.Image();
        target.depthImageView = depthBuffer.View();
        target.depthFormat = depthBuffer.NativeFormat();
        target.depthHasStencil = depthBuffer.HasStencilComponent();
    }

    // A dependency cycle in Compile() would throw; it is not caught here, the caller handles it.
    graph.Execute(cmd, rg::ExecuteTimingMode::PipelinedDeferredReadback,
        [&](rg::RenderGraphBuilder& builder) -> std::vector<rg::TextureHandle> {
            // The swapchain image is never sampled, only presented - no sampler exists for it.
            const rg::TextureHandle swapchainHandle =
                builder.ImportTexture("Swapchain", target, ImageLayout::Undefined);
            return build(builder, swapchainHandle);
        });

    // A requested capture records its copy and barriers here, before the manual finalize below.
    // That finalize then starts from TransferSrcOptimal for this call.
    const bool capturedThisFrame =
        m_swapchainCapture.RecordCaptureIfRequested(cmd, target.image, target.extent, target.format, m_currentFrame);

    // The graph never learns this image goes to present, so the final transition to PRESENT_SRC_KHR is done by
    // hand with the graph's own helpers; safe because Execute() has closed its rendering bracket.
    {
        // After a capture copy the image is in TransferSrcOptimal instead of ColorAttachmentWrite.
        const rg::ResourceState previous = capturedThisFrame
            ? rg::ResourceState{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_READ_BIT }
            : rg::RequiredStateFor(rg::ResourceAccess::ColorAttachmentWrite, false);
        const rg::ResourceState next{
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE
        };
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        rg::EmitImageBarrier(cmd, target.image, range, previous, next);

        // Runs on every present, and reuses `next` so the debug state cannot drift from the real transition.
        graph.NotifyDebugTextureStateOverride("Swapchain", next);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed (render graph present)");
    }

    const VkSemaphore waitSemaphore = m_frameSync.ImageAvailableSemaphore(m_currentFrame);
    const VkSemaphore signalSemaphore = m_frameSync.RenderFinishedSemaphore(imageIndex);

    VkSemaphoreSubmitInfo waitSemaphoreInfo{};
    waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemaphoreInfo.semaphore = waitSemaphore;
    waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalSemaphoreInfo{};
    signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemaphoreInfo.semaphore = signalSemaphore;
    signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

    if (m_vulkanDevice->SubmitToGraphicsQueue(submitInfo, fence) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit2 failed (render graph present)");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSemaphore;
    const VkSwapchainKHR swapchain = m_swapchain.Native();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        m_resizeRequested = true;
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed (VkResult=" + std::to_string(presentResult) + ")");
    }

    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;

    return true;
}

void FramePresenter::RequestSwapchainCapture()
{
    m_swapchainCapture.RequestCapture();
}

std::optional<CapturedSwapchainPixels> FramePresenter::TakeLastCompletedSwapchainCapture()
{
    return std::exchange(m_lastCompletedCapture, std::nullopt);
}

} // namespace gte
