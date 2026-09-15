#include "FrameDebuggerPanel.h"

#include "../EditorContext.h"
#include "../MemoryPanelData.h" // gte::ToString(VkFormat) - reused for the real render-target format label (PHASE3).
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/RenderTexture.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>

namespace gte {

namespace {
constexpr float kSplitterWidth = 6.0f;

void BuildPropertyRow(const char* label, const std::string& value)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(150.0f);
    ImGui::TextUnformatted(value.c_str());
}

} // namespace

FrameDebuggerCaptureContext* FrameDebuggerPanel::PrepareCaptureContextForThisFrame(EditorContext& ctx)
{
    if (!ctx.frameDebuggerWindowOpen || !m_enabled) {
        return nullptr;
    }
    m_captureContext.Reset();
    return &m_captureContext;
}

void FrameDebuggerPanel::NotifyStepConsumed() noexcept
{
    m_stepCaptureRequested = true;
}

void FrameDebuggerPanel::TriggerCapture()
{
    if (m_frameRenderer == nullptr || m_frameRenderGraph == nullptr || m_frameGameView == nullptr) {
        // Defensive only - Build() below always sets these, unconditionally,
        // before BuildToolbarRow() (the only caller of TriggerCapture())
        // ever runs, so this should be unreachable in practice.
        return;
    }

    const rg::RenderGraphSnapshot graphSnapshot =
        m_frameRenderGraph->LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback);

    FrameDebuggerRenderTargetInfo renderTargetInfo;
    const VkExtent2D extent = m_frameGameView->Extent();
    renderTargetInfo.width = static_cast<int>(extent.width);
    renderTargetInfo.height = static_cast<int>(extent.height);
    renderTargetInfo.format = ToString(m_frameGameView->Format());

    const FrameDebuggerSnapshot snapshot =
        BuildRealFrameDebuggerSnapshot(graphSnapshot, m_captureContext, m_frameGpuSkinningPassNames, renderTargetInfo);

    m_history.CaptureFrame(*m_frameRenderer, snapshot, *m_frameGameView);
    m_selectedEventIndex = -1;
}

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

        // PHASE3 (task_manager/frame-debugger-3/
        // PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md, Step
        // 3.2, call site 1) - the very first "Enable" click also performs
        // the very first real capture, so the tree is never left showing
        // nothing the moment Enable is checked. NOTE: this exact frame's
        // own m_captureContext was armed based on m_enabled as of the END
        // of LAST frame (still false) - see
        // PrepareCaptureContextForThisFrame(), called earlier THIS frame,
        // before Game::Render() ever ran - so this particular capture's
        // own shader/texture/matrix facts may be empty (a real, honest
        // "GameView" leaf with real draw-stats/blend-Z-stencil info, just
        // no per-draw facts yet); real per-draw facts start flowing from
        // the NEXT captured frame onward, once arming has caught up. This
        // is the same one-frame lag every other Editor<->engine feedback
        // loop in this codebase already accepts (see e.g.
        // IEditorLayer::IsPlaybackPaused()'s own doc comment).
        TriggerCapture();
    }

    ImGui::SameLine();

    // PHASE3's new explicit "Capture" button - re-captures on demand
    // without stepping (e.g. after moving the Scene-view camera, or after
    // an unrelated scene edit, while the simulation itself stays paused).
    // Only enabled/clickable while m_enabled is true.
    ImGui::BeginDisabled(!m_enabled);
    if (ImGui::Button("Capture")) {
        TriggerCapture();
    }
    ImGui::EndDisabled();

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

    // PHASE3's Step-triggered capture (call site 2) - serviced HERE
    // (rather than back where NotifyStepConsumed() itself was called,
    // Application::Run(), early in the frame, well before Game::Render()
    // even ran) because TriggerCapture() needs THIS frame's now-FINAL
    // RenderGraphSnapshot/FrameDebuggerCaptureContext/Game View pixels,
    // all of which only become available once BuildUI() (and therefore
    // this very Build() call) runs, later in the SAME frame Step was
    // consumed. Re-checks m_enabled here (its freshest value THIS frame,
    // including any edit the Enable checkbox above just made) rather than
    // trusting whatever it was back when NotifyStepConsumed() was called.
    if (m_stepCaptureRequested) {
        m_stepCaptureRequested = false;
        if (m_enabled) {
            TriggerCapture();
        }
    }
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

    const std::optional<FrameDebuggerEventDetails> details
        = FindEventDetailsByIndex(snapshot, m_selectedEventIndex);
    BuildEventDetailsSection(details);
}

