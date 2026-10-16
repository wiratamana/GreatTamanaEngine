#include "FrameDebuggerPanel.h"

#include "../EditorContext.h"
#include "../MemoryPanelData.h"
#include "../../Encoding/HdrColorVisualization.h"
#include "../../Encoding/CapturedPixelConversion.h"
#include "../../Encoding/DepthVisualization.h"
#include "../../Encoding/PixelConversion.h"
#include "../../Encoding/PngEncoder.h"
#include "../../Encoding/SingleChannelVisualization.h"
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "../../Renderer/RenderGraph/RenderPipeline.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderTexture.h"
#include "../../Core/Logging.h"
#include "../RenderPassHonestyChecker.h"
#include "../RenderPassHonestyGuard.h"
#include "../FrameDebuggerCoverageChecker.h"
#include "../FrameDebuggerCoverageGuard.h"
#include "../FrameDebuggerSideChannelChecker.h"
#include "../FrameDebuggerSideChannelGuard.h"
#include "../VolumeTexturePreviewSelection.h"

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

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
    // In-flight command buffers may still reference the preview descriptor and the preview processor's GPU
    // resources: wait for the GPU whenever a device is known.
    if (m_frameRenderer != nullptr) {
        m_frameRenderer->WaitForGpuIdle();
    }
    ReleasePreviewDescriptor();
    // Defense in depth: ImGuiEditorLayer releases this before ImGui_ImplVulkan_Shutdown(), so this is normally a no-op.
    ReleaseShaderPropertyTexturePreview();
}

void FrameDebuggerPanel::ReleasePreviewDescriptor()
{
    if (m_previewDescriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_previewDescriptor);
        m_previewDescriptor = VK_NULL_HANDLE;
        m_lastKnownPreviewView = VK_NULL_HANDLE;
    }
}

// Releases the shader-property preview texture and its descriptor (see FrameDebuggerPanel.h).
void FrameDebuggerPanel::ReleaseShaderPropertyTexturePreview()
{
    if ((m_shaderPropertyPreviewTexture.has_value() || m_shaderPropertyPreviewDescriptor != VK_NULL_HANDLE)
        && m_frameRenderer != nullptr) {
        m_frameRenderer->WaitForGpuIdle(); // Mirrors AssetPreviewTexture::Reset()'s own identical, already-accepted tradeoff.
    }
    if (m_shaderPropertyPreviewDescriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_shaderPropertyPreviewDescriptor);
        m_shaderPropertyPreviewDescriptor = VK_NULL_HANDLE;
    }
    m_shaderPropertyPreviewTexture.reset();
    m_shaderPropertyPreviewName.clear();
    m_shaderPropertyPreviewLookupFailed = false;
    m_shaderPropertyPreviewExtent = VkExtent2D{};
}

