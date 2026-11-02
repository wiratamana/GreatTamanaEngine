#include "Physics/PhysicsDiagnostics.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace gte {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDeg = kPi / 180.0f;

// Three joints hanging straight down from a root at the origin, one unit apart.
DynamicChainDefinition MakeChain()
{
    DynamicChainDefinition chain;
    chain.rootBoneIndex = 0;
    chain.jointBoneIndices = { 1, 2, 3 };
    chain.jointSettings.resize(3);
    chain.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(3);
    chain.restLengths = { 1.0f, 1.0f, 1.0f };
    return chain;
}

const std::array<std::string, 3> kNames = { "A_0", "A_1", "A_2" };

ChainFrameSample MakeStraightFrame()
{
    ChainFrameSample sample;
    sample.animated = { Vec3(0, -1, 0), Vec3(0, -2, 0), Vec3(0, -3, 0) };
    sample.display = sample.animated;
    sample.fk = sample.animated;
    return sample;
}

std::array<float, 3> FreeLimits()
{
    return { kPi, kPi, kPi };
}

TEST(PhysicsDiagnosticsTests, JointGroupKeyStripsTrailingNumberRuns)
{
    EXPECT_EQ(JointGroupKey("HB1_3_5"), "HB1");
    EXPECT_EQ(JointGroupKey("Q_5_2"), "Q");
    EXPECT_EQ(JointGroupKey("\xE5\xB7\xA6" "F_3_1"), "\xE5\xB7\xA6" "F");
    EXPECT_EQ(JointGroupKey("\xE5\x8F\xB3" "F_0_1"), "\xE5\x8F\xB3" "F");
    EXPECT_EQ(JointGroupKey("SZY_J1_2"), "SZY_J1");
    EXPECT_EQ(JointGroupKey("SZY_S_1"), "SZY_S");
    EXPECT_EQ(JointGroupKey("\xE5\xB8\xBD" "D1_1"), "\xE5\xB8\xBD" "D1");
    EXPECT_EQ(JointGroupKey("DM_0_1"), "DM");
    EXPECT_EQ(JointGroupKey("SZY\xE8\xA6\xAA"), "SZY\xE8\xA6\xAA");
    EXPECT_EQ(JointGroupKey("_12"), "_12");
}

TEST(PhysicsDiagnosticsTests, SummarizeUsesNearestRankPercentiles)
{
    const MetricStats stats = Summarize({ 5, 3, 1, 4, 2 });
    EXPECT_EQ(stats.count, 5u);
    EXPECT_DOUBLE_EQ(stats.mean, 3.0);
    EXPECT_DOUBLE_EQ(stats.p50, 3.0);
    EXPECT_DOUBLE_EQ(stats.p95, 5.0);
    EXPECT_DOUBLE_EQ(stats.p99, 5.0);
    EXPECT_DOUBLE_EQ(stats.max, 5.0);
}

TEST(PhysicsDiagnosticsTests, SummarizeOfEmptyInputIsAllZero)
{
    const MetricStats stats = Summarize({});
    EXPECT_EQ(stats.count, 0u);
    EXPECT_DOUBLE_EQ(stats.mean, 0.0);
    EXPECT_DOUBLE_EQ(stats.max, 0.0);
}

TEST(PhysicsDiagnosticsTests, SegDirErrIsNinetyForARotatedRootChild)
{
    const DynamicChainDefinition chain = MakeChain();
    ChainFrameSample frame = MakeStraightFrame();
    frame.display[0] = Vec3(1, 0, 0);

    const auto limits = FreeLimits();
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, frame, nullptr, nullptr, 60.0f, groups);

    ASSERT_EQ(groups.at("A").segDirErrDeg.size(), 3u);
    EXPECT_NEAR(groups.at("A").segDirErrDeg[0], 90.0f, 0.01f);
    EXPECT_EQ(groups.at("ALL").segDirErrDeg.size(), 3u);
    EXPECT_NEAR(groups.at("A").distFromAnimated[0], std::sqrt(2.0f), 1e-4f);
}

