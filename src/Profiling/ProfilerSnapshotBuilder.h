#pragma once

// Builds a ProfilerSnapshot from the newest finished frame. Main thread only, and only when a reader asked.

#include "FrameProfiler.h"
#include "ProfilerSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gte::Profiling {

// State the profiler cannot see by itself.
struct SnapshotInputs {
    bool deviceLost = false;
    std::uint32_t consecutiveFrameTimeouts = 0;
    std::uint32_t abandonedObjects = 0;
};

ProfilerSnapshot BuildProfilerSnapshot(const FrameProfiler& profiler, const SnapshotInputs& inputs);

// Verdict over the newest frames of the history. Unknown while capture is off or too few frames exist.
// `newestChecksFail` marks the newest frame as failing a self-check.
Verdict VerdictFromHistory(const FrameProfiler& profiler, bool capture, bool newestChecksFail);

// Copies `text` into `out`, cut to fit, always NUL terminated. Returns the stored length.
std::size_t CopySnapshotText(std::span<char> out, std::string_view text) noexcept;

// Fills `snapshot.liveBlocks` from the waits in progress now.
void FillLiveBlocks(ProfilerSnapshot& snapshot, const LiveBlockTable& table, std::uint64_t nowMicroseconds) noexcept;

// Text of a NUL terminated snapshot string.
std::string_view SnapshotText(std::span<const char> text) noexcept;

} // namespace gte::Profiling
