// Unit tests for the Frame Debugger's single-capture lifecycle
// (src/Editor/FrameDebuggerHistory.h) - covers only the PURE
// HasCapture()/Clear() state machine of a fresh, never-captured-into
// FrameDebuggerCurrentCapture object - no live Renderer/RenderTexture/
// VkDevice is exercised here, since FrameDebuggerCurrentCapture::
// CaptureFrame() itself is inherently Tier 2 (see AGENTS.md, "Testability &
// Regression Safety") - it needs a live Renderer/RenderGraph to call into,
// so it stays untested here exactly like the old 8-slot ring buffer's own
// CaptureFrame() body always was. Only built when GTE_ENABLE_EDITOR is ON,
// since FrameDebuggerHistory.h/.cpp are only compiled into gte_core then.
//
// task_manager/frame-debugger-7 campaign, PHASE1
// (PHASE1_REMOVE_HISTORY_AND_SINGLE_CAPTURE_LIFECYCLE.md) - REPLACES the old
// 8-slot ring-buffer tests (AdvanceFrameDebuggerHistoryWriteState()/
// ClampFrameDebuggerHistoryCursor() - both REMOVED, along with the
// ring-buffer arithmetic they used to cover) with this smaller, single-slot
// coverage.

#include "Editor/FrameDebuggerHistory.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(FrameDebuggerCurrentCaptureTest, FreshCaptureHasNoEntry)
{
    FrameDebuggerCurrentCapture capture;
    EXPECT_FALSE(capture.HasCapture());
    EXPECT_EQ(capture.CurrentEntry(), nullptr);
}

TEST(FrameDebuggerCurrentCaptureTest, ClearOnAFreshCaptureIsASafeNoOp)
{
    FrameDebuggerCurrentCapture capture;
    capture.Clear();
    EXPECT_FALSE(capture.HasCapture());
    EXPECT_EQ(capture.CurrentEntry(), nullptr);
}

} // namespace
} // namespace gte
