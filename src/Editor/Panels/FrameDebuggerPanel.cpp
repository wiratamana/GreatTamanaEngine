#include "FrameDebuggerPanel.h"

#include "../EditorContext.h"
#include "../EditorFrame.h"
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
constexpr int kMaxResizeDeferrals = 3;

void BuildPropertyRow(const char* label, const std::string& value)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(150.0f);
    ImGui::TextUnformatted(value.c_str());
}

} // namespace

FrameDebuggerPanel::~FrameDebuggerPanel()
{
    // In-flight command buffers may still reference the preview descriptor and the preview renderers' GPU
    // resources: retiring waits for the GPU whenever a renderer is known.
    if (m_frameRenderer != nullptr) {
        m_frameRenderer->RetireUiTexture(m_previewDescriptor, nullptr);
        m_previewDescriptor = UiTextureHandle::Invalid;
        m_lastKnownPreviewView = 0;
    }
    // Defense in depth: ImGuiEditorLayer releases this before the UI backend shuts down, so this is normally a no-op.
    ReleaseShaderPropertyTexturePreview();
}

void FrameDebuggerPanel::ReleasePreviewDescriptor()
{
    if (m_previewDescriptor != UiTextureHandle::Invalid && m_frameRenderer != nullptr) {
        m_frameRenderer->ReleaseUiTextureHandle(m_previewDescriptor);
    }
    m_previewDescriptor = UiTextureHandle::Invalid;
    m_lastKnownPreviewView = 0;
}

void FrameDebuggerPanel::ReleaseShaderPropertyTexturePreview()
{
    // A rare, user-driven event: an occasional full GPU wait is accepted.
    if (m_frameRenderer != nullptr && (m_shaderPropertyPreviewTexture || m_shaderPropertyPreviewDescriptor != UiTextureHandle::Invalid)) {
        m_frameRenderer->RetireUiTexture(m_shaderPropertyPreviewDescriptor, std::move(m_shaderPropertyPreviewTexture));
    }
    m_shaderPropertyPreviewTexture.reset();
    m_shaderPropertyPreviewDescriptor = UiTextureHandle::Invalid;
    m_shaderPropertyPreviewName.clear();
    m_shaderPropertyPreviewLookupFailed = false;
    m_shaderPropertyPreviewExtent = Extent2D{};
}

