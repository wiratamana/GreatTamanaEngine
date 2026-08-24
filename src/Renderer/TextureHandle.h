#pragma once

#include <cstdint>

namespace gte {

// Cheap, POD, 8-byte generational identifier for a MaterialTexture (see
// MaterialTexture.h) owned by a ResourcePool<MaterialTexture, TextureHandle>
// (see ResourcePool.h) - same index+generation shape and "never invented by
// calling code" rule as MeshHandle/PipelineHandle (see MeshHandle.h/
// PipelineHandle.h), Entity (ECS/Entity.h), and GpuResourceHandle
// (Memory/GpuResourceHandle.h). See AGENTS.md ("Entity-Component-System")
// for the full rationale behind this shape.
//
// Deliberately a standalone header with ZERO dependency on MaterialTexture.h/
// Vulkan - so a plain-data ECS component that merely references a texture
// (see ECS/Components/MeshRenderer.h) stays just as dependency-light as
// Entity itself, never pulling in Vulkan headers just to name a handle type.
struct TextureHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0; // 0 == never assigned / invalid.

    bool IsValid() const noexcept { return generation != 0; }

    friend bool operator==(const TextureHandle& a, const TextureHandle& b) noexcept
    {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const TextureHandle& a, const TextureHandle& b) noexcept
    {
        return !(a == b);
    }
};

inline constexpr TextureHandle kInvalidTextureHandle{};

} // namespace gte
