#include "GpuTimingService.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace gte {

namespace {

// Raw whole-pool slot count: 4 offscreen slots (Offscreen0 start/end,
// Offscreen1 start/end) + kGpuTimingFramesInFlight * 2 Present slots (one
// start/end pair per frame-in-flight index) - see GpuTimingService.h's own
// class comment for the full 8-slot layout table.
constexpr std::uint32_t kOffscreenSlotCount = 4;
constexpr std::uint32_t kTotalQuerySlotCount = kOffscreenSlotCount + kGpuTimingFramesInFlight * 2;

} // namespace

GpuTimingService::GpuTimingService(
    VkDevice device, VkQueue graphicsQueue, std::uint32_t graphicsQueueFamily, const GpuTimestampCapability& capability)
    : m_device(device)
    , m_capability(capability)
{
#if !GTE_ENABLE_PROFILER
    // A GTE_ENABLE_PROFILER=OFF build behaves EXACTLY as if the device
    // reported no timestamp support at all - no VkQueryPool is ever created
    // for the lifetime of the process, matching the "genuinely zero cost,
    // not just small" bar ScopeTimer's own compiled-out branch sets (see
    // AGENTS.md, "Profiling", and PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md,
    // Step 2.3). This is the ONLY place this class reads GTE_ENABLE_PROFILER
    // - every other method below is unconditional and simply inert whenever
    // m_capability.supported ends up false, whatever the reason.
    m_capability.supported = false;
#endif

    if (!m_capability.supported) {
        return;
    }

    m_queryPool.emplace(m_device, kTotalQuerySlotCount);

    // Full, one-time, up-front reset of every slot - what makes it safe for
    // RecordOffscreenPassStart()/RecordPresentPassStart()'s own per-call
    // reset-then-write sequences (Phase 4C/4D) to assume every slot starts
    // life already reset, rather than needing a separate "is this the very
    // first use" branch scattered through their own logic. See
    // PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md, Phase 4B.
    WarmUpResetEntirePool(graphicsQueue, graphicsQueueFamily);
}

void GpuTimingService::WarmUpResetEntirePool(VkQueue graphicsQueue, std::uint32_t graphicsQueueFamily)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("GpuTimingService: vkCreateCommandPool failed (warm-up reset)");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("GpuTimingService: vkAllocateCommandBuffers failed (warm-up reset)");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("GpuTimingService: vkBeginCommandBuffer failed (warm-up reset)");
    }

    // vkCmdResetQueryPool must be recorded OUTSIDE a dynamic rendering
    // instance (never between vkCmdBeginRendering/vkCmdEndRendering) - see
    // PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md, Step 2.3. Trivially true
    // here: this throwaway command buffer never begins one at all.
    vkCmdResetQueryPool(cmd, m_queryPool->Native(), 0, m_queryPool->SlotCount());

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("GpuTimingService: vkEndCommandBuffer failed (warm-up reset)");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("GpuTimingService: vkCreateFence failed (warm-up reset)");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
        vkDestroyFence(m_device, fence, nullptr);
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("GpuTimingService: vkQueueSubmit failed (warm-up reset)");
    }

    vkWaitForFences(m_device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    vkDestroyFence(m_device, fence, nullptr);
    vkDestroyCommandPool(m_device, commandPool, nullptr); // also frees `cmd`.
}

std::uint32_t GpuTimingService::OffscreenSlotBase(GpuTimingSlot slot) noexcept
{
    // Offscreen0 (0) -> base 0; Offscreen1 (1) -> base 2. Never called with
    // GpuTimingSlot::SwapchainPresent (that's PresentSlotBase()'s job).
    return static_cast<std::uint32_t>(slot) * 2;
}

std::uint32_t GpuTimingService::PresentSlotBase(std::uint32_t frameInFlightIndex) noexcept
{
    return kOffscreenSlotCount + PresentTimestampSlotBase(frameInFlightIndex);
}

