// Tier 1 tests for ExitGate.

#include "Editor/ExitGate.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

constexpr std::uint64_t kMs = 1'000'000;

TEST(ExitGateTest, NeverDueWhenNotArmed)
{
    const ExitGate gate;
    EXPECT_FALSE(gate.Due(1'000'000, 1'000'000'000'000));
}

TEST(ExitGateTest, DueOnlyWhenFramesAndTimeBothPassed)
{
    ExitGate gate;
    gate.Arm(10, 1000 * kMs);

    EXPECT_FALSE(gate.Due(12, 2000 * kMs)); // time passed, frames not
    EXPECT_FALSE(gate.Due(100, 1099 * kMs)); // frames passed, time not
    EXPECT_TRUE(gate.Due(13, 1100 * kMs));
    EXPECT_TRUE(gate.Due(500, 5000 * kMs));
}

TEST(ExitGateTest, ReArmKeepsTheFirstDeadline)
{
    ExitGate gate;
    gate.Arm(10, 1000 * kMs);
    gate.Arm(50, 9000 * kMs);

    EXPECT_TRUE(gate.Due(13, 1100 * kMs));
}

} // namespace
} // namespace gte
