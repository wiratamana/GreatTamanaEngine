#pragma once

#include "../../Renderer/RenderGraph/RenderGraphHandles.h"
#include "../GraphicsHandles.h"

#include <functional>

namespace gte {

// Everything a PostOpaque/PostTransparent callback needs to sample the
// current view's own already-rendered color/depth, for exactly the view it
// was invoked for. colorHandle/depthHandle carry the same underlying value
// in this engine today (color and depth are two sub-resources of one
// imported texture) - kept as two separate, named fields so a callback's
// own body can say which sub-resource a given pass.ReadTexture() call is
// declaring a dependency against.
//
// VALID FOR EXACTLY ONE CALLBACK INVOCATION. Never store a copy of this
// value, or any of its handle fields, past the single invocation it was
// handed in.
struct ScenePassReadHandles {
    rg::TextureHandle colorHandle;
    rg::TextureHandle depthHandle;
    SamplerHandle colorSampler;
    ImageViewHandle depthImageView;
    SamplerHandle depthSampler;
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
