#include "PhysicsDebugCommandDispatch.h"

#include "PhysicsDebugReportBuilders.h"
#include "../ECS/Components/DynamicChainRig.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/ResolvedAnimationPose.h"
#include "../ECS/Components/SkeletalAnimator.h"
#include "../ECS/EntityQuery.h"
#include "../Editor/PhysicsDebugImage.h"
#include "../Game/Game.h"
#include "../Game/Physics/PhysicsDebugRecorder.h"
#include "../Game/Physics/PhysicsDebugSettings.h"
#include "../Game/Physics/PhysicsSystem.h"
#include "../Math/MathTypes.h"
#include "../Physics/CollisionFilter.h"
#include "../Profiling/ScopeTimer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gte {

namespace {

constexpr float kMaxFrameValue = 100000.0f;
constexpr std::uint32_t kMaxRunFrames = 3600;
constexpr std::uint64_t kMaxRunJointFrames = 2000000;
constexpr std::uint32_t kMaxJointRows = 2000;
constexpr std::uint32_t kMaxCaptureWindow = 600;
constexpr std::uint32_t kProbeWindow = 4;

PhysicsDebugCommandResult MakeResult(PhysicsDebugCommandKind kind)
{
    PhysicsDebugCommandResult result;
    result.kind = kind;
    return result;
}

void Fail(PhysicsDebugCommandResult& result, int status, std::string message, std::string key = {})
{
    result.success = false;
    result.httpStatus = status;
    result.errorMessage = std::move(message);
    result.errorKey = std::move(key);
}

void Succeed(PhysicsDebugCommandResult& result) noexcept
{
    result.success = true;
    result.httpStatus = 200;
}

// The entity a command works on, with its rig and cached model.
struct Target {
    Entity entity = kInvalidEntity;
    DynamicChainRig* rig = nullptr;
    const DynamicChainRigCache::ModelEntry* model = nullptr;
};

bool ResolveTarget(Game& game, const std::string& name, Target& target, PhysicsDebugCommandResult& result)
{
    Registry& registry = game.GetRegistry();
    const Entity entity = FindEntityByName(registry, name);
    if (!entity.IsValid()) {
        Fail(result, 404, "unknown entity '" + name + "'", "entity");
        return false;
    }
    DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity);
    const DynamicChainRigCache::ModelEntry* model
        = rig != nullptr ? game.GetPhysicsSystem().GetDynamicChainRigCache().TryGet(rig->meshGtaPath) : nullptr;
    if (rig == nullptr || model == nullptr || model->chains.empty() || model->chains.size() != rig->chainStates.size()) {
        Fail(result, 409, "entity '" + name + "' has no physics chains", "entity");
        return false;
    }
    target = { entity, rig, model };
    return true;
}

std::uint32_t TotalJoints(const DynamicChainRigCache::ModelEntry& model) noexcept
{
    std::uint32_t total = 0;
    for (const DynamicChainDefinition& chain : model.chains) {
        total += static_cast<std::uint32_t>(chain.jointBoneIndices.size());
    }
    return total;
}

PhysicsAnimatorReport MakeAnimatorReport(Game& game, Entity entity)
{
    PhysicsAnimatorReport report;
    const SkeletalAnimator* animator = game.GetRegistry().TryGetComponent<SkeletalAnimator>(entity);
    if (animator == nullptr) {
        return report;
    }
    report.present = true;
    report.playing = animator->playing;
    report.loop = animator->loop;
    report.speed = animator->speed;
    report.frame = animator->frame;
    report.animationGtaPath = animator->animationGtaPath;
    if (const std::optional<std::uint32_t> last = game.TryGetClipLastFrame(animator->animationGtaPath)) {
        report.lastFrame = static_cast<std::int32_t>(*last);
    }
    return report;
}

PhysicsCaptureStatus MakeCaptureStatus(Game& game, const Entity* entity)
{
    const PhysicsDebugRecorder& recorder = game.GetPhysicsSystem().GetDebugRecorder();
    PhysicsCaptureStatus status;
    status.enabled = recorder.Enabled();
    status.windowFrames = entity != nullptr ? recorder.EffectiveWindowFrames(*entity) : recorder.RequestedWindowFrames();
    status.framesRecorded = entity != nullptr ? recorder.FramesRecorded(*entity) : 0;
    return status;
}

// Routes the physics system into a private recorder (target entity only) for the scope; the own recorder is restored on exit.
class RecorderOverrideScope {
public:
    RecorderOverrideScope(PhysicsSystem& physics, PhysicsDebugRecorder& recorder, Entity onlyEntity) : m_physics(physics)
    {
        m_physics.SetDebugRecorderOverride(&recorder, onlyEntity);
    }
    ~RecorderOverrideScope() { m_physics.SetDebugRecorderOverride(nullptr); }
    RecorderOverrideScope(const RecorderOverrideScope&) = delete;
    RecorderOverrideScope& operator=(const RecorderOverrideScope&) = delete;

private:
    PhysicsSystem& m_physics;
};

