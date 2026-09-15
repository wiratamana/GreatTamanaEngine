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
#include <utility>

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
    // referenced by an in-flight command buffer from a recent frame. PHASE6
    // widened this to an unconditional wait (whenever a live VkDevice is
    // even known) rather than gating on m_previewDescriptor alone -
    // m_previewProcessor (PHASE6) may itself own live GPU resources
    // (a ComputePipeline/descriptor set/scratch Texture2D) even in the rare
    // case m_previewDescriptor happens to be null at shutdown time, and
    // those must be just as safe to destroy as m_previewDescriptor's own
    // wrapped texture.
    if (m_device != VK_NULL_HANDLE) {
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

    // PHASE6 (task_manager/frame-debugger-3/PHASE6_CHANNELS_AND_LEVELS_REAL_PREVIEW.md,
    // Step 3.2) - Channels/Levels at their neutral defaults is a valid,
    // cheap optimization: display the RAW retained history texture
    // directly, skipping the compute dispatch entirely. Anything else
    // re-dispatches FrameDebuggerPreviewRenderer (only when actually dirty -
    // see below) and displays ITS OWN separate scratch texture instead -
    // the retained historical copy itself is never mutated in place
    // (Locked Design Decision #8).
    const bool isNeutral =
        (m_channel == FrameDebuggerPreviewChannel::All) && (m_levelsBlack <= 0.0f) && (m_levelsWhite >= 1.0f);

    VkImageView desiredView = entry->preview->View();
    VkSampler desiredSampler = entry->preview->Sampler();

    if (!isNeutral && m_frameRenderer != nullptr) {
        const VkImageView sourceView = entry->preview->View();
        const bool dirty = (m_lastProcessedSourceView != sourceView) || (m_lastProcessedChannel != m_channel)
            || (m_lastProcessedLevelsBlack != m_levelsBlack) || (m_lastProcessedLevelsWhite != m_levelsWhite);
        if (dirty) {
            m_previewProcessor.RenderPreview(*m_frameRenderer, *entry->preview, m_channel, m_levelsBlack, m_levelsWhite);
            m_lastProcessedSourceView = sourceView;
            m_lastProcessedChannel = m_channel;
            m_lastProcessedLevelsBlack = m_levelsBlack;
            m_lastProcessedLevelsWhite = m_levelsWhite;
        }
        desiredView = m_previewProcessor.OutputView();
        desiredSampler = m_previewProcessor.OutputSampler();
    }

    if (m_previewDescriptor != VK_NULL_HANDLE && desiredView == m_lastKnownPreviewView) {
        return; // Already wrapping the right VkImageView - nothing to do.
    }

    ReleasePreviewDescriptor();
    m_previewDescriptor = ImGui_ImplVulkan_AddTexture(desiredSampler, desiredView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_lastKnownPreviewView = desiredView;
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

// task_manager/frame-debugger-3 campaign, PHASE7
// (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - the exact
// same false->true-edge effect the "Enable" checkbox has always had
// (frame-debugger-2's Locked Design Decision #3 + PHASE3's Step 3.2 call
// site 1), now shared by BOTH BuildToolbarRow()'s own checkbox AND
// SetEnabledFromCommand() (the new HTTP-automation entry point) - see this
// method's own callers for why the false->true edge auto-engages Pause and
// triggers the very first real capture, and why turning Enable back OFF
// deliberately does NOT auto-resume (a user inspecting a paused frame
// should not be silently un-paused just for closing/disabling this debug
// window, whether that happened by hand or via GET /frame_debugger/enable).
void FrameDebuggerPanel::ApplyEnabledEdge(EditorContext& ctx, bool newEnabled)
{
    const bool wasEnabled = m_enabled;
    m_enabled = newEnabled;
    if (m_enabled && !wasEnabled) {
        ctx.playbackPaused = true;

        // NOTE: this exact frame's own m_captureContext was armed based on
        // m_enabled as of the END of LAST frame (still false) - see
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
}

void FrameDebuggerPanel::BuildToolbarRow(EditorContext& ctx)
{
    bool enabledValue = m_enabled;
    ImGui::Checkbox("Enable", &enabledValue);
    if (enabledValue != m_enabled) {
        ApplyEnabledEdge(ctx, enabledValue);
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
    // PHASE6 (task_manager/frame-debugger-3/PHASE6_CHANNELS_AND_LEVELS_REAL_PREVIEW.md,
    // Locked Design Decision #8) - now genuinely real: clicking a button
    // sets m_channel directly (read by EnsurePreviewDescriptor()), and is
    // highlighted (reusing the existing ImGuiCol_ButtonActive theme color -
    // never a hardcoded one, mirroring BoneViewerWindow.cpp/
    // Panels/InspectorPanel.cpp's own splitter-button precedent) exactly
    // when it is the currently active channel.
    static constexpr std::pair<const char*, FrameDebuggerPreviewChannel> kChannelButtons[] = {
        { "All", FrameDebuggerPreviewChannel::All },
        { "R", FrameDebuggerPreviewChannel::R },
        { "G", FrameDebuggerPreviewChannel::G },
        { "B", FrameDebuggerPreviewChannel::B },
        { "A", FrameDebuggerPreviewChannel::A },
    };
    for (const auto& [channelLabel, channelValue] : kChannelButtons) {
        ImGui::SameLine();
        const bool isActive = (m_channel == channelValue);
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::SmallButton(channelLabel)) {
            m_channel = channelValue;
        }
        if (isActive) {
            ImGui::PopStyleColor();
        }
    }

    // --- Levels slider (frame-level) ---
    ImGui::TextUnformatted("Levels");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    // A real two-handle range control (PHASE6) - writes m_levelsBlack/
    // m_levelsWhite directly (read by EnsurePreviewDescriptor()).
    // ImGuiSliderFlags_AlwaysClamp keeps both handles inside [0, 1] and
    // never lets them cross - the extra clamp immediately below is cheap,
    // purely defensive insurance on top of that (ApplyFrameDebuggerPreviewTransform()'s
    // own remap already tolerates a degenerate/inverted pair without ever
    // dividing by zero regardless).
    ImGui::DragFloatRange2("##FrameDebuggerLevels", &m_levelsBlack, &m_levelsWhite, 0.005f, 0.0f, 1.0f, "Black %.2f",
        "White %.2f", ImGuiSliderFlags_AlwaysClamp);
    m_levelsWhite = std::max(m_levelsWhite, m_levelsBlack + 0.001f);

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
    //
    // task_manager/frame-debugger-3 campaign, PHASE7
    // (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md, Step
    // 3.4) - the main-viewport pin: whenever RequestOpenWindow() armed
    // m_pinToMainViewportNextOpen (a PROGRAMMATIC open, e.g. via
    // GET /frame_debugger/open), force this window onto the main ImGui
    // viewport, at a fixed generous size anchored inside its own work
    // area - copying DockLayout.cpp's own already-proven
    // ImGui::SetNextWindowPos(viewport->WorkPos) precedent for the main
    // dockspace host window, PLUS an explicit SetNextWindowViewport() call
    // (the one piece that precedent doesn't need, since the dockspace host
    // is already the main-viewport window by construction) - this is what
    // prevents Dear ImGui from ever classifying THIS window as a separate
    // platform window (see ImGuiEditorLayer.cpp's own
    // ImGuiConfigFlags_ViewportsEnable comment), so GET /get_swapchain (which
    // only ever reads back the MAIN window's own swapchain image) can see it
    // on the very first frame it opens. A ONE-SHOT pin - cleared immediately
    // after, so a human can still freely drag it away afterwards, exactly
    // like ordinary manual "Window > Frame Debugger" use (which never sets
    // this bool at all, and therefore never takes this branch).
    if (m_pinToMainViewportNextOpen) {
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        const ImVec2 pinnedSize(
            std::min(900.0f, mainViewport->WorkSize.x), std::min(600.0f, mainViewport->WorkSize.y));
        ImGui::SetNextWindowViewport(mainViewport->ID);
        ImGui::SetNextWindowPos(mainViewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(pinnedSize, ImGuiCond_Always);
        m_pinToMainViewportNextOpen = false;
    } else {
        ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
    }
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
    // coincidence. Compared via the entry's own RAW retained preview
    // VkImageView (guaranteed fresh on every single real capture - see
    // FrameDebuggerHistory::CaptureFrame()'s own doc comment) tracked in
    // m_lastKnownRawPreviewView - PHASE6 deliberately does NOT reuse
    // m_lastKnownPreviewView for this check anymore, since that field can
    // now instead hold m_previewProcessor's own processed scratch
    // VkImageView (a non-neutral Channels/Levels state) - see
    // m_lastKnownRawPreviewView's own doc comment (FrameDebuggerPanel.h) for
    // why reusing m_lastKnownPreviewView here would have incorrectly reset
    // the selection on every single frame in that case.
    {
        const VkImageView previousRawPreviewView = m_lastKnownRawPreviewView;
        const VkImageView newRawPreviewView =
            (currentEntry != nullptr && currentEntry->preview.has_value()) ? currentEntry->preview->View()
                                                                            : VK_NULL_HANDLE;
        if (newRawPreviewView != previousRawPreviewView) {
            m_selectedEventIndex = -1;
        }
        m_lastKnownRawPreviewView = newRawPreviewView;
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

// task_manager/frame-debugger-3 campaign, PHASE7
// (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - HTTP
// automation entry points (see FrameDebuggerPanel.h's own doc comments for
// each method's exact contract).

void FrameDebuggerPanel::RequestOpenWindow(EditorContext& ctx)
{
    if (!ctx.frameDebuggerWindowOpen) {
        ctx.frameDebuggerWindowOpen = true;
        m_pinToMainViewportNextOpen = true;
    }
}

void FrameDebuggerPanel::SetEnabledFromCommand(EditorContext& ctx, bool enabled)
{
    ApplyEnabledEdge(ctx, enabled);
}

bool FrameDebuggerPanel::CaptureNowFromCommand()
{
    if (!m_enabled) {
        // Mirrors the "Capture" button's own BeginDisabled(!m_enabled)
        // guard - a real capture without Enable first would only ever
        // reflect stale/never-armed FrameDebuggerCaptureContext data (see
        // PrepareCaptureContextForThisFrame()'s own doc comment), so this
        // is a safe, honest no-op rather than a dishonest "successful"
        // capture of nothing meaningful.
        return false;
    }
    TriggerCapture();
    return true;
}

void FrameDebuggerPanel::SelectEventFromCommand(int index)
{
    const FrameDebuggerHistoryEntry* currentEntry = m_history.CurrentEntry();
    const int totalEventCount = (currentEntry != nullptr) ? currentEntry->snapshot.totalEventCount : 0;
    m_selectedEventIndex = ClampSelectedEventIndex(index, totalEventCount);
}

void FrameDebuggerPanel::StepFrameHistoryFromCommand(int delta)
{
    // Build()'s own existing "did the viewed history entry itself change"
    // detection (comparing m_lastKnownRawPreviewView) already resets
    // m_selectedEventIndex on the very next Build() call this same frame -
    // no separate handling needed here, exactly mirroring what a real
    // Prev/Next button click already relies on (see
    // BuildFrameHistoryToolbarRow()).
    m_history.StepCursor(delta);
}

bool FrameDebuggerPanel::SetChannelFromCommand(const std::string& channel)
{
    if (channel == "all") {
        m_channel = FrameDebuggerPreviewChannel::All;
        return true;
    }
    if (channel == "r") {
        m_channel = FrameDebuggerPreviewChannel::R;
        return true;
    }
    if (channel == "g") {
        m_channel = FrameDebuggerPreviewChannel::G;
        return true;
    }
    if (channel == "b") {
        m_channel = FrameDebuggerPreviewChannel::B;
        return true;
    }
    if (channel == "a") {
        m_channel = FrameDebuggerPreviewChannel::A;
        return true;
    }
    return false;
}

void FrameDebuggerPanel::SetLevelsFromCommand(float black, float white)
{
    // Same clamp discipline as the Levels DragFloatRange2 UI control
    // itself (ImGuiSliderFlags_AlwaysClamp keeps both handles inside
    // [0, 1] and never lets them cross) - see BuildInspectorPane().
    m_levelsBlack = std::clamp(black, 0.0f, 1.0f);
    m_levelsWhite = std::clamp(white, 0.0f, 1.0f);
    m_levelsWhite = std::max(m_levelsWhite, m_levelsBlack + 0.001f);
}

FrameDebuggerStateSnapshotView FrameDebuggerPanel::BuildStateSnapshotView(const EditorContext& ctx) const
{
    FrameDebuggerStateSnapshotView view;
    view.enabled = m_enabled;
    view.windowOpen = ctx.frameDebuggerWindowOpen;
    view.historyCount = m_history.Count();
    view.historyCursor = m_history.CursorIndex();

    const FrameDebuggerHistoryEntry* currentEntry = m_history.CurrentEntry();
    view.totalEventCount = (currentEntry != nullptr) ? currentEntry->snapshot.totalEventCount : 0;
    view.selectedEventIndex = m_selectedEventIndex;

    // Deliberately NO `default:` case - the same exhaustive-switch
    // convention RenderGraphTypes.cpp's ToString(ResourceAccess)/
    // IsWriteAccess() already establish (see PHASE6_COMPLETION_REPORT.md) -
    // a future channel enumerator added without updating this function
    // fails to compile.
    switch (m_channel) {
    case FrameDebuggerPreviewChannel::All:
        view.channel = "all";
        break;
    case FrameDebuggerPreviewChannel::R:
        view.channel = "r";
        break;
    case FrameDebuggerPreviewChannel::G:
        view.channel = "g";
        break;
    case FrameDebuggerPreviewChannel::B:
        view.channel = "b";
        break;
    case FrameDebuggerPreviewChannel::A:
        view.channel = "a";
        break;
    }

    view.levelsBlack = m_levelsBlack;
    view.levelsWhite = m_levelsWhite;
    return view;
}

} // namespace gte
