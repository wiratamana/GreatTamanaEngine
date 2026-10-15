// Tier-1 tests for the lazy-build retry gate.

#include "Renderer/RenderGraph/BuildRetryGate.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>

namespace gte::rg {
namespace {

using FailureKind = BuildRetryGate::FailureKind;

// Frames from `failFrame` until the gate lets the next attempt through.
std::uint64_t CooldownAfter(const BuildRetryGate& gate, std::uint64_t failFrame)
{
    std::uint64_t frame = failFrame;
    while (!gate.ShouldAttempt(frame)) {
        ++frame;
    }
    return frame - failFrame;
}

TEST(BuildRetryGateTest, FreshGateAttemptsAtAnyFrame)
{
    const BuildRetryGate gate;
    EXPECT_TRUE(gate.ShouldAttempt(0));
    EXPECT_TRUE(gate.ShouldAttempt(12345));
}

TEST(BuildRetryGateTest, FirstFailureBlocksForInitialCooldown)
{
    BuildRetryGate gate;
    EXPECT_EQ(gate.OnFailure(1000), FailureKind::First);
    EXPECT_FALSE(gate.ShouldAttempt(1000));
    EXPECT_FALSE(gate.ShouldAttempt(1000 + BuildRetryGate::kInitialCooldownFrames - 1));
    EXPECT_TRUE(gate.ShouldAttempt(1000 + BuildRetryGate::kInitialCooldownFrames));
}

TEST(BuildRetryGateTest, CooldownDoublesThenStaysAtTheCap)
{
    BuildRetryGate gate;
    const std::uint64_t expected[] = { 120, 240, 480, 960, 1800, 1800, 1800 };
    for (std::size_t i = 0; i < std::size(expected); ++i) {
        EXPECT_EQ(gate.OnFailure(0), i == 0 ? FailureKind::First : FailureKind::Repeat);
        EXPECT_EQ(CooldownAfter(gate, 0), expected[i]) << "failure " << i;
    }
}

TEST(BuildRetryGateTest, NeverPermanentlyBlockedAfterManyFailures)
{
    BuildRetryGate gate;
    std::uint64_t frame = 0;
    for (int i = 0; i < 1000; ++i) {
        gate.OnFailure(frame);
        const std::uint64_t cooldown = CooldownAfter(gate, frame);
        ASSERT_LE(cooldown, BuildRetryGate::kMaxCooldownFrames);
        frame += cooldown;
    }
}

TEST(BuildRetryGateTest, SuccessAfterFailureReportsRecoveryOnce)
{
    BuildRetryGate gate;
    gate.OnFailure(10);
    EXPECT_TRUE(gate.OnSuccess());
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
    EXPECT_EQ(CooldownAfter(gate, 500), BuildRetryGate::kInitialCooldownFrames);
}

TEST(BuildRetryGateTest, FrameCounterNearMaxSaturates)
{
    BuildRetryGate gate;
    const std::uint64_t maxFrame = std::numeric_limits<std::uint64_t>::max();
    gate.OnFailure(maxFrame - 1);
    EXPECT_FALSE(gate.ShouldAttempt(maxFrame - 1));
    EXPECT_TRUE(gate.ShouldAttempt(maxFrame));
}

} // namespace
} // namespace gte::rg