// Snapshot of everything a headless run changes; put back on scope exit.
class SimulationStateGuard {
public:
    explicit SimulationStateGuard(Game& game) : m_game(game), m_settings(game.GetPhysicsSystem().GetGlobalPhysicsSettings())
    {
        Registry& registry = game.GetRegistry();
        ComponentStorage<SkeletalAnimator>& animators = registry.Storage<SkeletalAnimator>();
        for (std::size_t i = 0; i < animators.Size(); ++i) {
            const SkeletalAnimator& a = animators.ComponentAt(i);
            m_animators.push_back({ animators.EntityAt(i), a.frame, a.speed, a.playing, a.loop });
        }
        ComponentStorage<DynamicChainRig>& rigs = registry.Storage<DynamicChainRig>();
        for (std::size_t i = 0; i < rigs.Size(); ++i) {
            const DynamicChainRig& r = rigs.ComponentAt(i);
            m_rigs.push_back({ rigs.EntityAt(i), r.enabled, r.frozen, r.chainStates, r.accumulatedSeconds });
        }
    }

    ~SimulationStateGuard()
    {
        m_game.GetPhysicsSystem().GetGlobalPhysicsSettings() = m_settings;
        Registry& registry = m_game.GetRegistry();
        for (const AnimatorState& s : m_animators) {
            if (SkeletalAnimator* a = registry.TryGetComponent<SkeletalAnimator>(s.entity)) {
                a->frame = s.frame;
                a->speed = s.speed;
                a->playing = s.playing;
                a->loop = s.loop;
            }
        }
        for (RigState& s : m_rigs) {
            if (DynamicChainRig* r = registry.TryGetComponent<DynamicChainRig>(s.entity)) {
                r->enabled = s.enabled;
                r->frozen = s.frozen;
                r->chainStates = std::move(s.chainStates);
                r->accumulatedSeconds = s.accumulatedSeconds;
            }
        }
    }

    SimulationStateGuard(const SimulationStateGuard&) = delete;
    SimulationStateGuard& operator=(const SimulationStateGuard&) = delete;

private:
    struct AnimatorState {
        Entity entity;
        float frame;
        float speed;
        bool playing;
        bool loop;
    };
    struct RigState {
        Entity entity;
        bool enabled;
        bool frozen;
        std::vector<DynamicChainRuntimeState> chainStates;
        float accumulatedSeconds;
    };

    Game& m_game;
    GlobalPhysicsSettings m_settings;
    std::vector<AnimatorState> m_animators;
    std::vector<RigState> m_rigs;
};

// Steps the simulation once with zero time and returns the newest frame sample of every chain.
// False when the entity has no pose or produced no frame.
bool ProbeEntityFrame(Game& game, const Target& target, std::vector<ChainFrameSample>& out)
{
    if (!game.GetRegistry().HasComponent<ResolvedAnimationPose>(target.entity)) {
        return false;
    }
    PhysicsDebugRecorder recorder;
    recorder.Configure(true, kProbeWindow);
    {
        RecorderOverrideScope scope(game.GetPhysicsSystem(), recorder, target.entity);
        game.StepSimulationHeadless(0.0);
    }
    out.clear();
    for (std::size_t c = 0; c < target.model->chains.size(); ++c) {
        const std::vector<const ChainFrameSample*> frames = recorder.Frames(target.entity, c);
        if (frames.empty()) {
            out.clear();
            return false;
        }
        out.push_back(*frames.back());
    }
    return true;
}

const std::vector<BoneLocalOffset>& PoseOrBind(Registry& registry, Entity entity)
{
    static const std::vector<BoneLocalOffset> kBindPose;
    const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    return pose != nullptr ? pose->pose : kBindPose;
}

void FillBindPoseMetrics(PhysicsMetricsReport& metrics, const DynamicChainRigCache::ModelEntry& model, const GlobalPhysicsSettings& settings)
{
    const BindPoseCollisionReport bindPose = ComputeBindPoseCollision(model, settings);
    metrics.collision.bindPosePenetratingJoints = bindPose.penetratingJoints;
    metrics.collision.bindPosePushedJoints = bindPose.pushedJoints;
}

// ---- List / State ---------------------------------------------------------

PhysicsDebugCommandResult DoList(Game& game)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::List);
    Registry& registry = game.GetRegistry();
    const DynamicChainRigCache& cache = game.GetPhysicsSystem().GetDynamicChainRigCache();
    ComponentStorage<DynamicChainRig>& rigs = registry.Storage<DynamicChainRig>();
    for (std::size_t i = 0; i < rigs.Size(); ++i) {
        const Entity entity = rigs.EntityAt(i);
        const DynamicChainRig& rig = rigs.ComponentAt(i);
        PhysicsDebugListEntry entry;
        if (const Name* name = registry.TryGetComponent<Name>(entity)) {
            entry.name = name->value;
        }
        entry.entityIndex = entity.index;
        entry.entityGeneration = entity.generation;
        entry.meshGtaPath = rig.meshGtaPath;
        entry.chainCount = static_cast<std::uint32_t>(rig.chainStates.size());
        if (const DynamicChainRigCache::ModelEntry* model = cache.TryGet(rig.meshGtaPath)) {
            entry.jointCount = TotalJoints(*model);
        }
        entry.rigEnabled = rig.enabled;
        entry.rigFrozen = rig.frozen;
        entry.animator = MakeAnimatorReport(game, entity);
        result.list.entries.push_back(std::move(entry));
    }
    Succeed(result);
    return result;
}

