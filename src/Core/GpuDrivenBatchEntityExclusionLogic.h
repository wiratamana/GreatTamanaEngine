#pragma once

// editor-core-separation-22 campaign, PHASE3
// (task_manager/editor-core-separation-22/PHASE3_FIX_AUDIT_FINDINGS_SIDE_CHANNEL_LEAKS.md)
// - fixes PHASE2_COMPLETION_REPORT.md's own finding #19, the ONLY
// Confirmed-Lie row in that phase's ledger: the INVERSE shape of this
// campaign's own Clause C (PHASE0_MASTER_STRATEGY.md Step 1.2) - instead of a
// DISABLED pass's side effect surviving it, an ENABLED pass's own entities
// were being silently, permanently excluded from RenderOpaque's normal
// per-entity draw path based ONLY on "this entity was eligible for GPU-driven
// batching at COLLECTION time", never re-checked against whether that
// specific batch's own "<batch> IndirectDraw" pass (the ONE pass that would
// actually draw it instead) survived ITS OWN toggle check this frame. A
// disabled "<batch> IndirectDraw" pass used to make its own entities
// invisible entirely - excluded from the normal path AND undrawn by the now-
// disabled indirect path.
//
// This header extracts the ONE pure decision the fix needs - "should this
// specific batch's own entities be added to RenderOpaque's own exclusion
// set THIS FRAME" - so it is Tier-1-testable with ZERO RenderPipeline/
// RenderGraphBuilder/Vulkan/RenderPassToggleRegistry involvement, mirroring
// src/Renderer/Atmosphere/AtmospherePassToggleLogic.h's own precedent
// exactly: the caller (Core.cpp's "GpuDrivenBatches" provider) resolves the
// one bool itself first (via RenderPassToggleGuard.h's
// ShouldDeclareBuiltInPassThisFrame(&m_renderPassToggleRegistry,
// batch.indirectDrawPassName) - the SAME idempotent-within-a-frame check
// RenderPipeline::DeclareOnePhase() itself will independently re-run, later,
// the same frame, for that exact desc's own debugName) and passes the result
// in as a plain bool, keeping this function usable from a Tier-1 test with no
// dependency on any of those heavier types at all.

#include "../ECS/Entity.h"

#include <unordered_set>
#include <vector>

namespace gte {

// Appends `entities` into `outExcluded` ONLY when `indirectDrawPassEnabledThisFrame`
// is true - a disabled batch's entities are left OUT of the exclusion set
// entirely (never inserted, never erased from a set that might already
// contain them from an EARLIER, still-enabled batch this same frame - a
// plain std::unordered_set::insert() is idempotent per-entity, and no two
// eligible batches this frame ever share the same entity, so this is safe as
// a pure accumulation across repeated calls, one per batch, into the SAME
// `outExcluded` set). This is the honest fallback: RenderOpaque's own normal
// per-entity draw path (Core.cpp's "RenderOpaque" provider `execute` lambda)
// picks up any entity NOT in this set and draws it the ordinary way, so a
// disabled indirect-draw pass degrades to "drawn normally" rather than
// "silently never drawn at all".
inline void AppendGpuDrivenBatchEntityExclusionsIfIndirectDrawEnabled(
    bool indirectDrawPassEnabledThisFrame, const std::vector<Entity>& entities, std::unordered_set<Entity>& outExcluded)
{
    if (!indirectDrawPassEnabledThisFrame) {
        return;
    }
    for (const Entity& entity : entities) {
        outExcluded.insert(entity);
    }
}

} // namespace gte
