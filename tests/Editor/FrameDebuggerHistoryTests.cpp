// Unit tests for the Frame Debugger's real ring-buffer history
// (src/Editor/FrameDebuggerHistory.h) - covers only the PURE cursor/count/
// eviction arithmetic (AdvanceFrameDebuggerHistoryWriteState()/
// ClampFrameDebuggerHistoryCursor(), plus a fresh, never-captured-into
// FrameDebuggerHistory object's own default state) - no live Renderer/
// RenderTexture/VkDevice is exercised here, since FrameDebuggerHistory::
// CaptureFrame() itself is inherently Tier 2 (see AGENTS.md, "Testability &
// Regression Safety" and this phase's own Step 3.6). Only built when
// GTE_ENABLE_EDITOR is ON, since FrameDebuggerHistory.h/.cpp are only
// compiled into gte_core then.
//
// task_manager/frame-debugger-3 campaign, PHASE3
// (PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md).

#include "Editor/FrameDebuggerHistory.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(FrameDebuggerHistoryTest, FreshHistoryIsEmpty)
{
    FrameDebuggerHistory history;
    EXPECT_EQ(history.Count(), 0);
    EXPECT_EQ(history.CursorIndex(), 0);
    EXPECT_EQ(history.CurrentEntry(), nullptr);
}

TEST(FrameDebuggerHistoryTest, StepCursorOnEmptyHistoryIsNoOp)
{
    FrameDebuggerHistory history;
    history.StepCursor(1);
    EXPECT_EQ(history.CurrentEntry(), nullptr);
    history.StepCursor(-1);
    EXPECT_EQ(history.CurrentEntry(), nullptr);
}

TEST(FrameDebuggerHistoryWriteStateTest, GrowsByOnePerAdvanceUntilCapacity)
{
    constexpr int kCapacity = FrameDebuggerHistory::kCapacity;
    FrameDebuggerHistoryWriteState state;

    for (int i = 0; i < kCapacity; ++i) {
        state = AdvanceFrameDebuggerHistoryWriteState(state, kCapacity);
        EXPECT_EQ(state.count, i + 1);
        EXPECT_EQ(state.nextWriteIndex, (i + 1) % kCapacity);
    }
    EXPECT_EQ(state.count, kCapacity);
}

TEST(FrameDebuggerHistoryWriteStateTest, PinsCountAtCapacityAndKeepsWrappingAfterward)
{
    constexpr int kCapacity = FrameDebuggerHistory::kCapacity;
    FrameDebuggerHistoryWriteState state;

    // Fill it up first.
    for (int i = 0; i < kCapacity; ++i) {
        state = AdvanceFrameDebuggerHistoryWriteState(state, kCapacity);
    }
    ASSERT_EQ(state.count, kCapacity);
    ASSERT_EQ(state.nextWriteIndex, 0);

    // Capturing MORE than kCapacity evicts the oldest, keeps Count() ==
    // kCapacity, and nextWriteIndex keeps circling through every slot.
    for (int i = 0; i < kCapacity * 2; ++i) {
        state = AdvanceFrameDebuggerHistoryWriteState(state, kCapacity);
        EXPECT_EQ(state.count, kCapacity);
        EXPECT_EQ(state.nextWriteIndex, (i + 1) % kCapacity);
    }
}

TEST(ClampFrameDebuggerHistoryCursorTest, ClampsAtBothEndsWithoutWrapping)
{
    EXPECT_EQ(ClampFrameDebuggerHistoryCursor(2, 5), 2);
    EXPECT_EQ(ClampFrameDebuggerHistoryCursor(-3, 5), 0);
    EXPECT_EQ(ClampFrameDebuggerHistoryCursor(10, 5), 4);
    EXPECT_EQ(ClampFrameDebuggerHistoryCursor(0, 5), 0);
    EXPECT_EQ(ClampFrameDebuggerHistoryCursor(4, 5), 4);
}

TEST(ClampFrameDebuggerHistoryCursorTest, AlwaysZeroForAnEmptyOrInvalidCount)
{
    EXPECT_EQ(ClampFrameDebuggerHistoryCursor(0, 0), 0);
    EXPECT_EQ(ClampFrameDebuggerHistoryCursor(5, 0), 0);
    EXPECT_EQ(ClampFrameDebuggerHistoryCursor(-5, -1), 0);
}

} // namespace
} // namespace gte
