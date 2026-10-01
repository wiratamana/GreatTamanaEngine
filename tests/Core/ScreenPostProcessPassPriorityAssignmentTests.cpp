// Tier-1 tests for src/Core/ScreenPostProcessPassPriorityAssignment.h's
// NextAutoScreenPostProcessPassPriority() - the pure counter-increment logic
// backing Core::AddScreenPostProcessPass()'s own RUNTIME auto-priority
// assignment (better-render-pass-1 campaign, PHASE9, Part B, Decision D3,
// Step 5).
#include "Core/ScreenPostProcessPassPriorityAssignment.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace gte {
namespace {

TEST(ScreenPostProcessPassPriorityAssignmentTest, FirstCallReturnsZeroAndIncrementsCounterToOne)
{
    std::int32_t counter = 0;
    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counter), 0);
    EXPECT_EQ(counter, 1);
}

TEST(ScreenPostProcessPassPriorityAssignmentTest, RepeatedCallsIncrementMonotonicallyWithNoGapsOrRepeats)
{
    std::int32_t counter = 0;
    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counter), 0);
    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counter), 1);
    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counter), 2);
    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counter), 3);
    EXPECT_EQ(counter, 4);
}

// A counter that already started somewhere other than zero (mirrors the
// real, process-wide, never-reset function-local `static` counter inside
// Core::AddScreenPostProcessPass() after some number of prior calls) keeps
// incrementing correctly from wherever it already was - this function never
// assumes/resets to zero itself.
TEST(ScreenPostProcessPassPriorityAssignmentTest, ContinuesCorrectlyFromANonZeroStartingValue)
{
    std::int32_t counter = 41;
    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counter), 41);
    EXPECT_EQ(counter, 42);
    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counter), 42);
    EXPECT_EQ(counter, 43);
}

// Two INDEPENDENT counters never interfere with each other - this function
// only ever touches the one counter it was handed by reference.
TEST(ScreenPostProcessPassPriorityAssignmentTest, TwoIndependentCountersDoNotInterfere)
{
    std::int32_t counterA = 0;
    std::int32_t counterB = 100;

    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counterA), 0);
    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counterB), 100);
    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counterA), 1);
    EXPECT_EQ(NextAutoScreenPostProcessPassPriority(counterB), 101);

    EXPECT_EQ(counterA, 2);
    EXPECT_EQ(counterB, 102);
}

} // namespace
} // namespace gte
