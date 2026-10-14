#pragma once

// PHASE 3 COMPLETE (see chat history - "create universal, migrate
// Atmosphere, migrate Shadow, then DELETE the legacy surface entirely").
// This header defines the types behind Core::RegisterRenderPass()
// (Core.h/Core.cpp) - the ONE AND ONLY way to register a built-in render
// pass now. The 4 methods this used to sit on top of as an additive wrapper
// (Core::AddBuiltInPreOpaquePass(), Core::AddBuiltInPostOpaquePass(),
// Core::AddBuiltInPostTransparentPass(), Core::RegisterBuiltInRenderFeature())
// are DELETED - not hidden, not private, gone - their validation/logging
// bodies are inlined directly into RegisterRenderPass()'s own switch
// statement. The real engine machinery underneath (RenderFeatureCompositor/
// RenderFeatureCompositorImpl.cpp - the GPU-slot + private-texture +
// compute-shader blend chain for PostComposite/PreUI) is UNCHANGED - every
// real safety check it enforces (name-collision detection, the
// PreOpaque/AfterOpaques/AfterTransparents pass-tag asserts, the
// PostComposite/PreUI "did you forget to ReadTexture() the depth handle"
// assert) still runs exactly as before.
//
// Deliberately does NOT cover:
//   - Core::RegisterProjectRenderPassProvider() (the raw/no-safety-net
//     escape hatch) - stays separate on purpose. See
//     Features/Atmosphere/AtmosphereFeature.cpp's "DrawSkyBackground"
//     registration for a real, confirmed case that cannot go through this
//     front door at all (it must write directly into the shared, already-
//     drawn view target - every FrameEvent below only ever hands out a
//     private target or a read-only view, by design).
//   - Core::RegisterViewContentPassName()/RegisterFinalizeForSamplingHook() -
//     neither one registers a render PASS at all (one is pure GPU-stats
//     bookkeeping, the other runs once, after the whole graph has already
//     executed) - folding either into "a kind of RegisterRenderPass call"
//     would be a type error dressed up as a simplification.

#include "ProjectRenderFeatureCallback.h"
#include "RenderFeatureDescriptor.h"

namespace gte {

// Mirrors RenderFeatureStage (RenderFeatureDescriptor.h) exactly, under a
// friendlier name - does NOT mirror rg::RenderPassEvent's raw tag set
// (RenderGraphHandles.h). Two values that exist over there are deliberately
// absent over here:
//   - BeforeEverything: permanently reserved for Core's own internal,
//     denylisted "ClearViewTarget" pass (Core.cpp) - no sanctioned external
//     registration path has ever existed for it, and adding one is real new
//     engine work, not a renaming exercise.
//   - AfterEverything: not something a feature ever "chooses" - a
//     PostComposite/PreUI feature's OWN declared passes are REQUIRED to
//     carry this tag already (enforced by a runtime assert inside
//     RenderFeatureCompositorImpl.cpp), so it is not a meaningful fourth
//     choice alongside the 5 below.
enum class FrameEvent {
    PreOpaque,       // Before any opaque geometry is drawn.
    PostOpaque,      // Immediately after opaque geometry is drawn.
    PostTransparent, // Immediately after transparent geometry is drawn.
    PostComposite,   // Near the end of the frame - gets a private target, auto-blended onto the screen.
    PreUI,           // Same contract as PostComposite, runs immediately after it, same frame.
};

// The ONE callback shape every FrameEvent above shares - identical to the
// existing ProjectRenderFeatureCallback (PostComposite/PreUI's own real
// shape already). For FrameEvent::PreOpaque/PostOpaque/PostTransparent,
// Core::RegisterRenderPass() hands your callback safe, default-constructed
// PLACEHOLDER values for the fields that genuinely do not exist yet at that
// point in the frame:
//   - `privateTarget` -> a default rg::TextureHandle{} (IsValid() == false)
//   - `extent`         -> Extent2D{} (0 x 0)
//   - `cameraData`     -> RenderFeatureCameraData{} (invViewProjectionValid == false)
//   - `currentViewHandles` -> ScenePassReadHandles{} (every handle invalid) -
//     PreOpaque ONLY (PostOpaque/PostTransparent still receive the real,
//     valid read handles, exactly like today).
// Reading one of these placeholder fields anyway is a silent logic bug, not
// a compile error - this is the one real, accepted safety trade-off of
// using a single shared signature instead of 3 narrower ones. Keep each
// callback's body scoped to only the fields that make sense for the
// FrameEvent you registered it under.
using UniversalRenderPassAction = ProjectRenderFeatureCallback;

} // namespace gte