// Click handler of the "View" button: releases the shown preview, then does one fresh capture and upload.
void FrameDebuggerPanel::RequestShaderPropertyTexturePreview(const std::string& textureName, rg::ResourceKind kind)
{
    ReleaseShaderPropertyTexturePreview(); // Always start fresh.

    if (m_frameRenderer == nullptr || m_frameRenderGraph == nullptr) {
        return; // Defensive only - unreachable in practice, see this method's own header doc comment.
    }

    m_shaderPropertyPreviewKind = kind;

    // The texture upload can throw on GPU resource exhaustion; an exception must not escape an ImGui click handler.
    auto uploadOrFail = [this, &textureName](const void* pixelsRgba8, int width, int height, const char* debugName) -> bool {
        try {
            m_shaderPropertyPreviewTexture.emplace(m_frameRenderer->CreateTexture2D(pixelsRgba8, width, height, debugName));
            return true;
        } catch (const std::exception&) {
            m_shaderPropertyPreviewTexture.reset();
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return false;
        }
    };

    if (kind == rg::ResourceKind::Texture) {
        const std::optional<rg::DebugTextureSnapshot> snapshot = m_frameRenderGraph->DebugTextureSnapshotFor(textureName);
        if (!snapshot.has_value()) {
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return;
        }

        m_frameRenderer->WaitForGpuIdle(); // Rare, explicit, human-driven - same accepted cost GET /get_texture already pays.

        // NON-const, deliberately: the BGRA/single-channel branches below
        // mutate/read raw.pixels, and CaptureImagePixels() already hands us
        // a buffer we own exclusively.
        Renderer::CapturedRawPixels raw = m_frameRenderer->CaptureImagePixels(
            snapshot->target.image, VK_IMAGE_ASPECT_COLOR_BIT, snapshot->target.format,
            FromVkExtent2D(snapshot->target.extent), snapshot->colorState);

        if (raw.pixels.empty()) {
            // BytesPerTexelForFormat() didn't recognize this format.
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return;
        }

        std::vector<std::uint8_t> converted;
        const std::uint8_t* rgba8Pixels = raw.pixels.data();
        bool ok = true;
        if (raw.format == PixelFormat::RGBA16_Float) {
            converted.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
            ok = Encoding::ConvertHdrRgba16fToRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, converted.data());
            rgba8Pixels = converted.data();
        } else if (raw.format == PixelFormat::R8_UNorm) {
            converted.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
            ok = Encoding::Convert1ChannelToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, converted.data());
            rgba8Pixels = converted.data();
        } else if (Encoding::IsBgraFormat(raw.format)) {
            // Safe in-place swizzle - CaptureImagePixels() already handed us a
            // buffer we own exclusively, and `raw` is non-const (see above), so
            // no const_cast is needed at all.
            Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
        }
        // else: already tightly-packed RGBA8 - no conversion needed.

        if (!ok) {
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return;
        }

        if (!uploadOrFail(rgba8Pixels, raw.width, raw.height, "FrameDebuggerShaderPropertyPreview")) {
            return; // uploadOrFail() already set the lookup-failed state.
        }
        m_shaderPropertyPreviewExtent = snapshot->target.extent;
    } else if (kind == rg::ResourceKind::VolumeTexture) {
        const std::optional<rg::DebugVolumeTextureSnapshot> snapshot =
            m_frameRenderGraph->DebugVolumeTextureSnapshotFor(textureName);
        if (!snapshot.has_value()) {
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return;
        }

        m_frameRenderer->WaitForGpuIdle();

        const VolumeTexturePreviewInterpretation interpretation = SelectVolumeTexturePreviewInterpretation(textureName);
        const VolumeTexturePreviewRenderer::CapturedRawPixels raw =
            m_shaderPropertyVolumeRenderer.RenderPreview(*m_frameRenderer, snapshot->target, snapshot->state, interpretation);

        if (!uploadOrFail(raw.pixels.data(), raw.width, raw.height, "FrameDebuggerShaderPropertyVolumePreview")) {
            return;
        }
        m_shaderPropertyPreviewExtent = VkExtent2D{ static_cast<std::uint32_t>(raw.width), static_cast<std::uint32_t>(raw.height) };
    } else if (kind == rg::ResourceKind::TextureArray) {
        // No texture-array preview exists (no per-layer image view): show the "not previewable" placeholder.
        return;
    } else {
        // Buffer: no "View" button is drawn for buffer rows, so this is unreachable in practice.
        return;
    }

    m_shaderPropertyPreviewDescriptor = ImGui_ImplVulkan_AddTexture(
        m_shaderPropertyPreviewTexture->Sampler(), m_shaderPropertyPreviewTexture->View(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // A freshly uploaded Texture2D is always left in this layout.
    m_shaderPropertyPreviewName = textureName;
    m_shaderPropertyPreviewLookupFailed = false;
}

void FrameDebuggerPanel::EnsurePreviewDescriptor()
{
    const FrameDebuggerHistoryEntry* entry = m_currentCapture.CurrentEntry();

    const std::optional<FrameDebuggerEventDetails> selectedDetails =
        (entry != nullptr) ? FindEventDetailsByIndex(entry->snapshot, m_selectedEventIndex) : std::nullopt;

    // A leaf that touches a private render-graph resource is previewed straight from that texture: no stall, no
    // CPU readback. On a miss, fall through to the retained-capture path below.
    std::optional<rg::DebugTextureSnapshot> privatePreview;
    if (selectedDetails.has_value() && selectedDetails->privateWriteTextureName.has_value()
        && m_frameRenderGraph != nullptr) {
        privatePreview = m_frameRenderGraph->DebugTextureSnapshotFor(*selectedDetails->privateWriteTextureName);
    }
    if (privatePreview.has_value() && privatePreview->target.imageView != VK_NULL_HANDLE) {
        const VkImageView desiredView = privatePreview->target.imageView;
        const VkSampler desiredSampler = privatePreview->sampler;
        if (m_previewDescriptor != VK_NULL_HANDLE && desiredView == m_lastKnownPreviewView) {
            return; // Already wrapping the right VkImageView - nothing to do.
        }
        ReleasePreviewDescriptor();
        m_previewDescriptor =
            ImGui_ImplVulkan_AddTexture(desiredSampler, desiredView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_lastKnownPreviewView = desiredView;
        return;
    }

    // The selected event's stepPreviewKind/stepPreviewIndex drive the choice. With nothing selected the final
    // composited image (PostComposite) is shown.
    FrameDebuggerStepPreviewKind stepPreviewKind = FrameDebuggerStepPreviewKind::PostComposite;
    int stepPreviewIndex = -1;
    if (selectedDetails.has_value()) {
        stepPreviewKind = selectedDetails->stepPreviewKind;
        stepPreviewIndex = selectedDetails->stepPreviewIndex;
    }

    // Resolved here (not in ChooseFrameDebuggerPreviewSource): does the step's preview index name a retained texture?
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
        // Nothing to preview: no capture yet, nothing drawn yet, or the retained texture is missing.
        ReleasePreviewDescriptor();
        return;
    }

    // Neutral channel/levels: show the retained capture texture directly and skip the compute dispatch.
    // Anything else re-dispatches the preview renderer (only when dirty) into its own scratch texture;
    // the retained copy is never mutated.
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
        if (const Texture2D* output = m_previewProcessor.OutputTexture()) {
            desiredView = output->View();
            desiredSampler = output->Sampler();
        }
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

// Early half of the pending/serviced replay handshake (see FrameDebuggerPanel.h).
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
        // Unreachable in practice: Build() sets these before the toolbar row runs.
        return;
    }

    const rg::RenderGraphSnapshot graphSnapshot =
        m_frameRenderGraph->LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback);

    // Render pass honesty detector, once per capture: a pass that is non-culled yet reported disabled is a
    // contradiction between the graph and the toggle registry. A no-op without a registry.
    if (m_frameToggleRegistry != nullptr) {
        const std::vector<std::string> mismatches = DetectRenderPassHonestyMismatches(
            graphSnapshot.passesInExecutionOrder,
            [this](const std::string& name) { return m_frameToggleRegistry->IsEnabled(name); });
        RenderPassHonestyGuard::Instance().ReportCaptureMismatches(mismatches);
    }

    // "Disabled side effect still visible" detector over a curated list of known-risk blackboard keys.
    // A no-op without a toggle registry or blackboard.
    if (m_frameToggleRegistry != nullptr && m_frameBlackboard != nullptr) {
        const std::vector<std::string> leaks = DetectDisabledPassBlackboardKeyLeaks(
            KnownRiskBlackboardKeyRules(),
            [this](const std::string& name) { return m_frameToggleRegistry->IsEnabled(name); },
            [this](rg::RenderPassId key) { return m_frameBlackboard->WasPublishedThisFrame(key); });
        FrameDebuggerSideChannelGuard::Instance().ReportCaptureMismatches(leaks);
    }

    FrameDebuggerRenderTargetInfo renderTargetInfo;
    const Extent2D extent = m_frameGameView->Extent();
    renderTargetInfo.width = static_cast<int>(extent.width);
    renderTargetInfo.height = static_cast<int>(extent.height);
    renderTargetInfo.format = ToString(m_frameGameView->Format());

    const FrameDebuggerSnapshot snapshot = BuildRealFrameDebuggerSnapshot(graphSnapshot, m_captureContext, renderTargetInfo);

    // "Ran but not shown" detector, once per capture: a non-culled pass that executed but has no leaf in the
    // snapshot tree.
    const std::vector<std::string> missingFromTree =
        DetectPassesMissingFromFrameDebuggerTree(graphSnapshot.passesInExecutionOrder, snapshot);
    FrameDebuggerCoverageGuard::Instance().ReportCaptureMismatches(missingFromTree);

    // Pass m_captureContext so the per-object step previews move into the history entry before Reset() wipes them.
    m_currentCapture.CaptureFrame(
        *m_frameRenderer, *m_frameRenderGraph, snapshot, *m_frameGameView, m_frameGameViewComposited, m_captureContext);
    SetSelectedEventIndex(-1);
}

