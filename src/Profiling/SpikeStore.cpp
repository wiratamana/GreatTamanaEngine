#include "SpikeStore.h"

#include "ProfilingClock.h"
#include "ScopeTree.h"

#include <algorithm>

namespace gte::Profiling {

const char* ToString(SpikeClass kind) noexcept
{
    return kind == SpikeClass::Explained ? "explained" : "unexplained";
}

bool IsSpikeFrame(double frameMs, double medianMs, double userThresholdMs) noexcept
{
    if (userThresholdMs > 0.0) {
        return frameMs > userThresholdMs;
    }
    return medianMs > 0.0 && frameMs > medianMs * kSpikeMedianFactor;
}

double MedianOf(std::span<const double> recent) noexcept
{
    if (recent.empty()) {
        return 0.0;
    }
    std::array<double, 128> scratch{};
    const std::size_t count = std::min(recent.size(), scratch.size());
    const std::span<const double> window = recent.last(count);
    std::copy(window.begin(), window.end(), scratch.begin());
    const auto middle = scratch.begin() + static_cast<std::ptrdiff_t>(count / 2);
    std::nth_element(scratch.begin(), middle, scratch.begin() + static_cast<std::ptrdiff_t>(count));
    return *middle;
}

SpikeClassification ClassifySpike(const FrameRecord& record, double medianMs) noexcept
{
    SpikeClassification result;
    result.excessMs = std::max(0.0, record.summary.cpuMilliseconds - medianMs);

    const std::span<const ScopeEvent> events = record.scopes.Events();
    std::array<ScopeTimes, kMaxScopeEventsPerFrame> times{};
    ComputeScopeTimes(events, times);

    std::uint64_t bestSelf = 0;
    const std::size_t known = std::min(events.size(), times.size());
    for (std::size_t i = 0; i < known; ++i) {
        if (events[i].kind == ScopeKind::Marker) {
            result.markerPresent = true;
            continue;
        }
        if (times[i].selfTicks > bestSelf) {
            bestSelf = times[i].selfTicks;
            result.dominant = events[i].name;
        }
    }
    result.dominantSelfMs = TicksToMilliseconds(bestSelf);
    const bool dominates = result.dominantSelfMs >= result.excessMs * kSpikeExplainedShare;
    result.kind = (result.markerPresent || dominates) ? SpikeClass::Explained : SpikeClass::Unexplained;
    return result;
}

int SpikeStore::FindLocked(std::uint64_t frameIndex) const noexcept
{
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].used && m_slots[i].record.summary.frameIndex == frameIndex) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::size_t SpikeStore::PinnedLocked() const noexcept
{
    std::size_t pinned = 0;
    for (const FrozenSpike& slot : m_slots) {
        pinned += (slot.used && slot.pinned) ? 1 : 0;
    }
    return pinned;
}

bool SpikeStore::Freeze(const FrameRecord& record, double medianMs, const SpikeClassification& classification) noexcept
{
    std::lock_guard lock(m_mutex);

    int target = -1;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        if (!m_slots[i].used) {
            target = static_cast<int>(i);
            break;
        }
    }
    if (target < 0) {
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            const FrozenSpike& slot = m_slots[i];
            if (slot.pinned) {
                continue;
            }
            if (target < 0 || slot.sequence < m_slots[static_cast<std::size_t>(target)].sequence) {
                target = static_cast<int>(i);
            }
        }
    }
    if (target < 0) {
        return false;
    }

    FrozenSpike& slot = m_slots[static_cast<std::size_t>(target)];
    slot.used = true;
    slot.pinned = false;
    slot.gpuAttached = false;
    slot.sequence = ++m_sequence;
    slot.medianMs = medianMs;
    slot.classification = classification;
    slot.record = record;
    return true;
}

