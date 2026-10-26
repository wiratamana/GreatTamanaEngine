#include "GpuDrivenScenePanel.h"

#include "../GpuDrivenSceneTypes.h"

#include <imgui.h>

#include <string>

namespace gte {

namespace {

void DrawViewInfo(const char* label, const GpuDrivenSceneViewSnapshot& v)
{
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    if (!v.rendered) {
        ImGui::TextDisabled("Not rendered");
        ImGui::PopID();
        return;
    }
    ImGui::Text("Claimed entities: %u (parts %u)", v.claimedEntities, v.parts);
    ImGui::Text("Clusters tested: %llu", static_cast<unsigned long long>(v.testedClusters));
    if (v.visibleValid) {
        ImGui::Text("Clusters visible: %u (frame %llu)", v.visibleClusters, static_cast<unsigned long long>(v.visibleFrame));
        if (v.hiddenValid) {
            ImGui::Text("Clusters hidden: %llu", static_cast<unsigned long long>(v.hidden));
        } else {
            ImGui::TextDisabled("Clusters hidden: counts are settling");
        }
    } else if (!v.statsCollected) {
        ImGui::TextDisabled("Clusters visible: stats are off");
    } else {
        ImGui::TextDisabled("Clusters visible: no data yet");
    }
    ImGui::Text("Buffer memory: %.1f MB", static_cast<double>(v.ledgerBytes) / (1024.0 * 1024.0));
    if (v.occlusion == fx::OcclusionState::Applied) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Occlusion culling active");
    } else if (v.occlusion != fx::OcclusionState::NotRequested) {
        const std::string state(fx::EnumName(fx::kOcclusionStateTable, v.occlusion));
        const std::string reason(fx::EnumName(fx::kHizReasonTable, v.hizReason));
        ImGui::TextDisabled("Occlusion: %s (%s)", state.c_str(), reason.c_str());
    }
    if (v.frozen) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Culling camera is frozen (frame %llu)",
            static_cast<unsigned long long>(v.frozenFrame));
    }
    if (!v.refusal.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Not drawing on the GPU: %s", v.refusal.c_str());
    }
    ImGui::PopID();
}

// Combo over the debug view table. Returns true when the selection changed.
bool DrawDebugViewCombo(GpuDrivenSceneDebugView& current)
{
    bool changed = false;
    const std::string currentName(fx::EnumName(kGpuDrivenSceneDebugViewTable, current));
    if (ImGui::BeginCombo("View", currentName.c_str())) {
        for (const auto& entry : kGpuDrivenSceneDebugViewTable) {
            const std::string name(entry.name);
            if (ImGui::Selectable(name.c_str(), entry.value == current)) {
                current = entry.value;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

bool BuildGpuDrivenScenePanel(GpuDrivenSceneSettings& edit, bool supported,
    const GpuDrivenSceneViewSnapshot& game, const GpuDrivenSceneViewSnapshot& scene)
{
    bool changed = false;

    if (supported) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Device supports the cluster draw");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Device cannot run the cluster draw; the CPU draw is used");
    }

    ImGui::SeparatorText("Culling");
    changed |= ImGui::Checkbox("Enabled", &edit.enabled);
    changed |= ImGui::Checkbox("Frustum culling", &edit.frustum);
    changed |= ImGui::Checkbox("Back-face cone culling", &edit.cone);
    if (edit.cone) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Only correct for closed meshes. Open surfaces lose their back faces.");
    }
    changed |= ImGui::Checkbox("Freeze culling camera (both views)", &edit.freezeCulling);
    changed |= ImGui::Checkbox("Occlusion culling (Hi-Z)", &edit.occlusion);
    if (edit.occlusion) {
        ImGui::TextDisabled("Uses last frame's depth. Off after cuts and resizes; moved instances are never culled.");
    }

    ImGui::SeparatorText("Scene view (set in the Scene panel)");
    const std::string choice(fx::EnumName(fx::kViewCullChoiceTable, edit.sceneCull));
    const std::string source(fx::EnumName(fx::kViewCullSourceTable, edit.sceneSource));
    const std::string ghost(fx::EnumName(fx::kGhostStyleTable, edit.ghost));
    ImGui::TextDisabled("Culling %s, cone %s, source %s, ghost %s, freeze serial %u", choice.c_str(),
        edit.sceneCone ? "on" : "off", source.c_str(), ghost.c_str(), edit.freezeSerial);

    ImGui::SeparatorText("Debug");
    changed |= ImGui::Checkbox("Count visible clusters (extra passes)", &edit.collectStats);
    changed |= DrawDebugViewCombo(edit.debugView);

    ImGui::SeparatorText("Live info");
    DrawViewInfo("Game view", game);
    DrawViewInfo("Scene view", scene);

    if (ImGui::Button("Reset to defaults")) {
        edit = DefaultsKeepingSerial(edit);
        changed = true;
    }
    return changed;
}

} // namespace gte
