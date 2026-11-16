#include "GrassPanel.h"

#include "../GrassTypes.h"

#include <imgui.h>

#include <algorithm>
#include <array>

namespace gte {

namespace {

constexpr std::array<const char*, 4> kDebugLabels{ "Normal", "LOD colour (0 green, 1 yellow, 2 red)", "Wind sway", "Chunk bounds" };

const char* StatusText(GrassFrameStatus status) noexcept
{
    switch (status) {
    case GrassFrameStatus::Ok: return "Ok";
    case GrassFrameStatus::NotRendered: return "Not rendered (the view is not drawn)";
    case GrassFrameStatus::Disabled: return "Disabled";
    case GrassFrameStatus::NoValidCamera: return "No valid camera";
    case GrassFrameStatus::NoTile: return "No terrain height tile yet (nothing to grow on)";
    case GrassFrameStatus::ShadersNotReady: return "Shaders not ready";
    case GrassFrameStatus::TargetFailed: return "Buffer allocation failed";
    case GrassFrameStatus::PassDisabled: return "A pass is disabled";
    }
    return "Unknown";
}

void DrawStatus(const char* label, const GrassViewSnapshot& view)
{
    const ImVec4 green(0.4f, 0.9f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.65f, 0.2f, 1.0f);
    ImGui::TextColored(view.status == GrassFrameStatus::Ok ? green : orange, "%s: %s", label, StatusText(view.status));
}

void DrawStats(const GrassViewSnapshot& view)
{
    if (!view.statsValid) {
        ImGui::TextDisabled("No counters yet (they arrive two frames late).");
        return;
    }
    const GrassStatsGpu& s = view.stats;
    const auto at = [&](GrassStatsWord w) { return static_cast<unsigned>(s.Get(w)); };
    ImGui::Text("Blades drawn: LOD0 %u, LOD1 %u, LOD2 %u", at(GrassStatsWord::Slot0), at(GrassStatsWord::Slot1),
        at(GrassStatsWord::Slot2));
    ImGui::Text("Chunks: grid %u, far %u, frustum %u, Hi-Z %u, water %u, no data %u", at(GrassStatsWord::ChunksInGrid),
        at(GrassStatsWord::ChunksCulledDistance), at(GrassStatsWord::ChunksCulledFrustum), at(GrassStatsWord::ChunksCulledHiz),
        at(GrassStatsWord::ChunksCulledWater), at(GrassStatsWord::ChunksCulledNoData));
    ImGui::Text("Candidates %u, rejected: LOD step %u, water %u, slope %u, patches %u", at(GrassStatsWord::Candidates),
        at(GrassStatsWord::RejectLod), at(GrassStatsWord::RejectWater), at(GrassStatsWord::RejectSlope),
        at(GrassStatsWord::RejectNoise));
    const unsigned dropped = at(GrassStatsWord::Dropped0) + at(GrassStatsWord::Dropped1) + at(GrassStatsWord::Dropped2);
    if (dropped > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "%u blades dropped: a LOD region is full (raise the capacity).", dropped);
    }
    ImGui::Text("GPU memory: %.1f MiB of instances. Hi-Z %s. Wind: %s.", static_cast<double>(view.instanceBytes) / (1024.0 * 1024.0),
        view.hizUsed ? "on" : "off", view.windShared ? "shared with the clouds" : "own fallback");
}

} // namespace