// The single place that changes m_selectedEventIndex. `newIndex` is already clamped by the caller.
void FrameDebuggerPanel::SetSelectedEventIndex(int newIndex)
{
    if (newIndex == m_selectedEventIndex) {
        return; // No real change - nothing to release/refresh.
    }
    m_selectedEventIndex = newIndex;
    // The user went elsewhere: drop any shader-property preview and fall back to the event's step preview.
    ReleaseShaderPropertyTexturePreview();
}

// Shared by the "Enable" checkbox and SetEnabledFromCommand(). Enabling pauses playback and requests a deferred
// capture (armed next frame, so that frame's draw records are recorded); disabling clears the capture but does
// not resume playback. The capture runs two steps later: ConsumePendingReplayRequest(), then Build().
void FrameDebuggerPanel::ApplyEnabledEdge(EditorContext& ctx, bool newEnabled)
{
    const bool wasEnabled = m_enabled;
    m_enabled = newEnabled;
    if (m_enabled && !wasEnabled) {
        ctx.playbackPaused = true;

        // Capture is deferred to the next Build() so this frame's draw records are armed first.
        m_pendingCaptureTrigger = true;
    } else if (!m_enabled && wasEnabled) {
        // Disabling clears the capture; a shader-property preview of it must not linger.
        m_currentCapture.Clear();
        ReleaseShaderPropertyTexturePreview();
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

    // "Capture" re-captures on demand while paused; it only sets the pending flag (the capture is deferred).
    ImGui::BeginDisabled(!m_enabled);
    if (ImGui::Button("Capture")) {
        m_pendingCaptureTrigger = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Cosmetic stub: there is no Play/Edit split, so this combo is permanently disabled.
    static constexpr const char* kModeItems[] = { "Editor" };
    int modeIndex = 0;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::BeginDisabled();
    ImGui::Combo("##FrameDebuggerMode", &modeIndex, kModeItems, 1);
    ImGui::EndDisabled();

    // A requested step capture is serviced here, after Game::Render(), using the freshest m_enabled. It only sets
    // the pending flag; the capture itself is deferred like the other two trigger sites.
    if (m_stepCaptureRequested) {
        m_stepCaptureRequested = false;
        if (m_enabled) {
            m_pendingCaptureTrigger = true;
        }
    }
}

void FrameDebuggerPanel::BuildFrameStepperRow(const FrameDebuggerSnapshot& snapshot)
{
    // SliderInt returns true on every change while dragging, so the preview updates live.
    int stepperValue = m_selectedEventIndex < 0 ? 0 : m_selectedEventIndex;
    const bool hasAnyEvents = snapshot.totalEventCount > 0;

    ImGui::BeginDisabled(!hasAnyEvents);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderInt(
            "##FrameDebuggerStepper", &stepperValue, 0, std::max(0, snapshot.totalEventCount - 1), "")) {
        SetSelectedEventIndex(ClampSelectedEventIndex(stepperValue, snapshot.totalEventCount));
    }

    // Left/Right nudges one event while the slider has focus; IsItemFocused() must follow the SliderInt directly.
    if (hasAnyEvents && ImGui::IsItemFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, /*repeat=*/true)) {
            SetSelectedEventIndex(ClampSelectedEventIndex(stepperValue - 1, snapshot.totalEventCount));
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, /*repeat=*/true)) {
            SetSelectedEventIndex(ClampSelectedEventIndex(stepperValue + 1, snapshot.totalEventCount));
        }
    }

    ImGui::SameLine();
    const std::string label = FormatFrameStepperLabel(m_selectedEventIndex, snapshot.totalEventCount);
    ImGui::TextUnformatted(label.c_str());
    ImGui::EndDisabled();
}

