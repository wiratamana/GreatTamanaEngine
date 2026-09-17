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
    const FrameDebuggerHistoryEntry* entry = m_currentCapture.CurrentEntry();

    // task_manager/frame-debugger-7 campaign, PHASE4
    // (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.4) - REWRITTEN. The
    // old `isViewingGameViewLeaf`/`selectedComputePassPreview` boolean-soup
    // (frame-debugger-4/frame-debugger-5 campaigns) is GONE - the currently-
    // selected event node's own `FrameDebuggerEventDetails::stepPreviewKind`/
    // `stepPreviewIndex` (FrameDebuggerData.h, computed once by
    // BuildRealFrameDebuggerSnapshot()) now drive
    // ChooseFrameDebuggerPreviewSource() directly. Nothing selected at all
    // (m_selectedEventIndex == -1, or no capture yet) defaults to
    // `PostComposite` - the same "final, fog-inclusive image" bucket an
    // explicit Post-GameView-at/after-composite selection also falls into
    // (see ChooseFrameDebuggerPreviewSource()'s own doc comment).
    FrameDebuggerStepPreviewKind stepPreviewKind = FrameDebuggerStepPreviewKind::PostComposite;
    int stepPreviewIndex = -1;
    if (entry != nullptr) {
        const std::optional<FrameDebuggerEventDetails> details =
            FindEventDetailsByIndex(entry->snapshot, m_selectedEventIndex);
        if (details.has_value()) {
            stepPreviewKind = details->stepPreviewKind;
            stepPreviewIndex = details->stepPreviewIndex;
        }
    }

    // Does the selected PerObjectStep leaf's own stepPreviewIndex actually
    // resolve to a real entry in entry->perObjectStepPreviews? Resolved
    // HERE (the caller), never by ChooseFrameDebuggerPreviewSource() itself
    // (which stays free of any FrameDebuggerHistoryEntry/RenderTexture
    // dependency, per that function's own pre-existing philosophy).
    const bool hasPerObjectStepPreviewAtIndex = entry != nullptr
        && stepPreviewKind == FrameDebuggerStepPreviewKind::PerObjectStep && stepPreviewIndex >= 0
        && static_cast<std::size_t>(stepPreviewIndex) < entry->perObjectStepPreviews.size();

    const FrameDebuggerPreviewSourceChoice choice = ChooseFrameDebuggerPreviewSource(entry != nullptr, stepPreviewKind,
        entry != nullptr && entry->preview.has_value(), entry != nullptr && entry->compositedPreview.has_value(),
        hasPerObjectStepPreviewAtIndex);
    m_lastPreviewChoice = choice; // Cached for BuildInspectorPane()'s own placeholder-text decision.

    const RenderTexture* selectedSource = nullptr;
    switch (choice) {
    case FrameDebuggerPreviewSourceChoice::Preview:
        selectedSource = (entry != nullptr && entry->preview.has_value()) ? &(*entry->preview) : nullptr;
        break;
    case FrameDebuggerPreviewSourceChoice::CompositedPreview:
        selectedSource = (entry != nullptr && entry->compositedPreview.has_value()) ? &(*entry->compositedPreview) : nullptr;
        break;
    case FrameDebuggerPreviewSourceChoice::PerObjectStepPreview:
        selectedSource = hasPerObjectStepPreviewAtIndex
            ? &entry->perObjectStepPreviews[static_cast<std::size_t>(stepPreviewIndex)]
            : nullptr;
        break;
    case FrameDebuggerPreviewSourceChoice::NotYetDrawn:
    case FrameDebuggerPreviewSourceChoice::None:
        break;
    }

    if (selectedSource == nullptr) {
        // Nothing to preview right now - see this function's own original
        // comment (no capture has ever happened yet, an honest "nothing
        // drawn yet" NotYetDrawn state, or - defensively - the relevant
        // retained texture is unpopulated for the current capture).
        ReleasePreviewDescriptor();
        return;
    }

    // PHASE6 (task_manager/frame-debugger-3/PHASE6_CHANNELS_AND_LEVELS_REAL_PREVIEW.md,
    // Step 3.2) - Channels/Levels at their neutral defaults is a valid,
    // cheap optimization: display the RAW retained capture texture
    // directly, skipping the compute dispatch entirely. Anything else
    // re-dispatches FrameDebuggerPreviewRenderer (only when actually dirty -
    // see below) and displays ITS OWN separate scratch texture instead -
    // the retained captured copy itself is never mutated in place
    // (Locked Design Decision #8). PHASE1 (frame-debugger-4) - every read
    // below is now against `*selectedSource` (the just-decided pointer,
    // above), not unconditionally against `entry->preview` anymore.
    const bool isNeutral =
        (m_channel == FrameDebuggerPreviewChannel::All) && (m_levelsBlack <= 0.0f) && (m_levelsWhite >= 1.0f);

    VkImageView desiredView = selectedSource->View();
    VkSampler desiredSampler = selectedSource->Sampler();

    if (!isNeutral && m_frameRenderer != nullptr) {
        const VkImageView sourceView = selectedSource->View();
        const bool dirty = (m_lastProcessedSourceView != sourceView) || (m_lastProcessedChannel != m_channel)
            || (m_lastProcessedLevelsBlack != m_levelsBlack) || (m_lastProcessedLevelsWhite != m_levelsWhite);
        if (dirty) {
            m_previewProcessor.RenderPreview(*m_frameRenderer, *selectedSource, m_channel, m_levelsBlack, m_levelsWhite);
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

// task_manager/frame-debugger-7 campaign, PHASE3
// (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step 3.0)
// - the EARLY half of the two-bool pending/serviced handshake - see this
// method's own doc comment (FrameDebuggerPanel.h) for the full contract.
bool FrameDebuggerPanel::ConsumePendingReplayRequest()
{
    if (!m_pendingCaptureTrigger) {
        return false;
    }
    m_pendingCaptureTrigger = false; // cleared HERE, exactly once - never cleared anywhere else.
    m_replayServicedThisFrame = true; // leaves a same-frame "receipt" for Build()'s own later check.
    return true;
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

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, m_captureContext, renderTargetInfo);

    // task_manager/frame-debugger-7 campaign, PHASE4
    // (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.2 point 2) - widened
    // CaptureFrame() call with the new, LAST `m_captureContext` argument, so
    // Phase 3's own ReplayStepPreviews() gets moved into permanent storage
    // (FrameDebuggerHistoryEntry::perObjectStepPreviews) before this same
    // frame's later Reset() would otherwise wipe it.
    m_currentCapture.CaptureFrame(
        *m_frameRenderer, *m_frameRenderGraph, snapshot, *m_frameGameView, m_frameGameViewComposited, m_captureContext);
    m_selectedEventIndex = -1;
}

// task_manager/frame-debugger-3 campaign, PHASE7
// (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - the exact
// same false->true-edge effect the "Enable" checkbox has always had
// (frame-debugger-2's Locked Design Decision #3 + PHASE3's Step 3.2 call
// site 1), now shared by BOTH BuildToolbarRow()'s own checkbox AND
// SetEnabledFromCommand() (the new HTTP-automation entry point) - see this
// method's own callers for why the false->true edge auto-engages Pause, and
// why turning Enable back OFF deliberately does NOT auto-resume (a user
// inspecting a paused frame should not be silently un-paused just for
// closing/disabling this debug window, whether that happened by hand or via
// GET /frame_debugger/enable).
//
// task_manager/frame-debugger-7 campaign, PHASE1
// (PHASE1_REMOVE_HISTORY_AND_SINGLE_CAPTURE_LIFECYCLE.md) - the true->false
// edge (Enable unticked) now ALSO clears the current capture
// (m_currentCapture.Clear()) - "the frame info got removed from memory" is
// the user's own exact wording for this new rule (PHASE0_MASTER_STRATEGY.md's
// Step 1) - only the captured DATA disappears; m_enabled itself is already
// set to `newEnabled` immediately above, exactly wherever the caller wanted
// it.
//
// task_manager/frame-debugger-7 campaign, PHASE2
// (PHASE2_DEFERRED_CAPTURE_TRIGGER.md) - the false->true edge NO LONGER
// captures synchronously. This used to be Bug 1's real root cause: THIS
// exact frame's own m_captureContext was armed (PrepareCaptureContextForThisFrame())
// based on m_enabled as of the END of LAST frame - still false at that
// point - and that arming call happens BEFORE Game::Render() runs, so this
// frame's RenderSystem::Draw() calls never recorded any per-object
// FrameDebuggerDrawRecords at all; a capture built moments later from that
// same frame's data was therefore real (render graph/pass list intact) but
// had zero per-entity draw records, so the "GameView" node's children
// (terrain/smoke cube/...) were silently missing. The fix: this edge no
// longer calls TriggerCapture() itself at all - it only sets
// m_pendingCaptureTrigger = true (see that member's own doc comment,
// FrameDebuggerPanel.h).
//
// task_manager/frame-debugger-7 campaign, PHASE3
// (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step 3.0)
// - RENAMED m_pendingCaptureAfterEnable to m_pendingCaptureTrigger, and the
// actual capture is now deferred TWO steps further than Phase 2's own
// description above: EARLY the very next frame, Application::Run() calls
// IEditorLayer::ConsumePendingFrameDebuggerReplayRequest() (before that
// frame's "GameView" pass is even declared), which consumes
// m_pendingCaptureTrigger and, if it was set, declares this phase's N new
// replay passes AND sets m_replayServicedThisFrame = true; LATE that SAME
// frame, the top of Build() consumes m_replayServicedThisFrame and only
// THEN calls TriggerCapture() - by which point this frame's Game::Render()
// (and the replay passes) have already executed with everything correctly
// armed. See ConsumePendingReplayRequest()'s own doc comment (this file,
// above) for the full two-bool handshake.
void FrameDebuggerPanel::ApplyEnabledEdge(EditorContext& ctx, bool newEnabled)
{
    const bool wasEnabled = m_enabled;
    m_enabled = newEnabled;
    if (m_enabled && !wasEnabled) {
        ctx.playbackPaused = true;

        // PHASE2 (frame-debugger-7 campaign) - defer the real capture to
        // the next Build() call instead of calling TriggerCapture()
        // synchronously here - see this method's own doc comment above.
        // PHASE3 - renamed to m_pendingCaptureTrigger; see that member's
        // own doc comment (FrameDebuggerPanel.h).
        m_pendingCaptureTrigger = true;
    } else if (!m_enabled && wasEnabled) {
        // NEW (frame-debugger-7 campaign, PHASE1) - "clear on Disable".
        m_currentCapture.Clear();
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
    //
    // task_manager/frame-debugger-7 campaign, PHASE3
    // (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
    // 3.0) - no longer calls TriggerCapture() synchronously (this frame's
    // "GameView" pass, and any replay passes, have ALREADY been declared/
    // executed by the time this button click is even processed - deferring
    // is what lets a future frame declare the replay passes BEFORE
    // Game::Render() runs). Only sets m_pendingCaptureTrigger = true - see
    // ConsumePendingReplayRequest()'s own doc comment for the rest of the
    // handshake.
    ImGui::BeginDisabled(!m_enabled);
    if (ImGui::Button("Capture")) {
        m_pendingCaptureTrigger = true;
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

    // PHASE3's Step-triggered capture (one of the three trigger sites) -
    // serviced HERE (rather than back where NotifyStepConsumed() itself
    // was called, Application::Run(), early in the frame, well before
    // Game::Render() even ran). Re-checks m_enabled here (its freshest
    // value THIS frame, including any edit the Enable checkbox above just
    // made) rather than trusting whatever it was back when
    // NotifyStepConsumed() was called.
    //
    // task_manager/frame-debugger-7 campaign, PHASE3
    // (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
    // 3.0) - no longer calls TriggerCapture() synchronously; only sets
    // m_pendingCaptureTrigger = true (see this file's own
    // ConsumePendingReplayRequest()/ApplyEnabledEdge() for the same
    // deferral applied to the other two trigger sites, and why).
    if (m_stepCaptureRequested) {
        m_stepCaptureRequested = false;
        if (m_enabled) {
            m_pendingCaptureTrigger = true;
        }
    }
}

void FrameDebuggerPanel::BuildFrameStepperRow(const FrameDebuggerSnapshot& snapshot)
{
    // PHASE4 - now shows REAL numbers (this is the EXISTING "which event,
    // within the currently-captured frame, is selected" axis). The slider
    // itself stays a purely cosmetic, disabled control (matching the
    // reference screenshot's own scrubber look) - clicking a tree row
    // (RenderEventNode() below) is still the only way to change
    // m_selectedEventIndex this campaign.
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
    // frame-debugger-6 campaign, PHASE4 - branches on "does this node have
    // children" FIRST, regardless of `isDrawCall`, so a selectable leaf that
    // ALSO has real children (the new "GameView" shape, once it has
    // per-entity draw-record children - see BuildRealFrameDebuggerSnapshot())
    // renders as an expandable AND selectable row, while every pre-existing
    // leaf/group shape (no children at all, or a pure group with
    // isDrawCall == false) keeps rendering exactly as before this phase.
    if (!node.children.empty()) {
        // Handles BOTH a pure group (isDrawCall == false, e.g.
        // "Compute Dispatches (Pre-GameView)") AND a selectable leaf that ALSO
        // has real children (new this campaign - "GameView" itself, once it
        // has per-entity draw-record children). ImGuiTreeNodeFlags_Selected
        // highlights the row exactly like the leaf-only Selectable() branch
        // below already does; IsItemClicked() right after TreeNodeEx() is
        // what makes clicking the row (not just its arrow) select it,
        // mirroring Selectable()'s own click-to-select behavior.
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (node.isDrawCall && node.eventIndex == m_selectedEventIndex) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        const bool open = ImGui::TreeNodeEx(node.name.c_str(), flags);
        if (node.isDrawCall && ImGui::IsItemClicked()) {
            m_selectedEventIndex = node.eventIndex;
        }
        if (open) {
            for (const FrameDebuggerEventNode& child : node.children) {
                RenderEventNode(child);
            }
            ImGui::TreePop();
        }
        return;
    }

    if (!node.isDrawCall) {
        // Defensive only - every real group this engine builds today only
        // gets added when it already has >= 1 child (see
        // BuildRealFrameDebuggerSnapshot()'s own "only add if non-empty"
        // rule) - this branch should be unreachable in practice.
        return;
    }

    // A leaf draw-call row with NO children - unchanged from every prior
    // campaign. Highlighted when it matches m_selectedEventIndex; clicking
    // it selects it (feeding PHASE6's BuildEventDetailsSection() via
    // FindEventDetailsByIndex()).
    const bool isSelected = (node.eventIndex == m_selectedEventIndex);
    if (ImGui::Selectable(node.name.c_str(), isSelected)) {
        m_selectedEventIndex = node.eventIndex;
    }
}

void FrameDebuggerPanel::BuildEventTreePane(const FrameDebuggerSnapshot& snapshot)
{
    if (snapshot.rootNodes.empty()) {
        // Real, reachable state whenever nothing has ever been captured yet
        // (e.g. the window was just opened and "Enable"/"Capture" hasn't run
        // this session) - see PHASE0_MASTER_STRATEGY.md's Locked Design
        // Decision #2: zero fake/mock rows, ever. task_manager/
        // frame-debugger-7 campaign, PHASE2 (PHASE2_DEFERRED_CAPTURE_TRIGGER.md,
        // Step 3.6) - this is also the exact fallback shown during the one
        // real frame between the Enable checkbox's false->true edge and its
        // deferred capture landing (m_pendingCaptureAfterEnable == true,
        // m_currentCapture.CurrentEntry() still nullptr) - an honest "not
        // captured YET" state, never a wrong/incomplete tree.
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
    // whenever the current capture actually has one. Nothing selected
    // (m_selectedEventIndex == -1) falls through to showing the texture
    // too, matching the RenderTarget row's own "frame-level, not
    // event-level" framing immediately above.
    //
    // task_manager/frame-debugger-7 campaign, PHASE4
    // (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.4) - the OLD dead
    // "hide the whole-frame preview specifically for a GPU-skinning leaf"
    // special case (`selectedEventIsGpuSkinning`, frame-debugger-5
    // campaign) is REMOVED - it is now fully superseded by
    // `m_lastPreviewChoice` (set by EnsurePreviewDescriptor(), called
    // earlier this SAME Build() call): `m_previewDescriptor` is already
    // VK_NULL_HANDLE for EVERY step whose image genuinely isn't available
    // (including every Pre-GameView compute leaf, not just one hardcoded
    // name), so `showPreviewTexture` needs no extra special-casing at all.
    const bool showPreviewTexture = (m_previewDescriptor != VK_NULL_HANDLE) && (currentEntry != nullptr);

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
            // task_manager/frame-debugger-7 campaign, PHASE4
            // (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.4) - a second,
            // distinct placeholder message for the honest "nothing drawn to
            // the screen yet" NotYetDrawn state (a Pre-GameView compute
            // leaf), right next to the ordinary "No Texture" one (Locked
            // Design Decision #6 - never fabricate an image for this bucket).
            const char* placeholderText = (m_lastPreviewChoice == FrameDebuggerPreviewSourceChoice::NotYetDrawn)
                ? "Nothing drawn yet at this point in the frame."
                : "No Texture";
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
    RenderTexture& gameView, RenderTexture* compositedGameView)
{
    // task_manager/frame-debugger-7 campaign, PHASE3
    // (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
    // 3.0) - the LATE half of the two-bool pending/serviced handshake,
    // checked at the very TOP of Build(), before anything else runs (see
    // ConsumePendingReplayRequest()'s own doc comment for the EARLY half).
    // By the time this runs, THIS frame's Game::Render() (and, if a replay
    // was actually declared this frame, AddFrameDebuggerReplayPasses()'s
    // own N replay passes) have ALREADY executed - Build() is only ever
    // called from ImGuiEditorLayer::BuildUI(), which always runs after
    // RenderGraph::Execute() for the offscreen regime has returned - so it
    // is now safe and correct to call TriggerCapture() right here.
    if (m_replayServicedThisFrame) {
        m_replayServicedThisFrame = false;
        TriggerCapture();
    }

    // task_manager/frame-debugger-7 campaign, PHASE1
    // (PHASE1_REMOVE_HISTORY_AND_SINGLE_CAPTURE_LIFECYCLE.md) - the new
    // "clear on Resume-while-Enabled" rule (PHASE0_MASTER_STRATEGY.md's Step
    // 1), checked at the very TOP of Build(), before anything else runs:
    // detects a paused->running transition (most likely via the "Resume"
    // button in PlaybackControls.cpp, which this class has no direct hook
    // into - it only shares EditorContext) and, if the Frame Debugger is
    // still Enabled, clears the current capture. m_enabled itself is NOT
    // forced back to false here - only the captured DATA disappears
    // (matches the user's own exact wording: "the frame info got removed
    // from memory"). m_wasPlaybackPaused is refreshed unconditionally right
    // after, every single Build() call, so the NEXT call can detect the
    // next such transition.
    if (m_enabled && m_wasPlaybackPaused && !ctx.playbackPaused) {
        m_currentCapture.Clear();
    }
    m_wasPlaybackPaused = ctx.playbackPaused;

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
    m_frameGameViewComposited = compositedGameView; // PHASE1 (frame-debugger-4) - nullable, see header comment.

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
    // to the current capture's own data, falling back to that exact same
    // placeholder's own empty-tree behavior whenever no real capture has
    // ever happened yet this session (m_currentCapture.CurrentEntry() ==
    // nullptr) - a real, honest, reachable "enabled, but not yet captured"
    // state (see BuildEventTreePane()'s own updated comment above).
    const FrameDebuggerHistoryEntry* currentEntry = m_currentCapture.CurrentEntry();
    const FrameDebuggerSnapshot snapshot =
        (currentEntry != nullptr) ? currentEntry->snapshot : BuildPlaceholderFrameDebuggerSnapshot();

    // PHASE4 - clears any stale event selection whenever the CAPTURED entry
    // itself changes underneath us. eventIndex values are only meaningful
    // relative to the specific snapshot they were assigned in - a brand-new
    // capture already resets m_selectedEventIndex directly, in
    // TriggerCapture(), and Clear() (Disable/Resume-while-Enabled) leaves
    // currentEntry == nullptr, which this same check below also handles.
    // Compared via the entry's own RAW retained preview VkImageView
    // (guaranteed fresh on every single real capture - see
    // FrameDebuggerCurrentCapture::CaptureFrame()'s own doc comment) tracked
    // in m_lastKnownRawPreviewView - PHASE6 deliberately does NOT reuse
    // m_lastKnownPreviewView for this check, since that field can instead
    // hold m_previewProcessor's own processed scratch VkImageView (a
    // non-neutral Channels/Levels state) - see m_lastKnownRawPreviewView's
    // own doc comment (FrameDebuggerPanel.h) for why reusing
    // m_lastKnownPreviewView here would have incorrectly reset the
    // selection on every single frame in that case.
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

    // PHASE4 - keeps m_previewDescriptor in sync with the current capture
    // (see EnsurePreviewDescriptor()'s own doc comment). Called
    // unconditionally here (cheap - a no-op unless the underlying
    // VkImageView actually changed).
    EnsurePreviewDescriptor();

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
    // task_manager/frame-debugger-7 campaign, PHASE3
    // (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
    // 3.0) - mirrors the hand-driven "Capture" button's own new deferred
    // behavior (BuildToolbarRow()) exactly: no longer calls TriggerCapture()
    // synchronously (this frame's "GameView" pass/replay passes have
    // ALREADY been declared/executed by the time an HTTP request is even
    // processed) - only sets m_pendingCaptureTrigger = true, deferring the
    // real capture (and this frame's worth of replay passes) to the next
    // properly-armed frame via ConsumePendingReplayRequest()/the
    // m_replayServicedThisFrame check at the top of Build(). This was a
    // genuine gap found and fixed during this phase's own Step 4 manual
    // spot-check (confirmed via a second HTTP-driven capture producing an
    // EMPTY ReplayStepPreviews() before this fix, since no replay passes
    // had been declared that frame).
    m_pendingCaptureTrigger = true;
    return true;
}

void FrameDebuggerPanel::SelectEventFromCommand(int index)
{
    const FrameDebuggerHistoryEntry* currentEntry = m_currentCapture.CurrentEntry();
    const int totalEventCount = (currentEntry != nullptr) ? currentEntry->snapshot.totalEventCount : 0;
    m_selectedEventIndex = ClampSelectedEventIndex(index, totalEventCount);
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
    view.hasCapturedFrame = m_currentCapture.HasCapture();

    const FrameDebuggerHistoryEntry* currentEntry = m_currentCapture.CurrentEntry();
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
