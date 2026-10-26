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
    m_presenter->EndOffscreenRecording();
}

} // namespace gte
