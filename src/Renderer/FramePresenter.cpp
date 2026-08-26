#include "FramePresenter.h"

#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/RenderGraphBarrierPlanner.h"
#include "RenderGraph/RenderGraphBuilder.h"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace gte {

FramePresenter::FramePresenter(VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface,
    std::uint32_t graphicsQueueFamily, std::uint32_t presentQueueFamily, VkQueue graphicsQueue,
    VkQueue presentQueue, int width, int height, VmaAllocator allocator, VkFormat depthFormat,
    std::shared_ptr<GpuMemoryTracker> memoryTracker, std::shared_ptr<GpuTimingService> gpuTiming)
    : m_device(device)
    , m_graphicsQueue(graphicsQueue)
    , m_presentQueue(presentQueue)
    , m_graphicsQueueFamily(graphicsQueueFamily)
    , m_allocator(allocator)
    , m_depthFormat(depthFormat)
    , m_memoryTracker(std::move(memoryTracker))
    , m_gpuTiming(std::move(gpuTiming))
    , m_swapchain(physicalDevice, device, surface, graphicsQueueFamily, presentQueueFamily, width, height)
    , m_frameSync(device, kFramesInFlight, m_swapchain.ImageCount())
{
    m_pendingWidth = width;
    m_pendingHeight = height;

    CreateCommandObjects();
    // m_depthBuffers is deliberately NOT created here - see its own comment
    // in FramePresenter.h for why the swapchain's own depth buffers are
    // allocated lazily (EnsureDepthBuffersForSwapchain(), called from
    // Present() only once a frame is actually found to need one) rather
    // than unconditionally up front.
}

FramePresenter::~FramePresenter()
{
    Destroy();
}

FramePresenter::FramePresenter(FramePresenter&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_graphicsQueue(std::exchange(other.m_graphicsQueue, VK_NULL_HANDLE))
    , m_presentQueue(std::exchange(other.m_presentQueue, VK_NULL_HANDLE))
    , m_graphicsQueueFamily(other.m_graphicsQueueFamily)
    , m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_depthFormat(other.m_depthFormat)
    , m_memoryTracker(std::move(other.m_memoryTracker))
    , m_gpuTiming(std::move(other.m_gpuTiming))
    , m_swapchain(std::move(other.m_swapchain))
    , m_frameSync(std::move(other.m_frameSync))
    , m_depthBuffers(std::move(other.m_depthBuffers))
    , m_commandPool(std::exchange(other.m_commandPool, VK_NULL_HANDLE))
    , m_commandBuffers(other.m_commandBuffers)
    , m_offscreenCommandBuffer(std::exchange(other.m_offscreenCommandBuffer, VK_NULL_HANDLE))
    , m_currentFrame(other.m_currentFrame)
    , m_resizeRequested(other.m_resizeRequested)
    , m_pendingWidth(other.m_pendingWidth)
    , m_pendingHeight(other.m_pendingHeight)
{
    other.m_commandBuffers.fill(VK_NULL_HANDLE);
}

FramePresenter& FramePresenter::operator=(FramePresenter&& other) noexcept
{
    if (this != &other) {
        Destroy();

        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_graphicsQueue = std::exchange(other.m_graphicsQueue, VK_NULL_HANDLE);
        m_presentQueue = std::exchange(other.m_presentQueue, VK_NULL_HANDLE);
        m_graphicsQueueFamily = other.m_graphicsQueueFamily;
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_depthFormat = other.m_depthFormat;
        m_memoryTracker = std::move(other.m_memoryTracker);
        m_gpuTiming = std::move(other.m_gpuTiming);
        m_swapchain = std::move(other.m_swapchain);
        m_frameSync = std::move(other.m_frameSync);
        m_depthBuffers = std::move(other.m_depthBuffers);
        m_commandPool = std::exchange(other.m_commandPool, VK_NULL_HANDLE);
        m_commandBuffers = other.m_commandBuffers;
        other.m_commandBuffers.fill(VK_NULL_HANDLE);
        m_offscreenCommandBuffer = std::exchange(other.m_offscreenCommandBuffer, VK_NULL_HANDLE);
        m_currentFrame = other.m_currentFrame;
        m_resizeRequested = other.m_resizeRequested;
        m_pendingWidth = other.m_pendingWidth;
        m_pendingHeight = other.m_pendingHeight;
    }
    return *this;
}

void FramePresenter::Destroy() noexcept
{
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    // m_depthBuffers, m_frameSync, m_swapchain clean themselves up
    // automatically right after this, in reverse declaration order.
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

    // Separate command buffer for RenderOffscreen() - see the member
    // comment in FramePresenter.h for why this doesn't share the per-frame
    // ones above.
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
    // Engine-owned depth buffers, one per swapchain image (see the class
    // comment in FramePresenter.h for why indexed by image index) - named
    // here (rather than left "(unnamed)" in the Editor's "Memory" panel) so
    // they're identifiable at a glance instead of showing up as anonymous
    // GPU textures. debugName must have static storage duration (see
    // DepthBuffer's constructor comment) - a fixed table of string literals
    // covers every swapchain image count this engine could realistically
    // negotiate (2-4 in practice); an index beyond the table falls back to
    // unnamed rather than risking a dangling pointer from a computed name.
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
            static_cast<int>(extent.height), m_depthFormat, debugName);
    }
}

