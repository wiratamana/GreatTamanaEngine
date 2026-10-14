#pragma once

#include "../../Renderer/RenderGraph/RenderGraphHandles.h"

#include <functional>

namespace gte {

// The current view's own already-rendered color/depth, for exactly the view the callback was invoked for.
// colorHandle/depthHandle carry the same underlying value today (color and depth are two aspects of one
// imported texture) - they are separate fields so a callback names which aspect each call declares.
// To sample one in a compute pass: pass.ReadTexture(handle, ...) then
// pass.BindCombinedImageSampler(binding, handle, rg::TextureAspect::Color/Depth).
//
// VALID FOR EXACTLY ONE CALLBACK INVOCATION. Never store a copy past the single invocation it was handed in.
struct ScenePassReadHandles {
    rg::TextureHandle colorHandle;
    rg::TextureHandle depthHandle;
};

// Shared callback type for both the PostOpaque and PostTransparent stages -
// one shape, not two near-duplicates. Trusted to call
// builder.AddRenderPass()/AddPass()/AddComputePass() itself; every pass it
// declares must carry the RenderPassEvent tag required by whichever stage
// registered it (AfterOpaques for PostOpaque, AfterTransparents for
// PostTransparent).
using ProjectScenePassCallback = std::function<void(rg::RenderGraphBuilder& builder,
    rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
    const ScenePassReadHandles& currentViewHandles)>;

} // namespace gte
