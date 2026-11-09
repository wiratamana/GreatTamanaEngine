#include "OffscreenRecording.h"

#include "FramePresenter.h"
#include "RenderGraph/RecordingAccess.h"

#include <stdexcept>

namespace gte {

OffscreenRecording::OffscreenRecording(FramePresenter& presenter, Renderer* renderer)
    : m_presenter(&presenter)
    , m_context(rg::RecordingAccess::Wrap(presenter.BeginOffscreenRecording(), renderer))
{
}

OffscreenRecording::OffscreenRecording(FramePresenter& presenter, Renderer* renderer, const OffscreenFrameTicket& ticket)
    : m_presenter(&presenter)
    , m_context(rg::RecordingAccess::Wrap(presenter.BeginAsyncOffscreenRecording(ticket), renderer))
    , m_ticket(ticket)
{
}

OffscreenRecording::~OffscreenRecording()
{
    if (m_pending) {
        m_presenter->DiscardOffscreen();
    }
}

void OffscreenRecording::Submit()
{
    if (!m_pending) {
        throw std::logic_error("offscreen recording already submitted");
    }
    m_pending = false;
    if (m_ticket.IsAsync()) {
        m_presenter->EndOffscreenSlotRecording();
    } else {
        m_presenter->EndOffscreenRecording();
    }
}

} // namespace gte
