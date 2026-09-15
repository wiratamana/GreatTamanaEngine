#include "PlaybackControls.h"

#include "EditorContext.h"

#include <imgui.h>

namespace gte {

void BuildPlaybackToolbar(EditorContext& ctx)
{
    // A single toggle button whose label reflects the CURRENT state -
    // Unity's own Play button also visually toggles rather than using two
    // separate buttons for the two states.
    if (ImGui::Button(ctx.playbackPaused ? "Resume" : "Pause")) {
        ctx.playbackPaused = !ctx.playbackPaused;
    }

    ImGui::SameLine();

    // "Step" only ever makes sense while already paused - rendered
    // visibly disabled otherwise (grayed out, un-clickable), rather than
    // silently doing nothing on click, so the control's own affordance
    // matches its actual behavior.
    ImGui::BeginDisabled(!ctx.playbackPaused);
    if (ImGui::Button("Step")) {
        ctx.stepOneFrameRequested = true;
    }
    ImGui::EndDisabled();

    if (ctx.playbackPaused) {
        ImGui::SameLine();
        ImGui::TextDisabled("(Paused)");
    }

    ImGui::Separator();
}

} // namespace gte
