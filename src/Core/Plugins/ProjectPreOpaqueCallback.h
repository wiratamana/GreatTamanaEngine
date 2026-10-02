#pragma once

// better-render-pass-5 effort, BLOCK 3, PHASE1 - the callback type a
// PreOpaque Project Assembly feature registers through
// Core::AddPreOpaquePass() (PHASE3). Deliberately NOT the same type as
// ProjectRenderFeatureCallback.h's own ProjectRenderFeatureCallback -
// that shape (RenderGraphBuilder&, TextureHandle privateTarget,
// VkExtent2D extent) exists because every PostComposite/PreUI pass
// wants "the screen so far," composited via RenderFeatureCompositor's
// own blend chain. A PreOpaque pass has no "screen so far" - nothing
// has been drawn yet this frame - so it gets no privateTarget/extent at
// all. It is expected to mint/own its own render view (Core::CreateRenderView())
// and declare its own real pass(es) directly against `builder`, then
// Publish() whatever it produces onto the RenderPassBlackboard itself.
//
// Deliberately NOT a nested member of RenderFeatureCompositor, for the
// EXACT same reason ProjectRenderFeatureCallback.h is not: Core.h only
// ever forward-declares RenderFeatureCompositor (never includes the
// real Plugins/RenderFeatureCompositor.h), so it has no way to name a
// type nested inside an incomplete class. Living here, in its own
// free-standing, minimal-dependency header, lets BOTH
// RenderFeatureCompositor.h and Core.h include this ONE small file
// directly, by value, with neither header's own existing include
// discipline having to change.
//
// **IMPORTANT, CORRECTED FROM THE ORIGINAL SOURCE SPEC**: the source
// spec text (BLOCK3_WIRE_PRE_POST_OPAQUE_STAGES.txt, Section 4) shows
// this type as a 2-parameter function - `(RenderGraphBuilder&,
// RenderViewId)` - with NO way to reach the RenderPassBlackboard at
// all, even though that SAME document repeatedly says the callback
// must "Publish() onto the Blackboard." That is a genuine bug in the
// source spec, confirmed and fixed by explicit user decision during
// this campaign's own planning (see PHASE0_MASTER_STRATEGY.md's own
// "Corrections to the source spec" note): `rg::RenderPassBlackboard&
// blackboard` is REQUIRED as a third parameter, since a Project
// Assembly's callback has no other way to reach the per-frame
// Blackboard instance at all - it is not a singleton, not reachable
// through Core, and lives only inside RenderPassFrameContext::blackboard
// (a plain reference member, freshly bound every frame). The new
// "PreOpaqueFeatures" provider (PHASE3) already has `frame` in scope at
// its own call site, so forwarding `frame.blackboard` straight through
// costs nothing and needs no new plumbing - mirrors how every other
// per-view immediate provider in this codebase already works
// (`"AtmosphereViewLut"`/`"AtmosphereSharedLut"` both just read
// `frame.blackboard` directly, with no indirection).
//
// `currentView` is NOT optional decoration either - see PHASE3's own
// "PreOpaqueFeatures" provider doc comment for the full "why" (this
// provider is ProviderScope::PerActiveView, so the exact same
// registered callback is invoked once per active view, in the SAME
// frame, against the SAME RenderPassBlackboard - any Blackboard key the
// callback Publish()es under MUST incorporate this parameter, mirroring
// Core.cpp's own already-shipped kAtmosphereViewLutGameKey/
// kAtmosphereViewLutSceneKey dual-key precedent for "AtmosphereViewLut").
//
// **The ONE hard rule every pass this callback declares must follow**:
// every pass it declares via builder.AddRenderPass()/AddPass()/
// AddComputePass() MUST carry rg::RenderPassEvent::PreOpaques
// explicitly - this is the ONLY legal tag. See
// docs/conventions/project-assembly-system.md's own new PreOpaque
// subsection (PHASE5) for the full, bolded statement of this rule, and
// Core.cpp's own "PreOpaqueFeatures" provider (PHASE3) for the runtime,
// debug-asserted safety net that catches a violation of it
// (FindPassesNotTaggedPreOpaque() below is that safety net's own
// pure-logic half).
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderPipeline.h"

#include <cassert>
#include <cstddef>
#include <functional>
#include <vector>

namespace gte {

using ProjectPreOpaqueCallback = std::function<void(
    rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView)>;

// better-render-pass-5 effort, BLOCK 3, PHASE1 - the runtime safety-net
// LOGIC (source spec Section 3, Step 4), deliberately extracted as a
// pure, side-effect-free free function (never logs, never asserts,
// never mutates `builder`) so it is directly Tier-1-testable in total
// isolation - construct a bare RenderGraphBuilder, declare a few passes
// with AddRenderPass(), call this function, assert on its return value.
// Core.cpp's own "PreOpaqueFeatures" provider (PHASE3) is the ONE real
// production caller, wrapping a non-empty result with the actual
// GTE_LOG_WARNING + assert() side effects.
//
// Scans every pass in `builder` whose declaration index is in the
// half-open range [before, after) and returns the (absolute,
// builder-relative, NOT range-relative) indices of every one whose
// RenderPassEvent is not EXACTLY rg::RenderPassEvent::PreOpaques. An
// empty returned vector means every pass in range was tagged correctly.
// `before`/`after` must satisfy `before <= after <= builder.DeclaredPassCount()`
// - asserted in debug builds, exactly like RenderGraphBuilder::PassEventAt()
// itself already asserts its own single-index bound.
inline std::vector<std::size_t> FindPassesNotTaggedPreOpaque(
    const rg::RenderGraphBuilder& builder, std::size_t before, std::size_t after)
{
    assert(before <= after && after <= builder.DeclaredPassCount()
        && "FindPassesNotTaggedPreOpaque() - [before, after) out of range");

    std::vector<std::size_t> violations;
    for (std::size_t index = before; index < after; ++index) {
        if (builder.PassEventAt(index) != rg::RenderPassEvent::PreOpaques) {
            violations.push_back(index);
        }
    }
    return violations;
}

} // namespace gte