PhysicsDebugCommandResult DoState(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::State);
    Target target;
    if (!ResolveTarget(game, request.entityName, target, result)) {
        return result;
    }
    const DynamicChainRigCache::ModelEntry& model = *target.model;
    PhysicsDebugStateReport& state = result.state;
    state.entityName = request.entityName;
    state.settings = DescribeSettings(game.GetPhysicsSystem().GetGlobalPhysicsSettings());

    for (std::size_t c = 0; c < model.chains.size(); ++c) {
        const DynamicChainDefinition& chain = model.chains[c];
        PhysicsChainStateReport chainReport;
        chainReport.index = static_cast<std::uint32_t>(c);
        chainReport.anchorBoneName = BoneNameOrEmpty(model.skeleton, chain.rootBoneIndex);
        chainReport.anchorBoneIndex = chain.rootBoneIndex;
        chainReport.joints = static_cast<std::uint32_t>(chain.jointBoneIndices.size());
        chainReport.rootChildren = static_cast<std::uint32_t>(std::count_if(
            chain.parentJointIndex.begin(), chain.parentJointIndex.end(), [](std::int32_t p) { return p < 0; }));
        chainReport.extraConstraints = static_cast<std::uint32_t>(chain.extraConstraints.size());
        chainReport.constraintIterations = chain.constraintIterations;
        chainReport.collisionEnabled = chain.collisionEnabled;
        chainReport.maxPlausibleRootDelta = chain.maxPlausibleRootDelta;
        chainReport.initialized = target.rig->chainStates[c].initialized;
        chainReport.reseedCount = target.rig->chainStates[c].reseedCount;
        state.chains.push_back(std::move(chainReport));

        std::map<std::string, std::uint32_t> counts;
        for (const std::string& name : ChainBoneNames(model.skeleton, chain)) {
            ++counts[JointGroupKey(name)];
        }
        for (const auto& [key, joints] : counts) {
            state.groups.push_back({ key, static_cast<std::uint32_t>(c), joints });
        }
    }
    state.colliderCount = static_cast<std::uint32_t>(model.colliders.size());
    state.orphanCount = static_cast<std::uint32_t>(model.diagnostics.orphanedDynamicBoneIndices.size());
    state.crossChainJointsDropped = static_cast<std::uint32_t>(model.diagnostics.crossChainJointsDropped.size());
    state.duplicateBodiesDropped
        = static_cast<std::uint32_t>(model.diagnostics.duplicateBoneRigidBodyAssignmentsDropped.size());
    state.bendLimitMissing = static_cast<std::uint32_t>(model.diagnostics.bendLimitMissingBoneIndices.size());
    state.capture = MakeCaptureStatus(game, &target.entity);
    Succeed(result);
    return result;
}

// ---- Settings, presets, playback -----------------------------------------

PhysicsDebugCommandResult DoSet(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Set);
    PhysicsSettingPairs globalPairs;
    std::optional<bool> rigEnabled;
    std::optional<bool> rigFrozen;
    for (const auto& [key, value] : request.pairs) {
        if (!IsPhysicsEntitySettingKey(key)) {
            globalPairs.emplace_back(key, value);
            continue;
        }
        std::optional<bool>& slot = key == "rig_enabled" ? rigEnabled : rigFrozen;
        bool parsed = false;
        if (request.entityName.empty()) {
            Fail(result, 400, key + " needs the entity parameter", key);
            return result;
        }
        if (slot.has_value()) {
            Fail(result, 400, "duplicate key " + key, key);
            return result;
        }
        if (!ParsePhysicsBool(value, parsed)) {
            Fail(result, 400, key + " must be true or false", key);
            return result;
        }
        slot = parsed;
    }
    if (globalPairs.empty() && !rigEnabled.has_value() && !rigFrozen.has_value()) {
        Fail(result, 400, "at least one key is required");
        return result;
    }

    Target target;
    if (!request.entityName.empty() && !ResolveTarget(game, request.entityName, target, result)) {
        return result;
    }

    GlobalPhysicsSettings updated = game.GetPhysicsSystem().GetGlobalPhysicsSettings();
    std::string message;
    std::string errorKey;
    if (!globalPairs.empty() && !ApplyPhysicsSettingPairs(updated, globalPairs, message, errorKey)) {
        Fail(result, 400, std::move(message), std::move(errorKey));
        return result;
    }

    game.GetPhysicsSystem().GetGlobalPhysicsSettings() = updated;
    if (target.rig != nullptr) {
        if (rigEnabled.has_value()) {
            target.rig->enabled = *rigEnabled;
        }
        if (rigFrozen.has_value()) {
            target.rig->frozen = *rigFrozen;
        }
    }
    result.settings = DescribeSettings(updated);
    Succeed(result);
    return result;
}