GpuTimingSample GpuTimingService::ReadResultAt(std::uint32_t slotBase) noexcept
{
    std::uint64_t ticks[2] = { 0, 0 };
    // VK_QUERY_RESULT_WAIT_BIT is not required here - the caller has
    // already confirmed (via its own fence wait) that this exact
    // submission is complete before calling this (see
    // ReadOffscreenResultNow()/ReadPresentResultIfAvailable() above) - but
    // is harmless to add defensively; deliberately omitted to keep this
    // call an obvious, honest reflection of "this data is already known
    // ready", not a hidden second wait.
    vkGetQueryPoolResults(m_device, m_queryPool->Native(), slotBase, 2, sizeof(ticks), ticks, sizeof(std::uint64_t),
        VK_QUERY_RESULT_64_BIT);

    const double milliseconds =
        ConvertTimestampDeltaToMilliseconds(ticks[0], ticks[1], m_capability.timestampPeriodNs, m_capability.validBits);
    return GpuTimingSample{ GpuTimingSample::Status::Present, milliseconds };
}

void GpuTimingService::RecordOffscreenPassStart(VkCommandBuffer cmd, GpuTimingSlot slot) noexcept
{
    if (!IsSupported() || !m_captureEnabled) {
        return;
    }
    const std::uint32_t base = OffscreenSlotBase(slot);
    vkCmdResetQueryPool(cmd, m_queryPool->Native(), base, 2);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_queryPool->Native(), base);
}

void GpuTimingService::RecordOffscreenPassEnd(VkCommandBuffer cmd, GpuTimingSlot slot) noexcept
{
    if (!IsSupported() || !m_captureEnabled) {
        return;
    }
    const std::uint32_t base = OffscreenSlotBase(slot);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, m_queryPool->Native(), base + 1);
}

GpuTimingSample GpuTimingService::ReadOffscreenResultNow(GpuTimingSlot slot) noexcept
{
    // RenderOffscreen() is already fully synchronous (see
    // PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md, Step 2.1) - there is no
    // Offscreen-path warm-up concept the way Present has, so
    // `hasWrittenData` is unconditionally true whenever this is even
    // reached in a state that would actually read the pool.
    const GpuTimingSample::Status status = ResolveGpuTimingStatus(IsSupported(), m_captureEnabled, /*hasWrittenData=*/true);
    GpuTimingSample sample{ status, 0.0 };
    if (status == GpuTimingSample::Status::Present) {
        sample = ReadResultAt(OffscreenSlotBase(slot));
    }
    m_lastKnown[static_cast<std::size_t>(slot)] = sample;
    return sample;
}

void GpuTimingService::RecordPresentPassStart(VkCommandBuffer cmd, std::uint32_t frameInFlightIndex) noexcept
{
    if (!IsSupported() || !m_captureEnabled) {
        return;
    }
    const std::uint32_t base = PresentSlotBase(frameInFlightIndex);
    vkCmdResetQueryPool(cmd, m_queryPool->Native(), base, 2);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_queryPool->Native(), base);
}

void GpuTimingService::RecordPresentPassEnd(VkCommandBuffer cmd, std::uint32_t frameInFlightIndex) noexcept
{
    if (!IsSupported() || !m_captureEnabled) {
        return;
    }
    const std::uint32_t base = PresentSlotBase(frameInFlightIndex);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, m_queryPool->Native(), base + 1);
}

GpuTimingSample GpuTimingService::ReadPresentResultIfAvailable(std::uint32_t frameInFlightIndex) noexcept
{
    const bool hasWritten =
        (frameInFlightIndex < kGpuTimingFramesInFlight) && m_presentSlotEverWritten[frameInFlightIndex];
    const GpuTimingSample::Status status = ResolveGpuTimingStatus(IsSupported(), m_captureEnabled, hasWritten);
    GpuTimingSample sample{ status, 0.0 };
    if (status == GpuTimingSample::Status::Present) {
        sample = ReadResultAt(PresentSlotBase(frameInFlightIndex));
    }
    m_lastKnown[static_cast<std::size_t>(GpuTimingSlot::SwapchainPresent)] = sample;
    return sample;
}

void GpuTimingService::MarkPresentSlotWritten(std::uint32_t frameInFlightIndex) noexcept
{
    if (!IsSupported() || !m_captureEnabled) {
        return;
    }
    if (frameInFlightIndex < kGpuTimingFramesInFlight) {
        m_presentSlotEverWritten[frameInFlightIndex] = true;
    }
}

GpuTimingSample GpuTimingService::LastKnown(GpuTimingSlot slot) const noexcept
{
    return m_lastKnown[static_cast<std::size_t>(slot)];
}

} // namespace gte
