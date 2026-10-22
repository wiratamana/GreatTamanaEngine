#include "RenderFeaturesPanel.h"

#include "../EditorContext.h"
#include "../../Core/Plugins/RenderFeatureCompositor.h"
#include "../ImGuiUniqueId.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace gte {

namespace {

// One row per registered render-feature entry: an "Enabled" checkbox and an editable priority field.
// Both edit `renderFeatureCompositor` directly; with a null compositor the widgets render but edits are no-ops.
void BuildRenderFeaturesSection(
    const std::vector<RenderFeatureDebugEntry>& entries, RenderFeatureCompositor* renderFeatureCompositor)
{
    ImGui::SeparatorText("Render Features");
    if (entries.empty()) {
        ImGui::TextDisabled("No render features are currently registered.");
        return;
    }

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const RenderFeatureDebugEntry& entry = entries[i];
        ScopedUniqueId idScope(static_cast<int>(i), "RenderFeaturesPanel::BuildRenderFeaturesSection", entry.name.c_str());

        bool enabled = entry.enabled;
        if (ImGui::Checkbox("##FeatureEnabled", &enabled) && renderFeatureCompositor != nullptr) {
            renderFeatureCompositor->SetFeatureEnabled(entry.name, enabled);
        }
        ImGui::SameLine();

        int priority = entry.priority;
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("##FeaturePriority", &priority) && renderFeatureCompositor != nullptr) {
            renderFeatureCompositor->SetFeaturePriority(entry.name, priority);
        }
        ImGui::SameLine();

        ImGui::Text("[%s] %s - blend %s%s", entry.stage.c_str(), entry.name.c_str(), entry.blendMode.c_str(),
            entry.enabled ? "" : " (DISABLED)");

        if (entry.isProjectFeature) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.5f, 1.0f), "[Project]");
        }
    }
}

} // namespace

void RenderFeaturesPanel::Build(EditorContext& ctx, const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
    RenderFeatureCompositor* renderFeatureCompositor)
{
    if (!ctx.renderFeaturesWindowOpen) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Render Features", &ctx.renderFeaturesWindowOpen)) {
        ImGui::End();
        return;
    }

    BuildRenderFeaturesSection(renderFeatureEntries, renderFeatureCompositor);

    ImGui::End();
}

} // namespace gte