PhysicsDebugCommandResult DoPreset(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Preset);
    GlobalPhysicsSettings& settings = game.GetPhysicsSystem().GetGlobalPhysicsSettings();
    if (!ApplyPhysicsPreset(settings, request.presetName)) {
        Fail(result, 400, "unknown preset '" + request.presetName + "' (use default or legacy)", "name");
        return result;
    }
    result.settings = DescribeSettings(settings);
    Succeed(result);
    return result;
}

PhysicsDebugCommandResult DoPlay(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Play);
    Target target;
    if (!ResolveTarget(game, request.entityName, target, result)) {
        return result;
    }
    if (request.animationGtaPath.empty()) {
        Fail(result, 400, "animation_gta_path is required", "animation_gta_path");
        return result;
    }
    if (request.hasFrame && !(request.frame >= 0.0f && request.frame <= kMaxFrameValue)) {
        Fail(result, 400, "frame must be between 0 and 100000", "frame");
        return result;
    }
    if (request.hasSpeed && !(request.speed >= 0.0f && request.speed <= kPhysicsDebugMaxPlaySpeed)) {
        Fail(result, 400, "speed must be between 0 and 100", "speed");
        return result;
    }
    if (!game.PlayAnimationOnEntity(target.entity, request.animationGtaPath)) {
        Fail(result, 400, "play refused (dead entity, model without skin data, or bad clip)");
        return result;
    }
    SkeletalAnimator* animator = game.GetRegistry().TryGetComponent<SkeletalAnimator>(target.entity);
    if (animator == nullptr) {
        Fail(result, 409, "entity '" + request.entityName + "' has no animator after play");
        return result;
    }
    if (request.hasFrame) {
        animator->frame = request.frame;
    }
    if (request.hasSpeed) {
        animator->speed = request.speed;
    }
    if (request.hasLoop) {
        animator->loop = request.loop;
    }
    result.animator = MakeAnimatorReport(game, target.entity);
    Succeed(result);
    return result;
}

PhysicsDebugCommandResult DoSeek(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Seek);
    Target target;
    if (!ResolveTarget(game, request.entityName, target, result)) {
        return result;
    }
    if (!(request.frame >= 0.0f && request.frame <= kMaxFrameValue)) {
        Fail(result, 400, "frame must be between 0 and 100000", "frame");
        return result;
    }
    SkeletalAnimator* animator = game.GetRegistry().TryGetComponent<SkeletalAnimator>(target.entity);
    if (animator == nullptr) {
        Fail(result, 409, "entity '" + request.entityName + "' has no animator");
        return result;
    }
    animator->frame = request.frame;
    if (request.resetPhysics) {
        PhysicsSystem::ResetRig(game.GetRegistry(), target.entity);
    }
    result.animator = MakeAnimatorReport(game, target.entity);
    Succeed(result);
    return result;
}

PhysicsDebugCommandResult DoReset(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Reset);
    Target target;
    if (!ResolveTarget(game, request.entityName, target, result)) {
        return result;
    }
    PhysicsSystem::ResetRig(game.GetRegistry(), target.entity);
    Succeed(result);
    return result;
}

PhysicsDebugCommandResult DoCapture(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Capture);
    Target target;
    if (!request.entityName.empty() && !ResolveTarget(game, request.entityName, target, result)) {
        return result;
    }
    if (request.windowFrames < 1 || request.windowFrames > kMaxCaptureWindow) {
        Fail(result, 400, "window must be between 1 and 600", "window");
        return result;
    }
    game.GetPhysicsSystem().GetDebugRecorder().Configure(request.captureEnabled, request.windowFrames);
    const Entity* entity = request.entityName.empty() ? nullptr : &target.entity;
    result.capture = MakeCaptureStatus(game, entity);
    Succeed(result);
    return result;
}

// ---- Metrics and the headless run ----------------------------------------

