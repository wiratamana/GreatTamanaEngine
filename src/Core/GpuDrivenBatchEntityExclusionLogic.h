#pragma once

// Pure decision for GPU-driven batches: whether a batch's entities are excluded from the normal draw path.
// The caller resolves the pass toggle itself, so this stays testable without any renderer types.

#include "../ECS/Entity.h"

#include <unordered_set>
#include <vector>

namespace gte {

// Adds `entities` to `outExcluded` only when the batch's indirect-draw pass is enabled this frame.
// A disabled batch stays out of the set, so its entities fall back to the normal per-entity draw path.
inline void AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(
    bool indirectDrawPassEnabledThisFrame, const std::vector<Entity>& entities, std::unordered_set<Entity>& outExcluded)
{
    if (!indirectDrawPassEnabledThisFrame) {
        return;
    }
    for (const Entity& entity : entities) {
        outExcluded.insert(entity);
    }
}

} // namespace gte
