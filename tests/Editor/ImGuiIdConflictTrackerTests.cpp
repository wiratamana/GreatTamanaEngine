// Unit tests for ImGuiIdConflictTracker (src/Editor/ImGuiIdConflictTracker.h)
// - task_manager/editor-core-separation-10 campaign, PHASE1
// (PHASE1_ID_CONFLICT_DETECTION_FOUNDATION.md). Genuinely Tier 1: exercises
// pure logic over plain std::uint32_t values - no live ImGui context or
// Logger sink needed at all (see ImGuiIdConflictTracker.h's own class
// comment for why this class is deliberately this narrow).

#include "Editor/ImGuiIdConflictTracker.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(ImGuiIdConflictTrackerTest, FirstRegistrationOfAFreshIdIsNotAConflict)
{
    ImGuiIdConflictTracker tracker;

    EXPECT_FALSE(tracker.RegisterAndCheckConflict(42));
    EXPECT_EQ(tracker.DistinctIdCount(), 1u);
}

TEST(ImGuiIdConflictTrackerTest, SecondRegistrationOfTheSameIdIsAConflict)
{
    ImGuiIdConflictTracker tracker;

    EXPECT_FALSE(tracker.RegisterAndCheckConflict(42));
    EXPECT_TRUE(tracker.RegisterAndCheckConflict(42));
}

TEST(ImGuiIdConflictTrackerTest, ThirdRegistrationOfTheSameIdIsAlsoAConflict)
{
    ImGuiIdConflictTracker tracker;

    EXPECT_FALSE(tracker.RegisterAndCheckConflict(7));
    EXPECT_TRUE(tracker.RegisterAndCheckConflict(7));
    EXPECT_TRUE(tracker.RegisterAndCheckConflict(7));
}

TEST(ImGuiIdConflictTrackerTest, ResetClearsSeenIdsSoAPreviouslyConflictingIdIsFreshAgain)
{
    ImGuiIdConflictTracker tracker;

    EXPECT_FALSE(tracker.RegisterAndCheckConflict(99));
    EXPECT_TRUE(tracker.RegisterAndCheckConflict(99));

    tracker.Reset();

    EXPECT_FALSE(tracker.RegisterAndCheckConflict(99));
}

TEST(ImGuiIdConflictTrackerTest, TwoDifferentIdsNeverConflictWithEachOther)
{
    ImGuiIdConflictTracker tracker;

    EXPECT_FALSE(tracker.RegisterAndCheckConflict(1));
    EXPECT_FALSE(tracker.RegisterAndCheckConflict(2));
}

TEST(ImGuiIdConflictTrackerTest, DistinctIdCountReflectsAMixOfRepeatsAndFreshIds)
{
    ImGuiIdConflictTracker tracker;

    tracker.RegisterAndCheckConflict(1);
    tracker.RegisterAndCheckConflict(2);
    tracker.RegisterAndCheckConflict(1); // repeat - conflict, not a new distinct id
    tracker.RegisterAndCheckConflict(3);

    EXPECT_EQ(tracker.DistinctIdCount(), 3u);
}

} // namespace
} // namespace gte
