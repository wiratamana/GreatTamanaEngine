#include "TerrainPanel.h"

#include "../../../Editor/EditorContext.h"
#include "../../../Editor/EditorFrame.h"
#include "../../../Editor/ImGuiUniqueId.h"
#include "../../../Editor/ProjectRootPath.h"
#include "../TerrainApplyPolicy.h"
#include "../TerrainSettingsTable.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>

namespace gte {

namespace {

constexpr const char* kProfileNames[] = { "Full (nothing switched off)", "Scenic (no SSGI, GTAO)",
    "Lean (no volumetric light, SSGI, GTAO)", "Geometry only" };
constexpr terrain::TerrainProfile kProfiles[] = { terrain::TerrainProfile::Full, terrain::TerrainProfile::Scenic,
    terrain::TerrainProfile::Lean, terrain::TerrainProfile::GeometryOnly };
constexpr const char* kCameraNames[] = { "Script / scripted flight", "Free-fly", "Orbit" };
constexpr terrain::CameraMode kCameraModes[] = { terrain::CameraMode::Script, terrain::CameraMode::FreeFly, terrain::CameraMode::Orbit };

constexpr ImVec4 kGood{ 0.45f, 0.9f, 0.5f, 1.0f };
constexpr ImVec4 kBad{ 1.0f, 0.45f, 0.4f, 1.0f };
constexpr ImVec4 kDim{ 0.65f, 0.65f, 0.7f, 1.0f };
constexpr int kGiveUpFrames = 5;

void WrappedText(const ImVec4& color, const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void Hint(const char* tip)
{
    if (tip != nullptr && tip[0] != '\0' && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tip);
    }
}

// Draws the widget of one field; true when the user changed the value.
bool DrawField(terrain::TerrainSettings& settings, const terrain::FieldDesc& field, bool& active)
{
    bool changed = false;
    switch (field.kind) {
    case terrain::FieldKind::Float: {
        float value = settings.*field.floatMember;
        const ImGuiSliderFlags flags = field.logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None;
        changed = ImGui::SliderFloat(field.label, &value, static_cast<float>(field.min), static_cast<float>(field.max), field.format, flags);
        if (changed) {
            settings.*field.floatMember = value;
        }
        break;
    }
    case terrain::FieldKind::Int: {
        int value = settings.*field.intMember;
        changed = ImGui::SliderInt(field.label, &value, static_cast<int>(field.min), static_cast<int>(field.max), field.format);
        if (changed) {
            settings.*field.intMember = value;
        }
        break;
    }
    case terrain::FieldKind::Bool: {
        bool value = settings.*field.boolMember;
        changed = ImGui::Checkbox(field.label, &value);
        if (changed) {
            settings.*field.boolMember = value;
        }
        break;
    }
    case terrain::FieldKind::Seed: {
        std::uint32_t value = settings.*field.seedMember;
        std::uint32_t step = 1;
        changed = ImGui::InputScalar(field.label, ImGuiDataType_U32, &value, &step, nullptr, "%u");
        if (changed) {
            settings.*field.seedMember = value;
        }
        break;
    }
    case terrain::FieldKind::Choice: {
        const std::span<const std::int32_t> choices = terrain::ChunkQuadChoices();
        char preview[16];
        std::snprintf(preview, sizeof(preview), "%d", settings.*field.intMember);
        if (ImGui::BeginCombo(field.label, preview)) {
            for (std::size_t i = 0; i < choices.size(); ++i) {
                ScopedUniqueId choiceId(static_cast<int>(i), "TerrainPanel::Choice");
                char text[16];
                std::snprintf(text, sizeof(text), "%d", choices[i]);
                if (ImGui::Selectable(text, choices[i] == settings.*field.intMember)) {
                    settings.*field.intMember = choices[i];
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        break;
    }
    }
    active = active || ImGui::IsItemActive();
    Hint(field.tip);
    return changed;
}

} // namespace

void TerrainPanel::MarkEdited()
{
    m_editPending = true;
    m_secondsSinceEdit = 0.0;
}

void TerrainPanel::SyncWorking(const TerrainUiSnapshot& snap)
{
    if (!m_haveWorking) {
        m_working = snap.settings;
        m_runtime = snap.runtime;
        m_haveWorking = true;
    }
    if (!m_pathInitialised) {
        const std::string path = (ResolveProjectRootDirectory() / "terrain_world.gta").string();
        std::strncpy(m_path.data(), path.c_str(), m_path.size() - 1);
        m_pathInitialised = true;
    }
    if (!m_editPending) {
        // Follow the world: HTTP calls, a loaded asset or a rejected change all show up here.
        m_working = snap.settings;
        m_runtime = snap.runtime;
    }
}

void TerrainPanel::CommitEdits(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap)
{
    if (!m_editPending) {
        return;
    }
    m_secondsSinceEdit += static_cast<double>(ImGui::GetIO().DeltaTime);
    const terrain::TerrainSettings target = terrain::Sanitize(m_working);
    const terrain::SettingsChange pending = terrain::ClassifyChange(snap.settings, target);
    if (pending == terrain::SettingsChange::None) {
        m_editPending = false;
        m_sent.reset();
        return;
    }
    if (m_sent.has_value() && *m_sent == target) {
        // Sent already; the world answers one frame later. If it never takes it (rejected), show the truth.
        if (!m_dragging && ++m_framesSinceSent > kGiveUpFrames) {
            m_editPending = false;
            m_sent.reset();
        }
        return;
    }
    terrain::EditState state;
    state.pending = pending;
    state.dragging = m_dragging;
    state.livePreview = snap.livePreview;
    state.secondsSinceEdit = m_secondsSinceEdit;
    if (terrain::ShouldApplyNow(state)) {
        bridge.request.settings = target;
        m_sent = target;
        m_framesSinceSent = 0;
    }
}

void TerrainPanel::OnGui(EditorFrame& frame)
{
    TerrainUiBridge& bridge = frame.ctx.extensions.Get<TerrainUiBridge>();
    const TerrainUiSnapshot& snap = bridge.snapshot;
    if (!snap.valid) {
        ImGui::TextUnformatted("Waiting for the first frame ...");
        return;
    }
    SyncWorking(snap);
    m_dragging = false;

    DrawWorld(bridge, snap);
    DrawCamera(bridge, snap);
    DrawParameters();
    DrawRuntime(bridge, snap);
    DrawAsset(bridge, snap);
    DrawDebug(bridge, snap);
    DrawStats(snap);

    CommitEdits(bridge, snap);
}

void TerrainPanel::DrawWorld(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap)
{
    ImGui::SeparatorText("World");
    ImGui::TextColored(snap.open ? kGood : kDim, snap.open ? "Terrain scene: open" : "Terrain scene: closed");
    if (!snap.message.empty()) {
        WrappedText(snap.messageIsError ? kBad : kDim, snap.message.c_str());
    }
    if (!snap.busy.empty()) {
        WrappedText(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), snap.busy.c_str());
    }
    if (!snap.stats.lastError.empty()) {
        WrappedText(kBad, snap.stats.lastError.c_str());
    }

    ImGui::BeginDisabled(snap.open);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##TerrainProfile", &m_profileIndex, kProfileNames, static_cast<int>(std::size(kProfileNames)));
    Hint("Which render features stay on while the terrain scene is open.");
    ImGui::Checkbox("Lower shadow and atmosphere cost", &m_budget);
    ImGui::SameLine();
    ImGui::Checkbox("Water", &m_water);
    ImGui::EndDisabled();

    if (!snap.open) {
        if (ImGui::Button("Open terrain world")) {
            terrain::TerrainOpenOptions options;
            options.profile = kProfiles[std::clamp(m_profileIndex, 0, static_cast<int>(std::size(kProfiles)) - 1)];
            options.renderBudget = m_budget;
            options.water = m_water;
            bridge.request.settings = terrain::Sanitize(m_working);
            bridge.request.open = options;
            bridge.request.cameraMode = terrain::CameraMode::FreeFly; // Opened from the panel: hands on the camera.
            m_editPending = false;
        }
        Hint("Replaces the current scene with the streamed terrain.");
    } else if (ImGui::Button("Close terrain world")) {
        bridge.request.close = true;
    }

    ImGui::BeginDisabled(!snap.open);
    bool preview = snap.livePreview;
    if (ImGui::Checkbox("Live preview mode", &preview)) {
        bridge.request.livePreview = preview;
    }
    Hint("One 129x129 chunk that is rebuilt on every slider move. The big world is not rebuilt while this is on.");
    ImGui::EndDisabled();
}

void TerrainPanel::DrawCamera(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap)
{
    ImGui::SeparatorText("Camera");
    ImGui::BeginDisabled(!snap.open);
    int mode = 0;
    for (int i = 0; i < 3; ++i) {
        if (kCameraModes[i] == snap.cameraMode) {
            mode = i;
        }
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##TerrainCameraMode", &mode, kCameraNames, 3)) {
        bridge.request.cameraMode = kCameraModes[mode];
    }
    if (snap.cameraMode == terrain::CameraMode::FreeFly) {
        ImGui::TextColored(kDim, "Game view: hold right mouse and drag to look, WASD move, E/Q up/down,\nShift boost, wheel speed. Speed follows the height.");
    } else if (snap.cameraMode == terrain::CameraMode::Orbit) {
        ImGui::TextColored(kDim, "Game view: drag (left or right mouse) to orbit, wheel zoom, WASD move the centre.");
    }
    if (ImGui::Button("Start orbit flight")) {
        terrain::FlightSpec spec;
        spec.centerX = snap.stats.cameraX;
        spec.centerZ = snap.stats.cameraZ;
        spec.radius = 2500.0;
        bridge.request.flight = spec;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop flight")) {
        bridge.request.stopFlight = true;
    }
    ImGui::EndDisabled();
}

void TerrainPanel::DrawParameters()
{
    ImGui::SeparatorText("Parameters");
    const std::span<const terrain::FieldDesc> fields = terrain::SettingFields();
    for (const char* group : terrain::SettingGroups()) {
        const bool open = ImGui::CollapsingHeader(group, std::strcmp(group, "Seed") == 0 || std::strcmp(group, "Height and frequency") == 0
                ? ImGuiTreeNodeFlags_DefaultOpen
                : ImGuiTreeNodeFlags_None);
        if (!open) {
            continue;
        }
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const terrain::FieldDesc& field = fields[i];
            if (std::strcmp(field.group, group) != 0) {
                continue;
            }
            ScopedUniqueId id(static_cast<int>(i), "TerrainPanel::Field", field.name);
            if (DrawField(m_working, field, m_dragging)) {
                m_working = terrain::Sanitize(m_working);
                MarkEdited();
            }
        }
    }
    if (ImGui::Button("Randomize seed")) {
        std::random_device device;
        m_working.seed = device();
        MarkEdited();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset defaults")) {
        m_working = terrain::Sanitize(terrain::TerrainSettings{});
        MarkEdited();
    }
}

void TerrainPanel::DrawRuntime(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap)
{
    static_cast<void>(snap);
    ImGui::SeparatorText("Streaming");
    bool changed = false;
    changed |= ImGui::SliderInt("Max visible nodes", &m_runtime.maxVisibleNodes, 0, 1200);
    Hint("Cap of nodes in view; the LOD selector lowers detail to stay under it. 0 means no cap.");
    changed |= ImGui::SliderInt("Uploads per frame", &m_runtime.maxUploadsPerTick, 1, 32);
    changed |= ImGui::SliderInt("Generation jobs", &m_runtime.streamingMaxInFlight, 1, 16);
    if (changed) {
        bridge.request.runtime = m_runtime;
    }
}

void TerrainPanel::DrawAsset(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap)
{
    ImGui::SeparatorText("Terrain asset (.gta)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##TerrainAssetPath", m_path.data(), m_path.size());
    if (ImGui::SliderInt("Bake nodes per side", &m_runtime.bakeNodesPerSide, 1, 32)) {
        bridge.request.runtime = m_runtime;
    }
    const int side = m_runtime.bakeNodesPerSide;
    ImGui::TextColored(kDim, "%d x %d nodes, about %d MB. Saving blocks the editor for about %d ms.", side, side, side * side * 41 / 1000 + 1,
        side * side * 1);
    const bool busy = !snap.busy.empty();
    ImGui::BeginDisabled(busy || !snap.open);
    if (ImGui::Button("Save Terrain Asset")) {
        bridge.request.savePath = std::string(m_path.data());
    }
    Hint("Bakes the nodes around the camera (heights, normals, materials, bounds) into one file.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Load Terrain Asset")) {
        bridge.request.loadPath = std::string(m_path.data());
        m_editPending = false;
    }
    ImGui::EndDisabled();
}

void TerrainPanel::DrawDebug(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap)
{
    ImGui::SeparatorText("Debug");
    bool boxes = snap.debugAabb;
    if (ImGui::Checkbox("Show chunk bounding boxes", &boxes)) {
        bridge.request.debugAabb = boxes;
    }
    Hint("Draws every resident node box over the Game and Scene views. Also needed for GET /terrain/map.");
    int colorMode = bridge.overlayColorByState ? 0 : 1;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##TerrainBoxColor", &colorMode, "Colour by state (drawn / outside view / hidden)\0Colour by LOD level\0")) {
        bridge.overlayColorByState = colorMode == 0;
    }
    if (bridge.overlayColorByState) {
        ImGui::TextColored(ImVec4(0.27f, 0.9f, 0.4f, 1.0f), "green: drawn");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.94f, 0.35f, 0.27f, 1.0f), "red: outside view");
        ImGui::SameLine();
        ImGui::TextColored(kDim, "grey: hidden by a finer/coarser node");
    }
}

