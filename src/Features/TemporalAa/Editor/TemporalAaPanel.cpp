#include "TemporalAaPanel.h"

#include "../TemporalAaTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>

namespace gte {

namespace {

constexpr std::array<const char*, 2> kDebugViewLabels{ "Off", "Jitter only (history ignored)" };
constexpr float kBytesPerMegabyte = 1024.0f * 1024.0f;

const char* StatusText(TemporalAaFrameStatus status) noexcept
{
    switch (status) {
    case TemporalAaFrameStatus::NotRendered: return "Not rendered (the view is not drawn)";
    case TemporalAaFrameStatus::Ok: return "Ok";
    case TemporalAaFrameStatus::Disabled: return "Disabled in the settings";
    case TemporalAaFrameStatus::PassDisabled: return "A TemporalAa pass is switched off";
    case TemporalAaFrameStatus::ShadersNotReady: return "Shader not ready";
    case TemporalAaFrameStatus::TargetFailed: return "History target allocation failed";
    case TemporalAaFrameStatus::NoValidCamera: return "No valid perspective camera";
    case TemporalAaFrameStatus::ExtentTooSmall: return "The view is too small";
    case TemporalAaFrameStatus::ViewNotSupported: return "This view is not supported";
    case TemporalAaFrameStatus::CaptureArmed: return "Off while a frame capture is armed";
    }
    return "Unknown";
}

const char* JitterText(TemporalAaJitterDecision decision) noexcept
{
    switch (decision) {
    case TemporalAaJitterDecision::NotRun: return "No jitter step ran";
    case TemporalAaJitterDecision::Apply: return "Jitter applied";
    case TemporalAaJitterDecision::NotPrimaryView: return "No jitter: not the Game view";
    case TemporalAaJitterDecision::Disabled: return "No jitter: disabled in the settings";
    case TemporalAaJitterDecision::CaptureArmed: return "No jitter: frame capture armed";
    case TemporalAaJitterDecision::PassDisabled: return "No jitter: a TemporalAa pass is switched off";
    case TemporalAaJitterDecision::NoValidCamera: return "No jitter: no valid perspective camera";
    case TemporalAaJitterDecision::ExtentTooSmall: return "No jitter: the view is too small";
    case TemporalAaJitterDecision::ShadersNotReady: return "No jitter: shader not ready";
    case TemporalAaJitterDecision::NoHeartbeat: return "No jitter: the resolve did not run last frame";
    }
    return "Unknown";
}

void DrawStatus(const char* label, const TemporalAaViewSnapshot& view)
{
    const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.65f, 0.2f, 1.0f);
    ImGui::TextColored(view.status == TemporalAaFrameStatus::Ok ? green : orange, "%s: %s", label, StatusText(view.status));
    ImGui::TextColored(view.jitterDecision == TemporalAaJitterDecision::Apply ? green : orange, "%s jitter: %s", label,
        JitterText(view.jitterDecision));
}

} // namespace

bool BuildTemporalAaPanel(
    TemporalAaSettings& edit, const TemporalAaViewSnapshot& game, const TemporalAaViewSnapshot& scene)
{
    bool changed = false;

    DrawStatus("Game view", game);
    DrawStatus("Scene view", scene);
    ImGui::TextWrapped("Slightly softer than no anti-aliasing. No per-object motion vectors: fast moving or animated "
                       "objects can ghost a little. The Scene view is not supported.");

    ImGui::SeparatorText("Anti-aliasing");
    changed |= ImGui::Checkbox("Enabled", &edit.enabled);

    int samples = static_cast<int>(edit.jitterSampleCount);
    if (ImGui::SliderInt("Jitter samples", &samples, static_cast<int>(kTemporalAaMinSamples),
            static_cast<int>(kTemporalAaMaxSamples))) {
        edit.jitterSampleCount = static_cast<std::uint32_t>(
            std::clamp(samples, static_cast<int>(kTemporalAaMinSamples), static_cast<int>(kTemporalAaMaxSamples)));
        changed = true;
    }
    changed |= ImGui::DragFloat("Jitter scale", &edit.jitterScale, 0.01f, 0.0f, 8.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("1 = +/-0.5 pixel. Values above 1 are for debugging only.");
    }

    ImGui::SeparatorText("Resolve");
    changed |= ImGui::SliderFloat("Feedback min", &edit.feedbackMin, 0.0f, 0.99f);
    changed |= ImGui::SliderFloat("Feedback max", &edit.feedbackMax, 0.0f, 0.99f);
    changed |= ImGui::SliderFloat("Clip gamma", &edit.clipGamma, 0.25f, 4.0f);
    changed |= ImGui::SliderFloat("Depth tolerance", &edit.depthTolerance, 0.001f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic);

    int debugView = static_cast<int>(edit.debugView);
    if (ImGui::Combo("Debug view", &debugView, kDebugViewLabels.data(), static_cast<int>(kDebugViewLabels.size()))) {
        edit.debugView = static_cast<TemporalAaDebugView>(std::clamp(debugView, 0, static_cast<int>(kDebugViewLabels.size()) - 1));
        changed = true;
    }

    if (ImGui::Button("Reset to defaults")) {
        edit = TemporalAaSettings{};
        changed = true;
    }

    ImGui::Text("Texture memory: %.1f MB", static_cast<double>(static_cast<float>(game.textureBytes) / kBytesPerMegabyte));

    return changed;
}

} // namespace gte
