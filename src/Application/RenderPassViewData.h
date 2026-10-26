#pragma once

#include "../Core/Plugins/RenderViewRegistry.h"
#include "../Math/Mat4.h"
#include "../Math/Vec3.h"
#include "../Renderer/RenderGraph/RecordingContext.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"
#include "../Renderer/RenderGraph/RenderPipeline.h"

#include <functional>

namespace gte {

class RenderTexture;

// One view's per-frame rendering data, built by Application each frame for every non-null view target.
struct RenderPassViewData {
    rg::RenderViewId id;
    rg::TextureHandle colorTarget;
    // Raw target for sampler/extent/format queries; the graph cannot resolve depth samplers itself.
    RenderTexture* renderTexture = nullptr;
    float aspectWidthOverHeight = 1.0f;
    Mat4 viewProjection;
    Vec3 eyeWorldPosition;

    // Secondary view only: draws the ground-grid overlay.
    std::function<void(const rg::RecordingContext&, const Mat4&)> recordSceneOverlay;
};

// Maps a named view to the ViewScope used by the Frame Debugger, Profiler and per-view pass identity.
inline rg::ViewScope TranslateLegacyViewScope(rg::RenderViewId view) noexcept
{
    if (view == rg::RenderViewId::Named(kPrimaryViewName)) {
        return rg::ViewScope::Primary;
    }
    if (view == rg::RenderViewId::Named(kSecondaryViewName)) {
        return rg::ViewScope::Secondary;
    }
    return rg::ViewScope::Shared; // Shared() itself, or any named view with no scope.
}

} // namespace gte
