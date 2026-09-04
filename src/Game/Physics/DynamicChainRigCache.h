#pragma once
#include "../../Assets/SkeletonData.h"
#include "../../Physics/DynamicChainDefinition.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

// PhysicsSystem's own per-model cache of detected dynamic bone chains -
// mirrors SkeletalRigCache.h's shape/convention exactly (a path-keyed
// unordered_map, "load/register once, look up every frame afterwards"), but
// owned entirely by PhysicsSystem, never by AnimationSystem (see
// task_manager/verlet-integration-1/PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md's
// own v3 Revision Notice for why this deliberate small duplication of a
// SkeletonData copy - once in AnimationSystem::m_rigCache, once here - is an
// accepted trade for genuine system independence).
//
// PHASE3 STUB: real chain detection (Physics/DynamicChainDetection.h) is
// PHASE4's job (see
// task_manager/verlet-integration-1/PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md).
// Register() is a real, callable method (so PhysicsSystem::RegisterDynamicChains()
// has a genuine call site to forward into today) but is intentionally never
// invoked with a non-empty ModelEntry::chains list yet - TryGet() therefore
// always returns nullptr for every model in THIS phase, which is what makes
// PhysicsSystem::Update() a PROVABLE no-op today: nothing has a populated
// DynamicChainRig to act on yet.
class DynamicChainRigCache {
public:
    struct ModelEntry {
        std::vector<DynamicChainDefinition> chains;
        SkeletonData skeleton;
    };

    void Register(const std::string& absoluteGtaPath, ModelEntry entry)
    {
        m_cache.insert_or_assign(absoluteGtaPath, std::move(entry));
    }

    const ModelEntry* TryGet(const std::string& absoluteGtaPath) const
    {
        const auto found = m_cache.find(absoluteGtaPath);
        return found != m_cache.end() ? &found->second : nullptr;
    }

private:
    std::unordered_map<std::string, ModelEntry> m_cache;
};

} // namespace gte
