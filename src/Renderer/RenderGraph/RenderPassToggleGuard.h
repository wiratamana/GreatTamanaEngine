#pragma once

// editor-core-separation-22 campaign, PHASE1
// (task_manager/editor-core-separation-22/
// PHASE1_FIX_DRAWSKYBACKGROUND_TOGGLE_SIDE_CHANNEL_LEAK.md) - the confirmed
// root cause (PHASE0_MASTER_STRATEGY.md's own Step 2.3): a RenderPipeline
// provider that performs a side effect (a RenderPassBlackboard::Publish()
// call, a cached-callback hand-off, a member-variable write some OTHER,
// independently-toggled pass might later read and reproduce) BEFORE it ever
// reaches RenderPipeline::DeclareOnePhase()'s own generic, LATE toggle gate
// (RenderPipeline.h, ~line 588-591) leaks that side effect even when the
// pass itself is disabled - the late gate only ever decides whether the
// already-fully-built RenderPassDesc reaches builder.AddRenderPass(); it has
// no way to "undo" a side effect the provider lambda already performed
// before returning.
//
// This is the generalized sibling of
// src/Renderer/Atmosphere/AtmospherePassToggleLogic.h's
// ShouldDeclareAtmospherePassThisFrame() - deliberately smaller and with NO
// atmosphere-specific concept (no upstream-handle-validity folding) baked
// in, so PHASE2/PHASE3 of this same campaign (the systemic audit/fix) can
// reuse this ONE helper for every OTHER Core.cpp/plugin-adapter provider
// found to have the identical bug shape, without inventing a new,
// bespoke, one-off guard each time.
//
// MUST be called at the very TOP of any provider body that performs ANY
// such side effect, BEFORE that side effect happens - calling it only
// immediately before `out.push_back(desc)` (mirroring
// RenderPipeline::DeclareOnePhase()'s own late gate) defeats its entire
// purpose; the whole point is to early-`return` before the side effect is
// ever reached.
//
// `registry` may be nullptr (mirrors every other toggle-registry consumer
// in this codebase's own "unset = old, always-enabled behavior" rule,
// e.g. RenderPipeline::DeclareOnePhase()'s own `m_passToggleRegistry !=
// nullptr` check) - returns true unconditionally in that case, so callers
// never need their own separate null check.
//
// `name` must be the EXACT same literal this provider's own
// RenderPassDesc::debugName uses, so this early check and
// RenderPipeline::DeclareOnePhase()'s own later, idempotent re-check of the
// identical name agree - both ultimately call the SAME
// RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled(), which is safe to
// call more than once per frame for the same name (see that method's own
// header comment: "restamps everDeclaredThisSession = true - a harmless,
// idempotent no-op once already true - and returns the CURRENT enabled
// state unchanged"). For a ProviderScope::PerActiveView provider (invoked
// once per active view, e.g. "DrawSkyBackground"), this means calling this
// guard at the top of EVERY invocation correctly, independently gates EACH
// view's own side effect - never a double-decrement, never a stale answer
// from a previous view's call this same frame.

#include "RenderPassToggleRegistry.h"

namespace gte::rg {

inline bool ShouldDeclareBuiltInPassThisFrame(RenderPassToggleRegistry* registry, const char* name)
{
    return registry == nullptr || registry->NoteDeclaredAndCheckEnabled(name);
}

} // namespace gte::rg
