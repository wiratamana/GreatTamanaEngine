#include "FramePresenter.h"

#include "../Core/FaultInjection.h"
#include "GpuTiming.h"
#include "GpuWaitPolicy.h"
#include "OffscreenRecording.h"
#include "PresentRecoveryPlan.h"
#include "RenderGraph/CommandBuffer.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/RenderGraphBarrierPlanner.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "Vulkan/VulkanDevice.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace gte {

namespace {

// A frame wait that did not complete means the GPU is busy or lost: the frame must not be recorded.
void ThrowIfNotCompleted(const char* what, BlockOutcome outcome)
{
    if (outcome != BlockOutcome::Completed) {
        throw std::runtime_error(std::string(what) + ": GPU wait " + ToString(outcome));
    }
}

} // namespace

FramePresenter::FramePresenter(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
    std::uint32_t graphicsQueueFamily, std::uint32_t presentQueueFamily, VkQueue presentQueue, int width, int height,
    VmaAllocator allocator, VkFormat depthFormat, std::shared_ptr<GpuMemoryTracker> memoryTracker,
    VulkanDevice& vulkanDevice, PresentPolicy presentPolicy)
    : m_device(device)
    , m_presentQueue(presentQueue)
    , m_graphicsQueueFamily(graphicsQueueFamily)
    , m_vulkanDevice(&vulkanDevice)
    , m_depthFormat(depthFormat)
    , m_memoryTracker(std::move(memoryTracker))
    , m_swapchain(physicalDevice, device, surface, graphicsQueueFamily, presentQueueFamily, width, height, presentPolicy)
    , m_frameSync(device, kFramesInFlight, m_swapchain.ImageCount())
    , m_swapchainCapture(allocator, m_memoryTracker, kFramesInFlight)
{
    m_pendingWidth = width;
    m_pendingHeight = height;

    CreateCommandObjects();
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
    // Frame sync and swapchain clean themselves up in reverse declaration order.
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

    // The old swapchain images may still be in flight. Without a drain, keep it and retry next frame.
    if (m_vulkanDevice->Waits().WaitIdle(kIdleWaitTimeout, BlockSite::SwapchainRecreate, "swapchain")
        != BlockOutcome::Completed) {
        return;
    }

    m_vulkanDevice->Waits().ReportMarker("Periodic.SwapchainRecreate");
    m_swapchain.Recreate(m_pendingWidth, m_pendingHeight);

    // The image count can change, so the per-image semaphores are rebuilt too.
    m_frameSync.RecreateRenderFinishedSemaphores(m_swapchain.ImageCount());

    // A real resize invalidates every readback buffer and any pending capture; this point is past the
    // minimized-window early return, so it only fires for a resize that happened.
    m_swapchainCapture.NotifySwapchainRecreated();

    m_resizeRequested = false;
}

DrawStats FramePresenter::RenderOffscreen(FrameRecorder& frameRecorder, RenderTexture& target,
    const std::array<float, 4>& clearColor, const std::function<void(rg::CommandBuffer&)>& recordExtra)
{
    OffscreenRecording recording(*this, nullptr);

    // RecordFrame() speaks raw VkCommandBuffer; wrap `recordExtra` so callers never need a raw handle.
    // recordExtra callbacks only use the raw escape hatch, never Draw()/Dispatch(), which need a Renderer.
    const std::function<void(VkCommandBuffer)> rawRecordExtra = [&recordExtra](VkCommandBuffer cmd) {
        if (recordExtra) {
            rg::CommandBuffer wrapped = rg::RecordingAccess::Adopt(cmd);
            recordExtra(wrapped);
        }
    };
    const DrawStats drawStats = frameRecorder.RecordFrame(m_offscreenCommandBuffer, target.Target(), ColorFormat(),
        m_depthFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, clearColor, rawRecordExtra);

    recording.Submit();
    return drawStats;
}