PhysicsDebugCommandResult DoMetrics(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Metrics);
    Target target;
    if (!ResolveTarget(game, request.entityName, target, result)) {
        return result;
    }
    PhysicsSystem& physics = game.GetPhysicsSystem();
    const PhysicsDebugRecorder& recorder = physics.GetDebugRecorder();
    const std::size_t chainCount = target.model->chains.size();

    std::vector<std::vector<const ChainFrameSample*>> frames(chainCount);
    std::size_t available = std::numeric_limits<std::size_t>::max();
    for (std::size_t c = 0; c < chainCount; ++c) {
        frames[c] = recorder.Frames(target.entity, c);
        available = std::min(available, frames[c].size());
    }
    if (!recorder.Enabled() || available < 3) {
        Fail(result, 409, "capture is off or has fewer than 3 frames");
        return result;
    }
    const std::size_t window = request.windowFrames == 0 ? available : std::max<std::size_t>(request.windowFrames, 3);
    const std::size_t first = available > window ? available - window : 0;

    double deltaSum = 0.0;
    for (std::size_t k = first; k < available; ++k) {
        deltaSum += frames[0][k]->renderDeltaSeconds;
    }
    const double meanDelta = deltaSum / static_cast<double>(available - first);
    if (!(meanDelta > 0.0)) {
        Fail(result, 409, "recorded frames carry no time delta");
        return result;
    }
    const float renderHz = static_cast<float>(1.0 / meanDelta);

    const GlobalPhysicsSettings& settings = physics.GetGlobalPhysicsSettings();
    PhysicsMetricsAccumulator accumulator(*target.model, BuildStepOptions(settings));
    std::vector<const ChainFrameSample*> cur(chainCount);
    std::vector<const ChainFrameSample*> prev1(chainCount);
    std::vector<const ChainFrameSample*> prev2(chainCount);
    for (std::size_t k = first; k < available; ++k) {
        for (std::size_t c = 0; c < chainCount; ++c) {
            cur[c] = frames[c][k];
            prev1[c] = k >= 1 ? frames[c][k - 1] : nullptr;
            prev2[c] = k >= 2 ? frames[c][k - 2] : nullptr;
        }
        accumulator.AddFrame(cur, prev1, prev2, renderHz);
    }

    result.run.metrics = accumulator.Finish(renderHz);
    if (!request.groupFilter.empty()) {
        std::erase_if(result.run.metrics.groups,
            [&request](const GroupMetricsReport& group) { return group.key != request.groupFilter; });
        if (result.run.metrics.groups.empty()) {
            Fail(result, 400, "unknown group '" + request.groupFilter + "'", "group");
            return result;
        }
    }
    FillBindPoseMetrics(result.run.metrics, *target.model, settings);
    result.run.settingsUsed = DescribeSettings(settings);
    Succeed(result);
    return result;
}

struct TraceSlot {
    std::size_t chain = 0;
    std::size_t joint = 0;
    std::size_t series = 0;
};

bool ValidateRun(const PhysicsRunRequest& run, std::uint32_t totalJoints, PhysicsDebugCommandResult& result)
{
    if (run.frames < 1 || run.frames > kMaxRunFrames) {
        Fail(result, 400, "frames must be between 1 and 3600", "frames");
        return false;
    }
    if (!(run.renderHz >= 10.0f && run.renderHz <= 480.0f)) {
        Fail(result, 400, "render_hz must be between 10 and 480", "render_hz");
        return false;
    }
    if (!(run.startFrame >= 0.0f && run.startFrame <= kMaxFrameValue)) {
        Fail(result, 400, "start_frame must be between 0 and 100000", "start_frame");
        return false;
    }
    if (run.warmupFrames >= run.frames) {
        Fail(result, 400, "warmup_frames must be smaller than frames", "warmup_frames");
        return false;
    }
    if (static_cast<std::uint64_t>(run.frames) * totalJoints > kMaxRunJointFrames) {
        Fail(result, 400, "frames * joints must not exceed 2000000", "frames");
        return false;
    }
    if (run.traceEvery < 1) {
        Fail(result, 400, "trace.every must be at least 1", "trace.every");
        return false;
    }
    for (const std::uint32_t frame : run.snapshotFrames) {
        if (frame >= run.frames) {
            Fail(result, 400, "snapshot_frames must be smaller than frames", "snapshot_frames");
            return false;
        }
    }
    return true;
}

