#include "ClusterViewRenderer.h"
#include "ClusterViewRendererImpl.h"

#include <utility>

namespace gte {

std::optional<CapturedPixels> ClusterViewRenderer::CaptureSlot(ClusterViewSlot slot)
{
    Impl& s = *m_impl;
    Impl::SlotTarget& target = slot == ClusterViewSlot::Main ? s.mainSlot : s.previewSlot;
    if (!target.texture.has_value()) {
        return std::nullopt;
    }
    CaptureResult captured = s.renderer.CaptureRenderTexturePixels(*target.texture);
    if (captured.status != CaptureStatus::Ok || captured.pixels.pixels.empty()) {
        if (captured.status != CaptureStatus::NothingToCapture) {
            s.LogOnce("the slot capture failed");
        }
        return std::nullopt;
    }
    return std::move(captured.pixels);
}

} // namespace gte
