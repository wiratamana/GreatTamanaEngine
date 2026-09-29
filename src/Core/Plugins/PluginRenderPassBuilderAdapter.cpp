#include "PluginRenderPassBuilderAdapter.h"

#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"

#include <array>

namespace gte {

// editor-core-separation-3 campaign, PHASE3 - confirmed against the REAL,
// current RenderGraphBuilder::AddRenderPass() overload
// (src/Renderer/RenderGraph/RenderGraphBuilder.h): the non-defaulted
// parameter order is (name, kind, viewScope, category, setup, execute[,
// drawKind, renderPassEvent, tags]), exactly as every real call site
// (GBufferValidation.cpp, FrameDebuggerReplayPasses.cpp) already uses it.
// ViewScope::Shared (not Game/SceneView) is used here since this adapter is
// constructed fresh per-view with that view's own resolved
// viewData->colorTarget already baked in - the plugin itself never needs to
// know Game View/Scene View exist as a distinct concept (PHASE3, Step 2).
// RenderPassCategory::Debug (not General) is the deliberate, correct
// category for every plugin-contributed pass - mirrors
// GBufferValidation.cpp's own precedent of tagging genuinely optional/
// debug-flavored passes this way. editor-core-separation-22 campaign,
// PHASE4 - CONFIRMED still correct under `Debug`'s own corrected meaning ("a
// real, optional/debug-flavored FEATURE pass, fully visible in the Frame
// Debugger tree when it runs" - RenderGraphTypes.h): this pass is exactly
// that (a real, user-toggleable feature, never Frame-Debugger-internal
// scaffolding), so no change was needed here - only the ENUM's own
// documented meaning was corrected, not this call site's tag.
//
// REAL, LIVE-TESTING-DISCOVERED DEVIATION from the phase file's own literal
// Step 3.3 sketch (which leaves `renderPassEvent` at its DEFAULT,
// RenderPassEvent::Opaques, by simply never passing it): a live
// GET /get_game_view smoke test showed NO visible magenta at all, and the
// Editor's own "Render Graph" panel showed "DemoRenderFeaturePlugin_Clear"
// scheduled BETWEEN "RenderOpaque" and "DrawSkyBackground" - i.e. exactly
// the WRONG place. Root cause, confirmed by re-reading
// RenderGraphCompiler::Compile()'s own "effective order" doc comment
// (RenderGraphTypes.h's RenderPassEvent, render-pass-4 campaign PHASE2):
// the RAW/WAW dependency-edge scan walks passes in
// (RenderPassEvent, original declaration index) order, NOT raw declaration
// order - a write-only pass (no ReadTexture() of its own) has no REAL data
// dependency forcing it after "DrawSkyBackground"/"RenderTransparent"/
// "AtmosphereComposite" the way a genuine read would; its ONLY ordering
// signal is its own RenderPassEvent tier. Leaving it at the default
// Opaques tier (same tier as "RenderOpaque") places it BEFORE every later-
// tier production pass in the WAW edge scan, so every one of those passes
// (which DO write the same handle) is scheduled - and therefore executes -
// strictly AFTER it, overwriting the clear before it is ever visible.
// Fixed by tagging this pass rg::RenderPassEvent::AfterEverything (the
// LATEST tier this engine defines - RenderGraphTypes.h) explicitly, the
// same "runs genuinely last" intent PHASE0/PHASE3's own Step 3.4 already
// documents for `ProviderTiming::AfterDeferredPasses` at the PROVIDER level -
// this is the matching fix at the PASS level, since ProviderTiming only
// controls WHEN the C++ call happens, never what tier the compiler
// schedules the resulting pass into. Confirmed via a second live
// GET /get_game_view smoke test after this fix - see this phase's own
// completion report.
// editor-core-separation-21 campaign, PHASE4 (fixing a confirmed lie from
// PHASE3's own audit, findings #18/#19: "DemoRenderFeaturePlugin_Clear"/
// "DemoRenderFeatureSecondPlugin_Clear" used to declare unconditionally,
// with zero RenderPassToggleRegistry consult, even though the "Render
// Graph" panel already drew a real, apparently-functional "Enabled"
// checkbox for them) - mirrors AtmosphereLutRenderer's own five-method
// precedent exactly: check-then-early-return, BEFORE any
// RenderPassEvent/attachment-declaration logic below, so a disabled clear
// pass declares NOTHING at all this frame (no downstream consumer reads
// this pass's own written target - confirmed by re-reading
// LegacyRenderFeatureOrchestrator::ContributeRenderGraphPasses(), which
// only pushes `resolved->target` into `frame.finalTextureOutputs` once,
// unconditionally, regardless of whether this clear pass itself ran - a
// harmless root reference to a texture some OTHER already-declared pass
// may still have validly written this same frame).
void PluginRenderPassBuilderAdapter::AddFullscreenClearPass(const char* debugName, float r, float g, float b, float a)
{
    if (m_toggleRegistry != nullptr && !m_toggleRegistry->NoteDeclaredAndCheckEnabled(debugName)) {
        return;
    }
    m_builder.AddRenderPass(debugName, rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::Debug,
        [this, r, g, b, a](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(m_viewTarget, std::array<float, 4>{ r, g, b, a });
        },
        [](rg::PassContext&) {
            // Intentionally empty - WriteColorAttachment()'s own declared
            // clear color IS the entire visible effect of this pass; no
            // additional draw call is issued.
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything);
}

} // namespace gte
