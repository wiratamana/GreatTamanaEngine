#include "MeshLifetimeRules.h"

#include "../../ECS/Components/MeshRenderer.h"

namespace gte {

MeshHandleKeySet CollectMeshesInUse(Registry& registry)
{
    MeshHandleKeySet inUse;
    ComponentStorage<MeshRenderer>& renderers = registry.Storage<MeshRenderer>();
    for (std::size_t i = 0; i < renderers.Size(); ++i) {
        const MeshHandle handle = renderers.ComponentAt(i).mesh;
        if (handle.IsValid()) {
            inUse.insert(MeshHandleKey(handle));
        }
    }
    return inUse;
}

MeshSweepPlan PlanMeshSweep(std::span<const MeshHandle> owned, std::span<const MeshHandle> cachedParts,
    const MeshHandleKeySet& inUseByEntities, std::span<const MeshHandle> skinningBindings)
{
    MeshHandleKeySet pinned;
    for (const MeshHandle handle : cachedParts) {
        pinned.insert(MeshHandleKey(handle));
    }
    for (const MeshHandle handle : skinningBindings) {
        pinned.insert(MeshHandleKey(handle));
    }

    MeshSweepPlan plan;
    for (const MeshHandle handle : owned) {
        const std::uint64_t key = MeshHandleKey(handle);
        if (pinned.contains(key)) {
            continue;
        }
        if (inUseByEntities.contains(key)) {
            plan.stillPending = true;
            continue;
        }
        plan.release.push_back(handle);
    }
    return plan;
}

} // namespace gte