void FrameDebuggerPanel::BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>& details)
{
    if (!details.has_value()) {
        // ALWAYS this branch in production this campaign - see this
        // file's own top-of-file comment and PHASE0_MASTER_STRATEGY.md's
        // Locked Design Decision #2. Not a bug; the correct final state.
        ImGui::TextDisabled("No event selected.");
        return;
    }

    // Unreachable in practice this campaign (details is always
    // std::nullopt - see FindEventDetailsByIndex()'s own doc comment in
    // FrameDebuggerData.h), but fully correct and ready for a future
    // real-capture campaign to exercise for free the moment
    // FrameDebuggerEventNode::details starts being populated for real.
    const FrameDebuggerEventDetails& d = *details;

    ImGui::Text("Event #%d: %s", d.eventIndex, d.eventLabel.c_str());
    ImGui::Separator();

    BuildPropertyRow("Shader", d.shaderName);
    BuildPropertyRow("Pass", d.passName);
    BuildPropertyRow("Blend", d.blendMode);
    BuildPropertyRow("ZClip", d.zClip);
    BuildPropertyRow("ZTest", d.zTest);
    BuildPropertyRow("ZWrite", d.zWrite);
    BuildPropertyRow("Cull", d.cull);
    BuildPropertyRow("Stencil Ref", d.stencilRef);
    BuildPropertyRow("Stencil Comp", d.stencilComp);
    BuildPropertyRow("Stencil Pass", d.stencilPass);
    BuildPropertyRow("Stencil Fail", d.stencilFail);
    BuildPropertyRow("Stencil ZFail", d.stencilZFail);

    ImGui::Spacing();

    if (ImGui::BeginTabBar("FrameDebuggerEventTabs")) {
        if (ImGui::BeginTabItem("Preview")) {
            // A real per-draw-call preview render is out of scope for
            // this campaign (see PHASE6's own Step 2) - a much larger,
            // separately-sized future feature.
            ImGui::TextDisabled("Not available yet.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ShaderProperties")) {
            if (!d.textures.empty()) {
                ImGui::SeparatorText("Textures");
                for (const FrameDebuggerTextureProperty& texture : d.textures) {
                    BuildPropertyRow(texture.name.c_str(), texture.valueLabel);
                }
            }
            if (!d.vectors.empty()) {
                ImGui::SeparatorText("Vectors");
                for (const FrameDebuggerVectorProperty& vector : d.vectors) {
                    BuildPropertyRow(vector.name.c_str(), FormatVectorProperty(vector));
                }
            }
            if (!d.matrices.empty()) {
                ImGui::SeparatorText("Matrices");
                for (const FrameDebuggerMatrixProperty& matrix : d.matrices) {
                    ImGui::TextUnformatted(matrix.name.c_str());
                    const std::string formatted = FormatMatrixProperty(matrix);
                    // Split on '\n' and draw each row as its own
                    // ImGui::Text() call - see FormatMatrixProperty()'s
                    // own doc comment for why a single multi-line
                    // ImGui::Text() call is avoided here.
                    std::size_t start = 0;
                    while (start <= formatted.size()) {
                        const std::size_t newlinePos = formatted.find('\n', start);
                        const std::string rowText = formatted.substr(
                            start, newlinePos == std::string::npos ? std::string::npos : newlinePos - start);
                        ImGui::Text("    %s", rowText.c_str());
                        if (newlinePos == std::string::npos) {
                            break;
                        }
                        start = newlinePos + 1;
                    }
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void FrameDebuggerPanel::Build(EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph,
    RenderTexture& gameView, const std::vector<std::string>& gpuSkinningPassNamesThisFrame)
{
    // PHASE3 - cached for TriggerCapture()'s own use for the rest of THIS
    // call (BuildToolbarRow(), below, is the only thing that reads these) -
    // see this class's own header comment for why these are safe,
    // non-owning raw pointers/reference here. Set unconditionally, even
    // though the window itself may turn out to be closed below - cheap
    // (a few pointer/reference copies), and keeps this the ONE place that
    // ever touches these members.
    m_frameRenderer = &renderer;
    m_frameRenderGraph = &renderGraph;
    m_frameGameView = &gameView;
    m_frameGpuSkinningPassNames = gpuSkinningPassNamesThisFrame;

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
        // PHASE3 explicitly does NOT switch this over to m_history's real
        // data yet (that is PHASE4's job - see this file's own header
        // comment) - the displayed tree/inspector still read the exact
        // same placeholder this campaign's earlier phases already used,
        // even though a real capture genuinely happened above (in
        // BuildToolbarRow()) and m_history now genuinely holds it.
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