PhysicsDebugCommandResult DoRun(Game& game, const PhysicsDebugCommandRequest& request)
{
    GTE_PROFILE_SCOPE("PhysicsDebug.Run");
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Run);
    const PhysicsRunRequest& run = request.run;
    Target target;
    if (!ResolveTarget(game, run.entityName.empty() ? request.entityName : run.entityName, target, result)) {
        return result;
    }
    const DynamicChainRigCache::ModelEntry& model = *target.model;
    if (!ValidateRun(run, TotalJoints(model), result)) {
        return result;
    }

    PhysicsSystem& physics = game.GetPhysicsSystem();
    Registry& registry = game.GetRegistry();

    GlobalPhysicsSettings runSettings = physics.GetGlobalPhysicsSettings();
    if (!run.settingsPairs.empty()) {
        std::string message;
        std::string errorKey;
        if (!ApplyPhysicsSettingPairs(runSettings, run.settingsPairs, message, errorKey)) {
            Fail(result, 400, std::move(message), std::move(errorKey));
            return result;
        }
    }

    std::vector<std::vector<std::string>> boneNames;
    for (const DynamicChainDefinition& chain : model.chains) {
        boneNames.push_back(ChainBoneNames(model.skeleton, chain));
    }
    std::vector<TraceSlot> traceSlots;
    std::vector<PhysicsTraceSeries> traceSeries;
    for (const std::string& bone : run.traceBones) {
        bool found = false;
        for (std::size_t c = 0; c < boneNames.size() && !found; ++c) {
            const auto it = std::find(boneNames[c].begin(), boneNames[c].end(), bone);
            if (it != boneNames[c].end()) {
                traceSlots.push_back({ c, static_cast<std::size_t>(it - boneNames[c].begin()), traceSeries.size() });
                traceSeries.push_back({ bone, {}, {}, {} });
                found = true;
            }
        }
        if (!found) {
            Fail(result, 400, "unknown trace bone '" + bone + "'", "trace.bones");
            return result;
        }
    }

    // From here on the simulation state changes; the guards put everything back.
    std::optional<SimulationStateGuard> stateGuard;
    if (run.restore) {
        stateGuard.emplace(game);
    }
    physics.GetGlobalPhysicsSettings() = runSettings;

    if (SkeletalAnimator* animator = registry.TryGetComponent<SkeletalAnimator>(target.entity)) {
        animator->frame = run.startFrame;
        animator->speed = 1.0f;
        animator->loop = true;
        animator->playing = true;
    }
    PhysicsSystem::ResetRig(registry, target.entity);

    const std::size_t chainCount = model.chains.size();
    PhysicsDebugRecorder recorder;
    recorder.Configure(true, kProbeWindow);
    RecorderOverrideScope recorderScope(physics, recorder, target.entity);

    PhysicsMetricsAccumulator accumulator(model, BuildStepOptions(runSettings));
    std::vector<std::array<ChainFrameSample, 3>> history(chainCount);
    std::size_t historyCount = 0;
    std::vector<float> physicsMs;
    physicsMs.reserve(run.frames);
    std::vector<const ChainFrameSample*> cur(chainCount);
    std::vector<const ChainFrameSample*> prev1(chainCount);
    std::vector<const ChainFrameSample*> prev2(chainCount);
    bool wrapped = false;
    const double deltaSeconds = 1.0 / static_cast<double>(run.renderHz);

    for (std::uint32_t f = 0; f < run.frames; ++f) {
        const SkeletalAnimator* before = registry.TryGetComponent<SkeletalAnimator>(target.entity);
        const float frameBefore = before != nullptr ? before->frame : 0.0f;

        game.StepSimulationHeadless(deltaSeconds);

        const SkeletalAnimator* after = registry.TryGetComponent<SkeletalAnimator>(target.entity);
        if (after != nullptr && after->frame < frameBefore) {
            wrapped = true;
        }

        for (std::size_t c = 0; c < chainCount; ++c) {
            const std::vector<const ChainFrameSample*> frames = recorder.Frames(target.entity, c);
            if (frames.empty()) {
                Fail(result, 409, "entity produced no physics frame (no pose, or the rig is disabled)");
                return result;
            }
            std::array<ChainFrameSample, 3>& h = history[c];
            std::swap(h[0], h[1]);
            std::swap(h[1], h[2]);
            h[2] = *frames.back();
        }
        historyCount = std::min<std::size_t>(historyCount + 1, 3);
        physicsMs.push_back(static_cast<float>(physics.LastUpdateMilliseconds()));

        for (std::size_t c = 0; c < chainCount; ++c) {
            cur[c] = &history[c][2];
            prev1[c] = historyCount >= 2 ? &history[c][1] : nullptr;
            prev2[c] = historyCount >= 3 ? &history[c][0] : nullptr;
        }
        if (f >= run.warmupFrames) {
            accumulator.AddFrame(cur, prev1, prev2, run.renderHz);
        }

        if (f % run.traceEvery == 0) {
            for (const TraceSlot& slot : traceSlots) {
                const ChainFrameSample& sample = *cur[slot.chain];
                PhysicsTraceSeries& series = traceSeries[slot.series];
                series.display.push_back(ToFloat3(sample.display[slot.joint]));
                series.animated.push_back(ToFloat3(sample.animated[slot.joint]));
                series.fk.push_back(ToFloat3(sample.fk[slot.joint]));
            }
        }
        if (std::find(run.snapshotFrames.begin(), run.snapshotFrames.end(), f) != run.snapshotFrames.end()) {
            PhysicsRunSnapshot snapshot;
            snapshot.frame = f;
            for (std::size_t c = 0; c < chainCount; ++c) {
                const ChainFrameSample& sample = *cur[c];
                for (std::size_t i = 0; i < boneNames[c].size(); ++i) {
                    snapshot.joints.push_back({ boneNames[c][i], ToFloat3(sample.display[i]), ToFloat3(sample.animated[i]),
                        ToFloat3(sample.fk[i]) });
                }
            }
            result.run.snapshots.push_back(std::move(snapshot));
        }
    }

    result.run.metrics = accumulator.Finish(run.renderHz);
    FillBindPoseMetrics(result.run.metrics, model, runSettings);
    result.run.settingsUsed = DescribeSettings(runSettings);
    result.run.wrapped = wrapped;
    const std::vector<float> measured(physicsMs.begin() + run.warmupFrames, physicsMs.end());
    const MetricStats timing = Summarize(measured);
    result.run.physicsMsMedian = timing.p50;
    result.run.physicsMsP95 = timing.p95;
    result.run.trace = std::move(traceSeries);
    Succeed(result);
    return result;
}

// ---- Joints, colliders, image --------------------------------------------

