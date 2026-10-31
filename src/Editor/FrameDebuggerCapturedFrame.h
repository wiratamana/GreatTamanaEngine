#pragma once

#include "FrameDebuggerData.h"
#include "../Renderer/RenderTexture.h"

#include <optional>
#include <string>
#include <vector>

namespace gte {

class Renderer;

// One captured frame: the snapshot plus GPU copies of the primary view.
struct FrameDebuggerCapturedFrame {
    FrameDebuggerSnapshot snapshot;

    // Copy of the primary view as drawn, before the composite stage. Set after every capture.
    std::optional<RenderTexture> preview;

    // Copy of the final composited primary view. Empty when no composited source existed.
    std::optional<RenderTexture> compositedPreview;

    // The view after objects [0..i], in draw-record order; a final entry may hold the background step.
    // Indexed by FrameDebuggerEventDetails::stepPreviewIndex when stepPreviewKind is PerObjectStep.
    std::vector<RenderTexture> perObjectStepPreviews;
};

// Holds the single retained capture. Owned by FrameDebuggerPanel.
class FrameDebuggerCurrentCapture {
public:
    // Stores `snapshot`, copies `gameViewSource` (and `compositedGameViewSource` when not null) into fresh retained
    // textures, and takes the replay step previews out of `capture`. Replaces any previous capture. Throws on GPU
    // failure and then leaves the previous capture untouched.
    void CaptureFrame(Renderer& renderer, const FrameDebuggerSnapshot& snapshot, RenderTexture& gameViewSource,
        RenderTexture* compositedGameViewSource, FrameDebuggerCaptureContext& capture);

    bool HasCapture() const noexcept { return m_current.has_value(); }

    // Releases every retained GPU resource.
    void Clear() noexcept { m_current.reset(); }

    // Null when there is no capture.
    const FrameDebuggerCapturedFrame* CurrentFrame() const noexcept { return m_current ? &*m_current : nullptr; }

private:
    std::optional<FrameDebuggerCapturedFrame> m_current;
};

} // namespace gte
