#include "ProfilerSnapshotBridge.h"

#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ProfilerSnapshotBuilder.h"
#include "../Profiling/ProfilingClock.h"

namespace gte {

std::uint64_t ProfilerSnapshotBridge::NowNanoseconds() noexcept
{
    const std::uint64_t now = Profiling::GetProfilingPerformanceCounter();
    return now != 0 ? now : 1;
}

void ProfilerSnapshotBridge::MarkAsked(std::uint64_t nowNanoseconds) noexcept
{
    m_lastAskedNanoseconds.store(nowNanoseconds, std::memory_order_relaxed);
}

bool ProfilerSnapshotBridge::WantsSnapshotAt(std::uint64_t nowNanoseconds) const noexcept
{
    const std::uint64_t asked = m_lastAskedNanoseconds.load(std::memory_order_relaxed);
    return asked != 0 && nowNanoseconds >= asked && nowNanoseconds - asked <= kAskWindowNanoseconds;
}

bool ProfilerSnapshotBridge::ShouldPublish(std::uint64_t completedFrames, std::uint64_t frozenSpikes) noexcept
{
    const bool spiked = frozenSpikes != m_seenFrozenSpikes;
    m_seenFrozenSpikes = frozenSpikes;
    const bool idleDue = completedFrames - m_lastPublishFrames >= kIdlePublishFrames;
    if (!(spiked || idleDue || !m_publishedOnce || WantsSnapshot())) {
        return false;
    }
    m_publishedOnce = true;
    m_lastPublishFrames = completedFrames;
    return true;
}

void ProfilerSnapshotBridge::Publish(const Profiling::ProfilerSnapshot& snapshot)
{
    const std::uint64_t now = NowNanoseconds();
    {
        std::lock_guard lock(m_mutex);
        m_latest = snapshot;
        m_publishedAtNanoseconds = now;
        ++m_generation;
    }
    m_published.notify_all();
}

void ProfilerSnapshotBridge::PublishFrameDetail(Profiling::SpikeDetail detail)
{
    std::lock_guard lock(m_mutex);
    m_latestDetail = std::move(detail);
}

std::optional<Profiling::SpikeDetail> ProfilerSnapshotBridge::LatestFrameDetail() const
{
    std::lock_guard lock(m_mutex);
    return m_latestDetail;
}

std::optional<Profiling::ProfilerSnapshot> ProfilerSnapshotBridge::Latest(std::chrono::milliseconds maxWait)
{
    const std::uint64_t asked = NowNanoseconds();
    MarkAsked(asked);

    Profiling::ProfilerSnapshot copy;
    std::uint64_t publishedAt = 0;
    {
        std::unique_lock lock(m_mutex);
        const std::uint64_t startGeneration = m_generation;
        const bool fresh = m_publishedAtNanoseconds != 0 && asked >= m_publishedAtNanoseconds
            && asked - m_publishedAtNanoseconds <= kFreshNanoseconds;
        if (!fresh) {
            m_published.wait_for(lock, maxWait, [&] { return m_generation != startGeneration; });
        }
        if (m_publishedAtNanoseconds == 0) {
            return std::nullopt;
        }
        copy = m_latest;
        publishedAt = m_publishedAtNanoseconds;
    }

    const std::uint64_t now = NowNanoseconds();
    const double ageMilliseconds = now > publishedAt ? static_cast<double>(now - publishedAt) / 1.0e6 : 0.0;
    copy.snapshotAge = Profiling::PresentMetric(ageMilliseconds, Profiling::Unit::Milliseconds, Profiling::MetricKind::Derived);
    Profiling::FillLiveBlocks(copy, Profiling::FrameProfiler::Instance().LiveBlocks(), Profiling::NowMicroseconds());
    return copy;
}

Profiling::ProfilerSnapshot ProfilerSnapshotBridge::LiveOnlySnapshot() const
{
    Profiling::ProfilerSnapshot live;
    Profiling::FillLiveBlocks(live, Profiling::FrameProfiler::Instance().LiveBlocks(), Profiling::NowMicroseconds());
    return live;
}

void ProfilerSnapshotBridge::SetCapture(bool enabled) noexcept
{
    Profiling::FrameProfiler::Instance().SetCaptureEnabled(enabled);
}

void ProfilerSnapshotBridge::SetDisplayPaused(bool paused) noexcept
{
    Profiling::FrameProfiler::Instance().SetDisplayPaused(paused);
}

bool ProfilerSnapshotBridge::CaptureEnabled() const noexcept
{
    return Profiling::FrameProfiler::Instance().IsCaptureEnabled();
}

bool ProfilerSnapshotBridge::DisplayPaused() const noexcept
{
    return Profiling::FrameProfiler::Instance().IsDisplayPaused();
}

Profiling::PinResult ProfilerSnapshotBridge::PinSpike(std::uint64_t frameIndex) noexcept
{
    return Profiling::FrameProfiler::Instance().Spikes().Pin(frameIndex);
}

bool ProfilerSnapshotBridge::UnpinSpike(std::uint64_t frameIndex) noexcept
{
    return Profiling::FrameProfiler::Instance().Spikes().Unpin(frameIndex);
}

std::size_t ProfilerSnapshotBridge::PinnedSpikes() const noexcept
{
    return Profiling::FrameProfiler::Instance().Spikes().PinnedCount();
}

void ProfilerSnapshotBridge::SetSpikeThresholdMs(float milliseconds) noexcept
{
    Profiling::FrameProfiler::Instance().SetSpikeThresholdMs(milliseconds);
}

float ProfilerSnapshotBridge::SpikeThresholdMs() const noexcept
{
    return Profiling::FrameProfiler::Instance().SpikeThresholdMs();
}

std::optional<Profiling::SpikeDetail> ProfilerSnapshotBridge::SpikeFrame(std::uint64_t frameIndex) const
{
    return Profiling::BuildSpikeDetail(Profiling::FrameProfiler::Instance().Spikes(), frameIndex);
}

} // namespace gte
