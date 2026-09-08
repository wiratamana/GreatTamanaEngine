#include "DynamicChainDetection.h"

#include "RigidBodyJointGraph.h"
#include "../Math/MathTypes.h" // kEpsilon
#include "../Math/Vec3.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// task_manager/verlet-integration-6 - full rewrite. See DynamicChainDetection.h's
// own doc comment for the algorithm summary (Steps A-H); this comment gives
// the fully detailed write-up the header only summarizes.
//
// Step A: physics == nullptr -> empty result immediately.
// Step B: build the RigidBody/Joint graph (RigidBodyJointGraph.h) and compute
//   which Dynamic/DynamicAndBoneMerge bodies are graph-reachable from >=1
//   Static body.
// Step C: map bones to their own eligible RigidBody in three passes: (1) seed
//   the orphan diagnostic straight from every graph-orphaned Dynamic body
//   (before anything else is built - this is the ONLY place a TRUE
//   graph-orphan is ever recorded, fixing what would otherwise be a silent
//   "vanishes from the result entirely" bug), (2) build the eligibility map
//   (Static bodies, or reachable Dynamic bodies, each with a valid boneIndex
//   - an unattached body is never inserted), (3) apply the "keep the lowest
//   rigid-body index" tie-break for two bodies sharing one boneIndex, and
//   diagnose every dropped duplicate.
// Step D: for every reachable Dynamic bone (ascending bone index, for
//   determinism), walk its REAL skeleton ancestor chain - never the raw
//   joint graph - skipping over any non-participating "pass-through" bone,
//   until it reaches a Static anchor, an already-resolved (memoized) Dynamic
//   member (join that SAME chain), a dead end (no further ancestor, or an
//   out-of-range parent index - both terminal identically), or a
//   revisited bone (an ancestry cycle - orphan, plus a debug-only assert).
//   Every bone actually visited during a walk that ultimately fails to reach
//   an anchor is unioned into the same orphan list Step C already seeded.
// Step E: group every resolved (non-orphaned) bone by its own anchor and
//   assemble each anchor's own tree (parent-before-child, breadth-first,
//   children visited in ascending bone-index order at every level) into a
//   single DynamicChainDefinition - a spider-web skirt's several branches
//   and cross-braces all land in ONE chain, never one per branch.
// Step F: fold every remaining original PMX Joint into an
//   ExtraStructuralConstraint when both endpoints land in the SAME chain but
//   aren't already a tree edge (de-duplicating two Joints connecting the
//   exact same pair), or drop-and-diagnose it when its two endpoints land in
//   two DIFFERENT chains.
// Step G: discard any chain shorter than defaults.minimumChainLength (never
//   retroactively orphans its own bones - "too short" and "unreachable" stay
//   two distinct concepts), then seed jointSettings/maxPlausibleRootDelta/
//   head-collider defaults exactly like the old algorithm did.
// Step H: sort the final chain list (ascending rootBoneIndex, tie-broken by
//   jointBoneIndices[0]) and every diagnostic list, so two calls against
//   logically-identical-but-differently-ordered PhysicsData always return
//   the same result, not just the same content.

