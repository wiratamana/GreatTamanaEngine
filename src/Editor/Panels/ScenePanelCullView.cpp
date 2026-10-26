#include "ScenePanelCullView.h"

#include "../EditorContext.h"
#include "../ImGuiUniqueId.h"
#include "../ViewCullHud.h"
#include "../ViewCullLayout.h"
#include "../../Core/FeatureKit/ViewCullLink.h"

#include <array>
#include <cfloat>
#include <cstdint>
#include <optional>

namespace gte {

namespace {

constexpr const char* kHizTooltip = "Hi-Z uses the Scene camera depth. It cannot represent another camera yet.";
constexpr const char* kFollowTooltip =
    "Follows the tests of the GPU Driven Scene panel, which also drive the Game view.";

struct ChoiceLabel {
    fx::ViewCullChoice choice;
    const char* label;
};
constexpr std::array kChoices{
    ChoiceLabel{ fx::ViewCullChoice::FollowSettings, "Follow settings" },
    ChoiceLabel{ fx::ViewCullChoice::Off, "Off (no culling)" },
    ChoiceLabel{ fx::ViewCullChoice::Frustum, "Frustum" },
    ChoiceLabel{ fx::ViewCullChoice::FrustumHiz, "Frustum + Hi-Z" },
};

const char* ChoiceText(fx::ViewCullChoice choice) noexcept
{
    for (const ChoiceLabel& entry : kChoices) {
        if (entry.choice == choice) {
            return entry.label;
        }
    }
    return kChoices[0].label;
}

const char* ModeText(fx::ViewCullMode mode) noexcept
{
    switch (mode) {
    case fx::ViewCullMode::Off: return "Off (no culling)";
    case fx::ViewCullMode::Frustum: return "Frustum";
    case fx::ViewCullMode::FrustumHiz: return "Frustum + Hi-Z";
    case fx::ViewCullMode::Custom: return "Custom (set elsewhere)";
    }
    return "Custom (set elsewhere)";
}

ImVec4 ToneColor(ViewCullHudTone tone) noexcept
{
    switch (tone) {
    case ViewCullHudTone::Dim: return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
    case ViewCullHudTone::Good: return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
    case ViewCullHudTone::Warn: return ImVec4(1.0f, 0.65f, 0.2f, 1.0f);
    case ViewCullHudTone::Normal: break;
    }
    return ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
}

void DrawChoiceCombo(fx::ViewCullLink& link)
{
    const fx::ViewCullReport& report = link.report;
    const bool custom = report.choice == fx::ViewCullChoice::FollowSettings && report.mode == fx::ViewCullMode::Custom;
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo("##cullChoice", custom ? ModeText(report.mode) : ChoiceText(report.choice))) {
        return;
    }
    const std::array<ViewCullChoiceOption, 4> options = ViewCullChoiceOptions(report);
    for (std::size_t i = 0; i < options.size(); ++i) {
        const ViewCullChoiceOption& option = options[i];
        ScopedUniqueId idScope(static_cast<int>(i), "ScenePanelCullView::Choice");
        ImGui::BeginDisabled(!option.enabled);
        const bool picked = ImGui::Selectable(ChoiceText(option.choice), option.choice == report.choice);
        ImGui::EndDisabled();
        if (option.choice == fx::ViewCullChoice::FollowSettings && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", kFollowTooltip);
        } else if (!option.enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", kHizTooltip);
        }
        if (picked && option.enabled) {
            BeginViewCullChange(link).choice = option.choice;
        }
    }
    if (custom) {
        ImGui::BeginDisabled();
        ImGui::Selectable(ModeText(fx::ViewCullMode::Custom), true);
        ImGui::EndDisabled();
    }
    ImGui::EndCombo();
}

void DrawSourceRow(fx::ViewCullLink& link)
{
    const fx::ViewCullReport& report = link.report;
    const bool hizChosen = report.choice == fx::ViewCullChoice::FrustumHiz;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Cull from");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::BeginCombo("##cullSource", report.source == fx::ViewCullSource::Frozen ? "Frozen" : "Scene camera")) {
        if (ImGui::Selectable("Scene camera", report.source == fx::ViewCullSource::Live)) {
            BeginViewCullChange(link).source = fx::ViewCullSource::Live;
        }
        ImGui::BeginDisabled(hizChosen);
        const bool frozenPicked = ImGui::Selectable("Frozen", report.source == fx::ViewCullSource::Frozen);
        ImGui::EndDisabled();
        if (hizChosen && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", kHizTooltip);
        }
        if (frozenPicked && !hizChosen) {
            BeginViewCullChange(link).source = fx::ViewCullSource::Frozen;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(hizChosen);
    if (ImGui::Button("Freeze here")) {
        BeginViewCullChange(link).freezeHere = true;
    }
    ImGui::EndDisabled();
    if (hizChosen && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", kHizTooltip);
    }
}

void DrawConeCheckbox(fx::ViewCullLink& link)
{
    const fx::ViewCullReport& report = link.report;
    const bool used = report.choice == fx::ViewCullChoice::Frustum || report.choice == fx::ViewCullChoice::FrustumHiz;
    bool cone = report.cone;
    ImGui::BeginDisabled(!used);
    if (ImGui::Checkbox("Back-face cone", &cone)) {
        BeginViewCullChange(link).cone = cone;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Closed meshes only. Used with Frustum and Frustum + Hi-Z.");
    }
}

void DrawHud(fx::ViewCullLink& link)
{
    link.request.hudShown = true;
    for (const ViewCullHudLine& line : FormatViewCullHud(link.report)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ToneColor(line.tone));
        ImGui::TextWrapped("%s", line.text.c_str());
        ImGui::PopStyleColor();
    }
}

} // namespace

bool BuildScenePanelCullView(EditorContext& ctx, ImVec2 imageMin, ImVec2 imageSize, float switcherBottom)
{
    ScenePanelCullViewUi& ui = ctx.cullViewUi;
    ui.stale = ctx.viewCull && ctx.viewCull->report.valid
        && ui.freshness.Update(ctx.viewCull->report.frame, static_cast<std::uint64_t>(ImGui::GetFrameCount()));
    if (ui.stale || !ctx.viewCull || !ctx.viewCull->report.valid || !ctx.viewCull->report.rendered) {
        return false;
    }
    const std::optional<ViewCullBox> box =
        PlaceViewCullBox(Vec2(imageMin.x, imageMin.y), Vec2(imageSize.x, imageSize.y), switcherBottom);
    if (!box) {
        return false;
    }
    fx::ViewCullLink& link = *ctx.viewCull;

    ImGui::SetCursorScreenPos(ImVec2(box->min.x, box->min.y));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, box->maxHeight));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.09f, 0.78f));
    const ImGuiChildFlags childFlags = ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders;
    if (ImGui::BeginChild("##SceneCullView", ImVec2(box->width, 0.0f), childFlags, ImGuiWindowFlags_NoScrollWithMouse)) {
        ScopedUniqueId idScope(0, "ScenePanelCullView::Build");
        const bool resetPending = link.request.pending && link.request.resetAll;
        bool on = ui.open || (!ViewCullReportIsDefault(link.report) && !resetPending);
        if (ImGui::Checkbox("Cull view", &on)) {
            ui.open = on;
            if (!on) {
                link.request.pending = true;
                link.request.resetAll = true;
            }
        }
        if (on) {
            DrawChoiceCombo(link);
            DrawSourceRow(link);
            DrawConeCheckbox(link);
            ImGui::Separator();
            DrawHud(link);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    return ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

} // namespace gte
