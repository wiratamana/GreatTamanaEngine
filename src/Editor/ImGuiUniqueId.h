#pragma once

namespace gte {

// THE mandated way to enter a per-iteration ImGui ID scope for any widget
// built inside a loop over a list/table of runtime data - see AGENTS.md's
// "ImGui Widget ID Uniqueness" section and
// task_manager/editor-core-separation-10/PHASE0_MASTER_STRATEGY.md for the
// full story (the Render Graph Panel duplicate-checkbox bug this exists to
// permanently prevent a recurrence of, engine-wide).
//
// `index` MUST be the current loop iteration's own distinct integer
// position (e.g. a plain `for (std::size_t i = 0; ...; ++i)` counter, or
// any other value the CALLER can guarantee is distinct across all
// iterations of the SAME loop, in the SAME frame) - this is what makes
// widget-ID uniqueness completely INDEPENDENT of whether `debugKey` happens
// to repeat across iterations. Relying on a name/label/data-derived string
// alone for uniqueness is exactly the mistake this class exists to make
// structurally impossible to repeat - "this name is always unique" is
// NEVER a safe assumption to build ImGui ID uniqueness on top of.
//
// `debugContext` (e.g. "RenderGraphPanel::BuildPassRow") and `debugKey`
// (e.g. a pass name, may be empty/omitted) are used ONLY for readability -
// Dear ImGui's own Item Picker, and this class's own conflict-log message
// (ImGuiIdConflictGuard) if a collision is ever still somehow detected -
// NEVER relied upon for uniqueness by themselves. `debugContext` must be a
// string literal (or otherwise outlive the call) - it is not copied.
class ScopedUniqueId {
public:
    explicit ScopedUniqueId(int index, const char* debugContext, const char* debugKey = "");
    ~ScopedUniqueId();

    ScopedUniqueId(const ScopedUniqueId&) = delete;
    ScopedUniqueId& operator=(const ScopedUniqueId&) = delete;
    ScopedUniqueId(ScopedUniqueId&&) = delete;
    ScopedUniqueId& operator=(ScopedUniqueId&&) = delete;

private:
    bool m_pushedDebugKeyScope;
};

} // namespace gte