void TerrainPanel::DrawStats(const TerrainUiSnapshot& snap) const
{
    ImGui::SeparatorText("Stats");
    const terrain::TerrainStats& s = snap.stats;
    if (!snap.open) {
        ImGui::TextColored(kDim, "Open the terrain world to see live numbers.");
        return;
    }
    ImGui::Text("Camera: %s  (%.0f, %.0f, %.0f)  yaw %.0f  pitch %.0f  %.0f m/s", s.cameraMode.c_str(), s.cameraX, s.cameraY, s.cameraZ,
        s.cameraYawDegrees, s.cameraPitchDownDegrees, s.cameraSpeed);
    ImGui::Text("Nodes: %d resident, %d pending, %d wanted, %d in view", s.nodesResident, s.nodesPending, s.nodesWanted, s.nodesInView);
    ImGui::Text("Pool slots: %d used, %d enabled, %d total", s.slotsUsed, s.slotsEnabled, s.slotsTotal);
    ImGui::Text("Triangles in enabled nodes: %.2f M", static_cast<double>(s.trianglesEnabled) / 1.0e6);
    if (s.gpuCullValid) {
        ImGui::Text("GPU cull (%s, hi-z %s):", s.gpuCullMode.c_str(), s.gpuOcclusion.c_str());
        ImGui::Text("  drawn %llu   frustum culled %llu   Hi-Z culled %llu", static_cast<unsigned long long>(s.gpuDrawnNodes),
            static_cast<unsigned long long>(s.gpuFrustumCulledNodes), static_cast<unsigned long long>(s.gpuHiZCulledNodes));
        ImGui::Text("  triangles drawn: %.2f M", static_cast<double>(s.gpuTrianglesDrawn) / 1.0e6);
    } else {
        ImGui::TextColored(kDim, "GPU cull counters: not available yet");
    }
    ImGui::Text("Memory: %.1f MB CPU, %.1f MB GPU", s.residentMbCpu, s.residentMbGpu);
    ImGui::Text("Generation: %.2f ms average, %.2f ms max, %.1f nodes/s", s.generationMsAverage, s.generationMsMax, s.generatedPerSecond);
    ImGui::Text("Main thread: tick %.2f ms (max %.2f), select %.3f ms", s.tickCpuMs, s.tickCpuMsMax, s.selectMs);
    if (s.livePreview) {
        ImGui::Text("Preview rebuild: %.1f ms (%llu rebuilds)", s.previewBuildMs, static_cast<unsigned long long>(s.previewRebuilds));
    }
    if (s.lodDistanceScale < 0.999) {
        ImGui::TextColored(kBad, "Visible cap is lowering detail (LOD scale %.2f)", s.lodDistanceScale);
    }
}

} // namespace gte
