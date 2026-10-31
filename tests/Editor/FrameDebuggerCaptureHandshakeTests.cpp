// Tier 1: capture handshake phases and the status word.
#include "Editor/FrameDebuggerCaptureHandshake.h"

#include <gtest/gtest.h>

namespace gte {

using Phase = FrameDebuggerCapturePhase;

TEST(FrameDebuggerCaptureHandshakeTest, StartsIdle)
{
    FrameDebuggerCaptureHandshake handshake;
    EXPECT_EQ(handshake.Phase(), Phase::Idle);
}

TEST(FrameDebuggerCaptureHandshakeTest, RequestMovesIdleToRequested)
{
    FrameDebuggerCaptureHandshake handshake;
    handshake.Request();
    EXPECT_EQ(handshake.Phase(), Phase::Requested);
}

TEST(FrameDebuggerCaptureHandshakeTest, RequestIsIdempotent)
{
    FrameDebuggerCaptureHandshake handshake;
    handshake.Request();
    handshake.Request();
    EXPECT_EQ(handshake.Phase(), Phase::Requested);
}

TEST(FrameDebuggerCaptureHandshakeTest, FullCycle)
{
    FrameDebuggerCaptureHandshake handshake;
    handshake.Request();
    EXPECT_TRUE(handshake.ConsumeForReplay());
    EXPECT_EQ(handshake.Phase(), Phase::ReplayDeclared);
    EXPECT_FALSE(handshake.ConsumeForReplay());
    EXPECT_TRUE(handshake.ConsumeForCapture());
    EXPECT_EQ(handshake.Phase(), Phase::Idle);
    EXPECT_FALSE(handshake.ConsumeForCapture());
}

TEST(FrameDebuggerCaptureHandshakeTest, ConsumeForReplayIsFalseWithoutRequest)
{
    FrameDebuggerCaptureHandshake handshake;
    EXPECT_FALSE(handshake.ConsumeForReplay());
    EXPECT_EQ(handshake.Phase(), Phase::Idle);
}

TEST(FrameDebuggerCaptureHandshakeTest, ConsumeForCaptureIsFalseBeforeReplay)
{
    FrameDebuggerCaptureHandshake handshake;
    handshake.Request();
    EXPECT_FALSE(handshake.ConsumeForCapture());
    EXPECT_EQ(handshake.Phase(), Phase::Requested);
}

TEST(FrameDebuggerCaptureHandshakeTest, RequestDuringReplayKeepsTheRunningCapture)
{
    FrameDebuggerCaptureHandshake handshake;
    handshake.Request();
    EXPECT_TRUE(handshake.ConsumeForReplay());
    handshake.Request();
    EXPECT_EQ(handshake.Phase(), Phase::ReplayDeclared);
}

TEST(FrameDebuggerCaptureHandshakeTest, NotifyFrameFailedFailsOnlyAnInFlightRequest)
{
    FrameDebuggerCaptureHandshake idle;
    EXPECT_FALSE(idle.NotifyFrameFailed());
    EXPECT_EQ(idle.Phase(), Phase::Idle);

    FrameDebuggerCaptureHandshake requested;
    requested.Request();
    EXPECT_TRUE(requested.NotifyFrameFailed());
    EXPECT_EQ(requested.Phase(), Phase::Failed);

    FrameDebuggerCaptureHandshake declared;
    declared.Request();
    EXPECT_TRUE(declared.ConsumeForReplay());
    EXPECT_TRUE(declared.NotifyFrameFailed());
    EXPECT_EQ(declared.Phase(), Phase::Failed);

    EXPECT_FALSE(declared.NotifyFrameFailed());
    EXPECT_EQ(declared.Phase(), Phase::Failed);
}

TEST(FrameDebuggerCaptureHandshakeTest, FailIsUnconditional)
{
    FrameDebuggerCaptureHandshake handshake;
    handshake.Fail();
    EXPECT_EQ(handshake.Phase(), Phase::Failed);
}

TEST(FrameDebuggerCaptureHandshakeTest, RequestLeavesFailed)
{
    FrameDebuggerCaptureHandshake handshake;
    handshake.Fail();
    handshake.Request();
    EXPECT_EQ(handshake.Phase(), Phase::Requested);
}

TEST(FrameDebuggerCaptureHandshakeTest, CancelDropsEverything)
{
    FrameDebuggerCaptureHandshake idle;
    idle.Cancel();
    EXPECT_EQ(idle.Phase(), Phase::Idle);

    FrameDebuggerCaptureHandshake requested;
    requested.Request();
    requested.Cancel();
    EXPECT_EQ(requested.Phase(), Phase::Idle);

    FrameDebuggerCaptureHandshake declared;
    declared.Request();
    EXPECT_TRUE(declared.ConsumeForReplay());
    declared.Cancel();
    EXPECT_EQ(declared.Phase(), Phase::Idle);

    FrameDebuggerCaptureHandshake failed;
    failed.Fail();
    failed.Cancel();
    EXPECT_EQ(failed.Phase(), Phase::Idle);
}

TEST(FrameDebuggerCaptureStatusNameTest, MapsEnabledPhaseAndCaptureToText)
{
    for (const Phase phase : {Phase::Idle, Phase::Requested, Phase::ReplayDeclared, Phase::Failed}) {
        EXPECT_STREQ(FrameDebuggerCaptureStatusName(false, phase, false), "disabled");
        EXPECT_STREQ(FrameDebuggerCaptureStatusName(false, phase, true), "disabled");
    }
    EXPECT_STREQ(FrameDebuggerCaptureStatusName(true, Phase::Failed, false), "failed");
    EXPECT_STREQ(FrameDebuggerCaptureStatusName(true, Phase::Failed, true), "failed");
    EXPECT_STREQ(FrameDebuggerCaptureStatusName(true, Phase::Requested, false), "pending");
    EXPECT_STREQ(FrameDebuggerCaptureStatusName(true, Phase::Requested, true), "pending");
    EXPECT_STREQ(FrameDebuggerCaptureStatusName(true, Phase::ReplayDeclared, false), "pending");
    EXPECT_STREQ(FrameDebuggerCaptureStatusName(true, Phase::ReplayDeclared, true), "pending");
    EXPECT_STREQ(FrameDebuggerCaptureStatusName(true, Phase::Idle, true), "captured");
    EXPECT_STREQ(FrameDebuggerCaptureStatusName(true, Phase::Idle, false), "pending");
}

} // namespace gte
