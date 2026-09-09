#include "Application/FrameCaptureBridge.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace gte {
namespace {

CapturedPngImage MakeImage(std::uint8_t seed, int width, int height)
{
    CapturedPngImage image;
    image.width = width;
    image.height = height;
    image.pngBytes = { seed, static_cast<std::uint8_t>(seed + 1), static_cast<std::uint8_t>(seed + 2) };
    return image;
}

// A request nobody ever fulfills must time out and report TimedOut, roughly
// within the requested timeout window - use a short timeout so the test
// itself stays fast.
TEST(FrameCaptureBridgeTest, RequestWithNoFulfillerTimesOut)
{
    FrameCaptureBridge bridge;

    const auto start = std::chrono::steady_clock::now();
    const FrameCaptureBridge::RequestResult result = bridge.RequestCaptureAndWait(FrameCaptureKind::Swapchain, 100);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.alreadyPending);
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(*result.failure, FrameCaptureFailureReason::TimedOut);
    EXPECT_FALSE(result.image.has_value());
    // Should not have waited drastically longer than the requested timeout.
    EXPECT_LT(elapsed, std::chrono::milliseconds(2000));
}

// A separate thread fulfilling the request shortly after it starts must
// wake the waiter well before a much longer timeout would have elapsed -
// proves the condition_variable wakeup, not the timeout, resolved it.
TEST(FrameCaptureBridgeTest, FulfilledRequestReturnsExactImageQuickly)
{
    FrameCaptureBridge bridge;

    std::thread fulfiller([&bridge] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bridge.FulfillPendingRequest(FrameCaptureKind::GameView, MakeImage(10, 640, 480));
    });

    const auto start = std::chrono::steady_clock::now();
    const FrameCaptureBridge::RequestResult result = bridge.RequestCaptureAndWait(FrameCaptureKind::GameView, 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    fulfiller.join();

    EXPECT_FALSE(result.alreadyPending);
    ASSERT_TRUE(result.image.has_value());
    EXPECT_FALSE(result.failure.has_value());
    EXPECT_EQ(result.image->width, 640);
    EXPECT_EQ(result.image->height, 480);
    const std::vector<std::uint8_t> expectedBytes = { 10, 11, 12 };
    EXPECT_EQ(result.image->pngBytes, expectedBytes);
    // Woken well before the 5 second timeout would have elapsed.
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

// A second request for the SAME kind, started strictly after the first has
// already set requested = true, must return alreadyPending == true
// IMMEDIATELY - never actually wait.
TEST(FrameCaptureBridgeTest, SecondConcurrentRequestOfSameKindReturnsAlreadyPendingImmediately)
{
    FrameCaptureBridge bridge;

    std::thread firstRequester([&bridge] {
        // Nobody ever fulfills or fails this one - it will simply time out
        // on its own after this test's own assertions are done reading it.
        bridge.RequestCaptureAndWait(FrameCaptureKind::Swapchain, 1500);
    });

    // Give the first request a moment to actually register as "requested".
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto start = std::chrono::steady_clock::now();
    const FrameCaptureBridge::RequestResult second = bridge.RequestCaptureAndWait(FrameCaptureKind::Swapchain, 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(second.alreadyPending);
    EXPECT_FALSE(second.image.has_value());
    EXPECT_FALSE(second.failure.has_value());
    // Must not have actually waited - a real wait would be at least
    // several hundred milliseconds given the timeouts above.
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));

    firstRequester.join();
}

// FailPendingRequest() must deliver TargetNotAvailable correctly, well
// before any timeout.
TEST(FrameCaptureBridgeTest, FailedRequestReturnsTargetNotAvailableQuickly)
{
    FrameCaptureBridge bridge;

    std::thread failer([&bridge] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bridge.FailPendingRequest(FrameCaptureKind::GameView, FrameCaptureFailureReason::TargetNotAvailable);
    });

    const auto start = std::chrono::steady_clock::now();
    const FrameCaptureBridge::RequestResult result = bridge.RequestCaptureAndWait(FrameCaptureKind::GameView, 5000);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    failer.join();

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_FALSE(result.image.has_value());
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(*result.failure, FrameCaptureFailureReason::TargetNotAvailable);
    EXPECT_LT(elapsed, std::chrono::milliseconds(4000));
}

