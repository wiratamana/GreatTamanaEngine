#pragma once
#include "PhysicsDiagnostics.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gte {

using Float3 = std::array<float, 3>;

// Largest animator speed the play command accepts (0 holds the frame).
inline constexpr float kPhysicsDebugMaxPlaySpeed = 100.0f;

// ---- List / State -------------------------------------------------------

struct PhysicsAnimatorReport {
    bool present = false;
    bool playing = false;
    bool loop = false;
    float speed = 0.0f;
    float frame = 0.0f;
    std::int32_t lastFrame = -1; // -1 = unknown.
    std::string animationGtaPath;
};

struct PhysicsDebugListEntry {
    std::string name;
    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    std::string meshGtaPath;
    std::uint32_t chainCount = 0;
    std::uint32_t jointCount = 0;
    bool rigEnabled = true;
    bool rigFrozen = false;
    PhysicsAnimatorReport animator;
};

struct PhysicsDebugListReport {
    std::vector<PhysicsDebugListEntry> entries;
};

struct PhysicsChainStateReport {
    std::uint32_t index = 0;
    std::string anchorBoneName;
    std::int32_t anchorBoneIndex = -1;
    std::uint32_t joints = 0;
    std::uint32_t rootChildren = 0;
    std::uint32_t extraConstraints = 0;
    std::uint32_t constraintIterations = 0;
    bool collisionEnabled = true;
    float maxPlausibleRootDelta = 0.0f;
    bool initialized = false;
    std::uint32_t reseedCount = 0;
};

struct PhysicsGroupInfo {
    std::string key;
    std::uint32_t chainIndex = 0;
    std::uint32_t joints = 0;
};

struct PhysicsCaptureStatus {
    bool enabled = false;
    std::uint32_t windowFrames = 0;
    std::uint32_t framesRecorded = 0;
};

struct PhysicsDebugStateReport {
    std::string entityName;
    std::vector<std::pair<std::string, std::string>> settings;
    bool timePaused = false;
    bool timeFrozenThisFrame = false;
    std::uint64_t timeFrameCount = 0;
    std::vector<PhysicsChainStateReport> chains;
    std::vector<PhysicsGroupInfo> groups;
    std::uint32_t colliderCount = 0;
    std::uint32_t orphanCount = 0;
    std::uint32_t crossChainJointsDropped = 0;
    std::uint32_t duplicateBodiesDropped = 0;
    std::uint32_t bendLimitMissing = 0;
    PhysicsCaptureStatus capture;
};

// ---- Metrics / Run ------------------------------------------------------

struct PhysicsRunRequest {
    std::string entityName;
    std::uint32_t frames = 0;
    float renderHz = 60.0f;
    float startFrame = 0.0f;
    std::uint32_t warmupFrames = 0;
    bool restore = true;
    std::vector<std::pair<std::string, std::string>> settingsPairs; // temporary overrides.
    std::vector<std::string> traceBones;
    std::uint32_t traceEvery = 1;
    std::vector<std::uint32_t> snapshotFrames;
};

struct GroupMetricsReport {
    std::string key;
    std::int32_t chainIndex = -1; // -1 = aggregate over all chains.
    std::uint32_t jointCount = 0;
    MetricStats distFromAnimated;
    MetricStats segDirErrDeg;
    MetricStats bendDeltaDeg;
    MetricStats limitViolationDeg;
    MetricStats absStretch;
    MetricStats writebackErr;
    MetricStats accelLocal;
};

struct PhysicsCollisionMetrics {
    double pushedParticlesPerFrame = 0.0;    // pushes that survive the cone.
    double rawPushedParticlesPerFrame = 0.0; // every push.
    double meanPushDistance = 0.0;
    std::uint32_t bindPosePenetratingJoints = 0; // raw radius, no ignore list.
    std::uint32_t bindPosePushedJoints = 0;      // active radius and ignore rules.
};

struct PhysicsMetricsReport {
    std::uint32_t frames = 0;
    float renderHz = 0.0f;
    std::vector<GroupMetricsReport> groups;
    GroupMetricsReport all;
    GroupMetricsReport weldedRootChildren; // key "WELDED_ROOT_CHILDREN"; jointCount 0 when none.
    PhysicsCollisionMetrics collision;
    double bendClampedJointsPerFrame = 0.0;
    std::array<std::uint32_t, 4> stepsHistogram{}; // 0, 1, 2, 3+ steps per render frame.
    std::uint32_t reseedCount = 0;
    std::uint32_t nanCount = 0;
};

