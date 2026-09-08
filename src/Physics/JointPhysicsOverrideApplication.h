#pragma once
#include "DynamicChainDefinition.h"
#include "../Assets/PhysicsData.h"

#include <vector>

namespace gte {

// task_manager/verlet-integration-11, PHASE2 - applies previously-SAVED,
// user-edited joint tuning (JointPhysicsOverride, Assets/PhysicsData.h,
// PHASE1) on TOP of a freshly-DetectDynamicChains()-produced chain list,
// mutating each matching joint's DynamicJointSettings::damping/stiffness/mass
// in place. Matched purely by SKELETON BONE INDEX (DynamicChainDefinition::
// jointBoneIndices entries) - see PHASE0_MASTER_STRATEGY.md, Step 2.4, for
// why bone index is the one stable identity available at this point, before
// any chain/joint-in-chain position has even been computed for this load.
//
// Pure/free function - no ECS, no Editor, no GPU, no file I/O - mirrors
// DynamicChainDefinition.h's own FindDynamicChainJointByBoneIndex() in spirit
// (see that function's own doc comment for the same "shared, tested,
// non-duplicated" rationale). The ONE production call site is
// PhysicsSystem::RegisterDynamicChains() (Game/Physics/PhysicsSystem.cpp),
// called once per model load, strictly AFTER DetectDynamicChains() and
// strictly BEFORE the result is registered into DynamicChainRigCache.
//
// Silently ignores (never asserts/crashes on) an override whose boneIndex
// does not match ANY joint of ANY chain in `chains` - a stale override left
// over from a model whose skeleton/detected chains have since changed is an
// entirely normal, expected input, not an error (mirrors
// DynamicChainJointLocation's own "no match is normal" contract). If more
// than one override in `overrides` names the SAME boneIndex (should never
// happen for a list built by SaveJointPhysicsOverridesToGtaFile(), PHASE4,
// but a hand-edited/corrupted file could still produce one), the LAST
// matching entry in `overrides` wins - documented explicitly here so a
// future caller/test never has to guess.
void ApplyJointPhysicsOverrides(
    std::vector<DynamicChainDefinition>& chains, const std::vector<JointPhysicsOverride>& overrides);

} // namespace gte
