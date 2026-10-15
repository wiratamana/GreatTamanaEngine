#pragma once

#include <cstdint>
#include <functional>

namespace gte {

// Generational id of a Pipeline in a ResourcePool. Standalone (no Vulkan), like MeshHandle.
struct PipelineHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0; // 0 == never assigned / invalid.

    bool IsValid() const noexcept { return generation != 0; }

    friend bool operator==(const PipelineHandle& a, const PipelineHandle& b) noexcept
    {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const PipelineHandle& a, const PipelineHandle& b) noexcept
    {
        return !(a == b);
    }
};

inline constexpr PipelineHandle kInvalidPipelineHandle{};

} // namespace gte

// Lets PipelineHandle be used directly as an unordered_map/unordered_set
// key. Mirrors Entity's own specialization (see ECS/Entity.h).
namespace std {
template <>
struct hash<gte::PipelineHandle> {
    std::size_t operator()(const gte::PipelineHandle& h) const noexcept
    {
        return static_cast<std::size_t>((static_cast<std::uint64_t>(h.index) << 32) | h.generation);
    }
};
} // namespace std
