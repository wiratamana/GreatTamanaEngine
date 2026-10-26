#pragma once

// Hand-off of profiler snapshots from the main thread to network readers. A reader never waits for the
// main thread longer than its own bound, so the routes still answer while the main thread is blocked.

#include "../Profiling/ProfilerSnapshot.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

namespace gte {

class ProfilerSnapshotBridge {
public:
    // A snapshot is built only while a reader asked within this window.
    static constexpr std::uint64_t kAskWindowNanoseconds = 2'000'000'000ull;

    // A snapshot older than this triggers a short wait for a newer one.
    static constexpr std::uint64_t kFreshNanoseconds = 250'000'000ull;

    // Main thread: true when someone asked recently, so building a snapshot is worth the cost.
    bool WantsSnapshot() const noexcept { return WantsSnapshotAt(NowNanoseconds()); }
    bool WantsSnapshotAt(std::uint64_t nowNanoseconds) const noexcept;

    // Main thread, after the frame closed.
    void Publish(const Profiling::ProfilerSnapshot& snapshot);

    // Any thread. Marks the bridge as asked, waits up to `maxWait` for a fresh snapshot when the stored one is
    // old, then returns a copy with its age and the waits in progress filled in. Empty before the first publish.
    std::optional<Profiling::ProfilerSnapshot> Latest(std::chrono::milliseconds maxWait);

    // Any thread. Runtime switches of the profiler.
    void SetCapture(bool enabled) noexcept;
    void SetDisplayPaused(bool paused) noexcept;
    bool CaptureEnabled() const noexcept;
    bool DisplayPaused() const noexcept;

    static std::uint64_t NowNanoseconds() noexcept;

private:
    void MarkAsked(std::uint64_t nowNanoseconds) noexcept;

    std::atomic<std::uint64_t> m_lastAskedNanoseconds{ 0 }; // 0 = never asked

    mutable std::mutex m_mutex;
    std::condition_variable m_published;
    Profiling::ProfilerSnapshot m_latest{};
    std::uint64_t m_publishedAtNanoseconds = 0; // 0 = nothing published
    std::uint64_t m_generation = 0;
};

} // namespace gte
