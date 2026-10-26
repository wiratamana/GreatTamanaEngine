#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace gte::Profiling {

// Interned name handle. 0 is invalid.
using NameId = std::uint32_t;
inline constexpr NameId kInvalidNameId = 0;

// Returned by the name table when it is full.
inline constexpr NameId kOverflowNameId = 1;

enum class ScopeKind : std::uint8_t {
    Work,    // CPU doing useful work
    Blocked, // CPU waiting on something else
    Marker,  // Instant event, zero duration
};

// Which render view a scope belongs to. Display labels live in the UI and network layers.
enum class ViewSlot : std::uint8_t {
    Shared,
    Primary,
    Secondary,
};
inline constexpr std::size_t kViewSlotCount = 3;

// Whether a measured value can be trusted. Never collapse these into a bare 0.
enum class SampleStatus : std::uint8_t {
    Present,
    Stale,
    Absent,
    Unsupported,
    Partial,
    Error,
    Inactive,
};
inline constexpr std::size_t kSampleStatusCount = 7;

// One finished CPU scope. Ticks are nanoseconds. Events are appended in post-order (children first).
struct ScopeEvent {
    std::uint64_t startTicks;
    std::uint64_t endTicks;
    NameId name;
    std::uint16_t depth; // 0 = top level
    ScopeKind kind;
    ViewSlot view;
};
static_assert(sizeof(ScopeEvent) == 24, "ScopeEvent must stay 24 bytes");
static_assert(alignof(ScopeEvent) == 8);
static_assert(std::is_trivially_copyable_v<ScopeEvent>);

// Elapsed ticks with unsigned math. A clock that ran backwards reads as 0.
constexpr std::uint64_t DurationTicks(const ScopeEvent& e) noexcept
{
    return e.endTicks >= e.startTicks ? e.endTicks - e.startTicks : 0;
}

} // namespace gte::Profiling