void FrameDebuggerPanel::RenderEventNode(const FrameDebuggerEventNode& node)
{
    // A node with children renders as an expandable row (and selectable when it is a draw call), whatever isDrawCall says.
    if (!node.children.empty()) {
        // A pure group or a selectable leaf with children. IsItemClicked() makes a click on the row (not just
        // its arrow) select it.
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (node.isDrawCall && node.eventIndex == m_selectedEventIndex) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        const bool open = ImGui::TreeNodeEx(node.name.c_str(), flags);
        if (node.isDrawCall && ImGui::IsItemClicked()) {
            SetSelectedEventIndex(node.eventIndex);
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
        // Unreachable in practice: groups are only added when they have a child.
        return;
    }

    // A leaf draw-call row without children: highlighted when selected, click selects it.
    const bool isSelected = (node.eventIndex == m_selectedEventIndex);
    if (ImGui::Selectable(node.name.c_str(), isSelected)) {
        SetSelectedEventIndex(node.eventIndex);
    }
}

void FrameDebuggerPanel::BuildEventTreePane(const FrameDebuggerSnapshot& snapshot)
{
    if (snapshot.rootNodes.empty()) {
        // Nothing captured yet, or the deferred capture has not landed: show an honest empty state, never a fake tree.
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
    // Each button sets m_channel; the active one uses the theme's ButtonActive color.
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
    // Two-handle range writing m_levelsBlack/m_levelsWhite. AlwaysClamp keeps both in [0, 1]; the clamp below
    // keeps white above black.
    ImGui::DragFloatRange2("##FrameDebuggerLevels", &m_levelsBlack, &m_levelsWhite, 0.005f, 0.0f, 1.0f, "Black %.2f",
        "White %.2f", ImGuiSliderFlags_AlwaysClamp);
    m_levelsWhite = std::max(m_levelsWhite, m_levelsBlack + 0.001f);

    // --- Texture preview box ---
    const std::string resolutionCaption = std::to_string(snapshot.renderTarget.width) + "x"
        + std::to_string(snapshot.renderTarget.height) + "  " + snapshot.renderTarget.format;
    ImGui::TextDisabled("%s", resolutionCaption.c_str());

    const std::optional<FrameDebuggerEventDetails> details
        = FindEventDetailsByIndex(snapshot, m_selectedEventIndex);

    // The retained preview belongs to the captured frame, not to one event, so it shows whenever the capture has
    // one. m_previewDescriptor is already null for every step without an image (set by EnsurePreviewDescriptor()).
    const bool showPreviewTexture = (m_previewDescriptor != VK_NULL_HANDLE) && (currentEntry != nullptr);

    const float previewHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.5f);
    // Opaque black behind the whole preview box (letterbox look), whether it shows an image or placeholder text.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 255));
    ImGui::BeginChild("FrameDebuggerTexturePreview", ImVec2(0.0f, previewHeight), true);
    {
        // A shader-property preview replaces the whole box; the two previews are never shown together.
        if (!m_shaderPropertyPreviewName.empty()) {
            ImGui::TextDisabled("Viewing: %s", m_shaderPropertyPreviewName.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Back to Step Preview")) {
                ReleaseShaderPropertyTexturePreview();
            }
            if (m_shaderPropertyPreviewDescriptor != VK_NULL_HANDLE) {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                if (avail.x >= 1.0f && avail.y >= 1.0f) {
                    const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(avail.x, avail.y,
                        static_cast<float>(m_shaderPropertyPreviewExtent.width),
                        static_cast<float>(m_shaderPropertyPreviewExtent.height));
                    const ImVec2 cursorBase = ImGui::GetCursorPos();
                    ImGui::SetCursorPos(ImVec2(cursorBase.x + fit.offsetX, cursorBase.y + fit.offsetY));
                    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_shaderPropertyPreviewDescriptor)),
                        ImVec2(fit.width, fit.height));
                }
            } else {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                // Distinct placeholder for a failed lookup or upload, separate from the plain "No Texture" case.
                const char* placeholderText = m_shaderPropertyPreviewLookupFailed
                    ? "Texture not currently available for preview."
                    : "No Texture";
                const ImVec2 textSize = ImGui::CalcTextSize(placeholderText);
                ImGui::SetCursorPos(ImVec2(
                    std::max(0.0f, (avail.x - textSize.x) * 0.5f), std::max(0.0f, (avail.y - textSize.y) * 0.5f)));
                ImGui::TextDisabled("%s", placeholderText);
            }
        } else if (showPreviewTexture) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x >= 1.0f && avail.y >= 1.0f) {
                // Fit the texture to its own aspect ratio, centered; black bars fill the slack axis.
                const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(avail.x, avail.y,
                    static_cast<float>(snapshot.renderTarget.width),
                    static_cast<float>(snapshot.renderTarget.height));
                const ImVec2 cursorBase = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(cursorBase.x + fit.offsetX, cursorBase.y + fit.offsetY));
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_previewDescriptor)),
                    ImVec2(fit.width, fit.height));
            }
        } else {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            // Distinct placeholder for the NotYetDrawn state (a pre-view compute leaf); no image is fabricated.
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
    ImGui::PopStyleColor();

    ImGui::Separator();

    BuildEventDetailsSection(details);
}

