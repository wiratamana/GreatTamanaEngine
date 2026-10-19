#include "Application/FrameDebuggerCommandBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>

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
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->request.kind, FrameDebuggerCommandKind::SetEnabled);
        EXPECT_TRUE(peeked->request.setEnabled.enabled);
        FrameDebuggerCommandResult result = MakeResult(FrameDebuggerCommandKind::SetEnabled, true);
        result.state.enabled = true;
        result.state.channel = "all";
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, result));
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

// Fulfilling with nothing pending is dropped.
TEST(FrameDebuggerCommandBridgeTest, FulfillCommandIsDroppedWhenNothingIsPending)
{
    FrameDebuggerCommandBridge bridge;
    EXPECT_FALSE(bridge.FulfillCommand(1, MakeResult(FrameDebuggerCommandKind::GetState, true)));
    EXPECT_FALSE(bridge.IsCommandPending());
}

// A fresh bridge with nothing submitted returns std::nullopt.
TEST(FrameDebuggerCommandBridgeTest, TryPeekPendingCommandReturnsNulloptWhenIdle)
{
    FrameDebuggerCommandBridge bridge;
    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());
}

// A late fulfil after a timeout is dropped and does not corrupt the next request.
TEST(FrameDebuggerCommandBridgeTest, LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest)
{
    FrameDebuggerCommandBridge bridge;

    std::uint64_t idA = 0;
    std::thread peeker([&bridge, &idA] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::yield();
        }
        const auto peeked = bridge.TryPeekPendingCommand();
        if (peeked) {
            idA = peeked->id;
        }
    });
    const FrameDebuggerCommandBridge::SubmitResult timedOut = bridge.SubmitAndWait(MakeGetStateRequest(), 50);
    peeker.join();
    EXPECT_TRUE(timedOut.timedOut);
    ASSERT_NE(idA, 0u);

    EXPECT_FALSE(bridge.FulfillCommand(idA, MakeResult(FrameDebuggerCommandKind::GetState, true)));

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeResult(FrameDebuggerCommandKind::CaptureNow, true)));
    });

    FrameDebuggerCommandRequest captureRequest;
    captureRequest.kind = FrameDebuggerCommandKind::CaptureNow;
    const FrameDebuggerCommandBridge::SubmitResult fresh = bridge.SubmitAndWait(captureRequest, 5000);
    fulfiller.join();

    ASSERT_TRUE(fresh.result.has_value());
    EXPECT_TRUE(fresh.result->success);
    EXPECT_EQ(fresh.result->kind, FrameDebuggerCommandKind::CaptureNow);
}

// Pending state is observable and clears after fulfilment.
TEST(FrameDebuggerCommandBridgeTest, PendingStateIsObservableAndClearsAfterFulfillment)
{
    FrameDebuggerCommandBridge bridge;

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());

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
    const auto peeked = bridge.TryPeekPendingCommand();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked->request.kind, FrameDebuggerCommandKind::SelectEvent);
    EXPECT_EQ(peeked->request.selectEvent.index, 3);

    EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeResult(FrameDebuggerCommandKind::SelectEvent, true)));
    requester.join();

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());
}

} // namespace
} // namespace gte