namespace gte {

namespace {

// One real-ancestry resolution result for a single participating (Dynamic/
// DynamicAndBoneMerge) bone (Step D).
struct MemberResolution {
    bool orphaned = false;
    // Real bone index of the Static anchor this member ultimately belongs
    // to - only meaningful when !orphaned.
    std::int32_t anchorBoneIndex = -1;
    // true => this member's tree-parent IS the anchor directly
    // (parentJointIndex == -1 once assembled in Step E).
    bool parentIsRoot = false;
    // Valid only when !parentIsRoot && !orphaned - another member's own real
    // bone index (this member's tree-parent).
    std::int32_t parentMemberBoneIndex = -1;
};

enum class BoneRole { PassThrough, Anchor, Member };

BoneRole RoleOf(const std::unordered_map<std::int32_t, std::int32_t>& boneIndexToRigidBodyIndex,
    const PhysicsData& physics, std::int32_t boneIndex, std::int32_t& outRigidBodyIndex)
{
    const auto it = boneIndexToRigidBodyIndex.find(boneIndex);
    if (it == boneIndexToRigidBodyIndex.end()) {
        return BoneRole::PassThrough;
    }
    outRigidBodyIndex = it->second;
    const RigidBodyMotionType motionType = physics.rigidBodies[static_cast<std::size_t>(it->second)].motionType;
    return motionType == RigidBodyMotionType::Static ? BoneRole::Anchor : BoneRole::Member;
}

// One anchor-rooted group of resolved members, still using REAL bone indices
// (converted into jointBoneIndices positions only once the final traversal
// order is known - Step E).
struct ChainGroup {
    std::vector<std::int32_t> rootChildren; // Real bone indices, direct (parentIsRoot) children of the anchor.
    std::unordered_map<std::int32_t, std::vector<std::int32_t>> childrenByMemberBone; // Real bone -> real bone children.
};

} // namespace

DynamicChainDetectionResult DetectDynamicChains(
    const SkeletonData& skeleton, const PhysicsData* physics, const DynamicChainDetectionDefaults& defaults)
{
    DynamicChainDetectionResult result;

    // Step A.
    if (physics == nullptr) {
        return result;
    }

    const std::size_t boneCount = skeleton.bones.size();

    // Step B.
    const RigidBodyJointGraph graph = RigidBodyJointGraph::Build(*physics);
    const ReachabilityResult reach = ComputeReachabilityFromStaticAnchors(*physics, graph);

    std::vector<std::int32_t> orphanedBoneIndices;

    // Step C, pass 1 (fixes the "true graph-orphan silently vanishes"
    // bug) - seed the orphan list directly from Phase 2's own output BEFORE
    // the eligibility map is even built.
    for (std::int32_t rigidBodyIndex : reach.orphanedDynamicRigidBodyIndices) {
        const RigidBody& body = physics->rigidBodies[static_cast<std::size_t>(rigidBodyIndex)];
        if (body.boneIndex >= 0 && static_cast<std::size_t>(body.boneIndex) < boneCount) {
            orphanedBoneIndices.push_back(body.boneIndex);
        }
    }

    std::vector<bool> isReachableDynamic(physics->rigidBodies.size(), false);
    for (std::int32_t idx : reach.reachableDynamicRigidBodyIndices) {
        isReachableDynamic[static_cast<std::size_t>(idx)] = true;
    }

    // Step C, pass 2 (eligibility map) + pass 3 (duplicate tie-break,
    // diagnosed). Unattached (boneIndex < 0, or out of range) bodies are
    // excluded BEFORE the tie-break runs, so two entirely unrelated
    // unattached bodies can never spuriously "collide."
    std::unordered_map<std::int32_t, std::int32_t> boneIndexToRigidBodyIndex;
    std::vector<std::int32_t> duplicateAssignmentsDropped;
    for (std::size_t i = 0; i < physics->rigidBodies.size(); ++i) {
        const RigidBody& body = physics->rigidBodies[i];
        if (body.boneIndex < 0 || static_cast<std::size_t>(body.boneIndex) >= boneCount) {
            continue;
        }
        const bool eligible = (body.motionType == RigidBodyMotionType::Static) || isReachableDynamic[i];
        if (!eligible) {
            continue;
        }
        const auto insertion = boneIndexToRigidBodyIndex.emplace(body.boneIndex, static_cast<std::int32_t>(i));
        if (!insertion.second) {
            // Ascending iteration order guarantees the map already holds the
            // LOWEST rigid-body index for this boneIndex - `i` is the
            // (higher-index) one being dropped.
            duplicateAssignmentsDropped.push_back(static_cast<std::int32_t>(i));
        }
    }

    // Step D - ascending bone index at the top level, for determinism.
    std::vector<std::int32_t> participatingBones;
    for (const auto& entry : boneIndexToRigidBodyIndex) {
        const RigidBody& body = physics->rigidBodies[static_cast<std::size_t>(entry.second)];
        if (body.motionType != RigidBodyMotionType::Static) {
            participatingBones.push_back(entry.first);
        }
    }
    std::sort(participatingBones.begin(), participatingBones.end());

    std::unordered_map<std::int32_t, MemberResolution> memberResolutions;

    for (std::int32_t startBone : participatingBones) {
        if (memberResolutions.find(startBone) != memberResolutions.end()) {
            continue; // Already resolved via an earlier bone's own walk.
        }

        std::vector<std::int32_t> walkedMembers;
        walkedMembers.push_back(startBone);
        std::unordered_set<std::int32_t> visitedRealBones;
        visitedRealBones.insert(startBone);

        enum class OutcomeKind { Anchor, ExistingMember, Orphan };
        OutcomeKind outcomeKind = OutcomeKind::Orphan;
        std::int32_t outcomeAnchorBoneIndex = -1;
        std::int32_t outcomeExistingMemberBoneIndex = -1;
        bool cycleDetected = false;

        std::int32_t current = startBone;
        while (true) {
            const std::int32_t parent = skeleton.bones[static_cast<std::size_t>(current)].parentBoneIndex;
            if (parent < 0 || static_cast<std::size_t>(parent) >= boneCount) {
                outcomeKind = OutcomeKind::Orphan; // Case 3 - no valid further ancestor.
                break;
            }
            if (visitedRealBones.find(parent) != visitedRealBones.end()) {
                outcomeKind = OutcomeKind::Orphan; // Case 4 - ancestry cycle.
                cycleDetected = true;
                break;
            }
            visitedRealBones.insert(parent);

            std::int32_t rigidBodyIndex = -1;
            const BoneRole role = RoleOf(boneIndexToRigidBodyIndex, *physics, parent, rigidBodyIndex);
            if (role == BoneRole::Anchor) {
                outcomeKind = OutcomeKind::Anchor; // Case 1.
                outcomeAnchorBoneIndex = parent;
                break;
            }
            if (role == BoneRole::Member) {
                const auto memoIt = memberResolutions.find(parent);
                if (memoIt != memberResolutions.end()) {
                    if (memoIt->second.orphaned) {
                        outcomeKind = OutcomeKind::Orphan;
                    } else {
                        outcomeKind = OutcomeKind::ExistingMember; // Case 2.
                        outcomeExistingMemberBoneIndex = parent;
                    }
                    break;
                }
                // Not yet resolved by an earlier top-level iteration - keep
                // climbing; this bone joins the SAME walked sequence (its
                // own resolution is a pure function of the ancestor chain
                // beyond it, identical whether resolved from here or from
                // its own would-be top-level iteration later).
                walkedMembers.push_back(parent);
                current = parent;
                continue;
            }
            // Pass-through - keep climbing without recording it as a member.
            current = parent;
        }

        if (cycleDetected) {
            // task_manager/verlet-integration-6, Phase 3 - a malformed/
            // corrupted skeleton (a parentBoneIndex cycle) should never
            // happen in practice; fail loudly in a debug build rather than
            // merely degrading to an orphan silently (see
            // DynamicChainSolver.cpp's own NaN-guard assert for the
            // established precedent of pairing an unconditional graceful
            // degrade with a debug-only diagnostic assert).
            assert(false && "DetectDynamicChains(): cyclic skeleton ancestry detected (parentBoneIndex cycle)");
        }

        if (outcomeKind == OutcomeKind::Orphan) {
            for (std::int32_t b : walkedMembers) {
                MemberResolution orphanRes;
                orphanRes.orphaned = true;
                memberResolutions[b] = orphanRes;
                orphanedBoneIndices.push_back(b);
            }
        } else {
            const std::int32_t anchorBoneIndex = (outcomeKind == OutcomeKind::Anchor)
                ? outcomeAnchorBoneIndex
                : memberResolutions[outcomeExistingMemberBoneIndex].anchorBoneIndex;
            for (std::size_t i = 0; i < walkedMembers.size(); ++i) {
                MemberResolution res;
                res.orphaned = false;
                res.anchorBoneIndex = anchorBoneIndex;
                if (i + 1 < walkedMembers.size()) {
                    res.parentIsRoot = false;
                    res.parentMemberBoneIndex = walkedMembers[i + 1];
                } else if (outcomeKind == OutcomeKind::Anchor) {
                    res.parentIsRoot = true;
                } else {
                    res.parentIsRoot = false;
                    res.parentMemberBoneIndex = outcomeExistingMemberBoneIndex;
                }
                memberResolutions[walkedMembers[i]] = res;
            }
        }
    }

    // Step E - group every resolved (non-orphaned) bone by its own anchor.
    std::unordered_map<std::int32_t, ChainGroup> groups;
    for (const auto& entry : memberResolutions) {
        const std::int32_t boneIndex = entry.first;
        const MemberResolution& res = entry.second;
        if (res.orphaned) {
            continue;
        }
        ChainGroup& group = groups[res.anchorBoneIndex];
        if (res.parentIsRoot) {
            group.rootChildren.push_back(boneIndex);
        } else {
            group.childrenByMemberBone[res.parentMemberBoneIndex].push_back(boneIndex);
        }
    }

    std::vector<std::int32_t> anchorBoneIndicesSorted;
    anchorBoneIndicesSorted.reserve(groups.size());
    for (const auto& entry : groups) {
        anchorBoneIndicesSorted.push_back(entry.first);
    }
    std::sort(anchorBoneIndicesSorted.begin(), anchorBoneIndicesSorted.end());

    std::vector<DynamicChainDefinition> preliminaryChains;
    // Real bone index -> (index within preliminaryChains, position within that chain's own jointBoneIndices).
    std::unordered_map<std::int32_t, std::pair<std::size_t, std::int32_t>> boneToChainJoint;

    for (std::int32_t anchorBoneIndex : anchorBoneIndicesSorted) {
        ChainGroup& group = groups[anchorBoneIndex];
        std::sort(group.rootChildren.begin(), group.rootChildren.end());
        for (auto& kv : group.childrenByMemberBone) {
            std::sort(kv.second.begin(), kv.second.end());
        }

        DynamicChainDefinition chain;
        chain.rootBoneIndex = anchorBoneIndex;

        std::unordered_map<std::int32_t, std::int32_t> positions; // Real bone -> position within THIS chain's jointBoneIndices.
        std::queue<std::int32_t> pending;
        for (std::int32_t child : group.rootChildren) {
            pending.push(child);
        }
        while (!pending.empty()) {
            const std::int32_t bone = pending.front();
            pending.pop();

            const MemberResolution& res = memberResolutions[bone];
            const std::int32_t position = static_cast<std::int32_t>(chain.jointBoneIndices.size());
            chain.jointBoneIndices.push_back(bone);
            positions[bone] = position;

            std::int32_t parentJoint = -1;
            if (!res.parentIsRoot) {
                const auto parentPosIt = positions.find(res.parentMemberBoneIndex);
                // Guaranteed present - BFS visits a parent strictly before any of its own children.
                parentJoint = (parentPosIt != positions.end()) ? parentPosIt->second : -1;
            }
            chain.parentJointIndex.push_back(parentJoint);

            const auto childrenIt = group.childrenByMemberBone.find(bone);
            if (childrenIt != group.childrenByMemberBone.end()) {
                for (std::int32_t child : childrenIt->second) {
                    pending.push(child);
                }
            }
        }

        chain.restLengths.resize(chain.jointBoneIndices.size());
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            const Vec3 jointPos = skeleton.bones[static_cast<std::size_t>(chain.jointBoneIndices[i])].position;
            const std::int32_t parentJoint = chain.parentJointIndex[i];
            const Vec3 parentPos = (parentJoint < 0)
                ? skeleton.bones[static_cast<std::size_t>(anchorBoneIndex)].position
                : skeleton.bones[static_cast<std::size_t>(chain.jointBoneIndices[static_cast<std::size_t>(parentJoint)])].position;
            chain.restLengths[i] = Length(jointPos - parentPos);
        }

        const std::size_t chainIndex = preliminaryChains.size();
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            boneToChainJoint[chain.jointBoneIndices[i]] = { chainIndex, static_cast<std::int32_t>(i) };
        }
        preliminaryChains.push_back(std::move(chain));
    }

    // Step F - fold every remaining original PMX Joint into extraConstraints
    // (or drop it as cross-chain, diagnosed), ascending joint index.
    std::vector<std::int32_t> crossChainJointsDropped;
    std::vector<std::unordered_set<std::uint64_t>> addedExtraPairsPerChain(preliminaryChains.size());

    const auto resolveBoneIndex = [&](std::int32_t rigidBodyIndex) -> std::int32_t {
        if (rigidBodyIndex < 0 || static_cast<std::size_t>(rigidBodyIndex) >= physics->rigidBodies.size()) {
            return -1;
        }
        return physics->rigidBodies[static_cast<std::size_t>(rigidBodyIndex)].boneIndex;
    };

    for (std::size_t jointIdx = 0; jointIdx < physics->joints.size(); ++jointIdx) {
        const Joint& joint = physics->joints[jointIdx];
        const std::int32_t boneA = resolveBoneIndex(joint.rigidBodyAIndex);
        const std::int32_t boneB = resolveBoneIndex(joint.rigidBodyBIndex);
        if (boneA < 0 || boneB < 0) {
            continue; // Case 1 - doesn't resolve to a chain member at all.
        }
        const auto itA = boneToChainJoint.find(boneA);
        const auto itB = boneToChainJoint.find(boneB);
        if (itA == boneToChainJoint.end() || itB == boneToChainJoint.end()) {
            continue; // Case 1 - one (or both) endpoints is an orphan/anchor/unresolved bone.
        }
        if (itA->second.first != itB->second.first) {
            crossChainJointsDropped.push_back(static_cast<std::int32_t>(jointIdx)); // Case 4.
            continue;
        }

        const std::size_t chainIndex = itA->second.first;
        const std::int32_t jointIndexA = itA->second.second;
        const std::int32_t jointIndexB = itB->second.second;
        if (jointIndexA == jointIndexB) {
            continue; // Degenerate self-referential joint - nothing meaningful to add.
        }

        DynamicChainDefinition& chain = preliminaryChains[chainIndex];
        const bool isTreeEdge
            = (static_cast<std::size_t>(jointIndexA) < chain.parentJointIndex.size() && chain.parentJointIndex[static_cast<std::size_t>(jointIndexA)] == jointIndexB)
            || (static_cast<std::size_t>(jointIndexB) < chain.parentJointIndex.size() && chain.parentJointIndex[static_cast<std::size_t>(jointIndexB)] == jointIndexA);
        if (isTreeEdge) {
            continue; // Case 2 - already represented as the implicit tree edge.
        }

        // Case 3 - a genuine "ring brace"/cross joint. De-duplicate two
        // different PMX Joints connecting the exact same pair of joints,
        // keeping only the first encountered (ascending jointIndex order).
        const std::int32_t lo = jointIndexA < jointIndexB ? jointIndexA : jointIndexB;
        const std::int32_t hi = jointIndexA < jointIndexB ? jointIndexB : jointIndexA;
        const std::uint64_t pairKey = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lo)) << 32) | static_cast<std::uint32_t>(hi);
        if (!addedExtraPairsPerChain[chainIndex].insert(pairKey).second) {
            continue;
        }

        ExtraStructuralConstraint extra;
        extra.jointIndexA = jointIndexA;
        extra.jointIndexB = jointIndexB;
        extra.restLength = Length(skeleton.bones[static_cast<std::size_t>(boneA)].position
            - skeleton.bones[static_cast<std::size_t>(boneB)].position);
        chain.extraConstraints.push_back(extra);
    }

    // Step G - discard chains shorter than defaults.minimumChainLength, then
    // seed jointSettings/maxPlausibleRootDelta defaults - collision detection
    // is now handled entirely separately by DetectModelColliders()
    // (task_manager/verlet-integration-9, PHASE3), not by this function at
    // all. A discarded chain's own bones are NOT retroactively moved into
    // orphanedBoneIndices ("too short" and "unreachable" remain distinct).
    std::vector<DynamicChainDefinition> finalChains;
    for (DynamicChainDefinition& chain : preliminaryChains) {
        if (chain.jointBoneIndices.size() < defaults.minimumChainLength) {
            continue;
        }

        chain.gravityScale = defaults.defaultGravityScale;
        chain.windScale = defaults.defaultWindScale;
        chain.constraintIterations = defaults.defaultConstraintIterations;
        chain.jointSettings.assign(chain.jointBoneIndices.size(), defaults.defaultJointSettings);

        float sumRestLengths = 0.0f;
        for (float len : chain.restLengths) {
            sumRestLengths += len;
        }
        if (sumRestLengths > kEpsilon) {
            chain.maxPlausibleRootDelta = sumRestLengths * 5.0f;
        }

        for (std::size_t j = 0; j < chain.jointBoneIndices.size(); ++j) {
            const auto rbIt = boneIndexToRigidBodyIndex.find(chain.jointBoneIndices[j]);
            if (rbIt != boneIndexToRigidBodyIndex.end()) {
                const RigidBody& body = physics->rigidBodies[static_cast<std::size_t>(rbIt->second)];
                chain.jointSettings[j].mass = body.mass;
                chain.jointSettings[j].damping = body.linearDamping;
            }
        }

        finalChains.push_back(std::move(chain));
    }

    // Step H - full determinism: sort the chain list and every diagnostic list.
    std::sort(finalChains.begin(), finalChains.end(), [](const DynamicChainDefinition& a, const DynamicChainDefinition& b) {
        if (a.rootBoneIndex != b.rootBoneIndex) {
            return a.rootBoneIndex < b.rootBoneIndex;
        }
        const std::int32_t aFirst = a.jointBoneIndices.empty() ? -1 : a.jointBoneIndices[0];
        const std::int32_t bFirst = b.jointBoneIndices.empty() ? -1 : b.jointBoneIndices[0];
        return aFirst < bFirst;
    });

    std::sort(orphanedBoneIndices.begin(), orphanedBoneIndices.end());
    orphanedBoneIndices.erase(std::unique(orphanedBoneIndices.begin(), orphanedBoneIndices.end()), orphanedBoneIndices.end());
    std::sort(crossChainJointsDropped.begin(), crossChainJointsDropped.end());
    crossChainJointsDropped.erase(std::unique(crossChainJointsDropped.begin(), crossChainJointsDropped.end()), crossChainJointsDropped.end());
    std::sort(duplicateAssignmentsDropped.begin(), duplicateAssignmentsDropped.end());
    duplicateAssignmentsDropped.erase(
        std::unique(duplicateAssignmentsDropped.begin(), duplicateAssignmentsDropped.end()), duplicateAssignmentsDropped.end());

    result.chains = std::move(finalChains);
    result.diagnostics.orphanedDynamicBoneIndices = std::move(orphanedBoneIndices);
    result.diagnostics.crossChainJointsDropped = std::move(crossChainJointsDropped);
    result.diagnostics.duplicateBoneRigidBodyAssignmentsDropped = std::move(duplicateAssignmentsDropped);
    return result;
}

} // namespace gte
