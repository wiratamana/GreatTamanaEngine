#pragma once

#include "ProfilingBudget.h"
#include "ProfilingTypes.h"
#include "ScopeAggregate.h"
#include "ScopeEvent.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gte::Profiling {

enum class ScopeSeries : std::uint8_t { TotalMs, SelfMs, Calls };

// Per-name history of the newest frames. Fixed memory, main thread only, never allocates.
// Keep it in static storage: it is close to 1 MB.
class ScopeHistory {
public:
    // Once per captured frame. A tracked scope missing from `rows` records 0 ms and 0 calls.
    void Record(std::uint64_t frameIndex, std::span<const ScopeAggregate> rows) noexcept;

    // Copies the newest out.size() frames, oldest first. Frames that were never recorded read 0.
    // Returns false and leaves `out` untouched when (lane, name) has no track.
    bool CopySeries(ScopeLane lane, NameId name, ScopeSeries series, std::span<float> out) const noexcept;

    // Frame index of the newest recorded frame; 0 before the first.
    std::uint64_t NewestFrameIndex() const noexcept;

    std::size_t FrameCount() const noexcept;      // <= kMaxFrameHistory
    std::size_t TrackCount() const noexcept;      // tracks in use
    std::uint32_t DroppedTracks() const noexcept { return m_droppedTracks; } // keys that found no track

    void Clear() noexcept;

private:
    struct Track {
        NameId name = kInvalidNameId;
        ScopeLane lane = ScopeLane::Main;
        bool used = false;
        std::uint64_t lastSeenCursor = 0;
        std::array<float, kMaxFrameHistory> totalMs{};
        std::array<float, kMaxFrameHistory> selfMs{};
        std::array<std::uint16_t, kMaxFrameHistory> calls{}; // clamped at 65535
    };

    // Index of the track for the key, creating or recycling one. kNoTrack when none is free.
    static constexpr std::size_t kNoTrack = static_cast<std::size_t>(-1);
    std::size_t FindOrCreateTrack(ScopeLane lane, NameId name) noexcept;

    std::array<Track, kHistoryTracks> m_tracks{};
    std::array<std::array<std::uint16_t, kMaxNames + 1>, kScopeLaneCount> m_trackOfName{}; // 0 = none, else index + 1
    std::array<std::uint64_t, kMaxFrameHistory> m_frameIndex{};
    std::uint64_t m_cursor = 0; // frames recorded so far
    std::uint32_t m_droppedTracks = 0;
};

static_assert(sizeof(ScopeHistory) <= 1024 * 1024, "ScopeHistory must stay within 1 MB");

} // namespace gte::Profiling
