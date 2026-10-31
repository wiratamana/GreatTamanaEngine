// Tier-1 tests of the HasCapture()/Clear() state of a fresh FrameDebuggerCurrentCapture.
// CaptureFrame() needs a live Renderer and is not covered here.

#include "Editor/FrameDebuggerCapturedFrame.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(FrameDebuggerCurrentCaptureTest, FreshCaptureHasNoFrame)
{
    FrameDebuggerCurrentCapture capture;
    EXPECT_FALSE(capture.HasCapture());
    EXPECT_EQ(capture.CurrentFrame(), nullptr);
}

TEST(FrameDebuggerCurrentCaptureTest, ClearOnAFreshCaptureIsASafeNoOp)
{
    FrameDebuggerCurrentCapture capture;
    capture.Clear();
    EXPECT_FALSE(capture.HasCapture());
    EXPECT_EQ(capture.CurrentFrame(), nullptr);
}

} // namespace
} // namespace gte
