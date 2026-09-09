#include "FrameCaptureBridge.h"

#include <chrono>
#include <utility>

namespace gte {

FrameCaptureBridge::Slot& FrameCaptureBridge::SlotFor(FrameCaptureKind kind)
{
    return kind == FrameCaptureKind::Swapchain ? m_swapchainSlot : m_gameViewSlot;
}

const FrameCaptureBridge::Slot& FrameCaptureBridge::SlotFor(FrameCaptureKind kind) const
{
    return kind == FrameCaptureKind::Swapchain ? m_swapchainSlot : m_gameViewSlot;
}

FrameCaptureBridge::RequestResult FrameCaptureBridge::RequestCaptureAndWait(FrameCaptureKind kind, int timeoutMilliseconds)
{
    Slot& slot = SlotFor(kind);

    std::unique_lock<std::mutex> lock(slot.mutex);

    if (slot.requested) {
        // Someone else's request of this same kind is already in flight -
        // never wait, return the "already pending" outcome immediately
        // (HTTP 503 - see PHASE0_MASTER_STRATEGY.md's Locked Design
        // Decision #4).
        RequestResult result;
        result.alreadyPending = true;
        return result;
    }

    slot.requested = true;
    slot.fulfilled = false;
    slot.result.reset();
    slot.failureReason.reset();

    const bool fulfilledInTime = slot.conditionVariable.wait_for(
        lock,
        std::chrono::milliseconds(timeoutMilliseconds),
        [&slot] { return slot.fulfilled; });

    RequestResult result;
    if (fulfilledInTime) {
        result.image = std::move(slot.result);
        result.failure = slot.failureReason;
        slot.requested = false;
        slot.result.reset();
        slot.failureReason.reset();
        return result;
    }

    // Timed out - reset back to idle so this slot isn't stuck "pending"
    // forever; a late FulfillPendingRequest()/FailPendingRequest() call
    // arriving after this point must be a safe, silent no-op (see below).
    slot.requested = false;
    result.failure = FrameCaptureFailureReason::TimedOut;
    return result;
}

bool FrameCaptureBridge::IsCaptureRequested(FrameCaptureKind kind) const
{
    const Slot& slot = SlotFor(kind);
    std::lock_guard<std::mutex> lock(slot.mutex);
    return slot.requested;
}

void FrameCaptureBridge::FulfillPendingRequest(FrameCaptureKind kind, CapturedPngImage image)
{
    Slot& slot = SlotFor(kind);
    {
        std::lock_guard<std::mutex> lock(slot.mutex);
        if (!slot.requested) {
            // Nobody is actually waiting (already timed out, or nobody ever
            // asked) - inert no-op, never crash/assert.
            return;
        }
        slot.result = std::move(image);
        slot.failureReason.reset();
        slot.fulfilled = true;
    }
    slot.conditionVariable.notify_one();
}

void FrameCaptureBridge::FailPendingRequest(FrameCaptureKind kind, FrameCaptureFailureReason reason)
{
    Slot& slot = SlotFor(kind);
    {
        std::lock_guard<std::mutex> lock(slot.mutex);
        if (!slot.requested) {
            return;
        }
        slot.result.reset();
        slot.failureReason = reason;
        slot.fulfilled = true;
    }
    slot.conditionVariable.notify_one();
}

} // namespace gte