TEST(PhysicsDiagnosticsTests, LimitViolationIsErrorMinusLimitClampedAtZero)
{
    const DynamicChainDefinition chain = MakeChain();
    ChainFrameSample frame = MakeStraightFrame();
    frame.display[0] = Vec3(std::sin(10.0f * kDeg), -std::cos(10.0f * kDeg), 0.0f);

    std::array<float, 3> limits = { 4.0f * kDeg, kPi, kPi };
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, frame, nullptr, nullptr, 60.0f, groups);
    EXPECT_NEAR(groups.at("A").limitViolationDeg[0], 6.0f, 0.01f);

    const auto freeLimits = FreeLimits();
    groups.clear();
    AccumulateChainFrame(chain, kNames, freeLimits, frame, nullptr, nullptr, 60.0f, groups);
    EXPECT_FLOAT_EQ(groups.at("A").limitViolationDeg[0], 0.0f);
}

TEST(PhysicsDiagnosticsTests, AbsStretchIsRelativeLengthError)
{
    const DynamicChainDefinition chain = MakeChain();
    const auto limits = FreeLimits();

    ChainFrameSample longer = MakeStraightFrame();
    longer.display[0] = Vec3(0, -1.2f, 0);
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, longer, nullptr, nullptr, 60.0f, groups);
    EXPECT_NEAR(groups.at("A").absStretch[0], 0.2f, 1e-5f);

    ChainFrameSample shorter = MakeStraightFrame();
    shorter.display[0] = Vec3(0, -0.8f, 0);
    groups.clear();
    AccumulateChainFrame(chain, kNames, limits, shorter, nullptr, nullptr, 60.0f, groups);
    EXPECT_NEAR(groups.at("A").absStretch[0], 0.2f, 1e-5f);
}

TEST(PhysicsDiagnosticsTests, AbsStretchSkipsTinyRestLengths)
{
    DynamicChainDefinition chain = MakeChain();
    chain.restLengths[0] = 0.0f;
    const auto limits = FreeLimits();
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, MakeStraightFrame(), nullptr, nullptr, 60.0f, groups);
    EXPECT_EQ(groups.at("A").absStretch.size(), 2u);
}

TEST(PhysicsDiagnosticsTests, WritebackErrIsZeroWhenFkEqualsDisplay)
{
    const DynamicChainDefinition chain = MakeChain();
    const auto limits = FreeLimits();
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, MakeStraightFrame(), nullptr, nullptr, 60.0f, groups);
    for (const float v : groups.at("A").writebackErr) {
        EXPECT_FLOAT_EQ(v, 0.0f);
    }

    ChainFrameSample off = MakeStraightFrame();
    off.fk[1] = Vec3(0.5f, -2, 0);
    groups.clear();
    AccumulateChainFrame(chain, kNames, limits, off, nullptr, nullptr, 60.0f, groups);
    EXPECT_NEAR(groups.at("A").writebackErr[1], 0.5f, 1e-6f);
}

TEST(PhysicsDiagnosticsTests, BendDeltaIsSampledOnlyForJointsWithAParentJoint)
{
    const DynamicChainDefinition chain = MakeChain();
    ChainFrameSample frame = MakeStraightFrame();
    frame.display[2] = Vec3(1, -3, 0); // 45 degrees off the parent segment.

    const auto limits = FreeLimits();
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, frame, nullptr, nullptr, 60.0f, groups);

    ASSERT_EQ(groups.at("A").bendDeltaDeg.size(), 2u);
    EXPECT_NEAR(groups.at("A").bendDeltaDeg[0], 0.0f, 1e-3f);
    EXPECT_NEAR(groups.at("A").bendDeltaDeg[1], 45.0f, 0.01f);
}

