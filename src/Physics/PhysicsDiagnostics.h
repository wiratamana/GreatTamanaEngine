#pragma once
#include "DynamicChainDefinition.h"
#include "../Math/Mat4.h"
#include "../Math/MathTypes.h"
#include "../Math/Vec3.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gte {

// A root child with an effective bend limit at or below this counts as welded to the animation.
inline constexpr float kWeldLimitRadians = DegToRad(1.0f);
constexpr bool IsWeldedLimit(float limitRadians) noexcept { return limitRadians <= kWeldLimitRadians; }

// Angle between two vectors in degrees; 0 when either is shorter than 1e-6.
float VectorAngleDegrees(const Vec3& a, const Vec3& b) noexcept;

// "HB1_3_5" -> "HB1". Removes every trailing "_<digits>" run; the full name when nothing is left.
std::string JointGroupKey(std::string_view boneName);

// One render frame of one chain. Raw positions only; all math happens later.
struct ChainFrameSample {
    float renderDeltaSeconds = 0.0f;
    Vec3 rootAnimWorld = Vec3::Zero();
    Mat4 anchorWorld = Mat4::Identity(); // anchor bone world matrix (entity applied).
    std::vector<Vec3> animated;          // per joint, before physics.
    std::vector<Vec3> display;           // per joint, handed to ApplyDynamicChainPhysicsToPose (world space).
    std::vector<Vec3> fk;                // per joint, bone origin after ApplyDynamicChainPhysicsToPose.
    std::uint16_t stepsThisFrame = 0;
    std::uint32_t collisionPushedParticles = 0;
    float collisionPushDistanceSum = 0.0f;
    std::uint32_t bendClampedJoints = 0;
    std::uint32_t reseedCount = 0;
    std::uint32_t nanGuardHits = 0;
};

struct MetricStats {
    std::uint64_t count = 0;
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

// Raw values of one group, collected over many frames.
struct GroupSamples {
    std::vector<float> distFromAnimated;
    std::vector<float> segDirErrDeg;
    std::vector<float> bendDeltaDeg;
    std::vector<float> limitViolationDeg;
    std::vector<float> absStretch;
    std::vector<float> writebackErr;
    std::vector<float> accelLocal;
};

// Mean and nearest-rank percentiles (index = ceil(p * n) - 1). Empty input gives zeros.
MetricStats Summarize(const std::vector<float>& values);

// Adds one chain frame to `groups` (key = JointGroupKey) and "ALL"; welded root children also to "WELDED_ROOT_CHILDREN".
// Spans are index-aligned with joints; prev1/prev2 are earlier frames (null skips accel_local).
void AccumulateChainFrame(const DynamicChainDefinition& chain, std::span<const std::string> boneNames,
    std::span<const float> effectiveLimitRadians, const ChainFrameSample& cur, const ChainFrameSample* prev1,
    const ChainFrameSample* prev2, float renderHz, std::map<std::string, GroupSamples>& groups);

} // namespace gte
