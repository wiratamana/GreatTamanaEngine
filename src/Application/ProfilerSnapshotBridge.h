#pragma once

// Hand-off of profiler snapshots from the main thread to network readers. A reader never waits for the
// main thread longer than its own bound, so the routes still answer while the main thread is blocked.

#include "../Profiling/ProfilerSnapshot.h"
#include "../Profiling/SpikeDetail.h"
#include "../Profiling/SpikeStore.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

namespace gte {

class ProfilerSnapshotBridge {
public:
    // A snapshot is built on demand only while a reader asked within this window.
    static constexpr std::uint64_t kAskWindowNanoseconds = 2'000'000'000ull;

    // Without a reader, a snapshot is still published this often so a cold reader finds one.
    static constexpr std::uint64_t kIdlePublishFrames = 30;

    // A snapshot older than this triggers a short wait for a newer one.
    static constexpr std::uint64_t kFreshNanoseconds = 250'000'000ull;

    // Main thread: true when someone asked recently, so building a snapshot is worth the cost.
    bool WantsSnapshot() const noexcept { return WantsSnapshotAt(NowNanoseconds()); }
    bool WantsSnapshotAt(std::uint64_t nowNanoseconds) const noexcept;

    // Main thread, once per closed frame: true when a snapshot should be published now. Reasons: a reader asked,
    // a spike was frozen since the last call, nothing was published yet, or kIdlePublishFrames passed.
    bool ShouldPublish(std::uint64_t completedFrames, std::uint64_t frozenSpikes) noexcept;

    // Main thread, after the frame closed.
    void Publish(const Profiling::ProfilerSnapshot& snapshot);

    // Main thread, with Publish: the scope tree of the same frame, for the timeline image.
    void PublishFrameDetail(Profiling::SpikeDetail detail);

    // Any thread. The newest published frame's scope tree; empty before the first publish.
    std::optional<Profiling::SpikeDetail> LatestFrameDetail() const;

    // Any thread. Marks the bridge as asked, waits up to `maxWait` for a fresh snapshot when the stored one is
    // old, then returns a copy with its age and the waits in progress filled in. Empty before the first publish.
    std::optional<Profiling::ProfilerSnapshot> Latest(std::chrono::milliseconds maxWait);

    // Any thread. A snapshot holding only the waits in progress, read without any lock.
    Profiling::ProfilerSnapshot LiveOnlySnapshot() const;

    // Any thread. Runtime switches of the profiler.
    void SetCapture(bool enabled) noexcept;
    void SetDisplayPaused(bool paused) noexcept;
    bool CaptureEnabled() const noexcept;
    bool DisplayPaused() const noexcept;

    // Any thread. Spike store access: pin limits are enforced by the store.
    Profiling::PinResult PinSpike(std::uint64_t frameIndex) noexcept;
    bool UnpinSpike(std::uint64_t frameIndex) noexcept;
    std::size_t PinnedSpikes() const noexcept;
    void SetSpikeThresholdMs(float milliseconds) noexcept;
    float SpikeThresholdMs() const noexcept;
    std::optional<Profiling::SpikeDetail> SpikeFrame(std::uint64_t frameIndex) const;

    static std::uint64_t NowNanoseconds() noexcept;

private:
    void MarkAsked(std::uint64_t nowNanoseconds) noexcept;

    std::atomic<std::uint64_t> m_lastAskedNanoseconds{ 0 }; // 0 = never asked

    // Main thread only: state of ShouldPublish().
    bool m_publishedOnce = false;
    std::uint64_t m_lastPublishFrames = 0;
    std::uint64_t m_seenFrozenSpikes = 0;

    mutable std::mutex m_mutex;
    std::condition_variable m_published;
    Profiling::ProfilerSnapshot m_latest{};
    std::uint64_t m_publishedAtNanoseconds = 0; // 0 = nothing published
    std::uint64_t m_generation = 0;
    std::optional<Profiling::SpikeDetail> m_latestDetail;
};

} // namespace gte
