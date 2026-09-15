#include "Application/FrameDebuggerCommandBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

// task_manager/frame-debugger-3 campaign, PHASE7
// (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - mirrors
// tests/Application/EditorUiCommandBridgeTests.cpp's own exact coverage
// shape, applied to this campaign's brand-new, independent sibling bridge.

namespace gte {
namespace {

FrameDebuggerCommandRequest MakeGetStateRequest()
{
    FrameDebuggerCommandRequest request;
    request.kind = FrameDebuggerCommandKind::GetState;
    return request;
}

FrameDebuggerCommandRequest MakeSetEnabledRequest(bool enabled)
{
    FrameDebuggerCommandRequest request;
    request.kind = FrameDebuggerCommandKind::SetEnabled;
    request.setEnabled.enabled = enabled;
    return request;
}

FrameDebuggerCommandResult MakeResult(FrameDebuggerCommandKind kind, bool success)
{
    FrameDebuggerCommandResult result;
    result.kind = kind;
    result.success = success;
    return result;
}

// A request nobody ever fulfills must time out, roughly within the
// requested timeout window.
TEST(FrameDebuggerCommandBridgeTest, SubmitAndWaitTimesOutWhenNeverFulfilled)
{
    FrameDebuggerCommandBridge bridge;

    const auto start = std::chrono::steady_clock::now();
    const FrameDebuggerCommandBridge::SubmitResult result = bridge.SubmitAndWait(MakeGetStateRequest(), 50);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.result.has_value());
    EXPECT_LT(elapsed, std::chrono::milliseconds(2000));
}

// A separate thread fulfilling the request shortly after it starts must
// wake the waiter well before a much longer timeout would have elapsed -
// proves the condition_variable wakeup, not the timeout, resolved it.
TEST(FrameDebuggerCommandBridgeTest, SubmitAndWaitReturnsFulfilledResult)
{
    FrameDebuggerCommandBridge bridge;

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const std::optional<FrameDebuggerCommandRequest> peeked = bridge.TryPeekPendingCommandRequest();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->kind, FrameDebuggerCommandKind::SetEnabled);
        EXPECT_TRUE(peeked->setEnabled.enabled);
        FrameDebuggerCommandResult result = MakeResult(FrameDebuggerCommandKind::SetEnabled, true);
        result.state.enabled = true;
        result.state.channel = "all";
        bridge.FulfillCommand(result);
    });

    const auto start = std::chrono::steady_clock::now();
    const FrameDebuggerCommandBridge::SubmitResult result = bridge.SubmitAndWait(MakeSetEnabledRequest(true), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    fulfiller.join();

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_FALSE(result.timedOut);
    ASSERT_TRUE(result.result.has_value());
    EXPECT_EQ(result.result->kind, FrameDebuggerCommandKind::SetEnabled);
    EXPECT_TRUE(result.result->success);
    EXPECT_TRUE(result.result->state.enabled);
    EXPECT_EQ(result.result->state.channel, "all");
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

// A second concurrent SubmitAndWait() call, started strictly after the
// first has already registered as pending, must return alreadyPending ==
// true IMMEDIATELY - never actually wait.
TEST(FrameDebuggerCommandBridgeTest, SubmitAndWaitReturnsAlreadyPendingWhenAnotherRequestIsInFlight)
{
    FrameDebuggerCommandBridge bridge;

    std::thread firstRequester([&bridge] {
        // Nobody ever fulfills this one - it will simply time out on its
        // own after this test's own assertions are done reading it.
        bridge.SubmitAndWait(MakeGetStateRequest(), 1500);
    });

    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto start = std::chrono::steady_clock::now();
    const FrameDebuggerCommandBridge::SubmitResult second = bridge.SubmitAndWait(MakeGetStateRequest(), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(second.alreadyPending);
    EXPECT_FALSE(second.timedOut);
    EXPECT_FALSE(second.result.has_value());
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));

    firstRequester.join();
}

// Calling FulfillCommand() with nothing ever submitted must not crash/assert.
TEST(FrameDebuggerCommandBridgeTest, FulfillCommandIsANoOpWhenNothingIsPending)
{
    FrameDebuggerCommandBridge bridge;
    bridge.FulfillCommand(MakeResult(FrameDebuggerCommandKind::GetState, true));
    EXPECT_FALSE(bridge.IsCommandPending());
}

// A fresh bridge with nothing submitted returns std::nullopt.
TEST(FrameDebuggerCommandBridgeTest, TryPeekPendingCommandRequestReturnsNulloptWhenIdle)
{
    FrameDebuggerCommandBridge bridge;
    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());
}

// A late FulfillCommand() call, arriving AFTER a request already timed out
// and reset itself, must be a safe no-op - and must not corrupt the NEXT
// request's own result.
TEST(FrameDebuggerCommandBridgeTest, LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest)
{
    FrameDebuggerCommandBridge bridge;

    const FrameDebuggerCommandBridge::SubmitResult timedOut = bridge.SubmitAndWait(MakeGetStateRequest(), 50);
    EXPECT_TRUE(timedOut.timedOut);

    bridge.FulfillCommand(MakeResult(FrameDebuggerCommandKind::GetState, true));

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        bridge.FulfillCommand(MakeResult(FrameDebuggerCommandKind::CaptureNow, true));
    });

    FrameDebuggerCommandRequest captureRequest;
    captureRequest.kind = FrameDebuggerCommandKind::CaptureNow;
    const FrameDebuggerCommandBridge::SubmitResult fresh = bridge.SubmitAndWait(captureRequest, 5000);
    fulfiller.join();

    ASSERT_TRUE(fresh.result.has_value());
    EXPECT_TRUE(fresh.result->success);
    EXPECT_EQ(fresh.result->kind, FrameDebuggerCommandKind::CaptureNow);
}

// TryPeekPendingCommandRequest()/IsCommandPending() must reflect pending
// state correctly, and go back to "nothing pending" once fulfilled.
TEST(FrameDebuggerCommandBridgeTest, PendingStateIsObservableAndClearsAfterFulfillment)
{
    FrameDebuggerCommandBridge bridge;

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());

    std::thread requester([&bridge] {
        FrameDebuggerCommandRequest request;
        request.kind = FrameDebuggerCommandKind::SelectEvent;
        request.selectEvent.index = 3;
        bridge.SubmitAndWait(request, 1000);
    });

    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(bridge.IsCommandPending());
    const std::optional<FrameDebuggerCommandRequest> peeked = bridge.TryPeekPendingCommandRequest();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked->kind, FrameDebuggerCommandKind::SelectEvent);
    EXPECT_EQ(peeked->selectEvent.index, 3);

    bridge.FulfillCommand(MakeResult(FrameDebuggerCommandKind::SelectEvent, true));
    requester.join();

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());
}

} // namespace
} // namespace gte