void FrameDebuggerPanel::BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>& details)
{
    if (!details.has_value()) {
        // Nothing selected (a fresh capture resets the selection to -1).
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
            // A per-draw-call preview render does not exist yet.
            ImGui::TextDisabled("Not available yet.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ShaderProperties")) {
            if (!d.textures.empty()) {
                ImGui::SeparatorText("Textures");
                // Button IDs use the row index: two rows can share a valueLabel (a pass reading and writing one
                // name) and ImGui IDs must be unique.
                for (std::size_t i = 0; i < d.textures.size(); ++i) {
                    const FrameDebuggerTextureProperty& texture = d.textures[i];
                    BuildPropertyRow(texture.name.c_str(), texture.valueLabel);
                    // Only a render-graph 2D or volume texture gets a "View" button: a buffer, material
                    // texture or texture array has no preview, and a button that does nothing would be dishonest.
                    if (texture.isRenderGraphResource && texture.kind != rg::ResourceKind::Buffer
                        && texture.kind != rg::ResourceKind::TextureArray) {
                        ImGui::SameLine();
                        const bool isCurrentlyViewing = (m_shaderPropertyPreviewName == texture.valueLabel);
                        if (isCurrentlyViewing) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        }
                        const std::string buttonId = "View##Texture" + std::to_string(i);
                        if (ImGui::SmallButton(buttonId.c_str())) {
                            RequestShaderPropertyTexturePreview(texture.valueLabel, texture.kind);
                        }
                        if (isCurrentlyViewing) {
                            ImGui::PopStyleColor();
                        }
                    }
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
                    // One ImGui::Text() per row (see FormatMatrixProperty()).
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
    RenderTexture& gameView, RenderTexture* compositedGameView, rg::RenderPassToggleRegistry* toggleRegistry,
    const rg::RenderPassBlackboard* offscreenBlackboard)
{
    // Late half of the replay handshake: Build() runs after the offscreen graph has executed, so this frame's
    // draw records and replay passes exist and the capture can run now.
    if (m_replayServicedThisFrame) {
        m_replayServicedThisFrame = false;
        TriggerCapture();
    }

    // Resuming playback while enabled clears the capture (m_enabled stays set). The Resume button lives elsewhere,
    // so detect the paused->running edge here and refresh m_wasPlaybackPaused on every call.
    if (m_enabled && m_wasPlaybackPaused && !ctx.playbackPaused) {
        m_currentCapture.Clear();
        // A displayed shader-property preview is cleared with the capture.
        ReleaseShaderPropertyTexturePreview();
    }
    m_wasPlaybackPaused = ctx.playbackPaused;

    // A toggle change in the Render Graph panel (earlier this frame) arms a re-capture while enabled, so a disabled
    // pass does not stay visible. Only arms the deferred trigger; the set_pass_enabled HTTP route captures directly.
    if (ctx.renderPassToggleRegistryChangedThisFrame) {
        ctx.renderPassToggleRegistryChangedThisFrame = false;
        if (m_enabled) {
            m_pendingCaptureTrigger = true;
            GTE_LOG_DEBUG("FrameDebugger",
                "Build() - RenderPassToggleRegistry changed this frame while Enabled - arming an automatic "
                "re-capture for the next frame.");
        }
    }

    // Cached for TriggerCapture() and the toolbar row; set even when the window is closed, in this one place.
    m_frameRenderer = &renderer;
    m_frameRenderGraph = &renderGraph;
    m_frameGameView = &gameView;
    m_frameGameViewComposited = compositedGameView; // Nullable.
    m_frameToggleRegistry = toggleRegistry; // Nullable.
    m_frameBlackboard = offscreenBlackboard; // Nullable.

    // Cheap and idempotent: must run every call, before the early return below.
    m_captureContext.InitializeEventCapture(renderer);

    if (!ctx.frameDebuggerWindowOpen) {
        return;
    }

    // p_open keeps the title-bar close button in sync with the "Window > Frame Debugger" menu bool.
    // A programmatic open pins the window once onto the main ImGui viewport, so swapchain captures can see it;
    // later it can be dragged away freely.
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
        // Collapsed: End() is still required after Begin() returns false.
        ImGui::End();
        return;
    }

    BuildToolbarRow(ctx);
    ImGui::Separator();

    // Show the current capture, or an empty tree when nothing has been captured yet.
    const FrameDebuggerHistoryEntry* currentEntry = m_currentCapture.CurrentEntry();
    const FrameDebuggerSnapshot snapshot =
        (currentEntry != nullptr) ? currentEntry->snapshot : BuildPlaceholderFrameDebuggerSnapshot();

    // Event indices are only valid within their snapshot, so a changed capture clears the selection. Detected via
    // the retained raw preview view (fresh on every capture); m_lastKnownPreviewView may hold a processed
    // scratch view and would reset the selection every frame.
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

    // Keeps m_previewDescriptor in sync with the current capture (a no-op unless the image view changed).
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

// HTTP automation entry points (contracts in FrameDebuggerPanel.h).

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
        // Same guard as the "Capture" button: without Enable the capture context holds no armed data.
        return false;
    }
    // Like the "Capture" button: only arms the deferred trigger, because this frame's graph has already executed.
    m_pendingCaptureTrigger = true;
    return true;
}

