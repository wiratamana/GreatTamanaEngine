#pragma once

#include <cstdint>

namespace gte {

// Generational id of a Mesh in a ResourcePool. Standalone (no Vulkan) so ECS components can name it.
struct MeshHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0; // 0 == never assigned / invalid.

    bool IsValid() const noexcept { return generation != 0; }

    friend bool operator==(const MeshHandle& a, const MeshHandle& b) noexcept
    {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const MeshHandle& a, const MeshHandle& b) noexcept
    {
        return !(a == b);
    }
};

inline constexpr MeshHandle kInvalidMeshHandle{};

} // namespace gte
