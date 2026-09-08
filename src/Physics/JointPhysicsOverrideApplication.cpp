#include "JointPhysicsOverrideApplication.h"

#include <unordered_map>

namespace gte {

void ApplyJointPhysicsOverrides(
    std::vector<DynamicChainDefinition>& chains, const std::vector<JointPhysicsOverride>& overrides)
{
    if (overrides.empty()) {
        return; // Overwhelmingly common case (a model that has never been saved) - avoid building a map for nothing.
    }

    // "Last one wins" on a duplicate boneIndex (see this function's own
    // header comment) - a plain insert_or_assign-shaped loop, never a
    // conditional insert.
    std::unordered_map<std::int32_t, const JointPhysicsOverride*> byBoneIndex;
    byBoneIndex.reserve(overrides.size());
    for (const JointPhysicsOverride& o : overrides) {
        byBoneIndex[o.boneIndex] = &o;
    }

    for (DynamicChainDefinition& chain : chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            const auto found = byBoneIndex.find(chain.jointBoneIndices[i]);
            if (found == byBoneIndex.end()) {
                continue; // No saved override for this joint - keep whatever DetectDynamicChains() already computed.
            }
            const JointPhysicsOverride& o = *found->second;
            DynamicJointSettings& settings = chain.jointSettings[i];
            settings.damping = o.damping;
            settings.stiffness = o.stiffness;
            settings.mass = o.mass;
            // Deliberately NOT touched: group/collisionMask/collisionRadius
            // (PMX-shape-derived, never user-editable) and anything at the
            // DynamicChainDefinition level (collisionEnabled, gravityScale,
            // ...) - see PHASE0_MASTER_STRATEGY.md's "What We Will NOT Do".
        }
    }
}

} // namespace gte
