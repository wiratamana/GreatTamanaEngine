#include "GpuDrivenScenePanel.h"

#include "../GpuDrivenSceneTypes.h"
#include "../../../Editor/EditorContext.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <string>

namespace gte {

namespace {

constexpr std::array<const char*, kGpuDrivenSceneDebugViewTable.size()> kDebugViews{ "Off", "Cluster Id" };

std::string Name(std::string_view text)
{
    return std::string(text);
}

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
        if (const auto hidden = fx::HiddenClusterCount(v.testedAtVisibleFrame, v.visibleClusters)) {
            ImGui::Text("Clusters hidden: %llu", static_cast<unsigned long long>(*hidden));
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
        ImGui::TextDisabled("Occlusion: %s (%s)", Name(fx::EnumName(fx::kOcclusionStateTable, v.occlusion)).c_str(),
            Name(fx::EnumName(fx::kHizReasonTable, v.hizReason)).c_str());
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

} // namespace

bool BuildGpuDrivenScenePanel(EditorContext& /*ctx*/, GpuDrivenSceneSettings& edit, bool supported,
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
    ImGui::TextDisabled("Culling: %s, cone %s, source %s", Name(fx::EnumName(fx::kViewCullChoiceTable, edit.sceneCull)).c_str(),
        edit.sceneCone ? "on" : "off", Name(fx::EnumName(fx::kViewCullSourceTable, edit.sceneSource)).c_str());

    ImGui::SeparatorText("Debug");
    changed |= ImGui::Checkbox("Count visible clusters (extra passes)", &edit.collectStats);
    int view = static_cast<int>(edit.debugView);
    if (ImGui::Combo("View", &view, kDebugViews.data(), static_cast<int>(kDebugViews.size()))) {
        edit.debugView = static_cast<GpuDrivenSceneDebugView>(std::clamp(view, 0, static_cast<int>(kDebugViews.size()) - 1));
        changed = true;
    }

    ImGui::SeparatorText("Live info");
    DrawViewInfo("Game view", game);
    DrawViewInfo("Scene view", scene);

    if (ImGui::Button("Reset to defaults")) {
        edit = GpuDrivenSceneSettings{};
        changed = true;
    }
    return changed;
}

} // namespace gte
