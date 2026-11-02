#include "DynamicChainDetection.h"

#include "JointBendLimit.h"
#include "RigidBodyJointGraph.h"
#include "TimeScaling.h"
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

// Chain detection walks the rigid body and joint graph and the real skeleton ancestry:
// A no physics -> empty. B graph reachability from Static bodies. C bone -> rigid body map (lowest index wins).
// D resolve each Dynamic bone to its anchor through real bone ancestors. E assemble one tree per anchor.
// F fold the remaining PMX joints into extra constraints. G short chains dropped, settings and bend limits seeded.
// H everything sorted for determinism.

namespace gte {

namespace {

// Real-ancestry resolution of one Dynamic or DynamicAndBoneMerge bone.
struct MemberResolution {
    bool orphaned = false;
    // Static anchor bone this member belongs to (valid when !orphaned).
    std::int32_t anchorBoneIndex = -1;
    // True when the tree parent is the anchor itself.
    bool parentIsRoot = false;
    // Tree parent bone (valid when !parentIsRoot && !orphaned).
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

// Collision radius of a joint from its rigid body shape: sphere and capsule radius, or the smallest box half-extent.
// Never larger than the body's thinnest half-thickness; a degenerate shape gives 0.
float DeriveJointCollisionRadius(RigidBodyShape shape, const Vec3& shapeSize) noexcept
{
    float radius = 0.0f;
    switch (shape) {
    case RigidBodyShape::Sphere:
        radius = shapeSize.x;
        break;
    case RigidBodyShape::Capsule:
        radius = shapeSize.x;
        break;
    case RigidBodyShape::Box:
        radius = std::min({ shapeSize.x, shapeSize.y, shapeSize.z });
        break;
    }
    return std::max(0.0f, radius);
}

// Resolved members of one anchor, still as real bone indices.
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

    // Step C, pass 1: graph-orphaned bodies go straight into the orphan list.
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

    // Step C, passes 2 and 3: eligibility map, then keep the lowest rigid body index per bone (drops are diagnosed).
    // Unattached bodies are excluded first so they never collide with each other.
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
            // Ascending order: the map already holds the lowest index, `i` is the one dropped.
            duplicateAssignmentsDropped.push_back(static_cast<std::int32_t>(i));
        }
    }

    // Step D: ascending bone index at the top level, for determinism.
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
                // Not resolved yet: keep climbing, this bone joins the same walked sequence.
                walkedMembers.push_back(parent);
                current = parent;
                continue;
            }
            // Pass-through - keep climbing without recording it as a member.
            current = parent;
        }

        if (cycleDetected) {
            // A parentBoneIndex cycle is a corrupt skeleton: degrade to an orphan, assert in debug.
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

    // Step F: fold PMX joints between two joints of one chain into extra constraints; drop cross-chain ones.
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

        // A ring brace. Keep only the first PMX joint of each joint pair.
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

    // Step G: drop short chains, seed per-joint settings from the matched rigid bodies, assign bend limits.
    std::vector<DynamicChainDefinition> finalChains;
    std::vector<std::vector<std::int32_t>> finalPmxJointIndices;
    std::vector<std::vector<std::int32_t>> finalMissingBones;
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
                chain.jointSettings[j].pmxLinearDamping = body.linearDamping;
                chain.jointSettings[j].damping = PmxDampingToStepFraction(body.linearDamping, defaults.pmxDampingRatePerSecond);
                chain.jointSettings[j].group = body.group;
                chain.jointSettings[j].collisionMask = body.collisionGroupMask;
                chain.jointSettings[j].collisionRadius = DeriveJointCollisionRadius(body.shape, body.shapeSize);
            }
        }

        std::vector<std::int32_t> pmxJointIndices = AssignBendLimits(chain, skeleton, *physics, boneIndexToRigidBodyIndex);
        pmxJointIndices.resize(chain.jointBoneIndices.size(), -1);
        std::vector<std::int32_t> missingBones;
        for (std::size_t j = 0; j < pmxJointIndices.size(); ++j) {
            if (pmxJointIndices[j] < 0) {
                missingBones.push_back(chain.jointBoneIndices[j]);
            }
        }

        finalChains.push_back(std::move(chain));
        finalPmxJointIndices.push_back(std::move(pmxJointIndices));
        finalMissingBones.push_back(std::move(missingBones));
    }

    // Step H: sort chains by anchor bone, then first joint. Sort every diagnostic list.
    std::vector<std::size_t> order(finalChains.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&finalChains](std::size_t ia, std::size_t ib) {
        const DynamicChainDefinition& a = finalChains[ia];
        const DynamicChainDefinition& b = finalChains[ib];
        if (a.rootBoneIndex != b.rootBoneIndex) {
            return a.rootBoneIndex < b.rootBoneIndex;
        }
        const std::int32_t aFirst = a.jointBoneIndices.empty() ? -1 : a.jointBoneIndices[0];
        const std::int32_t bFirst = b.jointBoneIndices.empty() ? -1 : b.jointBoneIndices[0];
        return aFirst < bFirst;
    });

    std::vector<std::int32_t> bendLimitMissingBoneIndices;
    result.chains.reserve(order.size());
    result.pmxJointIndexPerJoint.reserve(order.size());
    for (const std::size_t i : order) {
        result.chains.push_back(std::move(finalChains[i]));
        result.pmxJointIndexPerJoint.push_back(std::move(finalPmxJointIndices[i]));
        bendLimitMissingBoneIndices.insert(bendLimitMissingBoneIndices.end(), finalMissingBones[i].begin(), finalMissingBones[i].end());
    }

    const auto sortUnique = [](std::vector<std::int32_t>& values) {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };
    sortUnique(orphanedBoneIndices);
    sortUnique(crossChainJointsDropped);
    sortUnique(duplicateAssignmentsDropped);
    sortUnique(bendLimitMissingBoneIndices);

    result.diagnostics.orphanedDynamicBoneIndices = std::move(orphanedBoneIndices);
    result.diagnostics.crossChainJointsDropped = std::move(crossChainJointsDropped);
    result.diagnostics.duplicateBoneRigidBodyAssignmentsDropped = std::move(duplicateAssignmentsDropped);
    result.diagnostics.bendLimitMissingBoneIndices = std::move(bendLimitMissingBoneIndices);
    return result;
}

} // namespace gte
