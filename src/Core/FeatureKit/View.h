#pragma once

#include "ViewJitter.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/RenderGraph/RenderGraphHandles.h"
#include "../Extent.h"

namespace gte::fx {

// What a feature callback knows about the view it runs for. Plain data, no GPU types.
// viewProjection and invViewProjection are never jittered.
struct View {
    rg::ViewScope scope = rg::ViewScope::Shared;
    Extent2D extent{};
    Vec3 eye;
    Mat4 viewProjection = Mat4::Identity();
    Mat4 invViewProjection = Mat4::Identity();
    bool invViewProjectionValid = false;
    // Jitter of this view's main scene draw this frame, in NDC. (0, 0) when none.
    Vec2 jitterNdc;

    // The matrix the view's scene color and depth were drawn with. Equals viewProjection without jitter.
    Mat4 DrawViewProjection() const noexcept { return ApplyClipJitter(viewProjection, jitterNdc); }
};

} // namespace gte::fx