bool BuildGrassPanel(GrassSettings& edit, const GrassViewSnapshot& game, const GrassViewSnapshot& scene)
{
    bool changed = false;

    DrawStatus("Game view", game);
    DrawStatus("Scene view", scene);
    DrawStats(game);

    changed |= ImGui::Checkbox("Enabled", &edit.enabled);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Hi-Z culling", &edit.hizCulling);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Counters", &edit.collectStats);

    int debug = static_cast<int>(edit.debugView);
    if (ImGui::Combo("Debug view", &debug, kDebugLabels.data(), static_cast<int>(kDebugLabels.size()))) {
        edit.debugView = static_cast<GrassDebugView>(std::clamp(debug, 0, static_cast<int>(kDebugLabels.size()) - 1));
        changed = true;
    }

    if (ImGui::CollapsingHeader("Density", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Blades / m2, LOD0", &edit.densityLod0, 0.0f, 120.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("Blades / m2, LOD1", &edit.densityLod1, 0.0f, 40.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("Blades / m2, LOD2", &edit.densityLod2, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("Patchiness", &edit.patchiness, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Patch size (m)", &edit.patchScaleM, 5.0f, 300.0f);
        changed |= ImGui::SliderFloat("Bare threshold", &edit.bareThreshold, 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Blade", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Height (m)", &edit.bladeHeight, 0.05f, 2.0f);
        changed |= ImGui::SliderFloat("Width (m)", &edit.bladeWidth, 0.005f, 0.2f, "%.3f");
        changed |= ImGui::SliderFloat("Taper", &edit.bladeTaper, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Height variation", &edit.heightVariation, 0.0f, 0.9f);
        changed |= ImGui::SliderFloat("Rest lean (rad)", &edit.lean, 0.0f, 1.2f);
        changed |= ImGui::SliderFloat("Largest bend (rad)", &edit.maxBend, 0.1f, 1.5f);
        changed |= ImGui::SliderFloat("LOD1 width x", &edit.lod1WidthScale, 1.0f, 6.0f);
        changed |= ImGui::SliderFloat("LOD2 width x", &edit.lod2WidthScale, 1.0f, 12.0f);
    }
    if (ImGui::CollapsingHeader("Where grass grows", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Max slope (deg)", &edit.maxSlopeDeg, 1.0f, 89.0f);
        changed |= ImGui::SliderFloat("Slope fade (deg)", &edit.slopeFadeDeg, 0.0f, 30.0f);
        changed |= ImGui::SliderFloat("Water margin (m)", &edit.waterMargin, -1.0f, 5.0f);
        changed |= ImGui::SliderFloat("Waterline fade (m)", &edit.waterFadeBand, 0.0f, 6.0f);
    }
    if (ImGui::CollapsingHeader("LOD distances", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("LOD0 to (m)", &edit.lod0Distance, 5.0f, 120.0f);
        changed |= ImGui::SliderFloat("LOD1 to (m)", &edit.lod1Distance, 10.0f, 250.0f);
        changed |= ImGui::SliderFloat("LOD2 / cull (m)", &edit.lod2Distance, 20.0f, 360.0f);
        changed |= ImGui::SliderFloat("Far edge fade", &edit.edgeFade, 0.02f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Wind")) {
        changed |= ImGui::Checkbox("Follow the cloud wind", &edit.followCloudWind);
        changed |= ImGui::SliderFloat("Bend strength", &edit.windBend, 0.0f, 3.0f);
        changed |= ImGui::SliderFloat("Gust strength", &edit.gustStrength, 0.0f, 2.0f);
        changed |= ImGui::SliderFloat("Flutter speed (rad/s)", &edit.flutterSpeed, 0.0f, 40.0f);
        changed |= ImGui::SliderFloat("Flutter strength", &edit.flutterStrength, 0.0f, 3.0f);
        changed |= ImGui::SliderFloat("Wave length (m)", &edit.windWaveLengthM, 4.0f, 200.0f);
        changed |= ImGui::SliderFloat("Gust length (m)", &edit.windGustLengthM, 4.0f, 200.0f);
        ImGui::BeginDisabled(edit.followCloudWind);
        changed |= ImGui::SliderFloat("Own wind speed (m/s)", &edit.windSpeedFallback, 0.0f, 40.0f);
        changed |= ImGui::SliderFloat("Own wind direction (deg)", &edit.windDirectionDegFallback, -180.0f, 180.0f);
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Look")) {
        changed |= ImGui::ColorEdit3("Root colour", edit.colorRoot, ImGuiColorEditFlags_Float);
        changed |= ImGui::ColorEdit3("Tip colour", edit.colorTip, ImGuiColorEditFlags_Float);
        changed |= ImGui::SliderFloat("Tint variation", &edit.tintVariation, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Root darkening", &edit.rootAo, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Translucency", &edit.translucency, 0.0f, 3.0f);
        changed |= ImGui::SliderFloat("Sun visibility", &edit.sunVisibility, 0.0f, 1.5f);
    }
    if (ImGui::CollapsingHeader("Budget")) {
        int cells = static_cast<int>(edit.gridCells);
        if (ImGui::SliderInt("Chunk grid (per side)", &cells, 8, static_cast<int>(kGrassMaxGridCells))) {
            edit.gridCells = static_cast<std::uint32_t>(cells);
            changed = true;
        }
        const auto cap = [&](const char* label, std::uint32_t& value) {
            int v = static_cast<int>(value / 1000u);
            if (ImGui::SliderInt(label, &v, 10, 1500, "%d k")) {
                value = static_cast<std::uint32_t>(v) * 1000u;
                changed = true;
            }
        };
        cap("LOD0 capacity", edit.instanceCapLod0);
        cap("LOD1 capacity", edit.instanceCapLod1);
        cap("LOD2 capacity", edit.instanceCapLod2);
        ImGui::TextDisabled("Capacity changes reallocate the instance memory; do it with the view idle.");
    }

    if (ImGui::Button("Reset to defaults")) {
        edit = GrassSettings{};
        changed = true;
    }
    return changed;
}

} // namespace gte
