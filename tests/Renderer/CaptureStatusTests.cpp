// Tier 1 tests for the mapping from a GPU wait outcome to a capture status.

#include "Renderer/CaptureResult.h"
#include "Renderer/GpuWaitFailure.h"

#include <gtest/gtest.h>

namespace {

using namespace gte;

TEST(CaptureStatusTests, EveryWaitOutcomeHasItsOwnStatus)
{
    EXPECT_EQ(CaptureStatusFromBlockOutcome(BlockOutcome::Completed), CaptureStatus::Ok);
    EXPECT_EQ(CaptureStatusFromBlockOutcome(BlockOutcome::TimedOut), CaptureStatus::GpuBusy);
    EXPECT_EQ(CaptureStatusFromBlockOutcome(BlockOutcome::DeviceLost), CaptureStatus::DeviceLost);
    EXPECT_EQ(CaptureStatusFromBlockOutcome(BlockOutcome::Failed), CaptureStatus::Failed);
}

TEST(CaptureStatusTests, ResultDefaultsToFailedWithNoPixels)
{
    const CaptureResult result;
    EXPECT_EQ(result.status, CaptureStatus::Failed);
    EXPECT_TRUE(result.pixels.pixels.empty());
}

TEST(CaptureStatusTests, WaitFailureCarriesItsOutcome)
{
    try {
        throw GpuWaitFailure("wait", BlockOutcome::TimedOut);
    } catch (const std::runtime_error& e) {
        const auto* failure = dynamic_cast<const GpuWaitFailure*>(&e);
        ASSERT_NE(failure, nullptr);
        EXPECT_EQ(CaptureStatusFromBlockOutcome(failure->Outcome()), CaptureStatus::GpuBusy);
    }
}

} // namespace
