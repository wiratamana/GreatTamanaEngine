#include "ShadowPanel.h"

#include "../../../Editor/EditorContext.h"
#include "../ShadowTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace gte {

namespace {

constexpr std::array<std::uint32_t, 4> kResolutions{ 512, 1024, 2048, 4096 };
constexpr std::array<std::uint32_t, 4> kFilterSamples{ 1, 4, 8, 16 };
constexpr std::array<const char*, 3> kDebugViews{ "Off", "Cascade Colors", "Mask" };
constexpr float kLowSunDegrees = 10.0f;

const char* StatusText(ShadowFrameStatus status) noexcept
{
    switch (status) {
    case ShadowFrameStatus::Ok: return "Ok";
    case ShadowFrameStatus::NotRendered: return "Not rendered (the view is not drawn)";
    case ShadowFrameStatus::NoValidCamera: return "No valid camera";
    case ShadowFrameStatus::ShadersNotReady: return "Shaders not ready";
    case ShadowFrameStatus::TargetFailed: return "Target allocation failed";
    case ShadowFrameStatus::PassDisabled: return "A cascade pass is disabled";
    }
    return "Unknown";
}

// Combo over a fixed list of unsigned values. Returns true when the selection changed.
template <std::size_t N>
bool ComboValue(const char* label, std::uint32_t& value, const std::array<std::uint32_t, N>& options)
{
    bool changed = false;
    const std::string current = std::to_string(value);
    if (ImGui::BeginCombo(label, current.c_str())) {
        for (const std::uint32_t option : options) {
            const bool selected = option == value;
            if (ImGui::Selectable(std::to_string(option).c_str(), selected)) {
                value = option;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool SliderCount(const char* label, std::uint32_t& value, int lo, int hi)
{
    int v = static_cast<int>(value);
    const bool changed = ImGui::SliderInt(label, &v, lo, hi);
    if (changed) {
        value = static_cast<std::uint32_t>(std::clamp(v, lo, hi));
    }
    return changed;
}

void DrawStatus(const ShadowViewSnapshot& game)
{
    const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.65f, 0.2f, 1.0f);
    ImGui::TextColored(game.status == ShadowFrameStatus::Ok ? green : orange, "Game view: %s", StatusText(game.status));
    ImGui::TextColored(
        game.maskStatus == ShadowFrameStatus::Ok ? green : orange, "Mask: %s", StatusText(game.maskStatus));
    if (game.status != ShadowFrameStatus::NotRendered) {
        ImGui::Text("Sun elevation: %.1f deg", game.sunElevationDegrees);
        if (game.sunElevationDegrees < kLowSunDegrees) {
            ImGui::TextColored(orange, "Low sun: shadows are very long.");
        }
    }
}

void DrawSdsmLine(const ShadowViewSnapshot& game)
{
    if (game.cascades.sdsmActive) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "SDSM active");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Fallback to CSM (fixed splits)");
    }
    ImGui::Text("Measured range: %.2f .. %.2f", game.sdsm.raw.nearDepth, game.sdsm.raw.farDepth);
    ImGui::Text("Used range: %.2f .. %.2f", game.sdsm.used.nearDepth, game.sdsm.used.farDepth);
    ImGui::Text("Sample age: %llu frames", static_cast<unsigned long long>(game.sdsmAgeFrames));
}

void DrawLiveInfo(const ShadowViewSnapshot& game, const ShadowViewSnapshot& scene)
{
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("ShadowCascades", 6, flags)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Start");
        ImGui::TableSetupColumn("End");
        ImGui::TableSetupColumn("Radius");
        ImGui::TableSetupColumn("Texel");
        ImGui::TableSetupColumn("Depth span");
        ImGui::TableHeadersRow();
        const std::uint32_t count = std::min(game.cascades.count, kMaxShadowCascades);
        for (std::uint32_t i = 0; i < count; ++i) {
            const ShadowCascade& c = game.cascades.cascades[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u", i);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", c.sliceStart);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", c.sliceEnd);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", c.radius);
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", c.texelWorldSize);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", c.depthRange);
        }
        ImGui::EndTable();
    }
    ImGui::Text("Resolution in use: Game %u, Scene %u", game.resolutionInUse, scene.resolutionInUse);
    const double megabytes = static_cast<double>(game.cascadeBytes + scene.cascadeBytes) / (1024.0 * 1024.0);
    ImGui::Text("Cascade map memory (Game + Scene): %.1f MB", megabytes);
}

} // namespace

