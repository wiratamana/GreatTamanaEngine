#include "Application/RenderGraphControlCommandBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>

namespace gte {
namespace {

RenderGraphControlCommandRequest MakeListPassStatesRequest()
{
    RenderGraphControlCommandRequest request;
    request.kind = RenderGraphControlCommandKind::ListPassStates;
    return request;
}

RenderGraphControlCommandRequest MakeSetPassEnabledRequest(const std::string& name, bool enabled)
{
    RenderGraphControlCommandRequest request;
    request.kind = RenderGraphControlCommandKind::SetBuiltInPassEnabled;
    request.setPassEnabled.name = name;
    request.setPassEnabled.enabled = enabled;
    return request;
}

RenderGraphControlCommandResult MakeResult(RenderGraphControlCommandKind kind, bool success)
{
    RenderGraphControlCommandResult result;
    result.kind = kind;
    result.success = success;
    return result;
}

// A request nobody ever fulfills must time out, roughly within the
// requested timeout window.
TEST(RenderGraphControlCommandBridgeTest, SubmitAndWaitTimesOutWhenNeverFulfilled)
{
    RenderGraphControlCommandBridge bridge;

    const auto start = std::chrono::steady_clock::now();
    const RenderGraphControlCommandBridge::SubmitResult result = bridge.SubmitAndWait(MakeListPassStatesRequest(), 50);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.result.has_value());
    EXPECT_LT(elapsed, std::chrono::milliseconds(2000));
}

// A separate thread fulfilling the request shortly after it starts must
// wake the waiter well before a much longer timeout would have elapsed -
// proves the condition_variable wakeup, not the timeout, resolved it.
TEST(RenderGraphControlCommandBridgeTest, SubmitAndWaitReturnsFulfilledResult)
{
    RenderGraphControlCommandBridge bridge;

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->request.kind, RenderGraphControlCommandKind::SetBuiltInPassEnabled);
        EXPECT_EQ(peeked->request.setPassEnabled.name, "RenderTransparent");
        EXPECT_FALSE(peeked->request.setPassEnabled.enabled);
        RenderGraphControlCommandResult result = MakeResult(RenderGraphControlCommandKind::SetBuiltInPassEnabled, true);
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, result));
    });

    const auto start = std::chrono::steady_clock::now();
    const RenderGraphControlCommandBridge::SubmitResult result =
        bridge.SubmitAndWait(MakeSetPassEnabledRequest("RenderTransparent", false), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    fulfiller.join();

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_FALSE(result.timedOut);
    ASSERT_TRUE(result.result.has_value());
    EXPECT_EQ(result.result->kind, RenderGraphControlCommandKind::SetBuiltInPassEnabled);
    EXPECT_TRUE(result.result->success);
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

// A second concurrent SubmitAndWait() call, started strictly after the
// first has already registered as pending, must return alreadyPending ==
// true IMMEDIATELY - never actually wait.
TEST(RenderGraphControlCommandBridgeTest, SubmitAndWaitReturnsAlreadyPendingWhenAnotherRequestIsInFlight)
{
    RenderGraphControlCommandBridge bridge;

    std::thread firstRequester([&bridge] {
        // Nobody ever fulfills this one - it will simply time out on its
        // own after this test's own assertions are done reading it.
        bridge.SubmitAndWait(MakeListPassStatesRequest(), 1500);
    });

    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto start = std::chrono::steady_clock::now();
    const RenderGraphControlCommandBridge::SubmitResult second = bridge.SubmitAndWait(MakeListPassStatesRequest(), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(second.alreadyPending);
    EXPECT_FALSE(second.timedOut);
    EXPECT_FALSE(second.result.has_value());
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));

    firstRequester.join();
}

// Fulfilling with nothing pending is dropped.
TEST(RenderGraphControlCommandBridgeTest, FulfillCommandIsDroppedWhenNothingIsPending)
{
    RenderGraphControlCommandBridge bridge;
    EXPECT_FALSE(bridge.FulfillCommand(1, MakeResult(RenderGraphControlCommandKind::ListPassStates, true)));
    EXPECT_FALSE(bridge.IsCommandPending());
}

// A fresh bridge with nothing submitted returns std::nullopt.
TEST(RenderGraphControlCommandBridgeTest, TryPeekPendingCommandReturnsNulloptWhenIdle)
{
    RenderGraphControlCommandBridge bridge;
    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());
}

// A late fulfil after a timeout is dropped and does not corrupt the next request.
TEST(RenderGraphControlCommandBridgeTest, LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest)
{
    RenderGraphControlCommandBridge bridge;

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
    const RenderGraphControlCommandBridge::SubmitResult timedOut = bridge.SubmitAndWait(MakeListPassStatesRequest(), 50);
    peeker.join();
    EXPECT_TRUE(timedOut.timedOut);
    ASSERT_NE(idA, 0u);

    EXPECT_FALSE(bridge.FulfillCommand(idA, MakeResult(RenderGraphControlCommandKind::ListPassStates, true)));

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeResult(RenderGraphControlCommandKind::SetFeatureEnabled, true)));
    });

    RenderGraphControlCommandRequest freshRequest;
    freshRequest.kind = RenderGraphControlCommandKind::SetFeatureEnabled;
    const RenderGraphControlCommandBridge::SubmitResult fresh = bridge.SubmitAndWait(freshRequest, 5000);
    fulfiller.join();

    ASSERT_TRUE(fresh.result.has_value());
    EXPECT_TRUE(fresh.result->success);
    EXPECT_EQ(fresh.result->kind, RenderGraphControlCommandKind::SetFeatureEnabled);
}

// Pending state is observable and clears after fulfilment.
TEST(RenderGraphControlCommandBridgeTest, PendingStateIsObservableAndClearsAfterFulfillment)
{
    RenderGraphControlCommandBridge bridge;

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());

    std::thread requester([&bridge] {
        RenderGraphControlCommandRequest request;
        request.kind = RenderGraphControlCommandKind::SetFeaturePriority;
        request.setFeaturePriority.name = "DemoRenderFeatureV2";
        request.setFeaturePriority.priority = 3;
        bridge.SubmitAndWait(request, 1000);
    });

    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(bridge.IsCommandPending());
    const auto peeked = bridge.TryPeekPendingCommand();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked->request.kind, RenderGraphControlCommandKind::SetFeaturePriority);
    EXPECT_EQ(peeked->request.setFeaturePriority.name, "DemoRenderFeatureV2");
    EXPECT_EQ(peeked->request.setFeaturePriority.priority, 3);

    EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeResult(RenderGraphControlCommandKind::SetFeaturePriority, true)));
    requester.join();

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());
}

} // namespace
} // namespace gte
