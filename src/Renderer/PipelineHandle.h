#pragma once

#include <cstdint>

namespace gte {

// Cheap, POD, 8-byte generational identifier for a Pipeline owned by a
// ResourcePool<Pipeline, PipelineHandle> (see ResourcePool.h) - same shape
// and rationale as MeshHandle (see MeshHandle.h)/Entity (ECS/Entity.h)/
// GpuResourceHandle (Memory/GpuResourceHandle.h). See AGENTS.md
// ("Entity-Component-System").
//
// Deliberately a standalone header with ZERO dependency on Pipeline.h/
// Vulkan - see MeshHandle.h's comment for why.
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
