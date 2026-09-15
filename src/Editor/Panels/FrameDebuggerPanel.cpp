#include "FrameDebuggerPanel.h"

#include "../EditorContext.h"

#include <imgui.h>

#include <algorithm>

namespace gte {

namespace {
constexpr float kSplitterWidth = 6.0f;
} // namespace

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

void FrameDebuggerPanel::RenderEventNode(const FrameDebuggerEventNode& node)
{
    if (!node.isDrawCall) {
        // A group node (e.g. "Drawing", "Render.OpaqueGeometry") - an
        // expandable tree header; default-open so a shallow real
        // hierarchy is legible without extra clicking, mirroring the
        // reference screenshot's own mostly-expanded look.
        const bool open = ImGui::TreeNodeEx(node.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (open) {
            for (const FrameDebuggerEventNode& child : node.children) {
                RenderEventNode(child);
            }
            ImGui::TreePop();
        }
        return;
    }

    // A leaf draw-call row - a single selectable line, highlighted when
    // it matches m_selectedEventIndex; clicking it selects it (feeding
    // PHASE6's BuildEventDetailsSection() via FindEventDetailsByIndex()).
    const bool isSelected = (node.eventIndex == m_selectedEventIndex);
    if (ImGui::Selectable(node.name.c_str(), isSelected)) {
        m_selectedEventIndex = node.eventIndex;
    }
}

void FrameDebuggerPanel::BuildEventTreePane(const FrameDebuggerSnapshot& snapshot)
{
    if (snapshot.rootNodes.empty()) {
        // Always this branch this campaign - see PHASE0_MASTER_STRATEGY.md's
        // Locked Design Decision #2: zero fake/mock rows, ever.
        ImGui::TextDisabled("No frame captured yet.");
        return;
    }

    // Unreachable in practice this campaign (rootNodes is always
    // empty), but fully correct - ready for a future real-capture
    // campaign to exercise for free.
    for (const FrameDebuggerEventNode& root : snapshot.rootNodes) {
        RenderEventNode(root);
    }
}

void FrameDebuggerPanel::BuildInspectorPane(const FrameDebuggerSnapshot& snapshot)
{
    // --- RenderTarget selector row (frame-level, not event-level) ---
    ImGui::TextUnformatted("RenderTarget");
    ImGui::SameLine(150.0f);
    ImGui::TextUnformatted(snapshot.renderTarget.name.c_str());

    static constexpr const char* kRenderTargetItems[] = { "RT 0" };
    int rtIndex = 0;
    ImGui::SetNextItemWidth(80.0f);
    ImGui::BeginDisabled();
    ImGui::Combo("##FrameDebuggerRenderTarget", &rtIndex, kRenderTargetItems, 1);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextUnformatted("Channels");
    ImGui::SameLine();
    // Cosmetic-only toggle row - no real channel-isolation concept
    // exists this campaign (there is no real texture to isolate a
    // channel of yet). Purely visual parity with the reference
    // screenshot; clicking these currently has no effect beyond its own
    // pressed-highlight look.
    for (const char* channelLabel : { "All", "R", "G", "B", "A" }) {
        ImGui::SameLine();
        ImGui::SmallButton(channelLabel);
    }

    // --- Levels slider (frame-level) ---
    ImGui::TextUnformatted("Levels");
    ImGui::SameLine();
    float levelsValue = 0.0f;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled();
    ImGui::SliderFloat("##FrameDebuggerLevels", &levelsValue, 0.0f, 1.0f, "");
    ImGui::EndDisabled();

    // --- Texture preview placeholder box ---
    const std::string resolutionCaption = std::to_string(snapshot.renderTarget.width) + "x"
        + std::to_string(snapshot.renderTarget.height) + "  " + snapshot.renderTarget.format;
    ImGui::TextDisabled("%s", resolutionCaption.c_str());

    const float previewHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.5f);
    ImGui::BeginChild("FrameDebuggerTexturePreview", ImVec2(0.0f, previewHeight), true);
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const char* placeholderText = "No Texture";
        const ImVec2 textSize = ImGui::CalcTextSize(placeholderText);
        ImGui::SetCursorPos(ImVec2(
            std::max(0.0f, (avail.x - textSize.x) * 0.5f), std::max(0.0f, (avail.y - textSize.y) * 0.5f)));
        ImGui::TextDisabled("%s", placeholderText);
    }
    ImGui::EndChild();

    ImGui::Separator();

    // PHASE6 appends BuildEventDetailsSection() right here.
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
        const FrameDebuggerSnapshot snapshot = BuildPlaceholderFrameDebuggerSnapshot();

        const float totalAvailWidth = ImGui::GetContentRegionAvail().x;
        const float paneAreaHeight = ImGui::GetContentRegionAvail().y;
        const float maxLeftWidth = std::max(120.0f, totalAvailWidth - 200.0f - kSplitterWidth);
        m_leftPaneWidth = std::clamp(m_leftPaneWidth, 120.0f, maxLeftWidth);

        ImGui::BeginChild("FrameDebuggerEventTree", ImVec2(m_leftPaneWidth, paneAreaHeight), true);
        BuildEventTreePane(snapshot);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::Button("##FrameDebuggerSplitter", ImVec2(kSplitterWidth, paneAreaHeight));
        if (ImGui::IsItemActive()) {
            m_leftPaneWidth += ImGui::GetIO().MouseDelta.x;
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        ImGui::SameLine();

        ImGui::BeginChild("FrameDebuggerInspector", ImVec2(0.0f, paneAreaHeight), true);
        BuildInspectorPane(snapshot);
        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace gte
