#include "Application/ProjectLifecycleLoadCommandBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace gte {
namespace {

LoadProjectAssemblyCommandRequest MakeLoadRequest(const std::string& projectName)
{
    LoadProjectAssemblyCommandRequest request;
    request.projectName = projectName;
    return request;
}

LoadProjectAssemblyCommandResult MakeLoadResult(bool loadSucceeded)
{
    LoadProjectAssemblyCommandResult result;
    result.loadSucceeded = loadSucceeded;
    return result;
}

// A separate thread fulfilling the request shortly after it starts must
// wake the waiter well before a much longer timeout would have elapsed -
// proves the condition_variable wakeup, not the timeout, resolved it. This
// is the basic submit-from-one-thread + fulfill-from-another-thread round
// trip (spec 3.3, case 1).
TEST(ProjectLifecycleLoadCommandBridgeTest, SubmitAndWaitReturnsFulfilledResult)
{
    ProjectLifecycleLoadCommandBridge bridge;

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const std::optional<LoadProjectAssemblyCommandRequest> peeked = bridge.TryPeekPendingCommandRequest();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->projectName, "MyGame");
        bridge.FulfillCommand(MakeLoadResult(true));
    });

    const auto start = std::chrono::steady_clock::now();
    const ProjectLifecycleLoadCommandBridge::SubmitResult result =
        bridge.SubmitAndWait(MakeLoadRequest("MyGame"), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    fulfiller.join();

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_FALSE(result.timedOut);
    ASSERT_TRUE(result.result.has_value());
    EXPECT_TRUE(result.result->loadSucceeded);
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

// A fresh bridge with nothing submitted returns std::nullopt, and
// IsCommandPending() reports false - peek-when-idle (spec 3.3, case 2).
TEST(ProjectLifecycleLoadCommandBridgeTest, TryPeekPendingCommandRequestReturnsNulloptWhenIdle)
{
    ProjectLifecycleLoadCommandBridge bridge;
    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());
}

// A second concurrent SubmitAndWait() call, started strictly after the
// first has already registered as pending, must return alreadyPending ==
// true IMMEDIATELY - never actually wait (spec 3.3, case 3).
TEST(ProjectLifecycleLoadCommandBridgeTest, SubmitAndWaitReturnsAlreadyPendingWhenAnotherRequestIsInFlight)
{
    ProjectLifecycleLoadCommandBridge bridge;

    std::thread firstRequester([&bridge] {
        // Nobody ever fulfills this one - it will simply time out on its
        // own after this test's own assertions are done reading it.
        bridge.SubmitAndWait(MakeLoadRequest("First"), 1500);
    });

    // Give the first request a moment to actually register as pending.
    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto start = std::chrono::steady_clock::now();
    const ProjectLifecycleLoadCommandBridge::SubmitResult second =
        bridge.SubmitAndWait(MakeLoadRequest("Second"), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(second.alreadyPending);
    EXPECT_FALSE(second.timedOut);
    EXPECT_FALSE(second.result.has_value());
    // Must not have actually waited - a real wait would be at least
    // several hundred milliseconds given the timeouts above.
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));

    firstRequester.join();
}

// A request nobody ever fulfills must time out, roughly within the
// requested timeout window (spec 3.3, case 4).
TEST(ProjectLifecycleLoadCommandBridgeTest, SubmitAndWaitTimesOutWhenNeverFulfilled)
{
    ProjectLifecycleLoadCommandBridge bridge;

    const auto start = std::chrono::steady_clock::now();
    const ProjectLifecycleLoadCommandBridge::SubmitResult result =
        bridge.SubmitAndWait(MakeLoadRequest("NeverFulfilled"), 50);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.result.has_value());
    EXPECT_LT(elapsed, std::chrono::milliseconds(2000));
}

// A late FulfillCommand() call, arriving AFTER a request already timed out
// and reset itself, must be a safe no-op - and must not corrupt the NEXT
// request's own result (spec 3.3, case 5).
TEST(ProjectLifecycleLoadCommandBridgeTest, LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest)
{
    ProjectLifecycleLoadCommandBridge bridge;

    const ProjectLifecycleLoadCommandBridge::SubmitResult timedOut =
        bridge.SubmitAndWait(MakeLoadRequest("Stale"), 50);
    EXPECT_TRUE(timedOut.timedOut);

    // This arrives long after the waiter already gave up - must not crash
    // or assert, and must not affect anything.
    bridge.FulfillCommand(MakeLoadResult(true));

    // A fresh request afterward must behave completely normally.
    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        bridge.FulfillCommand(MakeLoadResult(false));
    });

    const ProjectLifecycleLoadCommandBridge::SubmitResult fresh =
        bridge.SubmitAndWait(MakeLoadRequest("Fresh"), 5000);
    fulfiller.join();

    ASSERT_TRUE(fresh.result.has_value());
    EXPECT_FALSE(fresh.result->loadSucceeded);
}

// Calling FulfillCommand() with nothing ever submitted must not crash/assert.
TEST(ProjectLifecycleLoadCommandBridgeTest, FulfillCommandIsANoOpWhenNothingIsPending)
{
    ProjectLifecycleLoadCommandBridge bridge;
    bridge.FulfillCommand(MakeLoadResult(true));
    EXPECT_FALSE(bridge.IsCommandPending());
}

// TryPeekPendingCommandRequest()/IsCommandPending() must reflect pending
// state correctly, and go back to "nothing pending" once fulfilled.
TEST(ProjectLifecycleLoadCommandBridgeTest, PendingStateIsObservableAndClearsAfterFulfillment)
{
    ProjectLifecycleLoadCommandBridge bridge;

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());

    std::thread requester([&bridge] {
        bridge.SubmitAndWait(MakeLoadRequest("SomeProject"), 1000);
    });

    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(bridge.IsCommandPending());
    const std::optional<LoadProjectAssemblyCommandRequest> peeked = bridge.TryPeekPendingCommandRequest();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked->projectName, "SomeProject");

    bridge.FulfillCommand(MakeLoadResult(false));
    requester.join();

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());
}

} // namespace
} // namespace gte
