#include "FrameDebuggerPanel.h"

#include "../EditorContext.h"

#include <imgui.h>

namespace gte {

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

    // PHASE3 replaces this placeholder with the real toolbar/stepper/
    // tree/inspector content.
    ImGui::TextDisabled("Coming soon.");

    ImGui::End();
}

} // namespace gte