bool BuildShadowPanel(EditorContext& /*ctx*/, ShadowSettings& edit, const ShadowViewSnapshot& game,
    const ShadowViewSnapshot& scene)
{
    bool changed = false;
    ImGui::Begin("Shadow");

    DrawStatus(game);

    ImGui::SeparatorText("Cascades");
    changed |= SliderCount("Cascade count", edit.cascadeCount, 1, static_cast<int>(kMaxShadowCascades));
    changed |= ImGui::DragFloat("Max distance", &edit.maxDistance, 1.0f, 1.0f, 100000.0f);
    changed |= ImGui::SliderFloat("Split lambda", &edit.splitLambda, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Blend fraction", &edit.cascadeBlendFraction, 0.0f, 0.5f);
    changed |= ImGui::SliderFloat("Fade fraction", &edit.distanceFadeFraction, 0.0f, 0.5f);
    changed |= ImGui::DragFloat("Caster range", &edit.casterRange, 1.0f, 1.0f, kMaxCasterRange);
    changed |= ImGui::Checkbox("Stabilize cascades", &edit.stabilizeCascades);

    ImGui::SeparatorText("SDSM");
    changed |= ImGui::Checkbox("Fit cascades to visible depth (SDSM)", &edit.useSdsm);
    changed |= ImGui::SliderFloat("Range padding", &edit.sdsmPaddingFraction, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Shrink time (s)", &edit.sdsmShrinkSeconds, 0.0f, 10.0f);
    changed |= SliderCount("Quantize steps per octave", edit.sdsmQuantizeSteps, 0, 16);
    DrawSdsmLine(game);

    ImGui::SeparatorText("Quality");
    changed |= ComboValue("Max map resolution", edit.maxMapResolution, kResolutions);
    changed |= ComboValue("Filter samples", edit.filterSamples, kFilterSamples);
    changed |= ImGui::DragFloat("Filter radius (texels)", &edit.filterRadiusTexels, 0.05f, 0.0f, 8.0f);
    changed |= ImGui::SliderFloat("Strength", &edit.strength, 0.0f, 1.0f);
    changed |= ImGui::Checkbox("Apply in materials", &edit.applyInMaterials);

    ImGui::SeparatorText("Culling");
    changed |= ImGui::Checkbox("Cull casters per cascade", &edit.cullCasters);
    changed |= ImGui::DragFloat("Cull padding fraction", &edit.casterCullPaddingFraction, 0.01f, 0.0f, 2.0f);

    ImGui::SeparatorText("Bias");
    changed |= ImGui::DragFloat("Constant (texels)", &edit.constBiasTexels, 0.05f, 0.0f, 16.0f);
    changed |= ImGui::DragFloat("Slope (texels)", &edit.slopeBiasTexels, 0.05f, 0.0f, 16.0f);
    changed |= ImGui::DragFloat("Normal offset (texels)", &edit.normalOffsetTexels, 0.05f, 0.0f, 16.0f);

    ImGui::SeparatorText("Debug");
    int view = static_cast<int>(edit.debugView);
    if (ImGui::Combo("View", &view, kDebugViews.data(), static_cast<int>(kDebugViews.size()))) {
        edit.debugView = static_cast<ShadowDebugView>(std::clamp(view, 0, static_cast<int>(kDebugViews.size()) - 1));
        changed = true;
    }

    ImGui::SeparatorText("Live info");
    DrawLiveInfo(game, scene);

    if (ImGui::Button("Reset to defaults")) {
        edit = ShadowSettings{};
        changed = true;
    }

    ImGui::End();
    return changed;
}

} // namespace gte
