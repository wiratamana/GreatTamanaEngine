#include "PhysicsDebugReportBuilders.h"

#include "../Editor/PhysicsDebugImage.h"
#include "../Editor/RigidBodyWireframe.h"
#include "../Game/Physics/PhysicsSystem.h"
#include "../Math/MathTypes.h"
#include "../Physics/CollisionFilter.h"
#include "../Physics/JointCollisionFit.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

const ChainFrameSample* At(std::span<const ChainFrameSample* const> samples, std::size_t index) noexcept
{
    return index < samples.size() ? samples[index] : nullptr;
}

GroupMetricsReport MakeGroupReport(const std::string& key, std::int32_t chainIndex, std::uint32_t jointCount, const GroupSamples* samples)
{
    GroupMetricsReport report;
    report.key = key;
    report.chainIndex = chainIndex;
    report.jointCount = jointCount;
    if (samples != nullptr) {
        report.distFromAnimated = Summarize(samples->distFromAnimated);
        report.segDirErrDeg = Summarize(samples->segDirErrDeg);
        report.bendDeltaDeg = Summarize(samples->bendDeltaDeg);
        report.limitViolationDeg = Summarize(samples->limitViolationDeg);
        report.absStretch = Summarize(samples->absStretch);
        report.writebackErr = Summarize(samples->writebackErr);
        report.accelLocal = Summarize(samples->accelLocal);
    }
    return report;
}

void AddSegment(PhysicsDebugSnapshot& snapshot, const Vec3& a, const Vec3& b, PhysicsDebugColorClass colorClass)
{
    snapshot.segments.push_back({ ToFloat3(a), ToFloat3(b), colorClass });
}

void AddPoint(PhysicsDebugSnapshot& snapshot, const Vec3& p, PhysicsDebugColorClass colorClass)
{
    snapshot.points.push_back({ ToFloat3(p), colorClass });
}

} // namespace

Float3 ToFloat3(const Vec3& v) noexcept
{
    return { v.x, v.y, v.z };
}

Vec3 ToVec3(const Float3& v) noexcept
{
    return Vec3(v[0], v[1], v[2]);
}

std::string BoneNameOrEmpty(const SkeletonData& skeleton, std::int32_t boneIndex)
{
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= skeleton.bones.size()) {
        return {};
    }
    return skeleton.bones[static_cast<std::size_t>(boneIndex)].name;
}

std::vector<std::string> ChainBoneNames(const SkeletonData& skeleton, const DynamicChainDefinition& chain)
{
    std::vector<std::string> names;
    names.reserve(chain.jointBoneIndices.size());
    for (const std::int32_t boneIndex : chain.jointBoneIndices) {
        names.push_back(BoneNameOrEmpty(skeleton, boneIndex));
    }
    return names;
}

std::string ColliderLabel(const DynamicChainRigCache::ModelEntry& model, std::size_t colliderIndex)
{
    if (colliderIndex < model.colliderRigidBodyNames.size() && !model.colliderRigidBodyNames[colliderIndex].empty()) {
        return model.colliderRigidBodyNames[colliderIndex];
    }
    return "collider#" + std::to_string(colliderIndex);
}

BindPoseCollisionReport ComputeBindPoseCollision(const DynamicChainRigCache::ModelEntry& model, const GlobalPhysicsSettings& settings)
{
    BindPoseCollisionReport report;
    if (model.colliders.empty()) {
        return report;
    }
    const std::vector<Collider> colliders = ResolveEntityColliders(model, {}, Mat4::Identity());

    for (std::size_t c = 0; c < model.chains.size(); ++c) {
        const DynamicChainDefinition& chain = model.chains[c];
        const bool activeChain = chain.collisionEnabled && settings.collisionEnabled;
        const std::vector<std::string> names = ChainBoneNames(model.skeleton, chain);

        for (std::size_t i = 0; i < chain.jointBoneIndices.size() && i < chain.jointSettings.size(); ++i) {
            const std::int32_t boneIndex = chain.jointBoneIndices[i];
            if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= model.skeleton.bones.size()) {
                continue;
            }
            const Vec3 bindPosition = model.skeleton.bones[static_cast<std::size_t>(boneIndex)].position;
            const DynamicJointSettings& js = chain.jointSettings[i];
            const float activeRadius
                = (settings.useCollisionFit && js.collisionFitRadius >= 0.0f) ? js.collisionFitRadius : js.collisionRadius;

            bool rawHit = false;
            bool activeHit = false;
            for (std::size_t k = 0; k < colliders.size(); ++k) {
                const Collider& collider = colliders[k];
                if (!GroupsMayCollide(js.group, js.collisionMask, collider.group, collider.collisionMask)) {
                    continue;
                }
                const float rawPush = ProbeCollisionPush(bindPosition, js.collisionRadius, collider);
                if (rawPush > 0.0f) {
                    rawHit = true;
                    report.penetrations.push_back({ static_cast<std::uint32_t>(c), static_cast<std::uint32_t>(i), names[i],
                        ColliderLabel(model, k), rawPush });
                }
                if (activeChain && !activeHit) {
                    const bool ignored = settings.useCollisionFit
                        && std::binary_search(js.ignoredColliderIndices.begin(), js.ignoredColliderIndices.end(),
                            static_cast<std::uint16_t>(k));
                    if (!ignored && ProbeCollisionPush(bindPosition, activeRadius, collider) > 0.0f) {
                        activeHit = true;
                    }
                }
            }
            report.penetratingJoints += rawHit ? 1u : 0u;
            report.pushedJoints += activeHit ? 1u : 0u;
        }
    }
    return report;
}

