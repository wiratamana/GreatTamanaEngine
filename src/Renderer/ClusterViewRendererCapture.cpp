#include "ClusterViewRenderer.h"
#include "ClusterViewRendererImpl.h"

#include <exception>

namespace gte {

std::optional<CapturedPixels> ClusterViewRenderer::CaptureSlot(ClusterViewSlot slot)
{
    Impl& s = *m_impl;
    Impl::SlotTarget& target = slot == ClusterViewSlot::Main ? s.mainSlot : s.previewSlot;
    if (!target.texture.has_value()) {
        return std::nullopt;
    }
    try {
        CapturedPixels pixels = s.renderer.CaptureRenderTexturePixels(*target.texture);
        if (pixels.pixels.empty()) {
            return std::nullopt;
        }
        return pixels;
    } catch (const std::exception& e) {
        s.LogOnce(std::string("the slot capture failed: ") + e.what());
        return std::nullopt;
    }
}

} // namespace gte
