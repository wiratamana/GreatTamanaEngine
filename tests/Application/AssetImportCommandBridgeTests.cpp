#include "Application/AssetImportCommandBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>

namespace gte {
namespace {

AssetImportCommandRequest MakeImportRequest(const std::string& sourceAbsolutePath,
    const std::string& destinationRelativeFolder = std::string())
{
    AssetImportCommandRequest request;
    request.kind = AssetImportCommandKind::ImportExternalFile;
    request.importExternalFile.sourceAbsolutePath = sourceAbsolutePath;
    request.importExternalFile.destinationRelativeFolder = destinationRelativeFolder;
    return request;
}

AssetImportCommandResult MakeImportResult(bool success, const std::string& message = std::string())
{
    AssetImportCommandResult result;
    result.kind = AssetImportCommandKind::ImportExternalFile;
    result.importExternalFile.projectAvailable = true;
    result.importExternalFile.success = success;
    result.importExternalFile.message = message;
    return result;
}

// A request nobody ever fulfills must time out, roughly within the
// requested timeout window.
TEST(AssetImportCommandBridgeTest, SubmitAndWaitTimesOutWhenNeverFulfilled)
{
    AssetImportCommandBridge bridge;

    const auto start = std::chrono::steady_clock::now();
    const AssetImportCommandBridge::SubmitResult result =
        bridge.SubmitAndWait(MakeImportRequest("C:\\some\\file.stl"), 50);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.result.has_value());
    EXPECT_LT(elapsed, std::chrono::milliseconds(2000));
}

// A separate thread fulfilling the request shortly after it starts must
// wake the waiter well before a much longer timeout would have elapsed -
// proves the condition_variable wakeup, not the timeout, resolved it.
TEST(AssetImportCommandBridgeTest, SubmitAndWaitReturnsFulfilledResult)
{
    AssetImportCommandBridge bridge;

    std::thread fulfiller([&bridge] {
        // Wait until the request is actually pending before fulfilling it.
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->request.importExternalFile.sourceAbsolutePath, "C:\\some\\file.stl");
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeImportResult(true, "ok")));
    });

    const auto start = std::chrono::steady_clock::now();
    const AssetImportCommandBridge::SubmitResult result =
        bridge.SubmitAndWait(MakeImportRequest("C:\\some\\file.stl"), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    fulfiller.join();

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_FALSE(result.timedOut);
    ASSERT_TRUE(result.result.has_value());
    EXPECT_EQ(result.result->kind, AssetImportCommandKind::ImportExternalFile);
    EXPECT_TRUE(result.result->importExternalFile.success);
    EXPECT_EQ(result.result->importExternalFile.message, "ok");
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

// A second concurrent SubmitAndWait() call, started strictly after the
// first has already registered as pending, must return alreadyPending ==
// true IMMEDIATELY - never actually wait.
TEST(AssetImportCommandBridgeTest, SubmitAndWaitReturnsAlreadyPendingWhenAnotherRequestIsInFlight)
{
    AssetImportCommandBridge bridge;

    std::thread firstRequester([&bridge] {
        // Nobody ever fulfills this one - it will simply time out on its
        // own after this test's own assertions are done reading it.
        bridge.SubmitAndWait(MakeImportRequest("C:\\first.stl"), 1500);
    });

    // Give the first request a moment to actually register as pending.
    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto start = std::chrono::steady_clock::now();
    const AssetImportCommandBridge::SubmitResult second =
        bridge.SubmitAndWait(MakeImportRequest("C:\\second.stl"), 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(second.alreadyPending);
    EXPECT_FALSE(second.timedOut);
    EXPECT_FALSE(second.result.has_value());
    // Must not have actually waited - a real wait would be at least
    // several hundred milliseconds given the timeouts above.
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));

    firstRequester.join();
}

// Fulfilling with nothing pending is dropped.
TEST(AssetImportCommandBridgeTest, FulfillCommandIsDroppedWhenNothingIsPending)
{
    AssetImportCommandBridge bridge;
    EXPECT_FALSE(bridge.FulfillCommand(1, MakeImportResult(true)));
    EXPECT_FALSE(bridge.IsCommandPending());
}

// A fresh bridge with nothing submitted returns std::nullopt.
TEST(AssetImportCommandBridgeTest, TryPeekPendingCommandReturnsNulloptWhenIdle)
{
    AssetImportCommandBridge bridge;
    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());
}

// A late fulfil after a timeout is dropped and does not corrupt the next request.
TEST(AssetImportCommandBridgeTest, LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest)
{
    AssetImportCommandBridge bridge;

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
    const AssetImportCommandBridge::SubmitResult timedOut = bridge.SubmitAndWait(MakeImportRequest("C:\\a.stl"), 50);
    peeker.join();
    EXPECT_TRUE(timedOut.timedOut);
    ASSERT_NE(idA, 0u);

    EXPECT_FALSE(bridge.FulfillCommand(idA, MakeImportResult(true, "late")));

    std::thread fulfiller([&bridge] {
        while (!bridge.IsCommandPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto peeked = bridge.TryPeekPendingCommand();
        ASSERT_TRUE(peeked.has_value());
        EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeImportResult(true, "fresh")));
    });

    const AssetImportCommandBridge::SubmitResult fresh = bridge.SubmitAndWait(MakeImportRequest("C:\\b.stl"), 5000);
    fulfiller.join();

    ASSERT_TRUE(fresh.result.has_value());
    EXPECT_TRUE(fresh.result->importExternalFile.success);
    EXPECT_EQ(fresh.result->importExternalFile.message, "fresh");
}

// Pending state is observable and clears after fulfilment.
TEST(AssetImportCommandBridgeTest, PendingStateIsObservableAndClearsAfterFulfillment)
{
    AssetImportCommandBridge bridge;

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());

    std::thread requester([&bridge] {
        bridge.SubmitAndWait(MakeImportRequest("C:\\terrain.stl", "Meshes/Terrain"), 1000);
    });

    while (!bridge.IsCommandPending()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(bridge.IsCommandPending());
    const auto peeked = bridge.TryPeekPendingCommand();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(peeked->request.kind, AssetImportCommandKind::ImportExternalFile);
    EXPECT_EQ(peeked->request.importExternalFile.sourceAbsolutePath, "C:\\terrain.stl");
    EXPECT_EQ(peeked->request.importExternalFile.destinationRelativeFolder, "Meshes/Terrain");

    EXPECT_TRUE(bridge.FulfillCommand(peeked->id, MakeImportResult(false, "nope")));
    requester.join();

    EXPECT_FALSE(bridge.IsCommandPending());
    EXPECT_FALSE(bridge.TryPeekPendingCommand().has_value());
}

} // namespace
} // namespace gte