void FrameDebuggerPanel::RequestShaderPropertyTexturePreview(const std::string& textureName, rg::ResourceKind kind)
{
    ReleaseShaderPropertyTexturePreview(); // Always start fresh.

    if (m_frameRenderer == nullptr || m_frameRenderGraph == nullptr) {
        return;
    }

    // Texture upload can throw on GPU exhaustion; the exception must not escape an ImGui click handler.
    auto uploadOrFail = [this, &textureName](const void* pixelsRgba8, int width, int height, const char* debugName) -> bool {
        try {
            m_shaderPropertyPreviewTexture = std::make_unique<Texture2D>(m_frameRenderer->CreateTexture2D(
                { .pixelsRgba8 = pixelsRgba8, .width = width, .height = height, .debugName = debugName }));
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

        // Non-const: the BGRA and single-channel branches below convert raw.pixels in place.
        CaptureResult captured = m_frameRenderer->CaptureDebugTexturePixels(*snapshot, /*depthAspect=*/false);
        CapturedPixels& raw = captured.pixels;

        if (captured.status != CaptureStatus::Ok || raw.pixels.empty()) {
            // The image is missing or its format is not supported.
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return;
        }

        std::vector<std::uint8_t> converted;
        const std::uint8_t* rgba8Pixels = raw.pixels.data();
        bool ok = true;
        if (raw.format == PixelFormat::RGBA16_Float) {
            converted.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
            ok = Encoding::ConvertRgba16fToRgba8(raw.pixels.data(), raw.format, raw.width, raw.height,
                converted.data(), Encoding::HdrVisualizationForTextureName(textureName));
            rgba8Pixels = converted.data();
        } else if (raw.format == PixelFormat::R8_UNorm || raw.format == PixelFormat::R32_Float) {
            converted.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
            ok = Encoding::Convert1ChannelToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, converted.data());
            rgba8Pixels = converted.data();
        } else if (Encoding::IsBgraFormat(raw.format)) {
            Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
        }
        // Raw RGBA8 needs no conversion.

        if (!ok) {
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return;
        }

        if (!uploadOrFail(rgba8Pixels, raw.width, raw.height, "FrameDebuggerShaderPropertyPreview")) {
            return; // uploadOrFail() already set the lookup-failed state.
        }
        m_shaderPropertyPreviewExtent = snapshot->ColorExtent();
    } else if (kind == rg::ResourceKind::VolumeTexture) {
        const std::optional<rg::DebugVolumeTextureSnapshot> snapshot =
            m_frameRenderGraph->DebugVolumeTextureSnapshotFor(textureName);
        if (!snapshot.has_value()) {
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return;
        }

        const VolumeTexturePreviewInterpretation interpretation = SelectVolumeTexturePreviewInterpretation(textureName);
        const CaptureResult preview =
            m_shaderPropertyVolumeRenderer.RenderPreview(*m_frameRenderer, snapshot->target, snapshot->state, interpretation);
        const CapturedPixels& raw = preview.pixels;

        if (preview.status != CaptureStatus::Ok
            || !uploadOrFail(raw.pixels.data(), raw.width, raw.height, "FrameDebuggerShaderPropertyVolumePreview")) {
            if (preview.status != CaptureStatus::Ok) {
                m_shaderPropertyPreviewName = textureName;
                m_shaderPropertyPreviewLookupFailed = true;
            }
            return;
        }
        m_shaderPropertyPreviewExtent = Extent2D{ static_cast<std::uint32_t>(raw.width), static_cast<std::uint32_t>(raw.height) };
    } else if (kind == rg::ResourceKind::TextureArray) {
        // No texture-array preview exists: show the "not previewable" placeholder.
        return;
    } else {
        // Buffer: no "View" button is drawn for buffer rows.
        return;
    }

    m_shaderPropertyPreviewDescriptor = m_frameRenderer->GetUiTextureHandle(*m_shaderPropertyPreviewTexture);
    m_shaderPropertyPreviewName = textureName;
    m_shaderPropertyPreviewLookupFailed = false;
}