void FramePresenter::EnsureDepthBuffersForSwapchain()
{
    // No-op once they already exist - see m_depthBuffers' own comment in
    // FramePresenter.h for why they're deliberately not created eagerly in
    // the constructor. Called from Present() only on a frame that's
    // actually found to need one (FrameRecorder::HasQueuedDraws()).
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
        // Minimized (or otherwise zero-sized) - nothing sensible to build
        // yet. Leave m_resizeRequested set so we keep retrying each frame
        // until the window has a real size again.
        return;
    }

    vkDeviceWaitIdle(m_device);

    m_swapchain.Recreate(m_pendingWidth, m_pendingHeight);

    // Per-swapchain-image semaphores must be rebuilt alongside the
    // swapchain, since the image count can change (or just to be safe).
    m_frameSync.RecreateRenderFinishedSemaphores(m_swapchain.ImageCount());

    // Same reasoning for the per-swapchain-image depth buffers - a
    // genuinely new size (and possibly image count) means genuinely new
    // depth images, not a resize-in-place. Only rebuilds them if they
    // already existed, though (see m_depthBuffers' own comment in
    // FramePresenter.h) - never creates them from scratch here; a
    // presenter that never actually needed them stays that way across a
    // resize too.
    if (!m_depthBuffers.empty()) {
        CreateDepthBuffers();
    }

    m_resizeRequested = false;
}

