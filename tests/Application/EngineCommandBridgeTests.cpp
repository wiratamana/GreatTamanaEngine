#include "Application/EngineCommandBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace gte {
namespace {

EngineCommandRequest MakeInstantiateRequest(const std::string& name)
{
    EngineCommandRequest request;
    request.kind = EngineCommandKind::InstantiatePrimitive;
    request.instantiatePrimitive.shape = "cube";
    request.instantiatePrimitive.requestedName = name;
    request.instantiatePrimitive.worldPosition = Vec3{ 1.0f, 2.0f, 3.0f };
    return request;
}

EngineCommandResult MakeInstantiateResult(bool success, const std::string& resolvedName)
{
    EngineCommandResult result;
    result.kind = EngineCommandKind::InstantiatePrimitive;
    result.instantiatePrimitive.success = success;
    result.instantiatePrimitive.resolvedName = resolvedName;
    return result;
}

// A request nobody ever fulfills must time out, roughly within the
// requested timeout window.
TEST(EngineCommandBridgeTest, RequestWithNoFulfillerTimesOut)
{
    EngineCommandBridge bridge;

    const auto start = std::chrono::steady_clock::now();
    const EngineCommandBridge::SubmitResult result = bridge.SubmitAndWait(MakeInstantiateRequest("Foo"), 100);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.result.has_value());
    EXPECT_LT(elapsed, std::chrono::milliseconds(2000));
}

// A separate thread fulfilling the request shortly after it starts must
// wake the waiter well before a much longer timeout would have elapsed -
// proves the condition_variable wakeup, not the timeout, resolved it.
TEST(EngineCommandBridgeTest, FulfilledRequestReturnsExactResultQuickly)
{
    EngineCommandBridge bridge;

    std::thread fulfiller([&bridge] {
        // Wait until the request is actually pending before fulfilling it.
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const std::optional<EngineCommandRequest> peeked = bridge.TryPeekPendingCommandRequest();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->instantiatePrimitive.requestedName, "Foo");
        bridge.FulfillCommand(MakeInstantiateResult(true, "Foo"));
    });

    const auto start = std::chrono::steady_clock::now();
    const EngineCommandBridge::SubmitResult result = bridge.SubmitAndWait(MakeInstantiateRequest("Foo"), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    fulfiller.join();

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_FALSE(result.timedOut);
    ASSERT_TRUE(result.result.has_value());
    EXPECT_EQ(result.result->kind, EngineCommandKind::InstantiatePrimitive);
    EXPECT_TRUE(result.result->instantiatePrimitive.success);
    EXPECT_EQ(result.result->instantiatePrimitive.resolvedName, "Foo");
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

// A second concurrent SubmitAndWait() call, started strictly after the
// first has already registered as pending, must return alreadyPending ==
// true IMMEDIATELY - never actually wait. This holds regardless of which
// EngineCommandKind either request carries - the bridge has a single global
// slot (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #5), unlike
// FrameCaptureBridge's per-kind slots.
TEST(EngineCommandBridgeTest, SecondConcurrentRequestReturnsAlreadyPendingImmediately)
{
    EngineCommandBridge bridge;

    std::thread firstRequester([&bridge] {
        // Nobody ever fulfills this one - it will simply time out on its
        // own after this test's own assertions are done reading it.
        bridge.SubmitAndWait(MakeInstantiateRequest("First"), 1500);
    });

    // Give the first request a moment to actually register as pending.
    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EngineCommandRequest deleteRequest;
    deleteRequest.kind = EngineCommandKind::DeleteEntity;
    deleteRequest.deleteEntity.name = "Second";

    const auto start = std::chrono::steady_clock::now();
    const EngineCommandBridge::SubmitResult second = bridge.SubmitAndWait(deleteRequest, 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(second.alreadyPending);
    EXPECT_FALSE(second.timedOut);
    EXPECT_FALSE(second.result.has_value());
    // Must not have actually waited - a real wait would be at least
    // several hundred milliseconds given the timeouts above.
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));

    firstRequester.join();
}

// TryPeekPendingCommandRequest()/IsCommandPending() must reflect pending
// state correctly, and go back to "nothing pending" once fulfilled.
TEST(EngineCommandBridgeTest, PendingStateIsObservableAndClearsAfterFulfillment)
{
    EngineCommandBridge bridge;

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());

    std::thread requester([&bridge] {
        EngineCommandRequest deleteRequest;
        deleteRequest.kind = EngineCommandKind::DeleteEntity;
        deleteRequest.deleteEntity.name = "ToDelete";
        bridge.SubmitAndWait(deleteRequest, 1000);
    });

    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(bridge.IsCommandPending());
    const std::optional<EngineCommandRequest> peeked = bridge.TryPeekPendingCommandRequest();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked->kind, EngineCommandKind::DeleteEntity);
    EXPECT_EQ(peeked->deleteEntity.name, "ToDelete");

    EngineCommandResult result;
    result.kind = EngineCommandKind::DeleteEntity;
    result.deleteEntity.success = true;
    bridge.FulfillCommand(result);
    requester.join();

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommandRequest().has_value());
}

