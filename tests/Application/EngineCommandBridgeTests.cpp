#include "Application/EngineCommandBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
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

// Spins until a request is pending.
void WaitUntilPending(const EngineCommandBridge& bridge)
{
    while (!bridge.IsCommandPending()) {
        std::this_thread::yield();
    }
}

// A request nobody fulfills must time out within the requested window.
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

// A fulfiller thread must wake the waiter long before the timeout.
TEST(EngineCommandBridgeTest, FulfilledRequestReturnsExactResultQuickly)
{
    EngineCommandBridge bridge;

    std::thread fulfiller([&bridge] {
        WaitUntilPending(bridge);
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->request.instantiatePrimitive.requestedName, "Foo");
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeInstantiateResult(true, "Foo")));
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

// A second submit while one is pending returns alreadyPending immediately, whatever its kind.
TEST(EngineCommandBridgeTest, SecondConcurrentRequestReturnsAlreadyPendingImmediately)
{
    EngineCommandBridge bridge;

    std::thread firstRequester([&bridge] {
        bridge.SubmitAndWait(MakeInstantiateRequest("First"), 1500);
    });

    WaitUntilPending(bridge);

    EngineCommandRequest deleteRequest;
    deleteRequest.kind = EngineCommandKind::DeleteEntity;
    deleteRequest.deleteEntity.name = "Second";

    const auto start = std::chrono::steady_clock::now();
    const EngineCommandBridge::SubmitResult second = bridge.SubmitAndWait(deleteRequest, 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(second.alreadyPending);
    EXPECT_FALSE(second.timedOut);
    EXPECT_FALSE(second.result.has_value());
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));

    firstRequester.join();
}

// Pending state is observable and clears after fulfilment.
TEST(EngineCommandBridgeTest, PendingStateIsObservableAndClearsAfterFulfillment)
{
    EngineCommandBridge bridge;

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());

    std::thread requester([&bridge] {
        EngineCommandRequest deleteRequest;
        deleteRequest.kind = EngineCommandKind::DeleteEntity;
        deleteRequest.deleteEntity.name = "ToDelete";
        bridge.SubmitAndWait(deleteRequest, 1000);
    });

    WaitUntilPending(bridge);

    EXPECT_TRUE(bridge.IsCommandPending());
    const auto peeked = bridge.TryPeekPendingCommand();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_NE(peeked->id, 0u);
    EXPECT_EQ(peeked->request.kind, EngineCommandKind::DeleteEntity);
    EXPECT_EQ(peeked->request.deleteEntity.name, "ToDelete");

    EngineCommandResult result;
    result.kind = EngineCommandKind::DeleteEntity;
    result.deleteEntity.success = true;
    EXPECT_TRUE(bridge.FulfillCommand(peeked->id, result));
    requester.join();

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());
}

// Fulfilling with nothing pending is a dropped no-op.
TEST(EngineCommandBridgeTest, FulfillWithNothingPendingIsDropped)
{
    EngineCommandBridge bridge;
    EXPECT_FALSE(bridge.FulfillCommand(1, MakeInstantiateResult(true, "Orphan")));
    EXPECT_FALSE(bridge.IsCommandPending());
}

// A late fulfil after a timeout is dropped and does not corrupt the next request.
TEST(EngineCommandBridgeTest, LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest)
{
    EngineCommandBridge bridge;

    std::uint64_t idA = 0;
    std::thread peeker([&bridge, &idA] {
        WaitUntilPending(bridge);
        const auto peeked = bridge.TryPeekPendingCommand();
        if (peeked) {
            idA = peeked->id;
        }
    });
    const EngineCommandBridge::SubmitResult timedOut = bridge.SubmitAndWait(MakeInstantiateRequest("Foo"), 50);
    peeker.join();
    EXPECT_TRUE(timedOut.timedOut);
    ASSERT_NE(idA, 0u);

    EXPECT_FALSE(bridge.FulfillCommand(idA, MakeInstantiateResult(true, "Stale")));

    std::thread fulfiller([&bridge] {
        WaitUntilPending(bridge);
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeInstantiateResult(true, "Fresh")));
    });

    const EngineCommandBridge::SubmitResult fresh = bridge.SubmitAndWait(MakeInstantiateRequest("Fresh"), 5000);
    fulfiller.join();

    ASSERT_TRUE(fresh.result.has_value());
    EXPECT_EQ(fresh.result->instantiatePrimitive.resolvedName, "Fresh");
}