void FrameDebuggerPanel::SelectEventFromCommand(int index)
{
    const FrameDebuggerHistoryEntry* currentEntry = m_currentCapture.CurrentEntry();
    const int totalEventCount = (currentEntry != nullptr) ? currentEntry->snapshot.totalEventCount : 0;
    SetSelectedEventIndex(ClampSelectedEventIndex(index, totalEventCount));
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

    // No `default:` so a new channel enumerator fails to compile here.
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
    view.perDrawEventCount = m_captureContext.LastFrameEventCount();
    return view;
}

FrameDebuggerEventTextureResult FrameDebuggerPanel::GetEventTextureFromCommand(int eventIndex)
{
    m_captureContext.ArmEventIndex(eventIndex);

    FrameDebuggerEventTextureResult result;
    if (!m_captureContext.HasRetainedEventImage() || m_frameRenderer == nullptr) {
        return result; // Just (re)armed, or no Renderer cached yet - caller polls again.
    }

    const rg::EventSnapshotResource& retained = m_captureContext.RetainedEventImage();
    if (retained.Image() == VK_NULL_HANDLE) {
        return result;
    }

    // Use the aspect of this capture: a depth-only pass retains a depth image, never color.
    const bool isDepth = m_captureContext.RetainedEventIsDepth();
    const VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    // Same rare, explicit, human/LLM-driven stall GET /get_texture already
    // pays - never called from a per-frame path.
    m_frameRenderer->WaitForGpuIdle();

    Renderer::CapturedRawPixels raw = m_frameRenderer->CaptureImagePixels(
        retained.Image(), aspect, retained.Format(), FromVkExtent2D(retained.Extent()),
        rg::ResourceState{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT });
    if (raw.pixels.empty()) {
        return result;
    }

    std::vector<std::uint8_t> converted;
    const std::uint8_t* encodePixels = raw.pixels.data();
    if (isDepth) {
        // Safe in-place conversion - mirrors EditorHost.cpp's identical
        // GET /get_texture depth-channel branch.
        if (!Encoding::ConvertDepthToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, raw.pixels.data())) {
            return result; // Depth format not recognized - fail cleanly.
        }
    } else if (raw.format == PixelFormat::RGBA16_Float) {
        converted.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
        if (!Encoding::ConvertHdrRgba16fToRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, converted.data())) {
            return result;
        }
        encodePixels = converted.data();
    } else if (Encoding::IsBgraFormat(raw.format)) {
        Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
    }
    // else: already tightly-packed RGBA8 - no conversion needed.

    result.found = true;
    result.pixels = Encoding::EncodeRgba8ToPng(encodePixels, raw.width, raw.height);
    result.width = static_cast<std::uint32_t>(raw.width);
    result.height = static_cast<std::uint32_t>(raw.height);
    return result;
}

} // namespace gte
