#pragma once

#include <cstdint>

namespace gte {

// Cheap, POD, 8-byte identifier for a GPU resource tracked by
// GpuMemoryTracker (see GpuMemoryTracker.h) - deliberately NOT a pointer or
// string. Handles are meant to be copied/stored/compared by the thousands
// with no real cost (unlike a std::string, which is large and unpredictable
// memory-wise), and are always generated automatically by the tracker -
// calling code never invents/assigns its own id. See AGENTS.md ("GPU
// resource memory tracking") for the full rationale.
//
// `index` is a direct slot index into GpuMemoryTracker's internal array (an
// O(1) lookup, not a hash). `generation` guards against a stale handle
// silently referring to a different resource that was later created in the
// same (reused) slot - this isn't a theoretical concern here, since e.g.
// RenderTexture::Resize() destroys and immediately recreates its
// allocation, which can and does reuse the just-freed slot.
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
