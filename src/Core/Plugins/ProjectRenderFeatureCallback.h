#pragma once

// The callback type a Project Assembly's own on-screen render feature
// registration (Core::RegisterProjectRenderFeature(),
// RenderFeatureCompositor::RegisterProjectFeature()) is built around - a
// PostComposite/PreUI callback. Zero Vulkan dependency, reachable from a
// third-party plugin translation unit with no volk.h pulled in.
//
// Exposes a RenderGraphBuilder&, a RenderPassBlackboard&, this frame's
// RenderViewId, a private target handle, the current color/depth read
// handles, and a camera-data struct - never a raw image/command buffer, so a
// callback body can only record GPU work through
// builder.AddRenderPass()/AddPass()/AddComputePass()'s own setup/execute
// pair, this engine's one official render-graph entry point. Every pass this
// callback declares must be tagged RenderPassEvent::AfterEverything.
#include "../../Renderer/RenderGraph/RenderGraphHandles.h"
#include "ProjectScenePassCallback.h"
#include "RenderFeatureCameraData.h"
#include "../Extent.h"

#include <functional>

namespace gte {

using ProjectRenderFeatureCallback = std::function<void(rg::RenderGraphBuilder& builder,
    rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView, rg::TextureHandle privateTarget,
    Extent2D extent, const ScenePassReadHandles& currentViewHandles, const RenderFeatureCameraData& cameraData)>;

} // namespace gte
