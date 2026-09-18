#pragma once

// render-pass-3 campaign (task_manager/render-pass-3), PHASE3
// (PHASE3_FULL_PRODUCTION_PASS_MIGRATION_AND_VIEW_UNIFICATION.md, Step 3.1 +
// Step 3.2) - the Application-layer "what is a view" data PHASE1's generic
// rg::RenderPipeline/RenderPassFrameContext must never carry directly (per
// that phase doc's own instruction: this struct is deliberately Application-
// layer, NOT living inside src/Renderer/RenderGraph/RenderPipeline.h).
//
// DEVIATION from the phase doc's own STATED PREFERENCE - documented here and
// in the campaign's own PHASE3 completion report. Step 3.1 says to store a
// per-frame std::vector<RenderPassViewData> "somewhere RenderPassFrameContext
// can reach - either as a NEW field directly on RenderPassFrameContext...
// prefer putting it ON the frame context itself". This implementation
// deliberately picks the OTHER option the same paragraph explicitly offers
// ("or as an Application-owned side table a provider's captured `this` can
// look up by frame.currentView") instead, for a real, non-negotiable Clean
// Architecture reason (AGENTS.md - "Renderer never depends on ECS/Editor/
// Application"): RenderPassFrameContext is defined in
// src/Renderer/RenderGraph/RenderPipeline.h, a Renderer-layer file. Adding a
// `std::vector<RenderPassViewData>` field there would force that file to
// #include this one (Application-layer), a genuine backward dependency this
// codebase's own layering rule forbids outright - unlike
// RenderPassFrameContext::builder (PHASE3, Step 3.3b), which is safe to add
// directly because RenderGraphBuilder is itself an rg-namespace type already
// included by RenderPipeline.h, RenderPassViewData carries no such
// pedigree. Application instead owns `std::vector<RenderPassViewData>
// m_currentViewDataThisFrame;`, refreshed every frame BEFORE calling
// DeclareInto(), plus a small `FindViewData(RenderViewId) const` helper a
// provider (registered once at construction time, capturing `this`) calls
// directly - see Application.cpp's RegisterOffscreenRenderPipelineProviders().

#include "../Math/Mat4.h"
#include "../Math/Vec3.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"
#include "../Renderer/RenderGraph/RenderPipeline.h"

#include <volk.h>

#include <functional>

namespace gte {

class RenderTexture;

// One view's worth of per-frame rendering data - constructed fresh, every
// frame, by Application::Run()'s own offscreen `build` lambda (see Step 3.6),
// for every one of `gameTarget`/`sceneTarget` that is actually non-null this
// frame. A provider reads this back out via
// `Application::FindViewData(frame.currentView)`.
struct RenderPassViewData {
    rg::RenderViewId id;
    rg::TextureHandle colorTarget;
    // For Sampler()/DepthSampler()/Extent()/Format() - see
    // AtmospherePassSequence.h's own AddAtmosphereCompositePass() doc
    // comment for why the raw RenderTexture (not just its imported
    // TextureHandle) is genuinely needed here, never resolved through
    // rg::PassContext::resolveTexture() (which has no depth-resolution path
    // at all).
    RenderTexture* renderTexture = nullptr;
    float aspectWidthOverHeight = 1.0f;
    Mat4 viewProjection;
    Vec3 eyeWorldPosition;

    // Scene-View-only; left default-constructed (empty/falsy) for Game View.
    // The Editor's infinite ground-grid overlay (IEditorLayer::RenderSceneGrid())
    // - see Step 3.4 for why this is folded into the "RenderTransparent"
    // provider's own body, gated on this being non-empty AND
    // frame.currentView being the Scene view.
    std::function<void(VkCommandBuffer, const Mat4&)> recordSceneOverlay;
};

// Step 3.2 - the Locked Design Decision 5 bridge. Exactly one small, private,
// Application-layer function translating this campaign's new opaque
// RenderViewId vocabulary back into the OLD, byte-for-byte-unchanged
// ViewScope enum every pre-existing Frame Debugger/Profiler/GPU-timing
// consumer already reads directly off PassRecord/RenderGraphPassSnapshot.
// Injected into rg::RenderPipeline::SetLegacyViewScopeTranslator() (PHASE3) -
// `rg::RenderPipeline` itself never sees the strings "Game"/"Scene" at all.
inline rg::ViewScope TranslateLegacyViewScope(rg::RenderViewId view) noexcept
{
    if (view == rg::RenderViewId::Named("Game")) {
        return rg::ViewScope::GameView;
    }
    if (view == rg::RenderViewId::Named("Scene")) {
        return rg::ViewScope::SceneView;
    }
    return rg::ViewScope::Shared; // Shared() itself, or any future named view with no legacy meaning.
}

} // namespace gte
