#include "FrameDebuggerPanel.h"

#include "../EditorContext.h"
#include "../MemoryPanelData.h" // gte::ToString(VkFormat) - reused for the real render-target format label (PHASE3).
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderTexture.h"

#include <backends/imgui_impl_vulkan.h>
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

FrameDebuggerPanel::~FrameDebuggerPanel()
{
    // Same "wait for the GPU to actually be done with it first" reasoning as
    // BoneViewerWindow::Reset() - m_previewDescriptor may still be
    // referenced by an in-flight command buffer from a recent frame.
    if (m_device != VK_NULL_HANDLE && m_previewDescriptor != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
    ReleasePreviewDescriptor();
}

void FrameDebuggerPanel::ReleasePreviewDescriptor()
{
    if (m_previewDescriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_previewDescriptor);
        m_previewDescriptor = VK_NULL_HANDLE;
        m_lastKnownPreviewView = VK_NULL_HANDLE;
    }
}

void FrameDebuggerPanel::EnsurePreviewDescriptor()
{
    const FrameDebuggerHistoryEntry* entry = m_history.CurrentEntry();
    const bool hasPreview = (entry != nullptr) && entry->preview.has_value();

    if (!hasPreview) {
        // Nothing to preview right now (no capture has ever happened yet) -
        // release any stale descriptor so BuildInspectorPane() falls back to
        // the "No Texture" placeholder cleanly.
        ReleasePreviewDescriptor();
        return;
    }

    const VkImageView currentView = entry->preview->View();
    if (m_previewDescriptor != VK_NULL_HANDLE && currentView == m_lastKnownPreviewView) {
        return; // Already wrapping the right VkImageView - nothing to do.
    }

    ReleasePreviewDescriptor();
    m_previewDescriptor = ImGui_ImplVulkan_AddTexture(
        entry->preview->Sampler(), currentView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_lastKnownPreviewView = currentView;
}

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

void FrameDebuggerPanel::BuildFrameHistoryToolbarRow()
{
    // PHASE4's new Frame-History mini-toolbar - a COMPLETELY SEPARATE
    // control from BuildFrameStepperRow() below (Locked Design Decision #4,
    // PHASE0_MASTER_STRATEGY.md): this one scrubs across WHICH CAPTURED
    // FRAME (of up to FrameDebuggerHistory::kCapacity) is being viewed, not
    // which event within it is selected. Deliberately shown regardless of
    // m_enabled - a user may have disabled further capturing but still want
    // to scrub back through frames captured earlier this session.
    const int count = m_history.Count();
    const int cursor = m_history.CursorIndex();

    ImGui::TextUnformatted("Frame History");
    ImGui::SameLine();

    ImGui::BeginDisabled(count == 0 || cursor <= 0);
    if (ImGui::ArrowButton("##FrameDebuggerHistoryPrev", ImGuiDir_Left)) {
        m_history.StepCursor(-1);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const std::string label = FormatFrameHistoryLabel(cursor, count);
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();

    ImGui::BeginDisabled(count == 0 || cursor >= count - 1);
    if (ImGui::ArrowButton("##FrameDebuggerHistoryNext", ImGuiDir_Right)) {
        m_history.StepCursor(1);
    }
    ImGui::EndDisabled();
}

void FrameDebuggerPanel::BuildFrameStepperRow(const FrameDebuggerSnapshot& snapshot)
{
    // PHASE4 - now shows REAL numbers (this is the EXISTING "which event,
    // within the currently-viewed captured frame, is selected" axis - see
    // Locked Design Decision #4; do not conflate this with
    // BuildFrameHistoryToolbarRow() above). The slider itself stays a purely
    // cosmetic, disabled control (matching the reference screenshot's own
    // scrubber look) - clicking a tree row (RenderEventNode() below) is
    // still the only way to change m_selectedEventIndex this campaign.
    int stepperValue = m_selectedEventIndex < 0 ? 0 : m_selectedEventIndex;
    ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::SliderInt(
        "##FrameDebuggerStepper", &stepperValue, 0, std::max(0, snapshot.totalEventCount - 1), "");
    ImGui::SameLine();
    const std::string label = FormatFrameStepperLabel(m_selectedEventIndex, snapshot.totalEventCount);
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
        // Real, reachable state whenever m_history has never captured
        // anything yet (e.g. the window was just opened and "Enable"/
        // "Capture" hasn't run this session) - see PHASE0_MASTER_STRATEGY.md's
        // Locked Design Decision #2: zero fake/mock rows, ever.
        ImGui::TextDisabled("No frame captured yet.");
        return;
    }

    for (const FrameDebuggerEventNode& root : snapshot.rootNodes) {
        RenderEventNode(root);
    }
}

void FrameDebuggerPanel::BuildInspectorPane(
    const FrameDebuggerSnapshot& snapshot, const FrameDebuggerHistoryEntry* currentEntry)
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

    // --- Texture preview box ---
    const std::string resolutionCaption = std::to_string(snapshot.renderTarget.width) + "x"
        + std::to_string(snapshot.renderTarget.height) + "  " + snapshot.renderTarget.format;
    ImGui::TextDisabled("%s", resolutionCaption.c_str());

    const std::optional<FrameDebuggerEventDetails> details
        = FindEventDetailsByIndex(snapshot, m_selectedEventIndex);

    // PHASE4 - real preview display (Locked Design Decision #5,
    // PHASE0_MASTER_STRATEGY.md): the retained preview texture is a
    // per-CAPTURED-FRAME thing (one real image, taken right when the
    // "GameView" pass finished), not a per-EVENT thing - so it is shown
    // whenever the currently-viewed history entry actually has one,
    // EXCEPT when the currently-SELECTED event is a GPU-skinning leaf
    // (details->passName == "GPU Skinning" - see FrameDebuggerData.cpp's
    // own BuildGpuSkinningLeaf()) - a compute dispatch genuinely has no
    // color image of its own, and showing one anyway would be dishonest.
    // Nothing selected (m_selectedEventIndex == -1) falls through to
    // showing the texture too, matching the RenderTarget row's own
    // "frame-level, not event-level" framing immediately above.
    const bool selectedEventIsGpuSkinning = details.has_value() && details->passName == "GPU Skinning";
    const bool showPreviewTexture =
        (m_previewDescriptor != VK_NULL_HANDLE) && (currentEntry != nullptr) && !selectedEventIsGpuSkinning;

    const float previewHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.5f);
    ImGui::BeginChild("FrameDebuggerTexturePreview", ImVec2(0.0f, previewHeight), true);
    {
        if (showPreviewTexture) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x >= 1.0f && avail.y >= 1.0f) {
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_previewDescriptor)), avail);
            }
        } else {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const char* placeholderText = "No Texture";
            const ImVec2 textSize = ImGui::CalcTextSize(placeholderText);
            ImGui::SetCursorPos(ImVec2(
                std::max(0.0f, (avail.x - textSize.x) * 0.5f), std::max(0.0f, (avail.y - textSize.y) * 0.5f)));
            ImGui::TextDisabled("%s", placeholderText);
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    BuildEventDetailsSection(details);
}

