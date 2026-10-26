// Tier 1 tests for how a failed host capture is reported over HTTP.

#include "Editor/CaptureFailureReason.h"
#include "Network/NetworkCaptureFailure.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace gte;
using namespace gte::Network;

TEST(NetworkCaptureFailureTests, GpuFailuresAnswer503)
{
    EXPECT_EQ(HttpStatusForCaptureFailure(FrameCaptureFailureReason::GpuBusy), 503);
    EXPECT_EQ(HttpStatusForCaptureFailure(FrameCaptureFailureReason::DeviceLost), 503);
}

TEST(NetworkCaptureFailureTests, OtherReasonsKeepTheirStatus)
{
    EXPECT_EQ(HttpStatusForCaptureFailure(FrameCaptureFailureReason::TimedOut), 504);
    EXPECT_EQ(HttpStatusForCaptureFailure(FrameCaptureFailureReason::TargetNotAvailable), 409);
}

TEST(NetworkCaptureFailureTests, GpuFailuresNameTheRealCause)
{
    EXPECT_NE(std::string(GpuFailureText(FrameCaptureFailureReason::GpuBusy)), "");
    EXPECT_EQ(std::string(GpuFailureText(FrameCaptureFailureReason::DeviceLost)), "GPU device lost, rendering stopped");
    EXPECT_EQ(std::string(GpuFailureText(FrameCaptureFailureReason::TimedOut)), "");
    EXPECT_EQ(std::string(GpuFailureText(FrameCaptureFailureReason::TargetNotAvailable)), "");
}

TEST(NetworkCaptureFailureTests, CaptureStatusMapsToTheReportedReason)
{
    EXPECT_EQ(FailureReasonForCapture(CaptureStatus::GpuBusy), FrameCaptureFailureReason::GpuBusy);
    EXPECT_EQ(FailureReasonForCapture(CaptureStatus::DeviceLost), FrameCaptureFailureReason::DeviceLost);
    EXPECT_EQ(FailureReasonForCapture(CaptureStatus::NothingToCapture), FrameCaptureFailureReason::TargetNotAvailable);
    EXPECT_EQ(FailureReasonForCapture(CaptureStatus::Failed), FrameCaptureFailureReason::TargetNotAvailable);
}

} // namespace