PhysicsMetricsAccumulator::PhysicsMetricsAccumulator(const DynamicChainRigCache::ModelEntry& model, const StepOptions& options)
    : m_model(&model)
{
    m_names.reserve(model.chains.size());
    m_limits.reserve(model.chains.size());
    for (std::size_t c = 0; c < model.chains.size(); ++c) {
        const DynamicChainDefinition& chain = model.chains[c];
        m_names.push_back(ChainBoneNames(model.skeleton, chain));
        m_limits.push_back(ComputeEffectiveBendLimits(chain, options));

        for (std::size_t i = 0; i < m_names[c].size(); ++i) {
            GroupInfo& info = m_info[JointGroupKey(m_names[c][i])];
            info.chainIndex = (info.joints == 0 || info.chainIndex == static_cast<std::int32_t>(c)) ? static_cast<std::int32_t>(c) : -1;
            ++info.joints;
            ++m_totalJoints;
            const bool rootChild = i < chain.parentJointIndex.size() && chain.parentJointIndex[i] < 0;
            if (rootChild && i < m_limits[c].size() && IsWeldedLimit(m_limits[c][i])) {
                ++m_weldedJoints;
            }
        }
    }
}

void PhysicsMetricsAccumulator::AddFrame(std::span<const ChainFrameSample* const> cur,
    std::span<const ChainFrameSample* const> prev1, std::span<const ChainFrameSample* const> prev2, float renderHz)
{
    for (std::size_t c = 0; c < m_model->chains.size(); ++c) {
        const ChainFrameSample* sample = At(cur, c);
        if (sample == nullptr || sample->animated.size() != m_names[c].size()) {
            continue;
        }
        AccumulateChainFrame(m_model->chains[c], m_names[c], m_limits[c], *sample, At(prev1, c), At(prev2, c), renderHz, m_groups);

        m_pushedParticles += sample->collisionPushedParticles;
        m_rawPushedParticles += sample->collisionRawPushedParticles;
        m_pushDistanceSum += sample->collisionPushDistanceSum;
        m_bendClamped += sample->bendClampedJoints;
        m_reseeds += sample->reseedCount;
        m_nanHits += sample->nanGuardHits;
        if (c == 0) {
            ++m_stepsHistogram[std::min<std::size_t>(sample->stepsThisFrame, 3)];
        }
    }
    ++m_frames;
}

PhysicsMetricsReport PhysicsMetricsAccumulator::Finish(float renderHz) const
{
    PhysicsMetricsReport report;
    report.frames = m_frames;
    report.renderHz = renderHz;

    for (const auto& [key, info] : m_info) {
        const auto found = m_groups.find(key);
        report.groups.push_back(MakeGroupReport(key, info.chainIndex, info.joints, found != m_groups.end() ? &found->second : nullptr));
    }
    const auto all = m_groups.find("ALL");
    report.all = MakeGroupReport("ALL", -1, m_totalJoints, all != m_groups.end() ? &all->second : nullptr);
    const auto welded = m_groups.find("WELDED_ROOT_CHILDREN");
    report.weldedRootChildren
        = MakeGroupReport("WELDED_ROOT_CHILDREN", -1, m_weldedJoints, welded != m_groups.end() ? &welded->second : nullptr);

    const double frames = static_cast<double>(std::max<std::uint32_t>(m_frames, 1));
    report.collision.pushedParticlesPerFrame = static_cast<double>(m_pushedParticles) / frames;
    report.collision.rawPushedParticlesPerFrame = static_cast<double>(m_rawPushedParticles) / frames;
    report.collision.meanPushDistance = m_pushedParticles > 0 ? m_pushDistanceSum / static_cast<double>(m_pushedParticles) : 0.0;
    report.bendClampedJointsPerFrame = static_cast<double>(m_bendClamped) / frames;
    report.stepsHistogram = m_stepsHistogram;
    report.reseedCount = m_reseeds;
    report.nanCount = m_nanHits;
    return report;
}

