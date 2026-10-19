#include "Application/ProjectLifecycleLoadCommandBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
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

// A fulfiller thread must wake the waiter long before the timeout.
TEST(ProjectLifecycleLoadCommandBridgeTest, SubmitAndWaitReturnsFulfilledResult)
{
    ProjectLifecycleLoadCommandBridge bridge;

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->request.projectName, "MyGame");
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeLoadResult(true)));
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

// A fresh bridge with nothing submitted returns std::nullopt.
TEST(ProjectLifecycleLoadCommandBridgeTest, TryPeekPendingCommandReturnsNulloptWhenIdle)
{
    ProjectLifecycleLoadCommandBridge bridge;
    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());
}

// A second submit while one is pending returns alreadyPending immediately.
TEST(ProjectLifecycleLoadCommandBridgeTest, SubmitAndWaitReturnsAlreadyPendingWhenAnotherRequestIsInFlight)
{
    ProjectLifecycleLoadCommandBridge bridge;

    std::thread firstRequester([&bridge] {
        bridge.SubmitAndWait(MakeLoadRequest("First"), 1500);
    });

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

// A request nobody ever fulfills must time out within the requested window.
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

// A late fulfil after a timeout is dropped and does not corrupt the next request.
TEST(ProjectLifecycleLoadCommandBridgeTest, LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest)
{
    ProjectLifecycleLoadCommandBridge bridge;

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
    const ProjectLifecycleLoadCommandBridge::SubmitResult timedOut =
        bridge.SubmitAndWait(MakeLoadRequest("Stale"), 50);
    peeker.join();
    EXPECT_TRUE(timedOut.timedOut);
    ASSERT_NE(idA, 0u);

    EXPECT_FALSE(bridge.FulfillCommand(idA, MakeLoadResult(true)));

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeLoadResult(false)));
    });

    const ProjectLifecycleLoadCommandBridge::SubmitResult fresh =
        bridge.SubmitAndWait(MakeLoadRequest("Fresh"), 5000);
    fulfiller.join();

    ASSERT_TRUE(fresh.result.has_value());
    EXPECT_FALSE(fresh.result->loadSucceeded);
}

// Fulfilling with nothing pending is dropped.
TEST(ProjectLifecycleLoadCommandBridgeTest, FulfillCommandIsDroppedWhenNothingIsPending)
{
    ProjectLifecycleLoadCommandBridge bridge;
    EXPECT_FALSE(bridge.FulfillCommand(1, MakeLoadResult(true)));
    EXPECT_FALSE(bridge.IsCommandPending());
}

// Pending state is observable and clears after fulfilment.
TEST(ProjectLifecycleLoadCommandBridgeTest, PendingStateIsObservableAndClearsAfterFulfillment)
{
    ProjectLifecycleLoadCommandBridge bridge;

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());

    std::thread requester([&bridge] {
        bridge.SubmitAndWait(MakeLoadRequest("SomeProject"), 1000);
    });

    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(bridge.IsCommandPending());
    const auto peeked = bridge.TryPeekPendingCommand();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked->request.projectName, "SomeProject");

    EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeLoadResult(false)));
    requester.join();

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());
}

} // namespace
} // namespace gte