void FrameDebuggerPanel::BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>& details)
{
    if (!details.has_value()) {
        // Real, reachable state whenever nothing is currently selected
        // (e.g. right after a fresh capture, which always resets
        // m_selectedEventIndex to -1 - see TriggerCapture()).
        ImGui::TextDisabled("No event selected.");
        return;
    }

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

    // PHASE4 - refreshed unconditionally every call, cheap, mirrors
    // BoneViewerWindow's own m_device precedent - only ever actually read by
    // this class's own destructor (see ~FrameDebuggerPanel()).
    m_device = renderer.GetVulkanContextInfo().device;

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

    // PHASE4 (task_manager/frame-debugger-3/
    // PHASE4_PANEL_REAL_TREE_AND_FRAME_HISTORY_UI.md, Step 3.1) - swap the
    // displayed snapshot's own source from BuildPlaceholderFrameDebuggerSnapshot()
    // to m_history's currently-viewed entry, falling back to that exact
    // same placeholder's own empty-tree behavior whenever no real capture
    // has ever happened yet this session (m_history.CurrentEntry() ==
    // nullptr) - a real, honest, reachable "enabled, but not yet captured"
    // state (see BuildEventTreePane()'s own updated comment above).
    const FrameDebuggerHistoryEntry* currentEntry = m_history.CurrentEntry();
    const FrameDebuggerSnapshot snapshot =
        (currentEntry != nullptr) ? currentEntry->snapshot : BuildPlaceholderFrameDebuggerSnapshot();

    // PHASE4 - clears any stale event selection whenever the VIEWED history
    // entry itself changes underneath us (a Frame-History Prev/Next
    // navigation - see BuildFrameHistoryToolbarRow() below; a brand-new
    // capture already resets m_selectedEventIndex directly, in
    // TriggerCapture()). eventIndex values are only meaningful relative to
    // the specific snapshot they were assigned in - carrying a selection
    // over to a DIFFERENT captured frame's tree (which may have a
    // completely different shape, e.g. no GPU-skinning leaves this time)
    // could otherwise highlight/describe the wrong event by sheer index
    // coincidence. Compared via the entry's own retained preview
    // VkImageView (guaranteed fresh on every single real capture - see
    // FrameDebuggerHistory::CaptureFrame()'s own doc comment), captured
    // BEFORE EnsurePreviewDescriptor() below updates it.
    {
        const VkImageView previousPreviewView = m_lastKnownPreviewView;
        const VkImageView newPreviewView =
            (currentEntry != nullptr && currentEntry->preview.has_value()) ? currentEntry->preview->View()
                                                                            : VK_NULL_HANDLE;
        if (newPreviewView != previousPreviewView) {
            m_selectedEventIndex = -1;
        }
    }

    // PHASE4 - keeps m_previewDescriptor in sync with whichever history
    // entry is currently being viewed (see EnsurePreviewDescriptor()'s own
    // doc comment). Called unconditionally here (cheap - a no-op unless the
    // underlying VkImageView actually changed), not gated on m_enabled,
    // since the Frame-History toolbar below lets a user scrub through past
    // captures even while further capturing is currently disabled.
    EnsurePreviewDescriptor();

    // PHASE4's new Frame-History mini-toolbar - see BuildFrameHistoryToolbarRow()'s
    // own doc comment for why this is a SEPARATE control from the
    // event-stepper row immediately below (Locked Design Decision #4).
    BuildFrameHistoryToolbarRow();
    ImGui::Separator();

    BuildFrameStepperRow(snapshot);
    ImGui::Separator();

    if (!m_enabled) {
        ImGui::TextDisabled("Enable Frame Debugger above to inspect the current frame's render events.");
    } else {
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
        BuildInspectorPane(snapshot, currentEntry);
        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace gte