VkCommandBuffer FramePresenter::BeginOffscreenRecording()
{
    if (m_offscreenOpen) {
        throw std::logic_error("offscreen recording is already open");
    }
    ThrowIfNotCompleted("BeginOffscreenRecording",
        m_vulkanDevice->Waits().WaitFence(m_frameSync.OffscreenFence(), kFrameWaitTimeout, WaitClass::Frame,
            BlockSite::PreviousOffscreen, "offscreen fence"));

    vkResetCommandBuffer(m_offscreenCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(m_offscreenCommandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("vkBeginCommandBuffer failed (offscreen)");
    }
    m_offscreenOpen = true;
    return m_offscreenCommandBuffer;
}

void FramePresenter::EndOffscreenRecording()
{
    m_offscreenOpen = false;
    if (vkEndCommandBuffer(m_offscreenCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed (offscreen)");
    }
    SubmitOffscreenAndWait(m_frameSync.OffscreenFence(), "EndOffscreenRecording");
}

void FramePresenter::DiscardOffscreen() noexcept
{
    // Legal while recording, also inside an open rendering instance; the buffer was never submitted.
    vkResetCommandBuffer(m_offscreenCommandBuffer, 0);
    m_offscreenOpen = false;
}

void FramePresenter::SubmitOffscreenAndWait(VkFence offscreenFence, const char* what)
{
    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = m_offscreenCommandBuffer;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    // Reset only now: a failure before this point leaves the fence signaled for the next frame.
    vkResetFences(m_device, 1, &offscreenFence);
    if (m_vulkanDevice->SubmitToGraphicsQueue(submitInfo, offscreenFence) != VK_SUCCESS) {
        // The fence is unsignaled and nothing will signal it: stop all further waits and submits.
        m_vulkanDevice->Waits().MarkDeviceLost();
        throw std::runtime_error(std::string("vkQueueSubmit2 failed (") + what + ")");
    }

    ThrowIfNotCompleted(what,
        m_vulkanDevice->Waits().WaitFence(
            offscreenFence, kFrameWaitTimeout, WaitClass::Frame, BlockSite::Offscreen, "offscreen submit"));
}

void FramePresenter::RecoverAcquiredImage(std::uint32_t imageIndex, bool fenceReset, bool capturePending) noexcept
{
    GpuWaitService& waits = m_vulkanDevice->Waits();
    const PresentRecoveryPlan plan = PlanPresentRecovery({ waits.DeviceLost(), true, fenceReset, capturePending });

    const std::uint32_t frame = m_currentFrame;
    const VkCommandBuffer cmd = m_commandBuffers[frame];
    const VkFence fence = m_frameSync.InFlightFence(frame);
    const VkSemaphore waitSemaphore = m_frameSync.ImageAvailableSemaphore(frame);
    const VkSemaphore signalSemaphore = m_frameSync.RenderFinishedSemaphore(imageIndex);

    try {
        for (const PresentRecoveryStep step : plan) {
            switch (step) {
            case PresentRecoveryStep::AbandonCapture:
                m_swapchainCapture.AbandonPendingCapture(frame);
                break;
            case PresentRecoveryStep::ResetCommandBuffer:
                if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS) {
                    waits.MarkDeviceLost();
                    return;
                }
                break;
            case PresentRecoveryStep::TransitionToPresent: {
                // The image shows undefined contents for one frame; the swapchain keeps every image in rotation.
                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
                    waits.MarkDeviceLost();
                    return;
                }
                const rg::ResourceState previous{
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE
                };
                const rg::ResourceState next{
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE
                };
                const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                rg::EmitImageBarrier(cmd, m_swapchain.Image(imageIndex), range, previous, next);
                if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
                    waits.MarkDeviceLost();
                    return;
                }
                break;
            }
            case PresentRecoveryStep::ResetFence:
                vkResetFences(m_device, 1, &fence);
                break;
            case PresentRecoveryStep::SubmitAndSignal: {
                VkSemaphoreSubmitInfo waitInfo{};
                waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                waitInfo.semaphore = waitSemaphore;
                waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

                VkSemaphoreSubmitInfo signalInfo{};
                signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                signalInfo.semaphore = signalSemaphore;
                signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

                VkCommandBufferSubmitInfo cmdInfo{};
                cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
                cmdInfo.commandBuffer = cmd;

                VkSubmitInfo2 submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
                submitInfo.waitSemaphoreInfoCount = 1;
                submitInfo.pWaitSemaphoreInfos = &waitInfo;
                submitInfo.commandBufferInfoCount = 1;
                submitInfo.pCommandBufferInfos = &cmdInfo;
                submitInfo.signalSemaphoreInfoCount = 1;
                submitInfo.pSignalSemaphoreInfos = &signalInfo;
                if (m_vulkanDevice->SubmitToGraphicsQueue(submitInfo, fence) != VK_SUCCESS) {
                    waits.MarkDeviceLost();
                    return;
                }
                break;
            }
            case PresentRecoveryStep::Present: {
                VkPresentInfoKHR presentInfo{};
                presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                presentInfo.waitSemaphoreCount = 1;
                presentInfo.pWaitSemaphores = &signalSemaphore;
                const VkSwapchainKHR swapchain = m_swapchain.Native();
                presentInfo.swapchainCount = 1;
                presentInfo.pSwapchains = &swapchain;
                presentInfo.pImageIndices = &imageIndex;

                const GpuWaitService::Presented presented = waits.Present(m_presentQueue, presentInfo);
                if (presented.result == VK_ERROR_OUT_OF_DATE_KHR || presented.result == VK_SUBOPTIMAL_KHR) {
                    m_resizeRequested = true;
                } else if (presented.result != VK_SUCCESS) {
                    waits.MarkDeviceLost();
                    return;
                }
                break;
            }
            case PresentRecoveryStep::AdvanceFrame:
                m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
                break;
            }
        }
    } catch (...) {
        waits.MarkDeviceLost();
    }
}

