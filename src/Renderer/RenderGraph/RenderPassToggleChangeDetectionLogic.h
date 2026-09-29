#pragma once

#include "RenderPassToggleRegistry.h"

#include <cstddef>
#include <vector>

// editor-core-separation-21 campaign, PHASE2
// (task_manager/editor-core-separation-21/PHASE2_FIX_AERIAL_PERSPECTIVE_COMPOSITE_TOGGLE_LIE.md)
// - a small, pure, dependency-free comparison extracted specifically so it is
// Tier-1-testable (mirrors AtmospherePassToggleLogic.h's own precedent of
// extracting a decision out of its one real call site into its own header).
//
// Confirmed root cause (PHASE1_COMPLETION_REPORT.md): the render-pass toggle
// mechanism itself was always honest - the ACTUAL bug was that nothing ever
// told the Frame Debugger's own frozen, one-shot capture to refresh itself
// the instant a built-in pass's enabled state changed, so its displayed event
// tree could sit stale (still showing a pass that had already, correctly,
// stopped being declared) until a human/HTTP caller happened to request a
// SECOND capture. This function is the pure "did anything actually change"
// half of the fix - see RenderGraphPanel::Build()'s own call site (the
// Render Graph panel's checkboxes are the one place this campaign's own
// audit found a built-in pass's enabled state can flip WITHOUT going through
// EditorHost.cpp's RenderGraphControlCommandBridge pump, which instead
// notifies the Frame Debugger directly via IEditorLayer::FrameDebuggerCaptureNow()
// at its own call site - see EditorHost.cpp's SetBuiltInPassEnabled case).
namespace gte::rg {

// Answers "did ANY pass's (name, enabled) pair actually change between these
// two RenderPassToggleRegistry::ListAll() snapshots". Deliberately compares
// ONLY `name`/`enabled` - NOT `everDeclaredThisSession` - since that field
// flipping true (a pass simply running for the first time this session, with
// its own enabled state completely unchanged) is not itself a reason to force
// a fresh Frame Debugger capture. `ListAll()` already returns entries sorted
// by name (see RenderPassToggleRegistry.h), so a plain index-aligned
// comparison is correct and this function never needs to re-sort anything
// itself. A size mismatch (a brand-new name registered this frame) always
// counts as "changed".
inline bool DidRenderPassToggleEnabledStatesChange(
    const std::vector<RenderPassToggleState>& before, const std::vector<RenderPassToggleState>& after) noexcept
{
    if (before.size() != after.size()) {
        return true;
    }
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i].name != after[i].name || before[i].enabled != after[i].enabled) {
            return true;
        }
    }
    return false;
}

} // namespace gte::rg
