#pragma once
#include "../Game/Physics/DynamicChainRigCache.h"
#include "../Math/Vec3.h"
#include "../Physics/Collider.h"
#include "../Physics/DynamicChainSolver.h"
#include "../Physics/GlobalPhysicsSettings.h"
#include "../Physics/PhysicsDebugReport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace gte {

Float3 ToFloat3(const Vec3& v) noexcept;
Vec3 ToVec3(const Float3& v) noexcept;

// Bone name per joint of the chain ("" for an out of range bone index).
std::vector<std::string> ChainBoneNames(const SkeletonData& skeleton, const DynamicChainDefinition& chain);

// Bone name, or "" for an out of range index.
std::string BoneNameOrEmpty(const SkeletonData& skeleton, std::int32_t boneIndex);

// Rigid body name of the k-th model collider, or "collider#<k>" when it has none.
std::string ColliderLabel(const DynamicChainRigCache::ModelEntry& model, std::size_t colliderIndex);

// Bind-pose collision probe of every chain joint against every model collider.
struct BindPoseCollisionReport {
    std::vector<PhysicsBindPosePenetration> penetrations; // raw radius, no ignore list.
    std::uint32_t penetratingJoints = 0;                  // distinct joints in `penetrations`.
    std::uint32_t pushedJoints = 0;                       // joints pushed under the active rules.
};
BindPoseCollisionReport ComputeBindPoseCollision(const DynamicChainRigCache::ModelEntry& model, const GlobalPhysicsSettings& settings);

// Collects metric samples over render frames and builds the report.
class PhysicsMetricsAccumulator {
public:
    PhysicsMetricsAccumulator(const DynamicChainRigCache::ModelEntry& model, const StepOptions& options);

    // One render frame of the whole entity. The spans are index-aligned with the chains.
    // prev1 and prev2 may be empty or hold null pointers (accel_local is then skipped).
    void AddFrame(std::span<const ChainFrameSample* const> cur, std::span<const ChainFrameSample* const> prev1,
        std::span<const ChainFrameSample* const> prev2, float renderHz);

    PhysicsMetricsReport Finish(float renderHz) const;

private:
    struct GroupInfo {
        std::int32_t chainIndex = -1;
        std::uint32_t joints = 0;
    };

    const DynamicChainRigCache::ModelEntry* m_model;
    std::vector<std::vector<std::string>> m_names;
    std::vector<std::vector<float>> m_limits;
    std::map<std::string, GroupSamples> m_groups;
    std::map<std::string, GroupInfo> m_info;
    std::uint32_t m_totalJoints = 0;
    std::uint32_t m_weldedJoints = 0;

    std::uint32_t m_frames = 0;
    std::uint64_t m_pushedParticles = 0;
    double m_pushDistanceSum = 0.0;
    std::uint64_t m_bendClamped = 0;
    std::uint32_t m_reseeds = 0;
    std::uint32_t m_nanHits = 0;
    std::array<std::uint32_t, 4> m_stepsHistogram{};
};

// Builds the drawing data of one entity from one frame sample per chain.
// `worldColliders` is index-aligned with model.colliders. The view and mode names must be valid.
PhysicsDebugSnapshot BuildPhysicsDebugSnapshot(const PhysicsDebugImageRequest& request,
    const DynamicChainRigCache::ModelEntry& model, std::span<const ChainFrameSample> frames,
    std::span<const Collider> worldColliders, const StepOptions& options);

} // namespace gte
