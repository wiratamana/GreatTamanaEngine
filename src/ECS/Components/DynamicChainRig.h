#pragma once
#include "../../Physics/DynamicChainRuntimeState.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Attached to a model's ROOT entity (same entity SkeletalAnimator lives on -
// see MeshAssetSource.h/SkeletalAnimator.h) whenever
// PhysicsSystem::RegisterDynamicChains()/AttachDynamicChainRigIfNeeded() (see
// PHASE4) detects at least one physics-driven bone chain in that model's
// SkeletonData. Follows SkeletalAnimator.h's own precedent (plain data, a
// path-string-keyed link back into a system-owned cache, no live pointers
// stored in the component itself) - with one deliberate difference: this
// component carries its OWN `meshGtaPath`, rather than PhysicsSystem reaching
// into a SkeletalAnimator component to read ITS meshGtaPath. This is what
// lets PhysicsSystem process an entity knowing only "this entity has a
// DynamicChainRig and (hopefully) a ResolvedAnimationPose," with no
// assumption that a SkeletalAnimator component (an Animation/AnimationSystem-
// owned shape) even exists on it. Zero cross-system component reach-through
// is a hard rule for this component, not a style preference.
struct DynamicChainRig {
    // Self-contained lookup key back into PhysicsSystem's OWN
    // DynamicChainRigCache (Game/Physics/DynamicChainRigCache.h, see PHASE4) -
    // set once, at registration time, mirroring SkeletalAnimator::
    // meshGtaPath's own "component stores a stable string key, never a live
    // pointer" convention. A deliberate, small, per-entity duplication of a
    // string SkeletalAnimator's own component also happens to carry -
    // accepted in exchange for PhysicsSystem never needing to know
    // SkeletalAnimator's shape exists.
    std::string meshGtaPath;

    // Index-aligned 1:1 with whatever DynamicChainRigCache::TryGet(meshGtaPath)
    // returns (see PHASE4) - one runtime state per chain in that model.
    std::vector<DynamicChainRuntimeState> chainStates;

    // Fixed-timestep accumulator (Culprit D) - carries "leftover" simulation
    // time across frames so the simulation steps at a constant rate
    // regardless of the real, variable render frame rate. See
    // Physics/FixedTimestepAccumulator.h for the stepping math this drives.
    float accumulatedSeconds = 0.0f;

    bool enabled = true; // lets PHASE4's Inspector toggle disable physics per-instance without removing the component.
};

} // namespace gte