// A late FulfillCommand() call, arriving AFTER a request already timed out
// and reset itself, must be a safe no-op - and must not corrupt the NEXT
// request's own result.
TEST(EngineCommandBridgeTest, LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest)
{
    EngineCommandBridge bridge;

    const EngineCommandBridge::SubmitResult timedOut = bridge.SubmitAndWait(MakeInstantiateRequest("Foo"), 50);
    EXPECT_TRUE(timedOut.timedOut);

    // This arrives long after the waiter already gave up - must not crash
    // or assert, and must not affect anything.
    bridge.FulfillCommand(MakeInstantiateResult(true, "Stale"));

    // A fresh request afterward must behave completely normally.
    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        bridge.FulfillCommand(MakeInstantiateResult(true, "Fresh"));
    });

    const EngineCommandBridge::SubmitResult fresh = bridge.SubmitAndWait(MakeInstantiateRequest("Fresh"), 5000);
    fulfiller.join();

    ASSERT_TRUE(fresh.result.has_value());
    EXPECT_EQ(fresh.result->instantiatePrimitive.resolvedName, "Fresh");
}

// network-impl-5 campaign (PHASE3_ENGINE_COMMAND_BRIDGE_AND_DISPATCH_EXTENSION.md)
// - a round-trip test per NEW EngineCommandKind, mirroring
// FulfilledRequestReturnsExactResultQuickly's own exact shape above. These
// exercise ONLY the bridge's existing generic mutex/condition-variable
// mechanics - proving directly that adding two more EngineCommandKind values
// needed zero changes to EngineCommandBridge.cpp itself (PHASE0's own Step 2
// claim).
TEST(EngineCommandBridgeTest, FulfilledSetEntityTrsRequestReturnsExactResultQuickly)
{
    EngineCommandBridge bridge;

    EngineCommandRequest request;
    request.kind = EngineCommandKind::SetEntityTrs;
    request.setEntityTrs.name = "MyLight";
    request.setEntityTrs.hasTranslation = true;
    request.setEntityTrs.translation = Vec3{ 0.0f, 5.0f, -2.0f };

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const std::optional<EngineCommandRequest> peeked = bridge.TryPeekPendingCommandRequest();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->kind, EngineCommandKind::SetEntityTrs);
        EXPECT_EQ(peeked->setEntityTrs.name, "MyLight");
        EXPECT_TRUE(peeked->setEntityTrs.hasTranslation);

        EngineCommandResult result;
        result.kind = EngineCommandKind::SetEntityTrs;
        result.setEntityTrs.success = true;
        result.setEntityTrs.translationChanged = true;
        result.setEntityTrs.resultingPosition = Vec3{ 0.0f, 5.0f, -2.0f };
        bridge.FulfillCommand(result);
    });

    const auto start = std::chrono::steady_clock::now();
    const EngineCommandBridge::SubmitResult result = bridge.SubmitAndWait(request, 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    fulfiller.join();

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_FALSE(result.timedOut);
    ASSERT_TRUE(result.result.has_value());
    EXPECT_EQ(result.result->kind, EngineCommandKind::SetEntityTrs);
    EXPECT_TRUE(result.result->setEntityTrs.success);
    EXPECT_TRUE(result.result->setEntityTrs.translationChanged);
    EXPECT_FALSE(result.result->setEntityTrs.rotationChanged);
    EXPECT_EQ(result.result->setEntityTrs.resultingPosition.y, 5.0f);
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

TEST(EngineCommandBridgeTest, FulfilledInstantiateLightRequestReturnsExactResultQuickly)
{
    EngineCommandBridge bridge;

    EngineCommandRequest request;
    request.kind = EngineCommandKind::InstantiateLight;
    request.instantiateLight.lightType = "directional";
    request.instantiateLight.requestedName = "Sun";
    request.instantiateLight.worldPosition = Vec3{ 0.0f, 10.0f, 0.0f };

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const std::optional<EngineCommandRequest> peeked = bridge.TryPeekPendingCommandRequest();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->kind, EngineCommandKind::InstantiateLight);
        EXPECT_EQ(peeked->instantiateLight.requestedName, "Sun");

        EngineCommandResult result;
        result.kind = EngineCommandKind::InstantiateLight;
        result.instantiateLight.success = true;
        result.instantiateLight.resolvedName = "Sun";
        bridge.FulfillCommand(result);
    });

    const auto start = std::chrono::steady_clock::now();
    const EngineCommandBridge::SubmitResult result = bridge.SubmitAndWait(request, 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    fulfiller.join();

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_FALSE(result.timedOut);
    ASSERT_TRUE(result.result.has_value());
    EXPECT_EQ(result.result->kind, EngineCommandKind::InstantiateLight);
    EXPECT_TRUE(result.result->instantiateLight.success);
    EXPECT_EQ(result.result->instantiateLight.resolvedName, "Sun");
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

} // namespace
} // namespace gte