void FrameDebuggerPanel::EnsurePreviewDescriptor()
{
    const FrameDebuggerCapturedFrame* frame = m_currentCapture.CurrentFrame();

    const std::optional<FrameDebuggerEventDetails> selectedDetails =
        (frame != nullptr) ? FindEventDetailsByIndex(frame->snapshot, m_selectedEventIndex) : std::nullopt;

    // A leaf that writes a private render-graph resource is previewed straight from that texture (no readback).
    std::optional<rg::DebugTextureSnapshot> privatePreview;
    if (selectedDetails.has_value() && selectedDetails->privateWriteTextureName.has_value()
        && m_frameRenderGraph != nullptr) {
        privatePreview = m_frameRenderGraph->DebugTextureSnapshotFor(*selectedDetails->privateWriteTextureName);
    }
    if (privatePreview.has_value() && privatePreview->ColorViewId() != 0 && m_frameRenderer != nullptr) {
        const std::uint64_t desiredView = privatePreview->ColorViewId();
        if (m_previewDescriptor != UiTextureHandle::Invalid && desiredView == m_lastKnownPreviewView) {
            return;
        }
        ReleasePreviewDescriptor();
        m_previewDescriptor = m_frameRenderer->GetUiTextureHandle(*privatePreview);
        m_lastKnownPreviewView = desiredView;
        return;
    }

    // With nothing selected the final composited image is shown.
    FrameDebuggerStepPreviewKind stepPreviewKind = FrameDebuggerStepPreviewKind::PostComposite;
    int stepPreviewIndex = -1;
    if (selectedDetails.has_value()) {
        stepPreviewKind = selectedDetails->stepPreviewKind;
        stepPreviewIndex = selectedDetails->stepPreviewIndex;
    }

    const bool hasPerObjectStepPreviewAtIndex = frame != nullptr
        && stepPreviewKind == FrameDebuggerStepPreviewKind::PerObjectStep && stepPreviewIndex >= 0
        && static_cast<std::size_t>(stepPreviewIndex) < frame->perObjectStepPreviews.size();

    const FrameDebuggerPreviewSourceChoice choice = ChooseFrameDebuggerPreviewSource(frame != nullptr, stepPreviewKind,
        frame != nullptr && frame->preview.has_value(), frame != nullptr && frame->compositedPreview.has_value(),
        hasPerObjectStepPreviewAtIndex);
    m_lastPreviewChoice = choice;

    const RenderTexture* selectedSource = nullptr;
    switch (choice) {
    case FrameDebuggerPreviewSourceChoice::Preview:
        selectedSource = (frame != nullptr && frame->preview.has_value()) ? &(*frame->preview) : nullptr;
        break;
    case FrameDebuggerPreviewSourceChoice::CompositedPreview:
        selectedSource = (frame != nullptr && frame->compositedPreview.has_value()) ? &(*frame->compositedPreview) : nullptr;
        break;
    case FrameDebuggerPreviewSourceChoice::PerObjectStepPreview:
        selectedSource = hasPerObjectStepPreviewAtIndex
            ? &frame->perObjectStepPreviews[static_cast<std::size_t>(stepPreviewIndex)]
            : nullptr;
        break;
    case FrameDebuggerPreviewSourceChoice::NotYetDrawn:
    case FrameDebuggerPreviewSourceChoice::None:
        break;
    }

    if (selectedSource == nullptr || m_frameRenderer == nullptr) {
        ReleasePreviewDescriptor();
        return;
    }

    // Per-object step previews hold linear scene color and always go through the preview shader.
    // Neutral channel/levels on any other source show the retained texture directly.
    const bool sceneReferred = (choice == FrameDebuggerPreviewSourceChoice::PerObjectStepPreview);
    const bool isNeutral = (m_channel == FrameDebuggerPreviewChannel::All) && (m_levelsBlack <= 0.0f)
        && (m_levelsWhite >= 1.0f) && !sceneReferred;

    std::uint64_t desiredView = selectedSource->ViewId();
    const Texture2D* processedOutput = nullptr;

    if (!isNeutral) {
        const std::uint64_t sourceView = selectedSource->ViewId();
        const bool dirty = (m_lastProcessedSourceView != sourceView) || (m_lastProcessedChannel != m_channel)
            || (m_lastProcessedLevelsBlack != m_levelsBlack) || (m_lastProcessedLevelsWhite != m_levelsWhite)
            || (m_lastProcessedSceneReferred != sceneReferred);
        if (dirty) {
            m_previewProcessor.RenderPreview(
                *m_frameRenderer, *selectedSource, m_channel, m_levelsBlack, m_levelsWhite, sceneReferred);
            m_lastProcessedSourceView = sourceView;
            m_lastProcessedChannel = m_channel;
            m_lastProcessedLevelsBlack = m_levelsBlack;
            m_lastProcessedLevelsWhite = m_levelsWhite;
            m_lastProcessedSceneReferred = sceneReferred;
        }
        processedOutput = m_previewProcessor.OutputTexture();
        if (processedOutput != nullptr) {
            desiredView = processedOutput->ViewId();
        }
    }

    if (m_previewDescriptor != UiTextureHandle::Invalid && desiredView == m_lastKnownPreviewView) {
        return;
    }

    ReleasePreviewDescriptor();
    m_previewDescriptor = processedOutput != nullptr ? m_frameRenderer->GetUiTextureHandle(*processedOutput)
                                                     : m_frameRenderer->GetUiTextureHandle(*selectedSource);
    m_lastKnownPreviewView = desiredView;
}

bool FrameDebuggerPanel::IsCaptureArmed() const noexcept
{
    return m_enabled && IsOpen();
}

