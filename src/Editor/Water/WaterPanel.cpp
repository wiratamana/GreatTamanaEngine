#include "WaterPanel.h"

#include "WaterUiBridge.h"
#include "../EditorContext.h"
#include "../EditorFrame.h"
#include "../ImGuiUniqueId.h"
#include "../../Water/WaterEdit.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gte {

namespace {

constexpr ImVec4 kGood{ 0.45f, 0.9f, 0.5f, 1.0f };
constexpr ImVec4 kBad{ 1.0f, 0.45f, 0.4f, 1.0f };
constexpr ImVec4 kDim{ 0.65f, 0.65f, 0.7f, 1.0f };
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

void Hint(const char* tip)
{
    if (tip != nullptr && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tip);
    }
}

bool Slider(const char* label, float& value, float lo, float hi, const char* format, const char* tip, bool log = false)
{
    const bool changed = ImGui::SliderFloat(label, &value, lo, hi, format, log ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
    Hint(tip);
    return changed;
}

bool Color(const char* label, float (&rgb)[3], const char* tip)
{
    const bool changed = ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_Float);
    Hint(tip);
    return changed;
}

void Report(WaterUiBridge& ui, const water::WaterResult& r)
{
    ui.status = r.message;
    ui.statusIsError = !r.ok;
}

const char* KindLabel(water::WaterBodyKind kind)
{
    switch (kind) {
    case water::WaterBodyKind::Ocean: return "Ocean";
    case water::WaterBodyKind::Lake: return "Lake";
    default: return "River";
    }
}

} // namespace

void WaterPanel::OnGui(EditorFrame& frame)
{
    WaterUiBridge& ui = frame.ctx.waterUi;
    if (ui.world == nullptr) {
        ImGui::TextUnformatted("The water world is not ready yet ...");
        return;
    }
    DrawStatus(ui);
    DrawBodyList(ui, frame.ctx);
    DrawSelectedBody(ui);
    DrawSettings(ui);
    DrawFile(ui);
}

