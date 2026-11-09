#pragma once

// Which offscreen path a frame takes and which slot its CPU writes target. Pure rules, so they run without a device.

#include "../Renderer/OffscreenFrameTicket.h"

#include <cstdint>
#include <optional>

namespace gte {

enum class OffscreenRecordingPath : std::uint8_t {
    Legacy, // Record, submit and wait for the GPU in one go.
    Async,  // Record into the slot buffer and submit without waiting.
};

// One frame in flight keeps the legacy path; so does a capture frame, which recreates images and must run alone.
constexpr OffscreenRecordingPath ChooseOffscreenRecordingPath(bool overlapped, bool captureArmed) noexcept
{
    return (overlapped && !captureArmed) ? OffscreenRecordingPath::Async : OffscreenRecordingPath::Legacy;
}

// The slot this frame's CPU writes into GPU-visible memory target. Without overlap that is the synchronous slot;
// with overlap it is the ticket, or nothing when no slot was free.
constexpr std::optional<OffscreenFrameTicket> WriteTicketFor(
    bool overlapped, const std::optional<OffscreenFrameTicket>& ticket) noexcept
{
    return overlapped ? ticket : std::optional<OffscreenFrameTicket>(OffscreenFrameTicket{});
}

// The frame build takes the slot itself only when the update never tried; a failed wait is not repeated.
constexpr bool BuildMustAcquireSlot(bool hasTicket, bool slotWaitAttempted) noexcept
{
    return !hasTicket && !slotWaitAttempted;
}

} // namespace gte