bool FramePresenter::PresentViaRenderGraph(rg::RenderGraph& graph,
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

    GpuWaitService& waits = m_vulkanDevice->Waits();
    const VkFence fence = m_frameSync.InFlightFence(m_currentFrame);
    ThrowIfNotCompleted("PresentViaRenderGraph",
        waits.WaitFence(fence, kFrameWaitTimeout, WaitClass::Frame, BlockSite::PresentInFlight, "in-flight fence"));

    // The fence wait proved the GPU work from this slot's previous use is done, including any capture copy, so
    // the earlier capture can be read now with no extra wait. Read before any swapchain recreation clears slots.
    m_lastCompletedCapture = m_swapchainCapture.TryTakeCompletedCapture(m_currentFrame);

    // The same wait proves this slot's previous stamps are written. A frame that left the ring is dropped, not read.
    const GpuFrameRing::Taken previousGpuFrame = m_gpuFrames.Take(m_currentFrame, m_submittedFrames);
    if (previousGpuFrame.status == GpuFrameRing::Status::Ready) {
        graph.ResolvePipelinedGpuFrame(m_currentFrame, previousGpuFrame.frameIndex);
    } else if (previousGpuFrame.status == GpuFrameRing::Status::Dropped) {
        graph.DropPipelinedGpuFrame(m_currentFrame);
    }

    const GpuWaitService::Acquired acquired = waits.AcquireImage(m_swapchain.Native(),
        m_frameSync.ImageAvailableSemaphore(m_currentFrame), kAcquireTimeout, BlockSite::AcquireImage,
        "swapchain image");
    ThrowIfNotCompleted("PresentViaRenderGraph", acquired.outcome);

    if (acquired.result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return false;
    }
    if (acquired.result != VK_SUCCESS && acquired.result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("swapchain image acquire failed (VkResult=" + std::to_string(acquired.result) + ")");
    }
    const std::uint32_t imageIndex = acquired.imageIndex;
    const VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // From here the image and the image-available semaphore belong to this frame until it is submitted.
    // A throw in between must still hand the image back, or the swapchain loses one image per throw.
    bool captureRecorded = false;
    bool fenceReset = false;
    try {
        if (FaultFires(FaultKind::PresentThrow)) {
            throw std::runtime_error("injected present fault");
        }
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

        graph.SetPipelinedGpuSlot(m_currentFrame);
        graph.Execute(rg::RecordingAccess::Wrap(cmd), rg::ExecuteTimingMode::PipelinedDeferredReadback,
            [&](rg::RenderGraphBuilder& builder) -> std::vector<rg::TextureHandle> {
                // The swapchain image is never sampled, only presented - no sampler exists for it.
                const rg::TextureHandle swapchainHandle =
                    builder.ImportTexture("Swapchain", target, ImageLayout::Undefined);
                return build(builder, swapchainHandle);
            });

        // A requested capture records its copy and barriers here, before the manual finalize below.
        // That finalize then starts from TransferSrcOptimal for this call.
        const bool capturedThisFrame = m_swapchainCapture.RecordCaptureIfRequested(
            cmd, target.image, target.extent, target.format, m_currentFrame);
        captureRecorded = capturedThisFrame;

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

        // Reset only now: a throw before this point leaves the fence signaled for the next frame.
        vkResetFences(m_device, 1, &fence);
        fenceReset = true;
        if (m_vulkanDevice->SubmitToGraphicsQueue(submitInfo, fence) != VK_SUCCESS) {
            // The fence is unsignaled and nothing will signal it: stop all further waits and submits.
            m_vulkanDevice->Waits().MarkDeviceLost();
            throw std::runtime_error("vkQueueSubmit2 failed (render graph present)");
        }
        // Only a frame that really reaches the queue owns stamps worth reading back.
        ++m_submittedFrames;
        m_gpuFrames.Submitted(m_currentFrame, m_submittedFrames);
    } catch (...) {
        // A recovery submit stamps nothing: the frame recorded into this slot is counted as dropped.
        graph.DropPipelinedGpuFrame(m_currentFrame);
        RecoverAcquiredImage(imageIndex, fenceReset, captureRecorded);
        throw;
    }

    const VkSemaphore signalSemaphore = m_frameSync.RenderFinishedSemaphore(imageIndex);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSemaphore;
    const VkSwapchainKHR swapchain = m_swapchain.Native();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    const GpuWaitService::Presented presented = waits.Present(m_presentQueue, presentInfo);
    if (presented.result == VK_ERROR_OUT_OF_DATE_KHR || presented.result == VK_SUBOPTIMAL_KHR) {
        m_resizeRequested = true;
    } else if (presented.result != VK_SUCCESS) {
        throw std::runtime_error("present failed (VkResult=" + std::to_string(presented.result) + ")");
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
