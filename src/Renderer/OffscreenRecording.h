#pragma once

#include "RenderGraph/RecordingContext.h"

namespace gte {

class FramePresenter;
class Renderer;

// One open recording of the offscreen command buffer.
// Submit() ends, submits and waits (bounded). Leaving scope without it discards the recording.
class OffscreenRecording {
public:
    ~OffscreenRecording();

    OffscreenRecording(const OffscreenRecording&) = delete;
    OffscreenRecording& operator=(const OffscreenRecording&) = delete;

    rg::RecordingContext Context() const noexcept { return m_context; }

    // Throws on failure. A recording whose submit failed is never discarded: the GPU may still read it.
    void Submit();

private:
    friend class Renderer;
    friend class FramePresenter;

    // Waits (bounded) for the previous offscreen submit, then begins recording. Throws on failure.
    OffscreenRecording(FramePresenter& presenter, Renderer* renderer);

    FramePresenter* m_presenter;
    rg::RecordingContext m_context;
    bool m_pending = true;
};

} // namespace gte
