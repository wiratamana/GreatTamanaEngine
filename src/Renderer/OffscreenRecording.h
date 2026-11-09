#pragma once

#include "OffscreenFrameTicket.h"
#include "RenderGraph/RecordingContext.h"

namespace gte {

class FramePresenter;
class Renderer;

// One open recording of an offscreen command buffer.
// A synchronous recording's Submit() ends, submits and waits (bounded); an async one (see Ticket()) only submits.
// Leaving scope without Submit() discards the recording.
class OffscreenRecording {
public:
    ~OffscreenRecording();

    OffscreenRecording(const OffscreenRecording&) = delete;
    OffscreenRecording& operator=(const OffscreenRecording&) = delete;

    rg::RecordingContext Context() const noexcept { return m_context; }

    // Slot and serial of the frame this recording belongs to; serial 0 marks a synchronous recording.
    const OffscreenFrameTicket& Ticket() const noexcept { return m_ticket; }
    bool IsAsync() const noexcept { return m_ticket.IsAsync(); }

    // Throws on failure. A recording whose submit failed is never discarded: the GPU may still read it.
    void Submit();

private:
    friend class Renderer;
    friend class FramePresenter;

    // Drains the offscreen ring and waits (bounded) for the previous synchronous submit, then begins recording.
    // Throws on failure.
    OffscreenRecording(FramePresenter& presenter, Renderer* renderer);

    // Begins recording into the slot of `ticket`, whose slot wait must already have completed. Throws on failure.
    OffscreenRecording(FramePresenter& presenter, Renderer* renderer, const OffscreenFrameTicket& ticket);

    FramePresenter* m_presenter;
    rg::RecordingContext m_context;
    OffscreenFrameTicket m_ticket;
    bool m_pending = true;
};

} // namespace gte
