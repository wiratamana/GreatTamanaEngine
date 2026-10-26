#include "Renderer/PresentRecoveryPlan.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte {
namespace {

using Step = PresentRecoveryStep;

std::vector<Step> Steps(const PresentRecoveryInput& in)
{
    const PresentRecoveryPlan plan = PlanPresentRecovery(in);
    return { plan.begin(), plan.end() };
}

PresentRecoveryInput Make(unsigned bits)
{
    PresentRecoveryInput in;
    in.deviceLost = (bits & 1u) != 0;
    in.imageAcquired = (bits & 2u) != 0;
    in.fenceReset = (bits & 4u) != 0;
    in.capturePending = (bits & 8u) != 0;
    return in;
}

TEST(PresentRecoveryPlanTests, ImageNotAcquiredHasNothingToReturn)
{
    EXPECT_TRUE(Steps(Make(0)).empty());
    EXPECT_EQ(Steps(Make(8)), std::vector<Step>{ Step::AbandonCapture });
}

TEST(PresentRecoveryPlanTests, LostDeviceNeverSubmits)
{
    EXPECT_TRUE(Steps(Make(1 | 2)).empty());
    EXPECT_EQ(Steps(Make(1 | 2 | 4 | 8)), std::vector<Step>{ Step::AbandonCapture });
}

TEST(PresentRecoveryPlanTests, AliveDeviceReturnsTheImageInOrder)
{
    const std::vector<Step> expected{ Step::ResetCommandBuffer, Step::TransitionToPresent, Step::ResetFence,
        Step::SubmitAndSignal, Step::Present, Step::AdvanceFrame };
    EXPECT_EQ(Steps(Make(2)), expected);
}

TEST(PresentRecoveryPlanTests, FenceIsResetOnlyOnce)
{
    const PresentRecoveryPlan plan = PlanPresentRecovery(Make(2 | 4));
    EXPECT_FALSE(plan.Contains(Step::ResetFence));
    EXPECT_TRUE(plan.Contains(Step::SubmitAndSignal));
}

TEST(PresentRecoveryPlanTests, PendingCaptureIsAbandonedFirst)
{
    const std::vector<Step> steps = Steps(Make(2 | 8));
    ASSERT_FALSE(steps.empty());
    EXPECT_EQ(steps.front(), Step::AbandonCapture);
    EXPECT_EQ(steps.size(), 7u);
}

TEST(PresentRecoveryPlanTests, EveryInputCombinationIsConsistent)
{
    for (unsigned bits = 0; bits < 16; ++bits) {
        const PresentRecoveryInput in = Make(bits);
        const PresentRecoveryPlan plan = PlanPresentRecovery(in);
        EXPECT_LE(plan.count, PresentRecoveryPlan::kMaxSteps) << bits;
        EXPECT_EQ(plan.Contains(Step::AbandonCapture), in.capturePending) << bits;

        const bool returnsImage = in.imageAcquired && !in.deviceLost;
        EXPECT_EQ(plan.Contains(Step::SubmitAndSignal), returnsImage) << bits;
        EXPECT_EQ(plan.Contains(Step::Present), returnsImage) << bits;
        EXPECT_EQ(plan.Contains(Step::AdvanceFrame), returnsImage) << bits;
        EXPECT_EQ(plan.Contains(Step::ResetFence), returnsImage && !in.fenceReset) << bits;
    }
}

} // namespace
} // namespace gte