std::optional<DrawStats> FramePresenter::Present(
    FrameRecorder& frameRecorder, const std::function<void(VkCommandBuffer)>& recordExtra)
{
    if (m_pendingWidth <= 0 || m_pendingHeight <= 0) {
        return std::nullopt; // Minimized - nothing to draw this frame.
    }

    if (m_resizeRequested) {
        RecreateSwapchain();
        if (m_resizeRequested) {
            return std::nullopt; // Still pending (stayed minimized) - try again next frame.
        }
    }

    const VkFence fence = m_frameSync.InFlightFence(m_currentFrame);
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
    // Phase 4D (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - safe to read
    // back whatever was written into THIS frame-in-flight slot
    // kFramesInFlight frames ago: the fence wait immediately above already
    // proves that submission has fully completed - this is the exact,
    // pre-existing synchronization point Phase 4D piggybacks on, never a
    // new wait added just to fetch this sooner. A no-op (cached
    // Absent/Unsupported, no Vulkan call) until this slot has genuinely
    // been written at least once - see GpuTimingService::
    // ReadPresentResultIfAvailable()'s own warm-up handling.
    m_gpuTiming->ReadPresentResultIfAvailable(m_currentFrame);

    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        m_device, m_swapchain.Native(), std::numeric_limits<std::uint64_t>::max(),
        m_frameSync.ImageAvailableSemaphore(m_currentFrame), VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return std::nullopt;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed (VkResult=" + std::to_string(acquireResult) + ")");
    }

    vkResetFences(m_device, 1, &fence);

    // Whether THIS frame's swapchain pass will actually draw real, depth-
    // tested engine geometry directly into the swapchain image, rather than
    // just Dear ImGui's own (never depth-tested) chrome - see
    // FrameRecorder::HasQueuedDraws()'s own comment for the two cases where
    // this is true (a release build with no Editor at all, or the rare
    // Editor edge case where both "Game" and "Scene" panels are
    // simultaneously hidden) versus the common Editor case where it's
    // false. Decided BEFORE beginning command buffer recording below, since
    // EnsureDepthBuffersForSwapchain() is a plain host-side Vulkan resource
    // creation call, not itself part of what gets recorded.
    const bool needsDepth = frameRecorder.HasQueuedDraws();
    if (needsDepth) {
        EnsureDepthBuffersForSwapchain();
    }

    const VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("vkBeginCommandBuffer failed");
    }

    // Phase 4D (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - reset (both
    // slots for THIS frame-in-flight index) + a TOP_OF_PIPE timestamp
    // write, recorded OUTSIDE any dynamic rendering instance (before
    // frameRecorder.RecordFrame() below opens/closes its own
    // vkCmdBeginRendering/vkCmdEndRendering pair - see AGENTS.md-equivalent
    // reasoning in that document's Step 2.3). Safe to reset+write here
    // specifically because the fence wait at the top of this function
    // already proved the LAST use of this exact slot pair
    // (kFramesInFlight frames ago) is complete. A no-op when unsupported/
    // uncompiled-in/capture-disabled.
    m_gpuTiming->RecordPresentPassStart(cmd, m_currentFrame);

    RenderTarget target;
    target.image = m_swapchain.Image(imageIndex);
    target.imageView = m_swapchain.ImageView(imageIndex);
    target.extent = m_swapchain.Extent();
    target.format = m_swapchain.ImageFormat();
    if (needsDepth) {
        // Depth buffer paired with THIS swapchain image - see the class
        // comment in FramePresenter.h for why indexing by imageIndex (not
        // m_currentFrame) is what makes this safe with no extra
        // synchronization.
        const DepthBuffer& depthBuffer = m_depthBuffers[imageIndex];
        target.depthImage = depthBuffer.Image();
        target.depthImageView = depthBuffer.View();
        target.depthFormat = depthBuffer.Format();
        target.depthHasStencil = depthBuffer.HasStencilComponent();
    }
    // else: target.depthImage/depthImageView stay VK_NULL_HANDLE and
    // target.depthFormat stays VK_FORMAT_UNDEFINED (RenderTarget's own
    // defaults) - FrameRecorder::RecordFrame() treats a VK_NULL_HANDLE
    // depthImage as "skip the depth attachment for this pass entirely".
    const DrawStats drawStats = frameRecorder.RecordFrame(
        cmd, target, ColorFormat(), m_depthFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, recordExtra);

    // BOTTOM_OF_PIPE timestamp write, immediately followed by marking this
    // frame-in-flight slot as having genuinely been written this call -
    // see RecordPresentPassStart()'s own comment above for why this
    // placement (bracketing the WHOLE recorded pass, including recordExtra
    // - Dear ImGui's own chrome) is correct. MarkPresentSlotWritten() is a
    // safe no-op under the exact same guard as the Record* calls, so it's
    // fine to call unconditionally right here (see GpuTimingService.h).
    m_gpuTiming->RecordPresentPassEnd(cmd, m_currentFrame);
    m_gpuTiming->MarkPresentSlotWritten(m_currentFrame);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed");
    }

    const VkSemaphore waitSemaphore = m_frameSync.ImageAvailableSemaphore(m_currentFrame);
    const VkSemaphore signalSemaphore = m_frameSync.RenderFinishedSemaphore(imageIndex);
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed");
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

    return drawStats;
}

DrawStats FramePresenter::RenderOffscreen(FrameRecorder& frameRecorder, RenderTexture& target,
    std::optional<GpuTimingSlot> timingSlot, const std::function<void(VkCommandBuffer)>& recordExtra)
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

    // Phase 4C (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - reset (both
    // slots) + a TOP_OF_PIPE timestamp write, recorded BEFORE
    // frameRecorder.RecordFrame() below records its own
    // vkCmdBeginRendering/vkCmdEndRendering pair - vkCmdResetQueryPool must
    // never be recorded INSIDE a dynamic rendering instance (see that
    // document's own Step 2.3), which this placement satisfies by
    // construction. Safe to reset+write here specifically because the
    // vkWaitForFences() above already proved the LAST use of this exact
    // slot pair (last frame's call for this SAME logical pass) is complete.
    // A no-op (no Vulkan call at all) when timingSlot is std::nullopt.
    if (timingSlot.has_value()) {
        m_gpuTiming->RecordOffscreenPassStart(m_offscreenCommandBuffer, *timingSlot);
    }

    const DrawStats drawStats = frameRecorder.RecordFrame(m_offscreenCommandBuffer, target.Target(), ColorFormat(),
        m_depthFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, recordExtra);

    // BOTTOM_OF_PIPE timestamp write - see the RecordOffscreenPassStart()
    // comment above for why this placement (bracketing the WHOLE recorded
    // pass, including recordExtra) is correct, and never inside
    // FrameRecorder::RecordFrame() itself (see AGENTS.md-equivalent
    // reasoning in PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md, Step 2.3).
    if (timingSlot.has_value()) {
        m_gpuTiming->RecordOffscreenPassEnd(m_offscreenCommandBuffer, *timingSlot);
    }

    if (vkEndCommandBuffer(m_offscreenCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed (offscreen)");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_offscreenCommandBuffer;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, offscreenFence) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed (offscreen)");
    }

    // Synchronous for now - see the declaration comment in Renderer.h.
    vkWaitForFences(m_device, 1, &offscreenFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    // Safe to read back now - the fence wait immediately above already
    // guarantees this exact submission (including the timestamp writes
    // above) has fully completed. A no-op (no Vulkan call, no cache update)
    // when timingSlot is std::nullopt - see
    // PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md, Phase 4C.
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

    // Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md) -
    // GPU timestamp queries (GpuTimingService) are NOT wired into the
    // render-graph-driven offscreen/present paths yet, matching Phase 6's
    // own explicitly-documented scope decision (see
    // RENDERGRAPH_PHASE6_COMPLETION_REPORT.md's "A deliberate scope
    // decision") - RenderGraph's own PassGpuStats::timing is honestly
    // Status::Absent for every pass today (see RenderGraph.h's own "GPU
    // TIMING NOTE"). Generalizing GpuTimingService's fixed-3-slot pool into
    // a real, per-pass-name query pool remains a follow-up, not done in
    // this session - see RENDERGRAPH_PHASE7_COMPLETION_REPORT.md.
    return m_offscreenCommandBuffer;
}

