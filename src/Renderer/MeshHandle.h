#pragma once

#include <cstdint>

namespace gte {

// Cheap, POD, 8-byte generational identifier for a Mesh owned by a
// ResourcePool<Mesh, MeshHandle> (see ResourcePool.h) - same index+
// generation shape and "never invented by calling code" rule as Entity
// (ECS/Entity.h) and GpuResourceHandle (Memory/GpuResourceHandle.h). See
// AGENTS.md ("Entity-Component-System") for the full rationale behind this
// shape.
//
// Deliberately a standalone header with ZERO dependency on Mesh.h/Vulkan -
// so a plain-data ECS component that merely references a mesh (see
// ECS/Components/MeshRenderer.h) stays just as dependency-light as Entity
// itself, never pulling in Vulkan headers just to name a handle type.
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
