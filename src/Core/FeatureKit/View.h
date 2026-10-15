#pragma once

#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/RenderGraph/RenderGraphHandles.h"
#include "../Extent.h"

namespace gte::fx {

// What a feature callback knows about the view it runs for. Plain data, no GPU types.
struct View {
    rg::ViewScope scope = rg::ViewScope::Shared;
    Extent2D extent{};
    Vec3 eye;
    Mat4 viewProjection = Mat4::Identity();
    Mat4 invViewProjection = Mat4::Identity();
    bool invViewProjectionValid = false;
};

} // namespace gte::fx
