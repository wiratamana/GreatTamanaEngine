#pragma once
#include "../../Assets/SkeletonData.h"
#include "../../Physics/DynamicChainDefinition.h"
#include "../../Physics/DynamicChainDetection.h"

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
// PHASE3 note: real chain detection now lives in
// Physics/DynamicChainDetection.h (task_manager/verlet-integration-1's own
// PHASE4, later fully rewritten to traverse the real RigidBody/Joint graph by
// task_manager/verlet-integration-6 - see that campaign's
// PHASE0_MASTER_STRATEGY.md). Register()/TryGet()/TryGetMutable() themselves
// are unchanged by that rewrite - only ModelEntry's own shape gained a new
// `diagnostics` field (below) to carry DetectDynamicChains()'s diagnostic
// output alongside its chains.
class DynamicChainRigCache {
public:
    struct ModelEntry {
        std::vector<DynamicChainDefinition> chains;
        SkeletonData skeleton;
        // task_manager/verlet-integration-6, Phase 3/4 - carried straight from
        // DetectDynamicChains()'s own DynamicChainDetectionResult::diagnostics,
        // unmodified - PHASE5's Editor visualization reads
        // diagnostics.orphanedDynamicBoneIndices to render a non-simulated rigid
        // body distinctly (see this campaign's PHASE0_MASTER_STRATEGY.md).
        DynamicChainDetectionDiagnostics diagnostics;
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

    // Mutable counterpart of TryGet() above - the ONE sanctioned way to
    // live-edit a cached chain's DynamicJointSettings (damping/stiffness/
    // mass) from the Editor's Inspector "Dynamic Chain Physics" section
    // (Panels/InspectorPanel.cpp, PHASE4 3.5) - edits apply to every entity
    // spawned from that same model path (per-INSTANCE overrides are
    // explicitly out of scope for this phase - see PHASE4's own "What We
    // Will NOT Do").
    ModelEntry* TryGetMutable(const std::string& absoluteGtaPath)
    {
        const auto found = m_cache.find(absoluteGtaPath);
        return found != m_cache.end() ? &found->second : nullptr;
    }

private:
    std::unordered_map<std::string, ModelEntry> m_cache;
};

} // namespace gte