ChainFrameSample MakeMovingFrame(int k, float localY)
{
    ChainFrameSample sample = MakeStraightFrame();
    sample.anchorWorld = Mat4::Translation(Vec3(5.0f * static_cast<float>(k), 0.0f, 0.0f));
    sample.fk[0] = sample.anchorWorld.TransformPoint(Vec3(0.0f, localY, 0.0f));
    return sample;
}

TEST(PhysicsDiagnosticsTests, AccelLocalIsZeroForConstantVelocityInAnchorSpace)
{
    const DynamicChainDefinition chain = MakeChain();
    const ChainFrameSample f0 = MakeMovingFrame(0, -1.0f);
    const ChainFrameSample f1 = MakeMovingFrame(1, -0.9f);
    const ChainFrameSample f2 = MakeMovingFrame(2, -0.8f);

    const auto limits = FreeLimits();
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, f2, &f1, &f0, 60.0f, groups);
    ASSERT_EQ(groups.at("A").accelLocal.size(), 3u);
    EXPECT_NEAR(groups.at("A").accelLocal[0], 0.0f, 1e-2f);
}

TEST(PhysicsDiagnosticsTests, AccelLocalOfAVelocityStepIsDeltaVelocityTimesHz)
{
    const DynamicChainDefinition chain = MakeChain();
    const ChainFrameSample f0 = MakeMovingFrame(0, -1.0f);
    const ChainFrameSample f1 = MakeMovingFrame(1, -1.0f);
    const ChainFrameSample f2 = MakeMovingFrame(2, -0.9f);

    const auto limits = FreeLimits();
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, f2, &f1, &f0, 60.0f, groups);
    // dv = 0.1 * 60 units/s, accel = dv * hz.
    EXPECT_NEAR(groups.at("A").accelLocal[0], 0.1f * 60.0f * 60.0f, 0.5f);
}

TEST(PhysicsDiagnosticsTests, AccelLocalIsSkippedWithoutHistory)
{
    const DynamicChainDefinition chain = MakeChain();
    const auto limits = FreeLimits();
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, MakeStraightFrame(), nullptr, nullptr, 60.0f, groups);
    EXPECT_TRUE(groups.at("A").accelLocal.empty());
}

TEST(PhysicsDiagnosticsTests, WeldedAggregateHoldsOnlyRootChildrenWithZeroLimit)
{
    const DynamicChainDefinition chain = MakeChain();
    ChainFrameSample frame = MakeStraightFrame();
    frame.display[0] = Vec3(0.1f, -1, 0);

    std::array<float, 3> limits = { 0.0f, 0.0f, kPi };
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, frame, nullptr, nullptr, 60.0f, groups);

    ASSERT_EQ(groups.count("WELDED_ROOT_CHILDREN"), 1u);
    const GroupSamples& welded = groups.at("WELDED_ROOT_CHILDREN");
    ASSERT_EQ(welded.distFromAnimated.size(), 1u);
    EXPECT_NEAR(welded.distFromAnimated[0], 0.1f, 1e-5f);
    EXPECT_EQ(welded.segDirErrDeg.size(), 1u);
    EXPECT_TRUE(welded.absStretch.empty());

    limits = { 10.0f * kDeg, 0.0f, 0.0f };
    groups.clear();
    AccumulateChainFrame(chain, kNames, limits, frame, nullptr, nullptr, 60.0f, groups);
    EXPECT_EQ(groups.count("WELDED_ROOT_CHILDREN"), 0u);
}

TEST(PhysicsDiagnosticsTests, MismatchedInputSizesAddNothing)
{
    const DynamicChainDefinition chain = MakeChain();
    ChainFrameSample frame = MakeStraightFrame();
    frame.display.pop_back();

    const auto limits = FreeLimits();
    std::map<std::string, GroupSamples> groups;
    AccumulateChainFrame(chain, kNames, limits, frame, nullptr, nullptr, 60.0f, groups);
    EXPECT_TRUE(groups.empty());
}

} // namespace
} // namespace gte