// A late Fulfill/Fail call, arriving AFTER a request already timed out and
// reset itself, must be a safe no-op - and must not corrupt the NEXT
// request's own result.
TEST(FrameCaptureBridgeTest, LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest)
{
    FrameCaptureBridge bridge;

    const FrameCaptureBridge::RequestResult timedOut = bridge.RequestCaptureAndWait(FrameCaptureKind::Swapchain, 50);
    ASSERT_TRUE(timedOut.failure.has_value());
    EXPECT_EQ(*timedOut.failure, FrameCaptureFailureReason::TimedOut);

    // This arrives long after the waiter already gave up - must not crash
    // or assert, and must not affect anything.
    bridge.FulfillPendingRequest(FrameCaptureKind::Swapchain, MakeImage(99, 1, 1));
    bridge.FailPendingRequest(FrameCaptureKind::Swapchain, FrameCaptureFailureReason::TargetNotAvailable);

    // A fresh request afterward must behave completely normally.
    std::thread fulfiller([&bridge] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        bridge.FulfillPendingRequest(FrameCaptureKind::Swapchain, MakeImage(20, 100, 200));
    });

    const FrameCaptureBridge::RequestResult fresh = bridge.RequestCaptureAndWait(FrameCaptureKind::Swapchain, 5000);
    fulfiller.join();

    ASSERT_TRUE(fresh.image.has_value());
    EXPECT_EQ(fresh.image->width, 100);
    EXPECT_EQ(fresh.image->height, 200);
}

// Swapchain and GameView slots must be provably independent - a pending
// request on one kind never blocks/interferes with a concurrent request on
// the other kind.
TEST(FrameCaptureBridgeTest, SwapchainAndGameViewSlotsAreIndependent)
{
    FrameCaptureBridge bridge;

    std::thread pendingSwapchainRequester([&bridge] {
        bridge.RequestCaptureAndWait(FrameCaptureKind::Swapchain, 1500);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // A concurrent GameView request must proceed completely normally,
    // unaffected by the pending Swapchain request.
    std::thread fulfiller([&bridge] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        bridge.FulfillPendingRequest(FrameCaptureKind::GameView, MakeImage(5, 16, 16));
    });

    const FrameCaptureBridge::RequestResult gameViewResult = bridge.RequestCaptureAndWait(FrameCaptureKind::GameView, 5000);
    fulfiller.join();

    ASSERT_TRUE(gameViewResult.image.has_value());
    EXPECT_EQ(gameViewResult.image->width, 16);
    EXPECT_EQ(gameViewResult.image->height, 16);

    // A second, immediate Swapchain request must still see the first one as
    // still pending (proves the GameView activity above never touched the
    // Swapchain slot).
    const FrameCaptureBridge::RequestResult secondSwapchain = bridge.RequestCaptureAndWait(FrameCaptureKind::Swapchain, 100);
    EXPECT_TRUE(secondSwapchain.alreadyPending);

    pendingSwapchainRequester.join();
}

// IsCaptureRequested() must reflect whichever slot currently has a pending
// request, and only that slot.
TEST(FrameCaptureBridgeTest, IsCaptureRequestedReflectsPendingState)
{
    FrameCaptureBridge bridge;

    EXPECT_FALSE(bridge.IsCaptureRequested(FrameCaptureKind::Swapchain));
    EXPECT_FALSE(bridge.IsCaptureRequested(FrameCaptureKind::GameView));

    std::thread requester([&bridge] {
        bridge.RequestCaptureAndWait(FrameCaptureKind::GameView, 1000);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_FALSE(bridge.IsCaptureRequested(FrameCaptureKind::Swapchain));
    EXPECT_TRUE(bridge.IsCaptureRequested(FrameCaptureKind::GameView));

    bridge.FulfillPendingRequest(FrameCaptureKind::GameView, MakeImage(1, 2, 2));
    requester.join();

    EXPECT_FALSE(bridge.IsCaptureRequested(FrameCaptureKind::GameView));
}

} // namespace
} // namespace gte
