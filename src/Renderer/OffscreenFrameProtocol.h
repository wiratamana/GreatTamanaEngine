#pragma once

// Wait rules of the offscreen frame ring: only a wait that Completed may finish a slot or free retired objects.
// The waits themselves are passed in, so the rules run without a device.

#include "OffscreenFrameRing.h"
#include "OffscreenFrameTicket.h"
#include "RenderTimingObserver.h"
#include "RetireQueue.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace gte {

class OffscreenFrameProtocol {
public:
    explicit OffscreenFrameProtocol(std::uint32_t slotCount) noexcept
        : m_ring(slotCount)
    {
    }

    const OffscreenFrameRing& Ring() const noexcept { return m_ring; }
    OffscreenFrameTicket Current() const noexcept { return m_ring.Current(); }

    // True when offscreen frames overlap; otherwise nothing is in flight while a frame is built.
    void SetOverlapped(bool overlapped) noexcept { m_overlapped = overlapped; }
    bool Overlapped() const noexcept { return m_overlapped; }

    // Opens the frame about to be built. `waitSlot(slot)` returns the outcome of a wait on the fence of the frame
    // last submitted into `slot`; it runs only when that frame is still pending. Repeats return the same ticket until
    // the frame is submitted. nullopt when the wait did not complete: nothing was opened or freed.
    template <class WaitSlot>
    std::optional<OffscreenFrameTicket> BeginFrame(WaitSlot&& waitSlot)
    {
        if (!m_ring.FrameOpen()) {
            const std::uint32_t slot = m_ring.Current().slot;
            if (m_ring.SlotPending(slot)) {
                if (waitSlot(slot) != BlockOutcome::Completed) {
                    return std::nullopt;
                }
                m_ring.NoteSlotComplete(slot);
                m_retired.Collect(m_ring.CompletedSerial());
            }
            m_ring.OpenFrame();
        }
        return m_ring.Current();
    }

    // Waits for every pending slot. `waitSlots(slots)` gets the pending slots and returns the outcome of waiting for
    // all of them. Anything but Completed leaves the ring and the retire queue untouched.
    template <class WaitSlots>
    BlockOutcome DrainAll(WaitSlots&& waitSlots)
    {
        std::array<std::uint32_t, OffscreenFrameRing::kMaxSlots> pending{};
        std::size_t count = 0;
        for (std::uint32_t slot = 0; slot < m_ring.SlotCount(); ++slot) {
            if (m_ring.SlotPending(slot)) {
                pending[count++] = slot;
            }
        }
        const BlockOutcome outcome = waitSlots(std::span<const std::uint32_t>(pending.data(), count));
        if (outcome == BlockOutcome::Completed) {
            m_ring.NoteAllComplete();
            if (count > 0) {
                m_retired.Collect(m_ring.CompletedSerial());
            }
        }
        return outcome;
    }

    // The frame being built reached the queue: its slot is pending and the next frame takes the next slot.
    void NoteSubmitted() noexcept { m_ring.NoteSubmitted(); }

    // Call after a wait outside the ring completed (the legacy fence) and the ring is drained.
    void CollectAfterCompletedWait() { m_retired.Collect(m_ring.CompletedSerial()); }

    // Keeps a dropped object alive until the frame being built, and every frame before it, is done.
    // Without overlap nothing is in flight, so the tag is 0 and the next completed wait frees it.
    template <class T>
    void Retire(T&& object)
    {
        m_retired.Push(m_overlapped ? m_ring.Current().serial : 0, std::forward<T>(object));
    }

    RetireQueue& Retired() noexcept { return m_retired; }
    std::size_t RetiredPending() const noexcept { return m_retired.Pending(); }

private:
    OffscreenFrameRing m_ring;
    RetireQueue m_retired;
    bool m_overlapped = false;
};

} // namespace gte
