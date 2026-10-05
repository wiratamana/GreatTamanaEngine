#pragma once

// The one callback type a Project Assembly's own on-screen render feature
// registration (Core::RegisterProjectRenderFeature(),
// RenderFeatureCompositor::RegisterProjectFeature()) is built around. Gives
// a PostComposite/PreUI callback the same view identity + blackboard +
// color/depth read-handle shape its PostOpaque/PostTransparent sibling
// (ProjectScenePassCallback/ScenePassReadHandles) already has, plus a
// separate per-view camera-data struct (RenderFeatureCameraData.h).
//
// Deliberately NOT a nested member of RenderFeatureCompositor - Core.h only
// ever forward-declares that class (never #includes the real
// Plugins/RenderFeatureCompositor.h - see that header's own "this header
// must not #include ../Core.h" comment for the matching half of this same
// discipline) and therefore has no way to name a type nested inside an
// incomplete class. Living here, in its own free-standing, zero-Core-
// dependency header, lets BOTH RenderFeatureCompositor.h and Core.h include
// this ONE small file directly, by value, with neither header's own
// existing include discipline having to change.
//
// This signature exposes a RenderGraphBuilder&, a RenderPassBlackboard&,
// this frame's RenderViewId, a TextureHandle private target, a real
// VkImageView/VkSampler read-handle struct for the current color/depth
// sub-resources, and a checked camera-data struct - never a raw VkImage/
// VkCommandBuffer/RenderTexture&, so a Project Assembly's own callback body
// still has no way to record a GPU command except through
// builder.AddRenderPass()/AddPass()/AddComputePass()'s own setup/execute
// pair, the Render Graph's own, one, official entry point.
// Synchronization/barriers/scheduling for whatever this callback declares
// are handled entirely by the SAME RenderGraphCompiler/RenderGraphBarrierPlanner
// every other pass in this engine already goes through - nothing new to
// build, nothing to bypass. See task_manager/editor-core-separation-23/
// PHASE7_FULL_REGRESSION_DOCS_AND_ENTRY_GATE_CLOSEOUT.md's own
// RenderPassEvent::AfterEverything requirement for the ONE convention every
// pass this callback declares must follow.
#include "ProjectScenePassCallback.h"
#include "RenderFeatureCameraData.h"

#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"
#include "../../Renderer/RenderGraph/RenderPipeline.h"

#include <volk.h>

#include <functional>

namespace gte {

using ProjectRenderFeatureCallback = std::function<void(rg::RenderGraphBuilder& builder,
    rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView, rg::TextureHandle privateTarget,
    VkExtent2D extent, const ScenePassReadHandles& currentViewHandles, const RenderFeatureCameraData& cameraData)>;

} // namespace gte
