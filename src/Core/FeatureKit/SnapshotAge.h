#pragma once

#include <cstdint>

namespace gte::fx {

// A view report older than this many frames (against the newest frame of any view) counts as not rendered.
inline constexpr std::uint64_t kSnapshotStaleFrames = 2;

// True when a report of `frame` is too old for `newestFrame`, or claims to be from the future.
constexpr bool SnapshotIsStale(std::uint64_t frame, std::uint64_t newestFrame) noexcept
{
    return frame > newestFrame || newestFrame - frame > kSnapshotStaleFrames;
}

} // namespace gte::fx