void FramePresenter::EndOffscreenRecording()
{
    if (vkEndCommandBuffer(m_offscreenCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed (offscreen render graph)");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_offscreenCommandBuffer;

    const VkFence offscreenFence = m_frameSync.OffscreenFence();
    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, offscreenFence) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed (offscreen render graph)");
    }

    // Synchronous for now - matches RenderOffscreen()'s own documented
    // behavior (see the declaration comment in Renderer.h).
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
        // Depth buffer paired with THIS swapchain image - see the class
        // comment for why indexing by imageIndex (not m_currentFrame) is
        // what makes this safe with no extra synchronization.
        const DepthBuffer& depthBuffer = m_depthBuffers[imageIndex];
        target.depthImage = depthBuffer.Image();
        target.depthImageView = depthBuffer.View();
        target.depthFormat = depthBuffer.Format();
        target.depthHasStencil = depthBuffer.HasStencilComponent();
    }

    // The render graph's own Compile() step can, in principle, throw
    // std::runtime_error on a genuine dependency cycle (structurally
    // unreachable through any graph this engine declares today - see
    // RENDERGRAPH_PHASE3_COMPLETION_REPORT.md) - deliberately NOT caught
    // here; the caller (Application::Run()) is responsible for that, per
    // RENDERGRAPH_PHASE6_COMPLETION_REPORT.md's own Step 3.5 guidance.
    graph.Execute(cmd, rg::ExecuteTimingMode::PipelinedDeferredReadback,
        [&](rg::RenderGraphBuilder& builder) -> std::vector<rg::TextureHandle> {
            const rg::TextureHandle swapchainHandle =
                builder.ImportTexture("Swapchain", target, VK_IMAGE_LAYOUT_UNDEFINED);
            return build(builder, swapchainHandle);
        });

    // Manual finalize: the render graph itself never learns that THIS
    // particular imported resource is about to be handed to
    // vkQueuePresentKHR (no pass ever declares a "PresentSrc" usage - see
    // RENDERGRAPH_PHASE5_COMPLETION_REPORT.md's own "design decision worth
    // flagging" note) - so the swapchain image's final transition from
    // ColorAttachmentWrite to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR is done here,
    // by hand, using the exact same pure Phase 5 helpers
    // (RequiredStateFor()/EmitImageBarrier()) the graph itself uses
    // internally - safe here because graph.Execute() above has already
    // closed its own vkCmdBeginRendering/vkCmdEndRendering bracket.
    {
        const rg::ResourceState previous = rg::RequiredStateFor(rg::ResourceAccess::ColorAttachmentWrite, false);
        const rg::ResourceState next{
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE
        };
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        rg::EmitImageBarrier(cmd, target.image, range, previous, next);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed (render graph present)");
    }

    const VkSemaphore waitSemaphore = m_frameSync.ImageAvailableSemaphore(m_currentFrame);
    const VkSemaphore signalSemaphore = m_frameSync.RenderFinishedSemaphore(imageIndex);
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed (render graph present)");
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

} // namespace gte