struct PhysicsTraceJoint {
    std::string bone;
    Float3 display{};
    Float3 animated{};
    Float3 fk{};
};

struct PhysicsTraceSeries {
    std::string bone;
    std::vector<Float3> display;
    std::vector<Float3> animated;
    std::vector<Float3> fk;
};

struct PhysicsRunSnapshot {
    std::uint32_t frame = 0;
    std::vector<PhysicsTraceJoint> joints;
};

struct PhysicsRunReport {
    PhysicsMetricsReport metrics;
    std::vector<std::pair<std::string, std::string>> settingsUsed;
    bool wrapped = false;
    double physicsMsMedian = 0.0;
    double physicsMsP95 = 0.0;
    std::vector<PhysicsTraceSeries> trace;
    std::vector<PhysicsRunSnapshot> snapshots;
};

// ---- Joints / Colliders -------------------------------------------------

struct PhysicsJointLive {
    bool valid = false;
    Float3 animated{};
    Float3 display{};
    Float3 particle{};
    Float3 fk{};
    float distFromAnimated = 0.0f;
    float segDirErrDeg = 0.0f;
};

struct PhysicsJointRow {
    std::uint32_t index = 0;
    std::uint32_t chain = 0;
    std::string bone;
    std::int32_t boneIndex = -1;
    std::string group;
    std::int32_t parentJoint = -1;
    std::string parentBone;
    float restLength = 0.0f;
    std::string rigidBodyName;
    float mass = 0.0f;
    float damping = 0.0f;
    float pmxLinearDamping = -1.0f;
    float stiffness = 0.0f;
    float collisionRadius = 0.0f;
    float collisionFitRadius = -1.0f;
    std::vector<std::string> ignoredColliders;
    std::uint32_t groupBit = 0;
    std::uint32_t collisionMask = 0;
    float bendLimitDeg = 180.0f; // raw PMX own limit.
    float ownLimitWithFloorDeg = 180.0f; // own limit after the bend floor.
    float cumulativeBendLimitDeg = 180.0f;
    float effectiveBendLimitDeg = 180.0f;
    std::string pmxJointName;
    Float3 pmxRotLowerDeg{};
    Float3 pmxRotUpperDeg{};
    bool pmxJointFound = false;
    PhysicsJointLive live;
};

struct PhysicsJointsReport {
    std::uint32_t total = 0;
    std::uint32_t first = 0;
    std::vector<PhysicsJointRow> rows;
};

struct PhysicsColliderRow {
    std::uint32_t index = 0;
    std::string rigidBodyName;
    std::string boneName;
    std::string shape; // "sphere", "box" or "capsule".
    Float3 size{};
    Float3 worldCenter{};
    std::array<float, 4> worldRotationQuat{}; // x, y, z, w.
    std::uint32_t group = 0;
    std::uint32_t mask = 0;
};

struct PhysicsBindPosePenetration {
    std::uint32_t chain = 0;
    std::uint32_t joint = 0;
    std::string bone;
    std::string collider;
    float push = 0.0f; // distance the bind-pose particle moves (raw radius).
};

struct PhysicsCollidersReport {
    std::vector<PhysicsColliderRow> rows;
    std::vector<PhysicsBindPosePenetration> bindPosePenetrations;
};

// ---- Image --------------------------------------------------------------

struct PhysicsDebugImageRequest {
    std::string entityName;
    std::string view = "negz"; // negz, posz, posx, negx, top.
    std::string mode = "both"; // both, sim, anim.
    std::uint32_t width = 900;
    std::uint32_t height = 900;
    bool showColliders = true;
    bool showJoints = true;
    bool showLimits = false;
    std::string highlight; // group key or exact bone name.
};

enum class PhysicsDebugColorClass : std::uint8_t { Anim, Sim, Collider, Limit, Highlight, Anchor };

struct PhysicsDebugSegment {
    Float3 a{};
    Float3 b{};
    PhysicsDebugColorClass colorClass = PhysicsDebugColorClass::Sim;
};

struct PhysicsDebugPoint {
    Float3 p{};
    PhysicsDebugColorClass colorClass = PhysicsDebugColorClass::Sim;
};

// World-space drawing data of one entity; the CPU rasterizer projects and draws exactly what is in it.
struct PhysicsDebugSnapshot {
    bool valid = false;
    std::string view; // "negz", "posz", "posx", "negx" or "top".
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<PhysicsDebugSegment> segments;
    std::vector<PhysicsDebugPoint> points;
    std::vector<std::string> highlightedBones;
};

} // namespace gte
