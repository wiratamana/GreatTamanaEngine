#include "FrameCaptureBridge.h"

#include <chrono>
#include <utility>

namespace gte {

FrameCaptureBridge::Slot& FrameCaptureBridge::SlotFor(FrameCaptureKind kind)
{
    switch (kind) {
    case FrameCaptureKind::Swapchain: return m_swapchainSlot;
    case FrameCaptureKind::GameView: return m_gameViewSlot;
    case FrameCaptureKind::NamedTexture: return m_namedTextureSlot;
    }
    return m_namedTextureSlot; // Unreachable - every enumerator handled above (no default: case, mirroring this codebase's own exhaustive-switch convention, e.g. RenderGraphTypes.h). Silences a "not all control paths return a value" warning on a compiler that doesn't prove the switch exhaustive on its own.
}

const FrameCaptureBridge::Slot& FrameCaptureBridge::SlotFor(FrameCaptureKind kind) const
{
    switch (kind) {
    case FrameCaptureKind::Swapchain: return m_swapchainSlot;
    case FrameCaptureKind::GameView: return m_gameViewSlot;
    case FrameCaptureKind::NamedTexture: return m_namedTextureSlot;
    }
    return m_namedTextureSlot; // Unreachable - see the non-const overload's own comment above.
}

FrameCaptureBridge::RequestResult FrameCaptureBridge::RequestCaptureAndWait(FrameCaptureKind kind, int timeoutMilliseconds,
    const std::string& textureName, DebugTextureChannel channel)
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

    if (kind == FrameCaptureKind::NamedTexture) {
        // Set BEFORE marking `requested = true` below, still under the same
        // lock, so no other thread can ever observe a request with a stale
        // name/channel (see FrameCaptureBridge.h's own doc comment on
        // RequestedTextureName()/RequestedTextureChannel()).
        m_requestedTextureName = textureName;
        m_requestedTextureChannel = channel;
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

std::string FrameCaptureBridge::RequestedTextureName() const
{
    std::lock_guard<std::mutex> lock(m_namedTextureSlot.mutex);
    if (!m_namedTextureSlot.requested) {
        return std::string();
    }
    return m_requestedTextureName;
}

DebugTextureChannel FrameCaptureBridge::RequestedTextureChannel() const
{
    std::lock_guard<std::mutex> lock(m_namedTextureSlot.mutex);
    if (!m_namedTextureSlot.requested) {
        return DebugTextureChannel::Color;
    }
    return m_requestedTextureChannel;
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

void FrameCaptureBridge::PublishTextureList(std::vector<PublishedTextureListEntry> entries)
{
    std::lock_guard<std::mutex> lock(m_textureListMutex);
    m_publishedTextureList = std::move(entries);
}

std::vector<PublishedTextureListEntry> FrameCaptureBridge::GetPublishedTextureList() const
{
    std::lock_guard<std::mutex> lock(m_textureListMutex);
    return m_publishedTextureList;
}

} // namespace gte
