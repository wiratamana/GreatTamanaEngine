#include "FrameDebuggerPanel.h"

#include "../EditorContext.h"

#include <imgui.h>

namespace gte {

void FrameDebuggerPanel::BuildToolbarRow(EditorContext& ctx)
{
    const bool wasEnabled = m_enabled;
    ImGui::Checkbox("Enable", &m_enabled);
    if (m_enabled && !wasEnabled) {
        // task_manager/frame-debugger-2 campaign, Locked Design Decision
        // #3 (PHASE0_MASTER_STRATEGY.md): turning Enable ON auto-engages
        // the existing Pause/Resume playback toolbar (frame-debugger-1
        // campaign) - the SAME ctx.playbackPaused field
        // PlaybackControls.cpp's own "Pause" button writes. Turning
        // Enable back OFF deliberately does NOT auto-resume - see this
        // file's own BuildToolbarRow() comment for why (a user
        // inspecting a paused frame should not be silently un-paused
        // just for closing/disabling this debug window).
        ctx.playbackPaused = true;
    }

    ImGui::SameLine();

    // Cosmetic-only stub - this engine has no Play/Edit-mode split (see
    // frame-debugger-1's own Locked Design Decision #1), so there is
    // nothing real for this control to switch between yet; it exists
    // purely for visual parity with the reference screenshot. Permanently
    // disabled - never becomes interactive by any state change in this
    // campaign.
    static constexpr const char* kModeItems[] = { "Editor" };
    int modeIndex = 0;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::BeginDisabled();
    ImGui::Combo("##FrameDebuggerMode", &modeIndex, kModeItems, 1);
    ImGui::EndDisabled();
}

void FrameDebuggerPanel::BuildFrameStepperRow()
{
    // Always "0 of 0" this campaign - see FrameDebuggerData.h's
    // FormatFrameStepperLabel() doc comment. Rendered as a disabled
    // slider (visual parity with the reference screenshot's scrubber)
    // plus the same text label a real implementation will show.
    int stepperValue = 0;
    ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderInt("##FrameDebuggerStepper", &stepperValue, 0, 0, "");
    ImGui::SameLine();
    const std::string label = FormatFrameStepperLabel(-1, 0);
    ImGui::TextUnformatted(label.c_str());
    ImGui::EndDisabled();
}

void FrameDebuggerPanel::Build(EditorContext& ctx)
{
    if (!ctx.frameDebuggerWindowOpen) {
        return;
    }

    // Passing &ctx.frameDebuggerWindowOpen as p_open keeps the window's
    // own titlebar [x] close button in sync with the SAME bool the
    // "Window > Frame Debugger" menu item flips (DockLayout.cpp) -
    // clicking either one closes/reopens the same shared state, with no
    // extra code needed on either side.
    ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Frame Debugger", &ctx.frameDebuggerWindowOpen)) {
        // Collapsed - still need End() (Dear ImGui's own Begin()/End()
        // pairing contract requires End() even when Begin() returns
        // false), but nothing worth drawing.
        ImGui::End();
        return;
    }

    BuildToolbarRow(ctx);
    ImGui::Separator();
    BuildFrameStepperRow();
    ImGui::Separator();

    if (!m_enabled) {
        ImGui::TextDisabled("Enable Frame Debugger above to inspect the current frame's render events.");
    } else {
        // PHASE4/PHASE5/PHASE6 fill this branch in (event tree pane +
        // splitter + inspector pane). Left as an explicit, clearly
        // labeled placeholder for now rather than an empty branch, so a
        // reader mid-campaign can tell this is intentionally
        // unfinished, not accidentally empty.
        ImGui::TextDisabled("(event tree + inspector - added in a later phase of this campaign)");
    }

    ImGui::End();
}

} // namespace gte
