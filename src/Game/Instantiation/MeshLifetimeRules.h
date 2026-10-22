#pragma once

#include "../../ECS/Registry.h"
#include "../../Renderer/MeshHandle.h"

#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace gte {

constexpr std::uint64_t MeshHandleKey(MeshHandle handle) noexcept
{
    return (static_cast<std::uint64_t>(handle.generation) << 32) | handle.index;
}

using MeshHandleKeySet = std::unordered_set<std::uint64_t>;

// Keys of every valid mesh that a MeshRenderer in `registry` uses.
MeshHandleKeySet CollectMeshesInUse(Registry& registry);

struct MeshSweepPlan {
    std::vector<MeshHandle> release; // Owned meshes nothing references.
    bool stillPending = false;       // An unreferenced-by-cache mesh is still drawn by an entity.
};

// Owned meshes are kept when a cached part, an entity or a skinning binding references them.
// Meshes this system did not register are never in `owned`, so they are never released.
MeshSweepPlan PlanMeshSweep(std::span<const MeshHandle> owned, std::span<const MeshHandle> cachedParts,
    const MeshHandleKeySet& inUseByEntities, std::span<const MeshHandle> skinningBindings);

} // namespace gte
