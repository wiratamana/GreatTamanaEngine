#pragma once

#include <cstdint>

namespace gte {

// Cheap POD id of a resource tracked by GpuMemoryTracker; always minted by the tracker.
// `index` is a direct slot index; `generation` rejects stale handles to a reused slot (e.g. after a resize).
struct GpuResourceHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0; // 0 == never assigned / invalid.

    bool IsValid() const noexcept { return generation != 0; }

    friend bool operator==(const GpuResourceHandle& a, const GpuResourceHandle& b) noexcept
    {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const GpuResourceHandle& a, const GpuResourceHandle& b) noexcept
    {
        return !(a == b);
    }
};

inline constexpr GpuResourceHandle kInvalidGpuResourceHandle{};

} // namespace gte