FrameDebuggerCaptureContext* FrameDebuggerPanel::PrepareCaptureContextForThisFrame()
{
    if (!IsCaptureArmed()) {
        m_captureContext.Deactivate();
        return nullptr;
    }
    m_captureContext.Reset();
    return &m_captureContext;
}

void FrameDebuggerPanel::NotifyStepConsumed() noexcept
{
    if (m_enabled) {
        m_handshake.Request();
    }
}

bool FrameDebuggerPanel::ConsumePendingReplayRequest()
{
    return m_handshake.ConsumeForReplay();
}

void FrameDebuggerPanel::NotifyOffscreenFailed()
{
    if (m_handshake.NotifyFrameFailed()) {
        m_captureError = "The Game view failed to render. See the Core log, then click Capture to retry.";
    }
}

void FrameDebuggerPanel::TriggerCapture()
{
    const rg::RenderGraphSnapshot graphSnapshot =
        m_frameRenderGraph->LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback);

    // Honesty detector: a non-culled pass reported disabled by the toggle registry.
    if (m_frameToggleRegistry != nullptr) {
        const std::vector<std::string> mismatches = DetectRenderPassHonestyMismatches(
            graphSnapshot.passesInExecutionOrder,
            [this](const std::string& name) { return m_frameToggleRegistry->IsEnabled(name); });
        RenderPassHonestyGuard::Instance().ReportCaptureMismatches(mismatches);
    }

    // Side-channel detector: a disabled pass whose known-risk blackboard key was still published.
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

    // Coverage detector: a non-culled pass that executed but has no leaf in the tree.
    const std::vector<std::string> missingFromTree =
        DetectPassesMissingFromFrameDebuggerTree(graphSnapshot.passesInExecutionOrder, snapshot);
    FrameDebuggerCoverageGuard::Instance().ReportCaptureMismatches(missingFromTree);

    try {
        m_currentCapture.CaptureFrame(
            *m_frameRenderer, snapshot, *m_frameGameView, m_frameGameViewComposited, m_captureContext);
    } catch (const std::exception& e) {
        m_currentCapture.Clear();
        m_handshake.Fail();
        m_captureError = std::string("Capture failed: ") + e.what();
        GTE_LOG_ERROR("FrameDebugger", m_captureError);
        return;
    }
    m_captureError.clear();
    SetSelectedEventIndex(-1);
}

void FrameDebuggerPanel::SetSelectedEventIndex(int newIndex)
{
    if (newIndex == m_selectedEventIndex) {
        return;
    }
    m_selectedEventIndex = newIndex;
    // A shader-property preview belongs to the old selection.
    ReleaseShaderPropertyTexturePreview();
}

// Enable pauses a running game. Disable resumes it only if Enable was what paused it.
void FrameDebuggerPanel::ApplyEnabledEdge(EditorContext& ctx, bool newEnabled)
{
    if (newEnabled == m_enabled) {
        return;
    }
    m_enabled = newEnabled;
    if (m_enabled) {
        m_pausedByFrameDebugger = !ctx.playbackPaused;
        ctx.playbackPaused = true;
        m_handshake.Request();
        return;
    }
    if (m_pausedByFrameDebugger) {
        ctx.playbackPaused = false;
    }
    m_pausedByFrameDebugger = false;
    m_handshake.Cancel();
    m_resizeDeferrals = 0;
    m_captureError.clear();
    m_currentCapture.Clear();
    ReleaseShaderPropertyTexturePreview();
    SetSelectedEventIndex(-1);
}

