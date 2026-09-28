// tests/Core/Plugins/ProjectAssemblyHotReloadDebugStatusTests.cpp
//
// editor-core-separation-12 campaign, PHASE1 (Project Assembly Hot Reload
// plan, BIG-STEP 1) - Tier-1 tests for the new, pure
// ProjectAssemblyHotReloadDebugStatus singleton (plain data + a mutex, no
// live VkDevice/SDL window needed - genuinely Tier-1-testable per
// AGENTS.md's own testability rule).
//
// NOTE: ProjectAssemblyHotReloadDebugStatus::Instance() is a real, single,
// process-wide Meyers singleton - every TEST_F/TEST body below shares the
// SAME instance across the whole test binary. Each test therefore only
// asserts INCREMENTAL, order-independent facts (cycleId strictly increases,
// never resets) rather than assuming a pristine "cycleId == 0" starting
// point, since GoogleTest does not guarantee this file's tests run before
// any other test that might also touch this singleton.

#include "Core/Plugins/ProjectAssemblyHotReloadDebugStatus.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(ProjectAssemblyHotReloadDebugStatusTest, FreshInstance_ReportsIdleByDefault)
{
    ProjectAssemblyHotReloadDebugStatus& status = ProjectAssemblyHotReloadDebugStatus::Instance();
    IHotReloadDebugCapability::Status snapshot = status.GetSnapshot();
    // Not necessarily the VERY first snapshot ever taken (see this file's
    // own header note), but whenever no cycle is in flight, phase must read
    // "Idle" - true both at process start and after Finish() below.
    EXPECT_EQ(snapshot.phase, "Idle");
}

TEST(ProjectAssemblyHotReloadDebugStatusTest, Set_ThenGetSnapshot_ReportsThatPhaseAndProjectAndAssignsANewCycleId)
{
    ProjectAssemblyHotReloadDebugStatus& status = ProjectAssemblyHotReloadDebugStatus::Instance();
    // Ensure we start this specific test from a known Idle baseline,
    // regardless of what any earlier test in this binary already did.
    status.Finish("Success", "");

    status.Set("Compiling", "Foo");
    IHotReloadDebugCapability::Status snapshot = status.GetSnapshot();
    EXPECT_EQ(snapshot.phase, "Compiling");
    EXPECT_EQ(snapshot.projectName, "Foo");
    const std::uint64_t firstCycleId = snapshot.cycleId;
    EXPECT_GT(firstCycleId, 0u);

    // A SECOND Set() call, same cycle (no Idle in between), must NOT
    // increment cycleId again.
    status.Set("Unloading", "Foo");
    snapshot = status.GetSnapshot();
    EXPECT_EQ(snapshot.phase, "Unloading");
    EXPECT_EQ(snapshot.projectName, "Foo");
    EXPECT_EQ(snapshot.cycleId, firstCycleId);

    // Finish() resets phase back to Idle and records the outcome.
    status.Finish("Success", "");
    snapshot = status.GetSnapshot();
    EXPECT_EQ(snapshot.phase, "Idle");
    EXPECT_EQ(snapshot.lastOutcome, "Success");
    EXPECT_EQ(snapshot.lastErrorMessage, "");

    // A subsequent Set() after Finish() starts a genuinely NEW cycle -
    // cycleId must increment.
    status.Set("Compiling", "Foo");
    snapshot = status.GetSnapshot();
    EXPECT_EQ(snapshot.cycleId, firstCycleId + 1u);

    // Leave the singleton back in a clean Idle state for any later test.
    status.Finish("Success", "");
}

TEST(ProjectAssemblyHotReloadDebugStatusTest, Finish_WithErrorMessage_RecordsBothOutcomeAndErrorMessage)
{
    ProjectAssemblyHotReloadDebugStatus& status = ProjectAssemblyHotReloadDebugStatus::Instance();
    status.Set("Compiling", "Bar");
    status.Finish("CriticalFailure", "compile failed: syntax error");

    IHotReloadDebugCapability::Status snapshot = status.GetSnapshot();
    EXPECT_EQ(snapshot.phase, "Idle");
    EXPECT_EQ(snapshot.lastOutcome, "CriticalFailure");
    EXPECT_EQ(snapshot.lastErrorMessage, "compile failed: syntax error");
}

} // namespace
} // namespace gte
