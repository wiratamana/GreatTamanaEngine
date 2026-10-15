// Tier-1 tests for the lazy-build retry gate.

#include "Renderer/RenderGraph/BuildRetryGate.h"

#include <gtest/gtest.h>

#include <limits>

namespace gte::rg {
namespace {

using FailureKind = BuildRetryGate::FailureKind;

TEST(BuildRetryGateTest, FreshGateAttemptsAtAnyFrame)
{
    const BuildRetryGate gate;
    EXPECT_TRUE(gate.ShouldAttempt(0));
    EXPECT_TRUE(gate.ShouldAttempt(12345));
    EXPECT_FALSE(gate.IsFailing());
}

TEST(BuildRetryGateTest, FirstFailureBlocksForInitialCooldown)
{
    BuildRetryGate gate;
    EXPECT_EQ(gate.OnFailure(1000), FailureKind::First);
    EXPECT_TRUE(gate.IsFailing());
    EXPECT_FALSE(gate.ShouldAttempt(1000));
    EXPECT_FALSE(gate.ShouldAttempt(1000 + BuildRetryGate::kInitialCooldownFrames - 1));
    EXPECT_TRUE(gate.ShouldAttempt(1000 + BuildRetryGate::kInitialCooldownFrames));
}

TEST(BuildRetryGateTest, RepeatFailureDoublesCooldown)
{
    BuildRetryGate gate;
    gate.OnFailure(0);
    EXPECT_EQ(gate.OnFailure(120), FailureKind::Repeat);
    EXPECT_EQ(gate.CooldownFrames(), 240u);
    EXPECT_FALSE(gate.ShouldAttempt(120 + 239));
    EXPECT_TRUE(gate.ShouldAttempt(120 + 240));
}

TEST(BuildRetryGateTest, CooldownIsCapped)
{
    BuildRetryGate gate;
    std::uint64_t frame = 0;
    for (int i = 0; i < 20; ++i) {
        gate.OnFailure(frame);
        frame = gate.NextAttemptFrame();
    }
    EXPECT_EQ(gate.CooldownFrames(), BuildRetryGate::kMaxCooldownFrames);
}

TEST(BuildRetryGateTest, CooldownSequenceReachesCapWithoutOvershoot)
{
    BuildRetryGate gate;
    const std::uint64_t expected[] = { 120, 240, 480, 960, 1800, 1800 };
    for (const std::uint64_t cooldown : expected) {
        gate.OnFailure(0);
        EXPECT_EQ(gate.CooldownFrames(), cooldown);
    }
}

TEST(BuildRetryGateTest, SuccessAfterFailureReportsRecoveryOnce)
{
    BuildRetryGate gate;
    gate.OnFailure(10);
    EXPECT_TRUE(gate.OnSuccess());
    EXPECT_FALSE(gate.IsFailing());
    EXPECT_TRUE(gate.ShouldAttempt(10));
    EXPECT_FALSE(gate.OnSuccess());
}

TEST(BuildRetryGateTest, SuccessWithoutFailureIsNotARecovery)
{
    BuildRetryGate gate;
    EXPECT_FALSE(gate.OnSuccess());
}

TEST(BuildRetryGateTest, SuccessResetsCooldownSoNextFailureIsFirstAgain)
{
    BuildRetryGate gate;
    gate.OnFailure(0);
    gate.OnFailure(120);
    gate.OnSuccess();
    EXPECT_EQ(gate.OnFailure(500), FailureKind::First);
    EXPECT_EQ(gate.CooldownFrames(), BuildRetryGate::kInitialCooldownFrames);
}

TEST(BuildRetryGateTest, NeverPermanentlyBlockedAfterManyFailures)
{
    BuildRetryGate gate;
    std::uint64_t frame = 0;
    for (int i = 0; i < 1000; ++i) {
        gate.OnFailure(frame);
        frame = gate.NextAttemptFrame();
        ASSERT_TRUE(gate.ShouldAttempt(frame));
    }
    EXPECT_LE(gate.CooldownFrames(), BuildRetryGate::kMaxCooldownFrames);
}

TEST(BuildRetryGateTest, FrameCounterNearMaxSaturates)
{
    BuildRetryGate gate;
    const std::uint64_t maxFrame = std::numeric_limits<std::uint64_t>::max();
    gate.OnFailure(maxFrame - 1);
    EXPECT_EQ(gate.NextAttemptFrame(), maxFrame);
    EXPECT_TRUE(gate.ShouldAttempt(maxFrame));
}

} // namespace
} // namespace gte::rg
