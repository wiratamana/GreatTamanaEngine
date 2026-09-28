#include "Application/ProjectAssemblyHotReloadCommandBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace gte {
namespace {

// A request nobody ever peeks/fulfills must time out, roughly within the
// requested timeout window - AND, unlike EngineCommandBridge, must leave
// the request still observable via TryPeekPendingProjectName() afterward
// (see ProjectAssemblyHotReloadCommandBridge.h's own "read this carefully"
// note for why this is the ONE deliberate behavior difference from that
// other bridge).
TEST(ProjectAssemblyHotReloadCommandBridgeTest, RequestWithNoServicerTimesOutButStaysPending)
{
    ProjectAssemblyHotReloadCommandBridge bridge;

    const auto start = std::chrono::steady_clock::now();
    const ProjectAssemblyHotReloadCommandBridge::SubmitResult result = bridge.SubmitAndWait("Foo", 100);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_LT(elapsed, std::chrono::milliseconds(2000));

    // The important, DIFFERENT-from-EngineCommandBridge assertion: the
    // request must still be pending after the caller gave up.
    const std::optional<std::string> peeked = bridge.TryPeekPendingProjectName();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(*peeked, "Foo");
}

// FulfillPending() called after a timeout (simulating the main thread
// finally reaching its drain point late) must clear the slot, and a
// brand-new SubmitAndWait() call for a different project name afterward
// must be serviced completely normally - mirrors
// EngineCommandBridgeTest.LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest's
// own shape, adapted to this bridge's own different timeout semantics.
TEST(ProjectAssemblyHotReloadCommandBridgeTest, FulfillPendingAfterATimeoutClearsTheSlotForAFreshRequest)
{
    ProjectAssemblyHotReloadCommandBridge bridge;

    const ProjectAssemblyHotReloadCommandBridge::SubmitResult timedOut = bridge.SubmitAndWait("Foo", 50);
    EXPECT_TRUE(timedOut.timedOut);
    ASSERT_TRUE(bridge.TryPeekPendingProjectName().has_value());

    // Simulate the main thread finally reaching its drain point late.
    bridge.FulfillPending();
    EXPECT_FALSE(bridge.TryPeekPendingProjectName().has_value());

    // A fresh request afterward must behave completely normally.
    std::thread fulfiller([&bridge] {
        while (!bridge.TryPeekPendingProjectName().has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const std::optional<std::string> peeked = bridge.TryPeekPendingProjectName();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(*peeked, "Fresh");
        bridge.FulfillPending();
    });

    const ProjectAssemblyHotReloadCommandBridge::SubmitResult fresh = bridge.SubmitAndWait("Fresh", 5000);
    fulfiller.join();

    EXPECT_FALSE(fresh.alreadyPending);
    EXPECT_FALSE(fresh.timedOut);
    EXPECT_FALSE(bridge.TryPeekPendingProjectName().has_value());
}

// A separate thread peeking then fulfilling the request shortly after it
// starts must wake the waiter well before a much longer timeout would have
// elapsed - proves the condition_variable wakeup, not the timeout, resolved
// it - mirrors EngineCommandBridgeTest.FulfilledRequestReturnsExactResultQuickly.
TEST(ProjectAssemblyHotReloadCommandBridgeTest, FulfilledBeforeTimeoutReturnsCleanlyWithNeitherFlagSet)
{
    ProjectAssemblyHotReloadCommandBridge bridge;

    std::thread fulfiller([&bridge] {
        std::optional<std::string> peeked;
        while (!(peeked = bridge.TryPeekPendingProjectName()).has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_EQ(*peeked, "Foo");
        bridge.FulfillPending();
    });

    const auto start = std::chrono::steady_clock::now();
    const ProjectAssemblyHotReloadCommandBridge::SubmitResult result = bridge.SubmitAndWait("Foo", 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    fulfiller.join();

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_FALSE(result.timedOut);
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

// A second concurrent SubmitAndWait() call, started strictly after the
// first has already registered as pending, must return alreadyPending ==
// true IMMEDIATELY - never actually wait - mirrors
// EngineCommandBridgeTest.SecondConcurrentRequestReturnsAlreadyPendingImmediately
// exactly, adapted to this bridge's single std::string payload.
TEST(ProjectAssemblyHotReloadCommandBridgeTest, SecondConcurrentRequestReturnsAlreadyPendingImmediately)
{
    ProjectAssemblyHotReloadCommandBridge bridge;

    std::thread firstRequester([&bridge] {
        // Nobody ever fulfills this one - it will simply time out on its
        // own after this test's own assertions are done reading it.
        bridge.SubmitAndWait("First", 1500);
    });

    while (!bridge.TryPeekPendingProjectName().has_value()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto start = std::chrono::steady_clock::now();
    const ProjectAssemblyHotReloadCommandBridge::SubmitResult second = bridge.SubmitAndWait("Second", 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(second.alreadyPending);
    EXPECT_FALSE(second.timedOut);
    // Must not have actually waited - a real wait would be at least
    // several hundred milliseconds given the timeouts above.
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));

    firstRequester.join();
}

// Calling FulfillPending() on a freshly-constructed bridge (nothing ever
// submitted) must not crash or assert.
TEST(ProjectAssemblyHotReloadCommandBridgeTest, FulfillPendingWithNothingPendingIsASafeNoOp)
{
    ProjectAssemblyHotReloadCommandBridge bridge;
    bridge.FulfillPending();
    EXPECT_FALSE(bridge.TryPeekPendingProjectName().has_value());
}

} // namespace
} // namespace gte