void FrameDebuggerPanel::BuildToolbarRow(EditorContext& ctx)
{
    bool enabledValue = m_enabled;
    ImGui::Checkbox("Enable", &enabledValue);
    if (enabledValue != m_enabled) {
        ApplyEnabledEdge(ctx, enabledValue);
    }

    ImGui::SameLine();

    ImGui::BeginDisabled(!m_enabled);
    if (ImGui::Button("Capture")) {
        m_handshake.Request();
    }
    ImGui::EndDisabled();
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
    // A node with children is an expandable row. IsItemClicked() makes a click anywhere on the row select it.
    if (!node.children.empty()) {
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
        return; // Groups are only added when they have a child.
    }

    // Leaf draw call: highlighted when selected, a click selects it.
    const bool isSelected = (node.eventIndex == m_selectedEventIndex);
    if (ImGui::Selectable(node.name.c_str(), isSelected)) {
        SetSelectedEventIndex(node.eventIndex);
    }
}

void FrameDebuggerPanel::BuildEventTreePane(const FrameDebuggerSnapshot& snapshot)
{
    for (const FrameDebuggerEventNode& root : snapshot.rootNodes) {
        RenderEventNode(root);
    }
}

void FrameDebuggerPanel::BuildInspectorPane(
    const FrameDebuggerSnapshot& snapshot, const FrameDebuggerCapturedFrame* currentFrame)
{
    ImGui::TextUnformatted("RenderTarget");
    ImGui::SameLine(150.0f);
    ImGui::TextUnformatted(snapshot.renderTarget.name.c_str());

    ImGui::TextUnformatted("Channels");
    // The active channel button uses the theme's ButtonActive color.
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

    ImGui::TextUnformatted("Levels");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    // AlwaysClamp keeps both handles in [0, 1]; the line below keeps white above black.
    ImGui::DragFloatRange2("##FrameDebuggerLevels", &m_levelsBlack, &m_levelsWhite, 0.005f, 0.0f, 1.0f, "Black %.2f",
        "White %.2f", ImGuiSliderFlags_AlwaysClamp);
    m_levelsWhite = std::max(m_levelsWhite, m_levelsBlack + 0.001f);

    // Preview box.
    const std::string resolutionCaption = std::to_string(snapshot.renderTarget.width) + "x"
        + std::to_string(snapshot.renderTarget.height) + "  " + snapshot.renderTarget.format;
    ImGui::TextDisabled("%s", resolutionCaption.c_str());

    const std::optional<FrameDebuggerEventDetails> details
        = FindEventDetailsByIndex(snapshot, m_selectedEventIndex);

    // The retained preview belongs to the capture, not to one event.
    const bool showPreviewTexture = (m_previewDescriptor != UiTextureHandle::Invalid) && (currentFrame != nullptr);

    const float previewHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.5f);
    // Opaque black letterbox behind the image or the placeholder text.
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
            if (m_shaderPropertyPreviewDescriptor != UiTextureHandle::Invalid) {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                if (avail.x >= 1.0f && avail.y >= 1.0f) {
                    const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(avail.x, avail.y,
                        static_cast<float>(m_shaderPropertyPreviewExtent.width),
                        static_cast<float>(m_shaderPropertyPreviewExtent.height));
                    const ImVec2 cursorBase = ImGui::GetCursorPos();
                    ImGui::SetCursorPos(ImVec2(cursorBase.x + fit.offsetX, cursorBase.y + fit.offsetY));
                    ImGui::Image(static_cast<ImTextureID>(static_cast<std::uint64_t>(m_shaderPropertyPreviewDescriptor)),
                        ImVec2(fit.width, fit.height));
                }
            } else {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                // A failed lookup or upload gets its own text, apart from the plain "No Texture".
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
                ImGui::Image(static_cast<ImTextureID>(static_cast<std::uint64_t>(m_previewDescriptor)),
                    ImVec2(fit.width, fit.height));
            }
        } else {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            // Pre-view leaf: nothing drawn yet, so no image is fabricated.
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
    if (!d.textures.empty()) {
        ImGui::SeparatorText("Textures");
        // Button IDs use the row index: two rows can share a valueLabel and ImGui IDs must be unique.
        for (std::size_t i = 0; i < d.textures.size(); ++i) {
            const FrameDebuggerTextureProperty& texture = d.textures[i];
            BuildPropertyRow(texture.name.c_str(), texture.valueLabel);
            // Only render-graph 2D and volume textures can be previewed.
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
    if (!d.gpuTimingText.empty()) {
        ImGui::SeparatorText("GPU");
        BuildPropertyRow("GPU Time", d.gpuTimingText);
        if (d.indirectDraws != 0) {
            BuildPropertyRow("Indirect Draws", std::to_string(d.indirectDraws));
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
            // One ImGui::Text() per matrix row.
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
}

void FrameDebuggerPanel::OnFrame(EditorFrame& frame)
{
    EditorContext& ctx = frame.ctx;
    Renderer& renderer = frame.renderer;
    const rg::RenderGraph& renderGraph = frame.renderGraph;

    // The collaborators are cached first: TriggerCapture() below reads them.
    m_frameRenderer = &renderer;
    m_frameRenderGraph = &renderGraph;
    m_frameGameView = &frame.gameView;
    m_frameGameViewComposited = frame.gameViewComposited;
    m_frameToggleRegistry = &frame.renderPassToggleRegistry;
    m_frameBlackboard = &frame.offscreenBlackboard;
    m_captureContext.InitializeEventCapture(renderer);

    // Two things end an enabled session: the window was closed, or the user resumed the game.
    if (m_enabled && !IsOpen()) {
        ApplyEnabledEdge(ctx, false);
    }
    if (m_enabled && !ctx.playbackPaused) {
        m_pausedByFrameDebugger = false;
        ApplyEnabledEdge(ctx, false);
    }

    // Second half of the handshake: this frame's replay passes and draw records exist now.
    if (m_handshake.ConsumeForCapture()) {
        // A frame that resized persistent targets drew into stale-size targets, so it is skipped.
        // The bound keeps a target that resizes every frame from starving the capture.
        if (renderGraph.PersistentTexturesResizedThisFrame() && m_resizeDeferrals < kMaxResizeDeferrals) {
            ++m_resizeDeferrals;
            m_handshake.Request();
            GTE_LOG_DEBUG("FrameDebugger", "Capture moved to the next frame: persistent targets were resized.");
        } else {
            m_resizeDeferrals = 0;
            TriggerCapture();
        }
    }

    // A pass toggle changed earlier this frame: capture again so a disabled pass does not linger.
    if (ctx.renderPassToggleRegistryChangedThisFrame) {
        ctx.renderPassToggleRegistryChangedThisFrame = false;
        if (m_enabled) {
            m_handshake.Request();
        }
    }
}

void FrameDebuggerPanel::OnGui(EditorFrame& frame)
{
    EditorContext& ctx = frame.ctx;

    BuildToolbarRow(ctx);
    ImGui::Separator();

    const FrameDebuggerCapturedFrame* currentFrame = m_currentCapture.CurrentFrame();

    // A new capture invalidates event indices, so the selection is cleared. The raw retained preview view
    // is new on every capture; the processed scratch view would reset the selection every frame.
    {
        const std::uint64_t newRawPreviewView =
            (currentFrame != nullptr && currentFrame->preview.has_value()) ? currentFrame->preview->ViewId() : 0;
        if (newRawPreviewView != m_lastKnownRawPreviewView) {
            m_selectedEventIndex = -1;
        }
        m_lastKnownRawPreviewView = newRawPreviewView;
    }

    // Keeps m_previewDescriptor in sync with the current capture (also releases it when there is no capture).
    EnsurePreviewDescriptor();

    if (currentFrame == nullptr) {
        BuildEmptyState();
        return;
    }
    const FrameDebuggerSnapshot& snapshot = currentFrame->snapshot;

    BuildFrameStepperRow(snapshot);
    ImGui::Separator();

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
    BuildInspectorPane(snapshot, currentFrame);
    ImGui::EndChild();
}

void FrameDebuggerPanel::BuildEmptyState()
{
    if (!m_enabled) {
        ImGui::TextDisabled("Frame Debugger is off. Tick Enable to freeze and inspect the current Game view frame.");
        return;
    }
    if (m_handshake.Phase() == FrameDebuggerCapturePhase::Failed) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_captureError.c_str());
        ImGui::TextDisabled("Click Capture to retry.");
        return;
    }
    ImGui::TextDisabled("Capturing the Game view frame...");
}

void FrameDebuggerPanel::SetEnabledFromCommand(EditorContext& ctx, bool enabled)
{
    if (enabled) {
        Show();
    }
    ApplyEnabledEdge(ctx, enabled);
}

bool FrameDebuggerPanel::CaptureNowFromCommand()
{
    if (!m_enabled) {
        return false;
    }
    m_handshake.Request();
    return true;
}

void FrameDebuggerPanel::SelectEventFromCommand(int index)
{
    const FrameDebuggerCapturedFrame* currentFrame = m_currentCapture.CurrentFrame();
    const int totalEventCount = (currentFrame != nullptr) ? currentFrame->snapshot.totalEventCount : 0;
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
    // Same clamp as the Levels control: both handles stay in [0, 1] and white stays above black.
    m_levelsBlack = std::clamp(black, 0.0f, 1.0f);
    m_levelsWhite = std::clamp(white, 0.0f, 1.0f);
    m_levelsWhite = std::max(m_levelsWhite, m_levelsBlack + 0.001f);
}

FrameDebuggerStateSnapshotView FrameDebuggerPanel::BuildStateSnapshotView() const
{
    FrameDebuggerStateSnapshotView view;
    view.enabled = m_enabled;
    view.windowOpen = IsOpen();
    view.hasCapturedFrame = m_currentCapture.HasCapture();

    const FrameDebuggerCapturedFrame* currentFrame = m_currentCapture.CurrentFrame();
    view.totalEventCount = (currentFrame != nullptr) ? currentFrame->snapshot.totalEventCount : 0;
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
    view.captureStatus = FrameDebuggerCaptureStatusName(m_enabled, m_handshake.Phase(), view.hasCapturedFrame);
    view.captureError = (m_handshake.Phase() == FrameDebuggerCapturePhase::Failed) ? m_captureError : std::string();
    view.perDrawEventCount = view.hasCapturedFrame ? m_captureContext.LastFrameEventCount() : 0;
    return view;
}

FrameDebuggerEventTextureResult FrameDebuggerPanel::GetEventTextureFromCommand(int eventIndex)
{
    m_captureContext.ArmEventIndex(eventIndex);

    FrameDebuggerEventTextureResult result;
    if (!m_captureContext.HasRetainedEventImage() || m_frameRenderer == nullptr) {
        return result; // Just (re)armed, or no Renderer cached yet - caller polls again.
    }

    const rg::RetainedEventSnapshot& retained = m_captureContext.RetainedEventImage();

    // A depth-only pass retains a depth image, never color.
    const bool isDepth = m_captureContext.RetainedEventIsDepth();

    // Rare, explicit capture: the renderer waits for the GPU first.
    CaptureResult captured = m_frameRenderer->CaptureEventSnapshotPixels(retained, isDepth);
    CapturedPixels& raw = captured.pixels;
    if (captured.status != CaptureStatus::Ok || raw.pixels.empty()) {
        return result;
    }

    std::vector<std::uint8_t> converted;
    const std::uint8_t* encodePixels = raw.pixels.data();
    if (isDepth) {
        // In-place conversion, same as the /get_texture depth branch.
        if (!Encoding::ConvertDepthToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, raw.pixels.data())) {
            return result; // Depth format not recognized - fail cleanly.
        }
    } else if (raw.format == PixelFormat::RGBA16_Float) {
        converted.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
        // Event images hold scene color as drawn at that event.
        if (!Encoding::ConvertRgba16fToRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, converted.data(),
                Encoding::HdrVisualization::SceneReferred)) {
            return result;
        }
        encodePixels = converted.data();
    } else if (raw.format == PixelFormat::R8_UNorm || raw.format == PixelFormat::R32_Float) {
        converted.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
        if (!Encoding::Convert1ChannelToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, converted.data())) {
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
