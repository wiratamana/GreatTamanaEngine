#include "Application/EditorUiCommandBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace gte {
namespace {

EditorUiCommandRequest MakeActivateTabRequest(const std::string& tabName)
{
    EditorUiCommandRequest request;
    request.kind = EditorUiCommandKind::ActivateTab;
    request.activateTab.tabName = tabName;
    return request;
}

EditorUiCommandResult MakeActivateTabResult(bool success, bool tabExists)
{
    EditorUiCommandResult result;
    result.kind = EditorUiCommandKind::ActivateTab;
    result.activateTab.success = success;
    result.activateTab.tabExists = tabExists;
    return result;
}

// A request nobody ever fulfills must time out, roughly within the
// requested timeout window.
TEST(EditorUiCommandBridgeTest, SubmitAndWaitTimesOutWhenNeverFulfilled)
{
    EditorUiCommandBridge bridge;

    const auto start = std::chrono::steady_clock::now();
    const EditorUiCommandBridge::SubmitResult result = bridge.SubmitAndWait(MakeActivateTabRequest("Profiler"), 50);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.result.has_value());
    EXPECT_LT(elapsed, std::chrono::milliseconds(2000));
}

// A separate thread fulfilling the request shortly after it starts must
// wake the waiter well before a much longer timeout would have elapsed -
// proves the condition_variable wakeup, not the timeout, resolved it.
TEST(EditorUiCommandBridgeTest, SubmitAndWaitReturnsFulfilledResult)
{
    EditorUiCommandBridge bridge;

    std::thread fulfiller([&bridge] {
        // Wait until the request is actually pending before fulfilling it.
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const std::optional<EditorUiCommandRequest> peeked = bridge.TryPeekPendingCommandRequest();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->activateTab.tabName, "Profiler");
        bridge.FulfillCommand(MakeActivateTabResult(true, true));
    });

    const auto start = std::chrono::steady_clock::now();
    const EditorUiCommandBridge::SubmitResult result = bridge.SubmitAndWait(MakeActivateTabRequest("Profiler"), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    fulfiller.join();

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_FALSE(result.timedOut);
    ASSERT_TRUE(result.result.has_value());
    EXPECT_EQ(result.result->kind, EditorUiCommandKind::ActivateTab);
    EXPECT_TRUE(result.result->activateTab.success);
    EXPECT_TRUE(result.result->activateTab.tabExists);
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

// A second concurrent SubmitAndWait() call, started strictly after the
// first has already registered as pending, must return alreadyPending ==
// true IMMEDIATELY - never actually wait.
TEST(EditorUiCommandBridgeTest, SubmitAndWaitReturnsAlreadyPendingWhenAnotherRequestIsInFlight)
{
    EditorUiCommandBridge bridge;

    std::thread firstRequester([&bridge] {
        // Nobody ever fulfills this one - it will simply time out on its
        // own after this test's own assertions are done reading it.
        bridge.SubmitAndWait(MakeActivateTabRequest("Hierarchy"), 1500);
    });

    // Give the first request a moment to actually register as pending.
    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto start = std::chrono::steady_clock::now();
    const EditorUiCommandBridge::SubmitResult second = bridge.SubmitAndWait(MakeActivateTabRequest("Inspector"), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(second.alreadyPending);
    EXPECT_FALSE(second.timedOut);
    EXPECT_FALSE(second.result.has_value());
    // Must not have actually waited - a real wait would be at least
    // several hundred milliseconds given the timeouts above.
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));

    firstRequester.join();
}

// Calling FulfillCommand() with nothing ever submitted must not crash/assert.
TEST(EditorUiCommandBridgeTest, FulfillCommandIsANoOpWhenNothingIsPending)
{
    EditorUiCommandBridge bridge;
    bridge.FulfillCommand(MakeActivateTabResult(true, true));
    EXPECT_FALSE(bridge.IsCommandPending());
}

// A fresh bridge with nothing submitted returns std::nullopt.
TEST(EditorUiCommandBridgeTest, TryPeekPendingCommandRequestReturnsNulloptWhenIdle)
{
    EditorUiCommandBridge bridge;
    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());
}

// A late FulfillCommand() call, arriving AFTER a request already timed out
// and reset itself, must be a safe no-op - and must not corrupt the NEXT
// request's own result.
TEST(EditorUiCommandBridgeTest, LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest)
{
    EditorUiCommandBridge bridge;

    const EditorUiCommandBridge::SubmitResult timedOut = bridge.SubmitAndWait(MakeActivateTabRequest("Profiler"), 50);
    EXPECT_TRUE(timedOut.timedOut);

    // This arrives long after the waiter already gave up - must not crash
    // or assert, and must not affect anything.
    bridge.FulfillCommand(MakeActivateTabResult(true, true));

    // A fresh request afterward must behave completely normally.
    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        bridge.FulfillCommand(MakeActivateTabResult(true, true));
    });

    const EditorUiCommandBridge::SubmitResult fresh = bridge.SubmitAndWait(MakeActivateTabRequest("Jobs"), 5000);
    fulfiller.join();

    ASSERT_TRUE(fresh.result.has_value());
    EXPECT_TRUE(fresh.result->activateTab.success);
}

// TryPeekPendingCommandRequest()/IsCommandPending() must reflect pending
// state correctly, and go back to "nothing pending" once fulfilled.
TEST(EditorUiCommandBridgeTest, PendingStateIsObservableAndClearsAfterFulfillment)
{
    EditorUiCommandBridge bridge;

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());

    std::thread requester([&bridge] {
        bridge.SubmitAndWait(MakeActivateTabRequest("Atmosphere"), 1000);
    });

    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(bridge.IsCommandPending());
    const std::optional<EditorUiCommandRequest> peeked = bridge.TryPeekPendingCommandRequest();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked->kind, EditorUiCommandKind::ActivateTab);
    EXPECT_EQ(peeked->activateTab.tabName, "Atmosphere");

    bridge.FulfillCommand(MakeActivateTabResult(false, false));
    requester.join();

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());
}

} // namespace
} // namespace gte
