#pragma once

#include "ProfilingBudget.h"
#include "ScopeEvent.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gte::Profiling {

// Fixed-capacity append-only event array. Clear() resets the count only, so a frame never zeroes the storage.
template <class Event, std::size_t Capacity>
class EventBuffer {
public:
    // Returns false and counts the drop when full.
    bool Append(const Event& event) noexcept
    {
        if (m_count >= Capacity) {
            ++m_dropped;
            return false;
        }
        m_events[m_count++] = event;
        return true;
    }

    void Clear() noexcept
    {
        m_count = 0;
        m_dropped = 0;
    }

    std::span<const Event> Events() const noexcept { return { m_events.data(), m_count }; }
    std::size_t Size() const noexcept { return m_count; }
    std::uint32_t Dropped() const noexcept { return m_dropped; }

    // True when events were lost: every total built from this buffer is a lower bound.
    bool Partial() const noexcept { return m_dropped != 0; }

    static constexpr std::size_t kCapacity = Capacity;

private:
    std::array<Event, Capacity> m_events{};
    std::size_t m_count = 0;
    std::uint32_t m_dropped = 0;
};

using ScopeEventBuffer = EventBuffer<ScopeEvent, kMaxScopeEventsPerFrame>;

} // namespace gte::Profiling
