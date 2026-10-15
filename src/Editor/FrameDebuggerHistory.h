#pragma once

#include "FrameDebuggerData.h"
#include "../Renderer/RenderTexture.h"

#include <optional>
#include <string>
#include <vector>

// The one retained Frame Debugger capture (not a history): a snapshot plus GPU copies of the captured frame.
// Owned by FrameDebuggerPanel.
namespace gte {

class Renderer;

namespace rg {
class RenderGraph;
} // namespace rg

struct FrameDebuggerHistoryEntry {
    FrameDebuggerSnapshot snapshot;

    // Copy of the Game View as drawn, before the composite stage. Always set after a capture.
    std::optional<RenderTexture> preview;

    // Copy of the final composited Game View. Empty when no composited source existed for this capture.
    std::optional<RenderTexture> compositedPreview;

    // One image per object drawn, in draw-record order: the Game View after objects [0..i]. A final entry may hold
    // the sky step. Indexed by FrameDebuggerEventDetails::stepPreviewIndex when stepPreviewKind is PerObjectStep.
    std::vector<RenderTexture> perObjectStepPreviews;
};

class FrameDebuggerCurrentCapture {
public:
    // Stores `snapshot`, copies `gameViewSource` (and `compositedGameViewSource` when not null) into fresh retained
    // textures, and takes the replay step previews out of `capture`. Replaces any previous capture.
    // `renderGraph` is unused and kept for the caller's signature.
    void CaptureFrame(Renderer& renderer, const rg::RenderGraph& renderGraph, const FrameDebuggerSnapshot& snapshot,
        RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource, FrameDebuggerCaptureContext& capture);

    bool HasCapture() const noexcept { return m_current.has_value(); }

    // Releases every retained GPU resource.
    void Clear() noexcept { m_current.reset(); }

    // Null when there is no capture.
    const FrameDebuggerHistoryEntry* CurrentEntry() const noexcept { return m_current ? &*m_current : nullptr; }

private:
    std::optional<FrameDebuggerHistoryEntry> m_current;
};

} // namespace gte
