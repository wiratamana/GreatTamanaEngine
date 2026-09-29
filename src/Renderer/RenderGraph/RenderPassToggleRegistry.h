#pragma once

// editor-core-separation-8 campaign, PHASE1 - a small, generic, gte_core-tier
// registry answering "is this built-in pass, by its own debugName string,
// currently enabled" - the missing piece requirement #1 (from the
// "Better Render Graph Editor" story) needs for BUILT-IN passes specifically
// (see PHASE0_MASTER_STRATEGY.md's Locked Product Decision #5/#6 for why this
// is keyed by debugName string rather than the existing-but-never-stamped
// RenderPassId).
//
// Deliberately NOT thread-safe (no mutex) - this class is touched EXCLUSIVELY
// from the main thread: RenderPipeline::DeclareOnePhase()'s own declare loop
// (every frame), the "Render Graph" panel's own checkbox handling (PHASE4),
// and RenderGraphControlCommandBridge's own main-thread pump (PHASE5) - see
// PHASE0_MASTER_STRATEGY.md's Locked Architecture Decision #12.
//
// Auto-discovery, not a hardcoded pass-name list (Locked Product Decision
// #6): NoteDeclaredAndCheckEnabled() is the ONLY place a NEW entry is ever
// created with everDeclaredThisSession == true - the first time
// RenderPipeline::DeclareOnePhase() actually sees a given debugName this
// session, it is auto-registered here, defaulting to enabled = true (zero
// behavior change for any pass nobody has touched). SetEnabled() may ALSO
// create a brand-new entry (with everDeclaredThisSession == false) if a
// caller pre-disables a pass that has genuinely never run yet this session -
// a legal, if unusual, forward-looking state.

#include <string>
#include <unordered_map>
#include <vector>

namespace gte::rg {

// One known pass's toggle state - returned by ListAll(), and (PHASE5) is
// exactly what GET /render_graph/passes reports per entry.
struct RenderPassToggleState {
    std::string name;
    bool enabled = true;
    // True once RenderPipeline::DeclareOnePhase() has actually seen this
    // pass's RenderPassDesc at least once this session (see this class's own
    // header comment above for the "SetEnabled() may pre-create an entry
    // with this false" case).
    bool everDeclaredThisSession = false;
};

class RenderPassToggleRegistry {
public:
    RenderPassToggleRegistry() = default;

    // Called by RenderPipeline::DeclareOnePhase(), once per collected
    // RenderPassDesc, EVERY frame - for BOTH ProviderTiming phases, for BOTH
    // regimes (offscreen AND present share the SAME registry instance - see
    // Core.cpp's wiring). Auto-discovers `name` the first time it is ever
    // seen (creates a new entry, enabled = true, everDeclaredThisSession =
    // true); on every subsequent call for an already-known name, simply
    // (re)stamps everDeclaredThisSession = true (a harmless, idempotent
    // no-op once already true) and returns the CURRENT enabled state
    // unchanged. Returns true (enabled) for an empty `name` defensively -
    // never disables anything by accident on a malformed/null debugName.
    bool NoteDeclaredAndCheckEnabled(const std::string& name);

    // Called by Core's own mutator method (Core::SetBuiltInRenderPassEnabled(),
    // PHASE1 too - see Step 3.3 below), in turn called from EITHER the
    // "Render Graph" panel's own checkbox (PHASE4, main thread, synchronous,
    // same frame) OR RenderGraphControlCommandBridge's pump (PHASE5, also
    // main-thread-only, never concurrent with the frame that calls
    // NoteDeclaredAndCheckEnabled() above - see PHASE5's own pump placement).
    // Upserts `name` if never seen before (may create an entry with
    // everDeclaredThisSession == false - see this class's own header
    // comment). Returns false, WITHOUT applying the change, if `name` is on
    // the permanent deny-list (IsDenyListed()) - true otherwise (including
    // when `enabled` already equalled the current value - a harmless no-op
    // "success").
    bool SetEnabled(const std::string& name, bool enabled);

    // Current enabled state for `name` - true (the safe default) if `name`
    // has never been seen by either method above. Used by the panel (PHASE4)
    // to initialize each checkbox's displayed value.
    bool IsEnabled(const std::string& name) const;

    // Every currently-known entry, sorted by `name` (lexical) for
    // deterministic iteration order in both the ImGui panel (PHASE4) and the
    // GET /render_graph/passes JSON body (PHASE5) - an unordered_map has no
    // useful iteration order of its own to expose directly.
    std::vector<RenderPassToggleState> ListAll() const;

    // True for a small, fixed, permanent set of pass names that can NEVER be
    // disabled via SetEnabled() - currently: "Present" (the pass that finally
    // writes the swapchain image; disabling it leaves the Editor rendering
    // nothing to the screen with no in-process recovery) and, since the
    // editor-core-separation-20 campaign's PHASE1, "ClearViewTarget" (the ONE
    // guaranteed clear of the Game/Scene View render target every frame - a
    // view with no defined clear has no safe fallback content to show). A plain
    // static function (not a data member) so it has zero interaction with
    // any registry instance's own state - deliberately extensible (add
    // another `name ==` comparison here later) without ever needing to
    // touch SetEnabled()'s own body.
    static bool IsDenyListed(const std::string& name) noexcept;

private:
    std::unordered_map<std::string, RenderPassToggleState> m_entries;
};

} // namespace gte::rg