PhysicsDebugCommandResult DoJoints(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Joints);
    Target target;
    if (!ResolveTarget(game, request.entityName, target, result)) {
        return result;
    }
    const DynamicChainRigCache::ModelEntry& model = *target.model;
    if (request.chainFilter >= static_cast<std::int32_t>(model.chains.size())) {
        Fail(result, 400, "chain is out of range", "chain");
        return result;
    }
    const GlobalPhysicsSettings& settings = game.GetPhysicsSystem().GetGlobalPhysicsSettings();
    const StepOptions options = BuildStepOptions(settings);
    const std::uint32_t count = std::clamp<std::uint32_t>(request.count, 1, kMaxJointRows);

    std::vector<ChainFrameSample> live;
    const bool liveValid = ProbeEntityFrame(game, target, live);

    PhysicsJointsReport& report = result.joints;
    report.first = request.first;
    for (std::size_t c = 0; c < model.chains.size(); ++c) {
        if (request.chainFilter >= 0 && static_cast<std::size_t>(request.chainFilter) != c) {
            continue;
        }
        const DynamicChainDefinition& chain = model.chains[c];
        const std::vector<std::string> names = ChainBoneNames(model.skeleton, chain);
        const std::vector<float> effective = ComputeEffectiveBendLimits(chain, options);
        std::vector<float> cumulative;
        std::vector<float> flooredOwn;
        ComputeCumulativeBendLimits(chain, options.bendJointFloorRadians, cumulative, &flooredOwn);
        const std::vector<JointDebugInfo>* debugInfo = c < model.jointDebugInfo.size() ? &model.jointDebugInfo[c] : nullptr;
        const ChainFrameSample* sample = (liveValid && c < live.size()) ? &live[c] : nullptr;

        for (std::size_t i = 0; i < names.size(); ++i) {
            const std::string group = JointGroupKey(names[i]);
            if (!request.groupFilter.empty() && request.groupFilter != group) {
                continue;
            }
            const std::uint32_t rowNumber = report.total++;
            if (rowNumber < request.first || report.rows.size() >= count) {
                continue;
            }

            const DynamicJointSettings& js = chain.jointSettings[i];
            const std::int32_t parent = chain.parentJointIndex[i];
            PhysicsJointRow row;
            row.index = static_cast<std::uint32_t>(i);
            row.chain = static_cast<std::uint32_t>(c);
            row.bone = names[i];
            row.boneIndex = chain.jointBoneIndices[i];
            row.group = group;
            row.parentJoint = parent;
            row.parentBone = parent >= 0 ? names[static_cast<std::size_t>(parent)] : BoneNameOrEmpty(model.skeleton, chain.rootBoneIndex);
            row.restLength = chain.restLengths[i];
            row.mass = js.mass;
            row.damping = js.damping;
            row.pmxLinearDamping = js.pmxLinearDamping;
            row.stiffness = js.stiffness;
            row.collisionRadius = js.collisionRadius;
            row.collisionFitRadius = js.collisionFitRadius;
            for (const std::uint16_t k : js.ignoredColliderIndices) {
                row.ignoredColliders.push_back(ColliderLabel(model, k));
            }
            row.groupBit = GroupBit(js.group);
            row.collisionMask = js.collisionMask;
            row.bendLimitDeg = RadToDeg(js.bendLimitRadians);
            row.ownLimitWithFloorDeg = RadToDeg(flooredOwn[i]);
            row.cumulativeBendLimitDeg = RadToDeg(cumulative[i]);
            row.effectiveBendLimitDeg = RadToDeg(effective[i]);
            if (debugInfo != nullptr && i < debugInfo->size()) {
                const JointDebugInfo& info = (*debugInfo)[i];
                row.rigidBodyName = info.rigidBodyName;
                row.pmxJointName = info.pmxJointName;
                row.pmxJointFound = info.pmxJointFound;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    row.pmxRotLowerDeg[axis] = RadToDeg(info.pmxRotLowerRad[axis]);
                    row.pmxRotUpperDeg[axis] = RadToDeg(info.pmxRotUpperRad[axis]);
                }
            }

            if (sample != nullptr && i < sample->animated.size() && i < sample->display.size() && i < sample->fk.size()) {
                const bool hasParent = parent >= 0;
                const Vec3 parentDisplay = hasParent ? sample->display[static_cast<std::size_t>(parent)] : sample->rootAnimWorld;
                const Vec3 parentAnimated = hasParent ? sample->animated[static_cast<std::size_t>(parent)] : sample->rootAnimWorld;
                const std::vector<VerletParticle>& particles = target.rig->chainStates[c].particles;
                row.live.valid = true;
                row.live.animated = ToFloat3(sample->animated[i]);
                row.live.display = ToFloat3(sample->display[i]);
                row.live.fk = ToFloat3(sample->fk[i]);
                row.live.particle = i < particles.size() ? ToFloat3(particles[i].position) : row.live.display;
                row.live.distFromAnimated = Length(sample->display[i] - sample->animated[i]);
                row.live.segDirErrDeg
                    = VectorAngleDegrees(sample->display[i] - parentDisplay, sample->animated[i] - parentAnimated);
            }
            report.rows.push_back(std::move(row));
        }
    }
    Succeed(result);
    return result;
}