void WaterPanel::DrawStatus(WaterUiBridge& ui)
{
    const water::WaterStats stats = ui.world->Stats();
    ImGui::SeparatorText("Status");
    ImGui::TextColored(stats.featureConnected ? kGood : kBad, stats.featureConnected ? "Water render feature: connected" : "Water render feature: not connected");
    Hint("Bodies and look are sent to the Water render feature once per edit. Without it the old flat ocean quad stays.");
    ImGui::Text("%d bodies, revision %llu", stats.bodyCount, static_cast<unsigned long long>(stats.revision));
    if (stats.carvePending) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Terrain carve waits for the edit to rest ...");
    }
    ImGui::TextColored(kDim, "Carve applied %d times, last %d dirty areas", stats.carveApplies, stats.carveDirtyRects);
    if (!stats.lastError.empty()) {
        ImGui::TextColored(kBad, "%s", stats.lastError.c_str());
    }
    for (const std::string& issue : stats.issues) {
        ImGui::TextColored(kBad, "%s", issue.c_str());
    }
    if (!ui.status.empty()) {
        ImGui::TextColored(ui.statusIsError ? kBad : kDim, "%s", ui.status.c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("Gizmo: %d handles, pick %d, hot %d, drag %d, free %d, hover %d, mouse %.0f,%.0f", ui.debugHandles,
        ui.debugPick, ui.handleHot ? 1 : 0, ui.dragging ? 1 : 0, ui.debugFree ? 1 : 0, ui.debugHovered ? 1 : 0, ui.debugMouse.x,
        ui.debugMouse.y);
    ImGui::PopStyleColor();
}

void WaterPanel::AddBodyHere(WaterUiBridge& ui, const EditorContext& ctx, bool river)
{
    const terrain::TerrainStats& cam = ctx.terrainUi.snapshot.stats;
    const double yaw = cam.cameraYawDegrees * kDegToRad;
    const double fx = std::sin(yaw);
    const double fz = std::cos(yaw);
    const double cx = cam.cameraX + fx * 300.0;
    const double cz = cam.cameraZ + fz * 300.0;
    const auto ground = [&ui](double x, double z) {
        const auto h = ui.world->TerrainHeight(x, z);
        return h ? static_cast<float>(h->baseHeight) : 0.0f;
    };
    if (river) {
        const double sx = -fz;
        const double sz = fx;
        std::vector<water::RiverPoint> points;
        for (int i = 0; i < 5; ++i) {
            const double t = (i - 2) * 80.0;
            const double bend = ((i % 2) ? 20.0 : -20.0);
            points.push_back({ cx + sx * t + fx * bend, cz + sz * t + fz * bend, 0.0f, 14.0f });
        }
        water::WaterBody body = water::MakeRiverBody("", std::move(points));
        water::AutoRiverLevels(body, ground, 1.0f);
        Report(ui, ui.world->AddBody(body));
    } else {
        water::WaterBody body = water::MakeBoxLake("", cx, cz, 220.0, 160.0, 0.0f);
        body.level = water::SuggestLakeLevel(body, ground);
        Report(ui, ui.world->AddBody(body));
    }
    const auto set = ui.world->Bodies();
    if (!set->Bodies().empty()) {
        ui.selectedId = set->Bodies().back().id;
        ui.selectedPoint = -1;
    }
}

void WaterPanel::DrawBodyList(WaterUiBridge& ui, const EditorContext& ctx)
{
    ImGui::SeparatorText("Bodies");
    const auto set = ui.world->Bodies();
    for (std::size_t i = 0; i < set->Bodies().size(); ++i) {
        const water::WaterBody& b = set->Bodies()[i];
        ScopedUniqueId rowId(static_cast<int>(i), "WaterPanel::BodyRow", b.name.c_str());
        char label[160];
        std::snprintf(label, sizeof(label), "%s: %s", KindLabel(b.kind), b.name.c_str());
        if (ImGui::Selectable(label, b.id == ui.selectedId)) {
            ui.selectedId = b.id;
            ui.selectedPoint = -1;
        }
    }
    if (ImGui::Button("Add ocean")) {
        Report(ui, ui.world->AddBody(water::MakeOceanBody(ui.world->Settings().seaLevel)));
    }
    Hint("One ocean: an endless plane at the sea level below.");
    ImGui::SameLine();
    if (ImGui::Button("Add lake here")) {
        AddBodyHere(ui, ctx, false);
    }
    Hint("A box lake 300 m in front of the terrain camera; the level is picked from the ground along its outline.");
    ImGui::SameLine();
    if (ImGui::Button("Add river here")) {
        AddBodyHere(ui, ctx, true);
    }
    Hint("A river across the view; the surface follows the ground and never rises downstream.");
    ImGui::BeginDisabled(ui.selectedId == 0);
    if (ImGui::Button("Remove selected")) {
        Report(ui, ui.world->RemoveBody(ui.selectedId));
        ui.selectedId = 0;
        ui.selectedPoint = -1;
        ui.world->FlushCarve();
    }
    ImGui::EndDisabled();
    ImGui::Checkbox("Handles in the Game and Scene views", &ui.gizmos);
    Hint("Drag a handle to move it. Drag a white square to add a point. Ctrl+click an outline to add a point. Delete removes the selected point.");
}

void WaterPanel::DrawSelectedBody(WaterUiBridge& ui)
{
    const auto set = ui.world->Bodies();
    const water::WaterBody* current = set->Find(ui.selectedId);
    if (current == nullptr) {
        return;
    }
    ImGui::SeparatorText("Selected body");
    water::WaterBody b = *current;
    bool changed = false;

    if (m_nameFor != b.id) {
        std::snprintf(m_name.data(), m_name.size(), "%s", b.name.c_str());
        m_nameFor = b.id;
    }
    if (ImGui::InputText("Name", m_name.data(), m_name.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
        b.name = m_name.data();
        changed = true;
    }
    ImGui::TextColored(kDim, "%s, id %u", KindLabel(b.kind), b.id);

    if (b.kind == water::WaterBodyKind::Lake) {
        changed |= Slider("Water level", b.level, -300.0f, 1500.0f, "%.2f m", "Height of the flat lake surface.");
    }
    if (b.kind != water::WaterBodyKind::Ocean) {
        changed |= Slider("Depth", b.depth, 0.5f, 60.0f, "%.1f m", "How deep the bed is carved below the surface.");
        changed |= Slider("Shore width", b.shoreWidth, 1.0f, 120.0f, "%.1f m", "Lake: distance from the edge to the full depth.");
        changed |= Slider("Bank width", b.bankWidth, 1.0f, 200.0f, "%.1f m", "Distance outside the edge where the land is pulled down.");
        changed |= ImGui::Checkbox("Carve terrain", &b.carveTerrain);
        Hint("Off: the terrain is left alone and only the surface is drawn.");
        if (b.kind == water::WaterBodyKind::Lake) {
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Seal rim", &b.sealRim);
            Hint("Raises low ground around the lake so the water cannot spill over it.");
            changed |= Slider("Rim height", b.rimHeight, 0.0f, 10.0f, "%.2f m", "Dam height above the surface.");
        }
    }
    changed |= Slider("Wave scale", b.look.waveScale, 0.0f, 2.0f, "%.2f", "Multiplies the global wave height on this body.");

    if (b.kind == water::WaterBodyKind::Lake) {
        if (ImGui::Button("Suggest level")) {
            const auto ground = [&ui](double x, double z) {
                const auto h = ui.world->TerrainHeight(x, z);
                return h ? static_cast<float>(h->baseHeight) : 0.0f;
            };
            b.level = water::SuggestLakeLevel(b, ground);
            changed = true;
        }
        Hint("A low percentile of the ground along the outline, so most of the outline stays dry.");
        ImGui::SameLine();
        if (ImGui::Button("Check leaks")) {
            const auto ground = [&ui](double x, double z) {
                const auto h = ui.world->TerrainHeight(x, z);
                return h ? static_cast<float>(h->baseHeight) : 0.0f;
            };
            const water::LakeLeakReport r = water::CheckLakeLeaks(b, ground);
            char text[160];
            std::snprintf(text, sizeof(text), "%d of %d outline samples lie below the level (ground %.1f .. %.1f m)", r.below, r.samples, r.lowest, r.highest);
            ui.status = text;
            ui.statusIsError = r.below > 0 && !b.sealRim;
        }
    } else if (b.kind == water::WaterBodyKind::River) {
        if (ImGui::Button("Heights from ground")) {
            const auto ground = [&ui](double x, double z) {
                const auto h = ui.world->TerrainHeight(x, z);
                return h ? static_cast<float>(h->baseHeight) : 0.0f;
            };
            water::AutoRiverLevels(b, ground, 1.0f);
            changed = true;
        }
        Hint("Puts each point 1 m under the ground, then makes the surface never rise downstream.");
    }

    if (b.kind != water::WaterBodyKind::Ocean) {
        ImGui::SeparatorText("Points");
        const int count = b.kind == water::WaterBodyKind::Lake ? static_cast<int>(b.polygon.size()) : static_cast<int>(b.river.size());
        for (int i = 0; i < count; ++i) {
            ScopedUniqueId pointId(i, "WaterPanel::Point");
            char label[64];
            std::snprintf(label, sizeof(label), "%s %d", b.kind == water::WaterBodyKind::Lake ? "Corner" : "Point", i);
            if (ImGui::Selectable(label, ui.selectedPoint == i)) {
                ui.selectedPoint = i;
            }
        }
        if (ui.selectedPoint >= 0 && ui.selectedPoint < count) {
            if (b.kind == water::WaterBodyKind::River) {
                water::RiverPoint& p = b.river[static_cast<std::size_t>(ui.selectedPoint)];
                changed |= Slider("Surface height", p.surfaceY, -300.0f, 1500.0f, "%.2f m", "Water height at this point.");
                changed |= Slider("Width", p.width, 1.0f, 200.0f, "%.1f m", "Full river width at this point.");
            }
            if (ImGui::Button("Remove point")) {
                Report(ui, ui.world->RemovePoint(b.id, ui.selectedPoint));
                ui.selectedPoint = -1;
                ui.world->FlushCarve();
                return;
            }
        }
    }

    if (ImGui::TreeNode("Colour override")) {
        changed |= ImGui::Checkbox("Use own colours", &b.look.overrideColors);
        ImGui::BeginDisabled(!b.look.overrideColors);
        changed |= Color("Shallow", b.look.shallowColor, nullptr);
        changed |= Color("Deep", b.look.deepColor, nullptr);
        changed |= Color("Foam", b.look.foamColor, nullptr);
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    if (changed) {
        Report(ui, ui.world->UpdateBody(b));
    }
    // The terrain rebuild of a slider drag is debounced; releasing the slider applies it at once.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ui.dragging) {
        ui.world->FlushCarve();
    }
}

void WaterPanel::DrawSettings(WaterUiBridge& ui)
{
    water::WaterSettings s = ui.world->Settings();
    bool changed = false;

    ImGui::SeparatorText("Waves");
    changed |= Slider("Wave height", s.waveHeight, 0.0f, 12.0f, "%.2f m", "Crest to trough height of the biggest waves.");
    changed |= Slider("Choppiness", s.choppiness, 0.0f, 1.0f, "%.2f", "0 is round smooth waves, 1 is sharp crests.");
    changed |= Slider("Speed", s.waveSpeed, 0.0f, 4.0f, "%.2f", "Multiplier of the wave speed.");
    changed |= Slider("Wind direction", s.windDirectionDeg, -360.0f, 360.0f, "%.0f deg", "0 is +X, 90 is +Z.");
    changed |= Slider("Longest wave", s.waveLength, 4.0f, 400.0f, "%.0f m", "Wave length of the biggest wave.", true);

    ImGui::SeparatorText("Colour");
    changed |= Color("Shallow", s.shallowColor, "Colour of the water where it is thin.");
    changed |= Color("Deep", s.deepColor, "Colour of the water where it is deep.");
    changed |= Color("Foam", s.foamColor, "Colour of the foam on the shore and the crests.");
    changed |= Slider("Clarity", s.clarity, 0.5f, 200.0f, "%.1f m", "Water depth at which the seabed is no longer seen.", true);
    changed |= Slider("Foam", s.foamAmount, 0.0f, 2.0f, "%.2f", "Amount of shore and crest foam.");

    ImGui::SeparatorText("Wet shore");
    changed |= Slider("Wet band", s.wetBand, 0.0f, 10.0f, "%.2f m", "Height above the water that the land stays wet.");
    changed |= Slider("Wet darkening", s.wetDarken, 0.0f, 1.0f, "%.2f", "How much darker the wet land is.");

    ImGui::SeparatorText("Performance");
    static const char* kQualityNames[] = { "Low (2 waves, no SSR)", "Medium (4 waves, no SSR)", "High (8 waves, SSR)" };
    int quality = static_cast<int>(s.quality);
    if (ImGui::Combo("Quality preset", &quality, kQualityNames, 3)) {
        s = water::ApplyQuality(s, static_cast<water::WaterQuality>(quality));
        changed = true;
    }
    Hint("Sets the wave count and the screen-space reflections together.");
    changed |= ImGui::Checkbox("Screen-space reflections", &s.ssrEnabled);
    Hint("Reflects the scene on the water. Costs GPU time; the sky is the fallback.");
    int waves = s.waveCount;
    if (ImGui::SliderInt("Wave count", &waves, 1, 8)) {
        s.waveCount = waves;
        changed = true;
    }

    ImGui::SeparatorText("Terrain");
    changed |= ImGui::Checkbox("Bodies carve the terrain", &s.carveTerrain);
    changed |= ImGui::Checkbox("Hide the old flat ocean quad", &s.hideOceanQuad);
    Hint("The quad writes depth over the seabed and costs shadow time. It stays only when the Water feature is missing.");
    float debounce = static_cast<float>(ui.world->CarveDebounce());
    if (Slider("Carve delay", debounce, 0.0f, 2.0f, "%.2f s", "How long an edit must rest before the terrain is rebuilt.")) {
        ui.world->SetCarveDebounce(debounce);
    }
    if (ImGui::Button("Reset water settings")) {
        s = water::WaterSettings{};
        changed = true;
    }

    if (changed) {
        Report(ui, ui.world->SetSettings(s));
    }
}

void WaterPanel::DrawFile(WaterUiBridge& ui)
{
    ImGui::SeparatorText("File");
    if (!m_pathInitialised) {
        std::snprintf(m_path.data(), m_path.size(), "%s", "water.json");
        m_pathInitialised = true;
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##WaterPath", m_path.data(), m_path.size());
    if (ImGui::Button("Save water")) {
        Report(ui, ui.world->SaveFile(m_path.data()));
    }
    ImGui::SameLine();
    if (ImGui::Button("Load water")) {
        Report(ui, ui.world->LoadFile(m_path.data()));
        ui.selectedId = 0;
        ui.selectedPoint = -1;
        ui.world->FlushCarve();
    }
    Hint("Bodies and settings as JSON. The terrain scene file does not keep them.");
}

} // namespace gte