PhysicsDebugSnapshot BuildPhysicsDebugSnapshot(const PhysicsDebugImageRequest& request,
    const DynamicChainRigCache::ModelEntry& model, std::span<const ChainFrameSample> frames,
    std::span<const Collider> worldColliders, const StepOptions& options)
{
    PhysicsDebugSnapshot snapshot;
    PhysicsDebugViewBasis basis;
    if (!TryGetPhysicsDebugViewBasis(request.view, basis)) {
        return snapshot;
    }
    snapshot.valid = true;
    snapshot.view = request.view;
    snapshot.width = request.width;
    snapshot.height = request.height;

    const bool drawSim = request.mode != "anim";
    const bool drawAnim = request.mode != "sim";
    const Vec3 forward = ToVec3(basis.forward);

    for (std::size_t c = 0; c < model.chains.size() && c < frames.size(); ++c) {
        const DynamicChainDefinition& chain = model.chains[c];
        const ChainFrameSample& frame = frames[c];
        const std::size_t joints = chain.jointBoneIndices.size();
        if (frame.animated.size() != joints || frame.display.size() != joints) {
            continue;
        }
        const std::vector<std::string> names = ChainBoneNames(model.skeleton, chain);
        const std::vector<float> limits
            = request.showLimits ? ComputeEffectiveBendLimits(chain, options) : std::vector<float>();

        AddPoint(snapshot, frame.rootAnimWorld, PhysicsDebugColorClass::Anchor);

        for (std::size_t i = 0; i < joints; ++i) {
            const std::int32_t parent = chain.parentJointIndex[i];
            const bool hasParent = parent >= 0 && static_cast<std::size_t>(parent) < joints;
            const Vec3 parentSim = hasParent ? frame.display[static_cast<std::size_t>(parent)] : frame.rootAnimWorld;
            const Vec3 parentAnim = hasParent ? frame.animated[static_cast<std::size_t>(parent)] : frame.rootAnimWorld;

            const bool highlighted = !request.highlight.empty()
                && (request.highlight == names[i] || request.highlight == JointGroupKey(names[i]));
            if (highlighted) {
                snapshot.highlightedBones.push_back(names[i]);
            }
            const PhysicsDebugColorClass simClass = highlighted ? PhysicsDebugColorClass::Highlight : PhysicsDebugColorClass::Sim;

            if (drawSim) {
                AddSegment(snapshot, parentSim, frame.display[i], simClass);
                if (request.showJoints) {
                    AddPoint(snapshot, frame.display[i], simClass);
                }
            }
            if (drawAnim) {
                AddSegment(snapshot, parentAnim, frame.animated[i], PhysicsDebugColorClass::Anim);
                if (request.showJoints) {
                    AddPoint(snapshot, frame.animated[i], PhysicsDebugColorClass::Anim);
                }
            }

            if (request.showLimits && i < limits.size() && limits[i] < kPi - 1e-4f) {
                const Vec3 animSegment = frame.animated[i] - parentAnim;
                if (Length(animSegment) >= 1e-6f) {
                    const Vec3 animDir = Normalize(animSegment);
                    const float rest = i < chain.restLengths.size() ? chain.restLengths[i] : 0.0f;
                    for (const float sign : { -1.0f, 1.0f }) {
                        const Vec3 dir = Quat::FromAxisAngle(forward, sign * limits[i]).RotateVector(animDir);
                        AddSegment(snapshot, parentSim, parentSim + dir * rest, PhysicsDebugColorClass::Limit);
                    }
                }
            }
        }
    }

    if (request.showColliders && worldColliders.size() == model.colliders.size()) {
        for (std::size_t k = 0; k < worldColliders.size(); ++k) {
            const Collider& collider = worldColliders[k];
            const std::vector<WireframeSegment> wire
                = BuildRigidBodyWireframe(model.colliders[k].shape, collider.size, Vec3::Zero(), Vec3::Zero());
            for (const WireframeSegment& segment : wire) {
                AddSegment(snapshot, collider.center + collider.rotation.RotateVector(segment.a),
                    collider.center + collider.rotation.RotateVector(segment.b), PhysicsDebugColorClass::Collider);
            }
        }
    }
    return snapshot;
}

} // namespace gte