const char* ColliderShapeName(ColliderShape shape) noexcept
{
    switch (shape) {
    case ColliderShape::Sphere: return "sphere";
    case ColliderShape::Box: return "box";
    case ColliderShape::Capsule: return "capsule";
    }
    return "sphere";
}

PhysicsDebugCommandResult DoColliders(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Colliders);
    Target target;
    if (!ResolveTarget(game, request.entityName, target, result)) {
        return result;
    }
    const DynamicChainRigCache::ModelEntry& model = *target.model;
    Registry& registry = game.GetRegistry();
    const std::vector<Collider> world
        = ResolveEntityColliders(model, PoseOrBind(registry, target.entity), ComputeEntityPhysicsMatrix(registry, target.entity));

    for (std::size_t k = 0; k < world.size() && k < model.colliders.size(); ++k) {
        const Collider& collider = world[k];
        PhysicsColliderRow row;
        row.index = static_cast<std::uint32_t>(k);
        row.rigidBodyName = ColliderLabel(model, k);
        row.boneName = BoneNameOrEmpty(model.skeleton, model.colliders[k].boneIndex);
        row.shape = ColliderShapeName(collider.shape);
        row.size = ToFloat3(collider.size);
        row.worldCenter = ToFloat3(collider.center);
        row.worldRotationQuat = { collider.rotation.x, collider.rotation.y, collider.rotation.z, collider.rotation.w };
        row.group = collider.group;
        row.mask = collider.collisionMask;
        result.colliders.rows.push_back(std::move(row));
    }
    result.colliders.bindPosePenetrations
        = ComputeBindPoseCollision(model, game.GetPhysicsSystem().GetGlobalPhysicsSettings()).penetrations;
    Succeed(result);
    return result;
}

PhysicsDebugCommandResult DoImage(Game& game, const PhysicsDebugCommandRequest& request)
{
    PhysicsDebugCommandResult result = MakeResult(PhysicsDebugCommandKind::Image);
    const PhysicsDebugImageRequest& image = request.image;
    Target target;
    if (!ResolveTarget(game, image.entityName.empty() ? request.entityName : image.entityName, target, result)) {
        return result;
    }
    PhysicsDebugViewBasis basis;
    if (!TryGetPhysicsDebugViewBasis(image.view, basis)) {
        Fail(result, 400, "view must be negz, posz, posx, negx or top", "view");
        return result;
    }
    if (image.mode != "both" && image.mode != "sim" && image.mode != "anim") {
        Fail(result, 400, "mode must be both, sim or anim", "mode");
        return result;
    }
    if (image.width < 200 || image.width > 2000) {
        Fail(result, 400, "width must be between 200 and 2000", "width");
        return result;
    }
    if (image.height < 200 || image.height > 2000) {
        Fail(result, 400, "height must be between 200 and 2000", "height");
        return result;
    }

    std::vector<ChainFrameSample> frames;
    if (!ProbeEntityFrame(game, target, frames)) {
        Fail(result, 409, "no pose yet, wait one frame");
        return result;
    }
    Registry& registry = game.GetRegistry();
    const std::vector<Collider> world = ResolveEntityColliders(
        *target.model, PoseOrBind(registry, target.entity), ComputeEntityPhysicsMatrix(registry, target.entity));
    result.snapshot = BuildPhysicsDebugSnapshot(
        image, *target.model, frames, world, BuildStepOptions(game.GetPhysicsSystem().GetGlobalPhysicsSettings()));
    Succeed(result);
    return result;
}

} // namespace

PhysicsDebugCommandResult ExecutePhysicsDebugCommand(Game& game, const PhysicsDebugCommandRequest& request)
{
    try {
        switch (request.kind) {
        case PhysicsDebugCommandKind::List: return DoList(game);
        case PhysicsDebugCommandKind::State: return DoState(game, request);
        case PhysicsDebugCommandKind::Set: return DoSet(game, request);
        case PhysicsDebugCommandKind::Preset: return DoPreset(game, request);
        case PhysicsDebugCommandKind::Play: return DoPlay(game, request);
        case PhysicsDebugCommandKind::Seek: return DoSeek(game, request);
        case PhysicsDebugCommandKind::Reset: return DoReset(game, request);
        case PhysicsDebugCommandKind::Run: return DoRun(game, request);
        case PhysicsDebugCommandKind::Capture: return DoCapture(game, request);
        case PhysicsDebugCommandKind::Metrics: return DoMetrics(game, request);
        case PhysicsDebugCommandKind::Joints: return DoJoints(game, request);
        case PhysicsDebugCommandKind::Colliders: return DoColliders(game, request);
        case PhysicsDebugCommandKind::Image: return DoImage(game, request);
        }
    } catch (const std::exception& e) {
        PhysicsDebugCommandResult result = MakeResult(request.kind);
        Fail(result, 500, std::string("physics debug command failed: ") + e.what());
        return result;
    }
    PhysicsDebugCommandResult result = MakeResult(request.kind);
    Fail(result, 400, "unknown command");
    return result;
}

} // namespace gte
