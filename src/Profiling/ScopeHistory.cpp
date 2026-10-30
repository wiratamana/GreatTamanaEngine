#include "ScopeHistory.h"

#include "ProfilingClock.h"

#include <algorithm>
#include <limits>

namespace gte::Profiling {

std::size_t ScopeHistory::FindOrCreateTrack(ScopeLane lane, NameId name, bool& exhausted) noexcept
{
    std::uint16_t& mapped = m_trackOfName[static_cast<std::size_t>(lane)][name];
    if (mapped != 0) {
        return static_cast<std::size_t>(mapped) - 1;
    }
    if (exhausted) {
        return kNoTrack;
    }

    // A free track, or one unseen for a whole ring (all its values are already zero).
    std::size_t target = kNoTrack;
    for (std::size_t i = 0; i < m_tracks.size(); ++i) {
        const Track& track = m_tracks[i];
        if (!track.used) {
            target = i;
            break;
        }
        if (target == kNoTrack && m_cursor - track.lastSeenCursor >= kMaxFrameHistory) {
            target = i;
        }
    }
    if (target == kNoTrack) {
        exhausted = true;
        return kNoTrack;
    }

    Track& track = m_tracks[target];
    if (track.used) {
        m_trackOfName[static_cast<std::size_t>(track.lane)][track.name] = 0;
    }
    track.used = true;
    track.name = name;
    track.lane = lane;
    track.lastSeenCursor = m_cursor;
    mapped = static_cast<std::uint16_t>(target + 1);
    return target;
}

void ScopeHistory::Record(std::uint64_t frameIndex, std::span<const ScopeAggregate> rows) noexcept
{
    const std::size_t slot = static_cast<std::size_t>(m_cursor % kMaxFrameHistory);
    m_droppedTracks = 0;

    for (Track& track : m_tracks) {
        if (track.used) {
            track.totalMs[slot] = 0.0f;
            track.selfMs[slot] = 0.0f;
            track.calls[slot] = 0;
        }
    }

    bool exhausted = false;
    for (const ScopeAggregate& row : rows) {
        if (row.kind == ScopeKind::Marker || row.name > kMaxNames || static_cast<std::size_t>(row.lane) >= kScopeLaneCount) {
            continue;
        }
        const std::size_t index = FindOrCreateTrack(row.lane, row.name, exhausted);
        if (index == kNoTrack) {
            ++m_droppedTracks;
            continue;
        }
        Track& track = m_tracks[index];
        track.lastSeenCursor = m_cursor;
        track.totalMs[slot] += static_cast<float>(TicksToMilliseconds(row.totalTicks));
        track.selfMs[slot] += static_cast<float>(TicksToMilliseconds(row.selfTicks));
        const std::uint32_t calls = std::uint32_t{ track.calls[slot] } + row.calls;
        track.calls[slot] = static_cast<std::uint16_t>(std::min<std::uint32_t>(calls, std::numeric_limits<std::uint16_t>::max()));
    }

    m_frameIndex[slot] = frameIndex;
    ++m_cursor;
}

bool ScopeHistory::CopySeries(ScopeLane lane, NameId name, ScopeSeries series, std::span<float> out) const noexcept
{
    if (static_cast<std::size_t>(lane) >= kScopeLaneCount || name > kMaxNames) {
        return false;
    }
    const std::uint16_t mapped = m_trackOfName[static_cast<std::size_t>(lane)][name];
    if (mapped == 0) {
        return false;
    }
    const Track& track = m_tracks[static_cast<std::size_t>(mapped) - 1];

    const std::size_t have = FrameCount();
    const std::size_t count = out.size();
    for (std::size_t k = 0; k < count; ++k) {
        const std::size_t age = count - 1 - k; // 0 = newest frame
        if (age >= have) {
            out[k] = 0.0f;
            continue;
        }
        const std::size_t slot = static_cast<std::size_t>((m_cursor - 1 - age) % kMaxFrameHistory);
        switch (series) {
        case ScopeSeries::TotalMs: out[k] = track.totalMs[slot]; break;
        case ScopeSeries::SelfMs: out[k] = track.selfMs[slot]; break;
        case ScopeSeries::Calls: out[k] = static_cast<float>(track.calls[slot]); break;
        }
    }
    return true;
}

std::uint64_t ScopeHistory::NewestFrameIndex() const noexcept
{
    return m_cursor == 0 ? 0 : m_frameIndex[static_cast<std::size_t>((m_cursor - 1) % kMaxFrameHistory)];
}

std::size_t ScopeHistory::FrameCount() const noexcept
{
    return static_cast<std::size_t>(std::min<std::uint64_t>(m_cursor, kMaxFrameHistory));
}

std::size_t ScopeHistory::TrackCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(m_tracks.begin(), m_tracks.end(), [](const Track& t) { return t.used; }));
}

void ScopeHistory::Clear() noexcept
{
    for (Track& track : m_tracks) {
        track = Track{};
    }
    for (auto& lane : m_trackOfName) {
        lane.fill(0);
    }
    m_frameIndex.fill(0);
    m_cursor = 0;
    m_droppedTracks = 0;
}

} // namespace gte::Profiling