// A stale result must not wake a newer request that is already waiting.
TEST(EngineCommandBridgeTest, StaleFulfillWhileNextRequestWaitsIsDropped)
{
    EngineCommandBridge bridge;

    EngineCommandBridge::SubmitResult resultA;
    std::thread threadX([&] { resultA = bridge.SubmitAndWait(MakeInstantiateRequest("A"), 50); });

    WaitUntilPending(bridge);
    const auto peekedA = bridge.TryPeekPendingCommand();
    ASSERT_TRUE(peekedA.has_value());
    const std::uint64_t idA = peekedA->id;

    threadX.join();
    EXPECT_TRUE(resultA.timedOut);

    EngineCommandBridge::SubmitResult resultB;
    std::thread threadY([&] { resultB = bridge.SubmitAndWait(MakeInstantiateRequest("B"), 5000); });

    WaitUntilPending(bridge);
    const auto peekedB = bridge.TryPeekPendingCommand();
    ASSERT_TRUE(peekedB.has_value());
    const std::uint64_t idB = peekedB->id;
    EXPECT_NE(idA, idB);
    EXPECT_EQ(peekedB->request.instantiatePrimitive.requestedName, "B");

    EXPECT_FALSE(bridge.FulfillCommand(idA, MakeInstantiateResult(true, "Stale")));
    EXPECT_TRUE(bridge.IsCommandPending());

    EXPECT_TRUE(bridge.FulfillCommand(idB, MakeInstantiateResult(true, "Fresh")));
    threadY.join();

    EXPECT_FALSE(resultB.timedOut);
    ASSERT_TRUE(resultB.result.has_value());
    EXPECT_EQ(resultB.result->instantiatePrimitive.resolvedName, "Fresh");
}

// Ids strictly increase and 0 is never issued.
TEST(EngineCommandBridgeTest, RequestIdsIncreaseAndZeroIsNeverIssued)
{
    EngineCommandBridge bridge;

    std::uint64_t previousId = 0;
    for (int i = 0; i < 3; ++i) {
        EngineCommandBridge::SubmitResult submitted;
        std::thread requester([&] { submitted = bridge.SubmitAndWait(MakeInstantiateRequest("Req"), 5000); });

        WaitUntilPending(bridge);
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_NE(peeked->id, 0u);
        EXPECT_GT(peeked->id, previousId);
        previousId = peeked->id;

        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeInstantiateResult(true, "Ok")));
        requester.join();
        EXPECT_TRUE(submitted.result.has_value());
    }
}

// A wrong id is dropped and must not wake the waiter.
TEST(EngineCommandBridgeTest, FulfillWithWrongIdWhileRequestWaitsIsDroppedAndDoesNotWakeWaiter)
{
    EngineCommandBridge bridge;

    EngineCommandBridge::SubmitResult submitted;
    std::thread requester([&] { submitted = bridge.SubmitAndWait(MakeInstantiateRequest("Foo"), 5000); });

    WaitUntilPending(bridge);
    const auto peeked = bridge.TryPeekPendingCommand();
    ASSERT_TRUE(peeked.has_value());

    EXPECT_FALSE(bridge.FulfillCommand(peeked->id + 1000, MakeInstantiateResult(true, "Wrong")));
    EXPECT_TRUE(bridge.IsCommandPending());

    EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeInstantiateResult(true, "Right")));
    requester.join();

    ASSERT_TRUE(submitted.result.has_value());
    EXPECT_EQ(submitted.result->instantiatePrimitive.resolvedName, "Right");
}

// Round trip for SetEntityTrs through the generic bridge.
TEST(EngineCommandBridgeTest, FulfilledSetEntityTrsRequestReturnsExactResultQuickly)
{
    EngineCommandBridge bridge;

    EngineCommandRequest request;
    request.kind = EngineCommandKind::SetEntityTrs;
    request.setEntityTrs.name = "MyLight";
    request.setEntityTrs.hasTranslation = true;
    request.setEntityTrs.translation = Vec3{ 0.0f, 5.0f, -2.0f };

    std::thread fulfiller([&bridge] {
        WaitUntilPending(bridge);
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->request.kind, EngineCommandKind::SetEntityTrs);
        EXPECT_EQ(peeked->request.setEntityTrs.name, "MyLight");
        EXPECT_TRUE(peeked->request.setEntityTrs.hasTranslation);

        EngineCommandResult result;
        result.kind = EngineCommandKind::SetEntityTrs;
        result.setEntityTrs.success = true;
        result.setEntityTrs.translationChanged = true;
        result.setEntityTrs.resultingPosition = Vec3{ 0.0f, 5.0f, -2.0f };
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, result));
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

// Round trip for InstantiateLight through the generic bridge.
TEST(EngineCommandBridgeTest, FulfilledInstantiateLightRequestReturnsExactResultQuickly)
{
    EngineCommandBridge bridge;

    EngineCommandRequest request;
    request.kind = EngineCommandKind::InstantiateLight;
    request.instantiateLight.lightType = "directional";
    request.instantiateLight.requestedName = "Sun";
    request.instantiateLight.worldPosition = Vec3{ 0.0f, 10.0f, 0.0f };

    std::thread fulfiller([&bridge] {
        WaitUntilPending(bridge);
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->request.kind, EngineCommandKind::InstantiateLight);
        EXPECT_EQ(peeked->request.instantiateLight.requestedName, "Sun");

        EngineCommandResult result;
        result.kind = EngineCommandKind::InstantiateLight;
        result.instantiateLight.success = true;
        result.instantiateLight.resolvedName = "Sun";
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, result));
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