bool SpikeStore::AttachGpuSpan(std::uint64_t frameIndex, const GpuViewMetric& span, std::uint64_t newestFrame) noexcept
{
    if (span.status != SampleStatus::Present) {
        return false;
    }
    std::lock_guard lock(m_mutex);
    const int index = FindLocked(frameIndex);
    if (index < 0 || newestFrame < frameIndex || newestFrame - frameIndex > kSpikeLateGpuFrames) {
        return false;
    }
    FrozenSpike& slot = m_slots[static_cast<std::size_t>(index)];
    GpuViewMetric& total = slot.record.summary.gpu.span;
    if (total.status == SampleStatus::Present) {
        total.milliseconds += span.milliseconds; // two submits of one frame run one after the other
        total.foldedPasses += span.foldedPasses;
    } else {
        total = span;
    }
    slot.gpuAttached = true;
    return true;
}

PinResult SpikeStore::Pin(std::uint64_t frameIndex) noexcept
{
    std::lock_guard lock(m_mutex);
    const int index = FindLocked(frameIndex);
    if (index < 0) {
        return PinResult::NotFound;
    }
    FrozenSpike& slot = m_slots[static_cast<std::size_t>(index)];
    if (slot.pinned) {
        return PinResult::Ok;
    }
    if (PinnedLocked() >= kMaxPinnedSpikes) {
        return PinResult::LimitReached;
    }
    slot.pinned = true;
    return PinResult::Ok;
}

bool SpikeStore::Unpin(std::uint64_t frameIndex) noexcept
{
    std::lock_guard lock(m_mutex);
    const int index = FindLocked(frameIndex);
    if (index < 0) {
        return false;
    }
    m_slots[static_cast<std::size_t>(index)].pinned = false;
    return true;
}

std::size_t SpikeStore::Count() const noexcept
{
    std::lock_guard lock(m_mutex);
    return static_cast<std::size_t>(std::count_if(m_slots.begin(), m_slots.end(), [](const FrozenSpike& s) { return s.used; }));
}

std::size_t SpikeStore::PinnedCount() const noexcept
{
    std::lock_guard lock(m_mutex);
    return PinnedLocked();
}

std::uint64_t SpikeStore::TotalFrozen() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_sequence;
}

std::size_t SpikeStore::Rows(std::span<Row> out) const noexcept
{
    std::lock_guard lock(m_mutex);

    std::array<std::size_t, kSlots> order{};
    std::size_t used = 0;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].used) {
            order[used++] = i;
        }
    }
    std::sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(used),
        [this](std::size_t a, std::size_t b) { return m_slots[a].sequence > m_slots[b].sequence; });

    const std::size_t written = std::min(used, out.size());
    for (std::size_t i = 0; i < written; ++i) {
        const FrozenSpike& slot = m_slots[order[i]];
        Row& row = out[i];
        row.frameIndex = slot.record.summary.frameIndex;
        row.frameMs = slot.record.summary.cpuMilliseconds;
        row.medianMs = slot.medianMs;
        row.pinned = slot.pinned;
        row.gpuAttached = slot.gpuAttached;
        row.classification = slot.classification;
        row.gpu = slot.record.summary.gpu;
    }
    return written;
}

bool SpikeStore::CopyFrame(std::uint64_t frameIndex, FrameRecord& out, SpikeClassification* classification) const noexcept
{
    std::lock_guard lock(m_mutex);
    const int index = FindLocked(frameIndex);
    if (index < 0) {
        return false;
    }
    const FrozenSpike& slot = m_slots[static_cast<std::size_t>(index)];
    out = slot.record;
    if (classification != nullptr) {
        *classification = slot.classification;
    }
    return true;
}

void SpikeStore::Clear() noexcept
{
    std::lock_guard lock(m_mutex);
    for (FrozenSpike& slot : m_slots) {
        slot.used = false;
        slot.pinned = false;
        slot.gpuAttached = false;
    }
    m_sequence = 0;
}

} // namespace gte::Profiling
