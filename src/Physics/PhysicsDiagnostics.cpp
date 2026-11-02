#include "PhysicsDiagnostics.h"

#include "../Math/MathTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace gte {

namespace {

constexpr float kMinRestLength = 1e-4f;
constexpr float kMinVectorLength = 1e-6f;

bool IsDigit(char c) noexcept { return c >= '0' && c <= '9'; }

} // namespace

float VectorAngleDegrees(const Vec3& a, const Vec3& b) noexcept
{
    const float la = Length(a);
    const float lb = Length(b);
    if (la < kMinVectorLength || lb < kMinVectorLength) {
        return 0.0f;
    }
    const float c = std::clamp(Dot(a, b) / (la * lb), -1.0f, 1.0f);
    return RadToDeg(std::acos(c));
}

std::string JointGroupKey(std::string_view boneName)
{
    std::string_view key = boneName;
    for (;;) {
        std::size_t end = key.size();
        std::size_t digits = 0;
        while (digits < end && IsDigit(key[end - 1 - digits])) {
            ++digits;
        }
        if (digits == 0 || digits >= end || key[end - 1 - digits] != '_') {
            break;
        }
        key.remove_suffix(digits + 1);
    }
    return key.empty() ? std::string(boneName) : std::string(key);
}

MetricStats Summarize(const std::vector<float>& values)
{
    MetricStats stats;
    if (values.empty()) {
        return stats;
    }
    std::vector<float> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t n = sorted.size();

    double sum = 0.0;
    for (const float v : sorted) {
        sum += static_cast<double>(v);
    }
    const auto percentile = [&](double p) {
        const double rank = std::ceil(p * static_cast<double>(n));
        const std::size_t index = rank < 1.0 ? 0 : std::min(static_cast<std::size_t>(rank) - 1, n - 1);
        return static_cast<double>(sorted[index]);
    };

    stats.count = n;
    stats.mean = sum / static_cast<double>(n);
    stats.p50 = percentile(0.50);
    stats.p95 = percentile(0.95);
    stats.p99 = percentile(0.99);
    stats.max = static_cast<double>(sorted.back());
    return stats;
}

void AccumulateChainFrame(const DynamicChainDefinition& chain, std::span<const std::string> boneNames,
    std::span<const float> effectiveLimitRadians, const ChainFrameSample& cur, const ChainFrameSample* prev1,
    const ChainFrameSample* prev2, float renderHz, std::map<std::string, GroupSamples>& groups)
{
    const std::size_t jointCount = chain.jointBoneIndices.size();
    if (boneNames.size() < jointCount || effectiveLimitRadians.size() < jointCount
        || chain.parentJointIndex.size() != jointCount || chain.restLengths.size() != jointCount
        || cur.animated.size() != jointCount || cur.display.size() != jointCount || cur.fk.size() != jointCount) {
        return;
    }

    // accel_local needs three consistent frames and an invertible anchor in each.
    const auto frameUsable = [&](const ChainFrameSample* f) {
        return f != nullptr && f->fk.size() == jointCount;
    };
    Mat4 inverseCur, inversePrev1, inversePrev2;
    const bool accelPossible = frameUsable(prev1) && frameUsable(prev2) && cur.anchorWorld.TryInverse(inverseCur)
        && prev1->anchorWorld.TryInverse(inversePrev1) && prev2->anchorWorld.TryInverse(inversePrev2);
    const float hzSquared = renderHz * renderHz;

    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::int32_t parent = chain.parentJointIndex[i];
        const bool isRootChild = parent < 0;
        const std::size_t p = isRootChild ? 0 : static_cast<std::size_t>(parent);

        const Vec3 parentAnim = isRootChild ? cur.rootAnimWorld : cur.animated[p];
        const Vec3 parentDisp = isRootChild ? cur.rootAnimWorld : cur.display[p];
        const Vec3 animSegment = cur.animated[i] - parentAnim;
        const Vec3 dispSegment = cur.display[i] - parentDisp;

        const float distFromAnimated = Length(cur.display[i] - cur.animated[i]);
        const float segDirErr = VectorAngleDegrees(dispSegment, animSegment);
        const float limitDeg = RadToDeg(effectiveLimitRadians[i]);
        const float limitViolation = std::max(0.0f, segDirErr - limitDeg);

        GroupSamples& group = groups[JointGroupKey(boneNames[i])];
        GroupSamples& all = groups["ALL"];
        const auto push = [&](std::vector<float> GroupSamples::*field, float value) {
            (group.*field).push_back(value);
            (all.*field).push_back(value);
        };

        push(&GroupSamples::distFromAnimated, distFromAnimated);
        push(&GroupSamples::segDirErrDeg, segDirErr);
        push(&GroupSamples::limitViolationDeg, limitViolation);

        if (isRootChild && IsWeldedLimit(effectiveLimitRadians[i])) {
            GroupSamples& welded = groups["WELDED_ROOT_CHILDREN"];
            welded.distFromAnimated.push_back(distFromAnimated);
            welded.segDirErrDeg.push_back(segDirErr);
        }

        if (!isRootChild) {
            const std::int32_t grandParent = chain.parentJointIndex[p];
            const Vec3 gpAnim = grandParent < 0 ? cur.rootAnimWorld : cur.animated[static_cast<std::size_t>(grandParent)];
            const Vec3 gpDisp = grandParent < 0 ? cur.rootAnimWorld : cur.display[static_cast<std::size_t>(grandParent)];
            const float animBend = VectorAngleDegrees(parentAnim - gpAnim, animSegment);
            const float dispBend = VectorAngleDegrees(parentDisp - gpDisp, dispSegment);
            push(&GroupSamples::bendDeltaDeg, std::abs(dispBend - animBend));
        }

        const float rest = chain.restLengths[i];
        if (rest >= kMinRestLength) {
            push(&GroupSamples::absStretch, std::abs(Length(dispSegment) / rest - 1.0f));
        }

        push(&GroupSamples::writebackErr, Length(cur.fk[i] - cur.display[i]));

        if (accelPossible) {
            const Vec3 l0 = inverseCur.TransformPoint(cur.fk[i]);
            const Vec3 l1 = inversePrev1.TransformPoint(prev1->fk[i]);
            const Vec3 l2 = inversePrev2.TransformPoint(prev2->fk[i]);
            push(&GroupSamples::accelLocal, Length(l0 - l1 * 2.0f + l2) * hzSquared);
        }
    }
}

} // namespace gte
