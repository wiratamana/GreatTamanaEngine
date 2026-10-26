// Timeout and device-lost policy of blocking GPU waits (src/Renderer/GpuWaitPolicy.h). No device involved.

#include "Renderer/GpuWaitPolicy.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace gte {
namespace {

TEST(GpuWaitPolicyTest, CompletedWaitsKeepTheDeviceAlive)
{
    GpuWaitPolicy policy;
    EXPECT_EQ(policy.Judge(WaitClass::Frame, BlockOutcome::Completed), BlockOutcome::Completed);
    EXPECT_EQ(policy.Judge(WaitClass::Upload, BlockOutcome::Completed), BlockOutcome::Completed);
    EXPECT_FALSE(policy.Lost());
}

TEST(GpuWaitPolicyTest, ThreeFrameTimeoutsAreToleratedAndTheFourthLosesTheDevice)
{
    GpuWaitPolicy policy;
    for (std::uint32_t i = 0; i < kToleratedFrameTimeouts; ++i) {
        EXPECT_EQ(policy.Judge(WaitClass::Frame, BlockOutcome::TimedOut), BlockOutcome::TimedOut);
        EXPECT_FALSE(policy.Lost());
    }
    EXPECT_EQ(policy.Judge(WaitClass::Frame, BlockOutcome::TimedOut), BlockOutcome::DeviceLost);
    EXPECT_TRUE(policy.Lost());
}

TEST(GpuWaitPolicyTest, ACompletedFrameWaitResetsTheTimeoutStreak)
{
    GpuWaitPolicy policy;
    for (int round = 0; round < 5; ++round) {
        EXPECT_EQ(policy.Judge(WaitClass::Frame, BlockOutcome::TimedOut), BlockOutcome::TimedOut);
        EXPECT_EQ(policy.Judge(WaitClass::Frame, BlockOutcome::TimedOut), BlockOutcome::TimedOut);
        EXPECT_EQ(policy.Judge(WaitClass::Frame, BlockOutcome::Completed), BlockOutcome::Completed);
        EXPECT_EQ(policy.ConsecutiveFrameTimeouts(), 0u);
    }
    EXPECT_FALSE(policy.Lost());
}

TEST(GpuWaitPolicyTest, AnUploadTimeoutLosesTheDeviceButStillReportsTimedOut)
{
    GpuWaitPolicy policy;
    EXPECT_EQ(policy.Judge(WaitClass::Upload, BlockOutcome::TimedOut), BlockOutcome::TimedOut);
    EXPECT_TRUE(policy.Lost());
}

TEST(GpuWaitPolicyTest, AnUploadFailureLosesTheDevice)
{
    GpuWaitPolicy policy;
    EXPECT_EQ(policy.Judge(WaitClass::Upload, BlockOutcome::Failed), BlockOutcome::Failed);
    EXPECT_TRUE(policy.Lost());
}

TEST(GpuWaitPolicyTest, AFrameFailureAloneDoesNotLoseTheDevice)
{
    GpuWaitPolicy policy;
    EXPECT_EQ(policy.Judge(WaitClass::Frame, BlockOutcome::Failed), BlockOutcome::Failed);
    EXPECT_FALSE(policy.Lost());
}

TEST(GpuWaitPolicyTest, ADriverDeviceLostResultLosesTheDevice)
{
    GpuWaitPolicy policy;
    EXPECT_EQ(policy.Judge(WaitClass::Frame, BlockOutcome::DeviceLost), BlockOutcome::DeviceLost);
    EXPECT_TRUE(policy.Lost());
}

TEST(GpuWaitPolicyTest, ALostDeviceShortCircuitsEveryLaterWait)
{
    GpuWaitPolicy policy;
    policy.MarkLost();
    for (const WaitClass waitClass : { WaitClass::Frame, WaitClass::Upload }) {
        for (const BlockOutcome raw :
            { BlockOutcome::Completed, BlockOutcome::TimedOut, BlockOutcome::Failed, BlockOutcome::DeviceLost }) {
            EXPECT_EQ(policy.Judge(waitClass, raw), BlockOutcome::DeviceLost);
        }
    }
}

TEST(GpuWaitPolicyTest, AFailedUploadWhileAlreadyLostStaysDeviceLost)
{
    GpuWaitPolicy policy;
    policy.MarkLost();
    EXPECT_EQ(policy.Judge(WaitClass::Upload, BlockOutcome::Failed), BlockOutcome::DeviceLost);
    EXPECT_TRUE(policy.Lost());
}

TEST(GpuWaitPolicyTest, ConsecutiveFrameTimeoutsCountUpAndResetOnACompletedFrameWait)
{
    GpuWaitPolicy policy;
    EXPECT_EQ(policy.ConsecutiveFrameTimeouts(), 0u);
    static_cast<void>(policy.Judge(WaitClass::Frame, BlockOutcome::TimedOut));
    static_cast<void>(policy.Judge(WaitClass::Frame, BlockOutcome::TimedOut));
    EXPECT_EQ(policy.ConsecutiveFrameTimeouts(), 2u);

    // An upload wait says nothing about frame pacing.
    static_cast<void>(policy.Judge(WaitClass::Upload, BlockOutcome::Completed));
    EXPECT_EQ(policy.ConsecutiveFrameTimeouts(), 2u);

    static_cast<void>(policy.Judge(WaitClass::Frame, BlockOutcome::Completed));
    EXPECT_EQ(policy.ConsecutiveFrameTimeouts(), 0u);
}

TEST(GpuWaitPolicyTest, DefaultTimeoutsAreBounded)
{
    EXPECT_GT(kFrameWaitTimeout.count(), 0);
    EXPECT_GT(kAcquireTimeout.count(), 0);
    EXPECT_GT(kUploadWaitTimeout.count(), 0);
    EXPECT_GT(kIdleWaitTimeout.count(), 0);
    EXPECT_LE(kIdleWaitTimeout, std::chrono::seconds(60));
}

TEST(GpuWaitPolicyTest, EveryBlockSiteHasADistinctName)
{
    std::set<std::string> names;
    for (std::uint32_t i = 0; i < kBlockSiteCount; ++i) {
        const std::string name = ToString(static_cast<BlockSite>(i));
        EXPECT_NE(name, "Unknown");
        names.insert(name);
    }
    EXPECT_EQ(names.size(), kBlockSiteCount);
}

} // namespace
} // namespace gte
