#include "ScenePanelCullView.h"

#include "../EditorContext.h"
#include "../ImGuiUniqueId.h"
#include "../ViewCullHud.h"
#include "../../Core/FeatureKit/ViewCullLink.h"

#include <algorithm>
#include <array>
#include <string>

namespace gte {

namespace {

constexpr float kMaxWidth = 300.0f;
constexpr float kMinWidth = 160.0f;
constexpr float kMargin = 8.0f;
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

// Starts a request from what is applied now, so one change never overwrites the other fields.
fx::ViewCullRequest& StartRequest(fx::ViewCullLink& link)
{
    fx::ViewCullRequest& request = link.request;
    if (!request.pending) {
        const fx::ViewCullReport& report = link.report;
        request.choice = report.choice;
        request.cone = report.cone;
        request.source = report.source;
        request.ghost = report.ghost;
    }
    request.pending = true;
    return request;
}

void DrawChoiceCombo(fx::ViewCullLink& link)
{
    const fx::ViewCullReport& report = link.report;
    const char* preview = report.choice == fx::ViewCullChoice::FollowSettings && report.mode == fx::ViewCullMode::Custom
        ? ModeText(report.mode)
        : ChoiceText(report.choice);
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo("##cullChoice", preview)) {
        return;
    }
    for (std::size_t i = 0; i < kChoices.size(); ++i) {
        const ChoiceLabel& entry = kChoices[i];
        ScopedUniqueId idScope(static_cast<int>(i), "ScenePanelCullView::Choice");
        const bool blocked = entry.choice == fx::ViewCullChoice::FrustumHiz && report.source == fx::ViewCullSource::Frozen;
        ImGui::BeginDisabled(blocked);
        const bool picked = ImGui::Selectable(entry.label, entry.choice == report.choice);
        ImGui::EndDisabled();
        if (entry.choice == fx::ViewCullChoice::FollowSettings && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", kFollowTooltip);
        } else if (blocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", kHizTooltip);
        }
        if (picked && !blocked) {
            StartRequest(link).choice = entry.choice;
        }
    }
    if (report.choice == fx::ViewCullChoice::FollowSettings && report.mode == fx::ViewCullMode::Custom) {
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
        if (ImGui::Selectable("Scene camera", report.source == fx::ViewCullSource::SceneCamera)) {
            StartRequest(link).source = fx::ViewCullSource::SceneCamera;
        }
        ImGui::BeginDisabled(hizChosen);
        const bool frozenPicked = ImGui::Selectable("Frozen", report.source == fx::ViewCullSource::Frozen);
        ImGui::EndDisabled();
        if (hizChosen && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", kHizTooltip);
        }
        if (frozenPicked && !hizChosen) {
            StartRequest(link).source = fx::ViewCullSource::Frozen;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(hizChosen);
    if (ImGui::Button("Freeze here")) {
        StartRequest(link).freezeHere = true;
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
        StartRequest(link).cone = cone;
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

bool BuildScenePanelCullView(EditorContext& ctx, ImVec2 imageMin, ImVec2 imageSize)
{
    if (!ctx.viewCull || !ctx.viewCull->report.valid || !ctx.viewCull->report.rendered) {
        return false;
    }
    const float width = std::min(kMaxWidth, imageSize.x - 2.0f * kMargin);
    if (width < kMinWidth) {
        return false;
    }
    fx::ViewCullLink& link = *ctx.viewCull;

    ImGui::SetCursorScreenPos(ImVec2(imageMin.x + imageSize.x - width - kMargin, imageMin.y + kMargin));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.09f, 0.78f));
    const ImGuiChildFlags childFlags = ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders;
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("##SceneCullView", ImVec2(width, 0.0f), childFlags, windowFlags)) {
        ScopedUniqueId idScope(0, "ScenePanelCullView::Build");
        if (ImGui::Checkbox("Cull view", &ctx.sceneCullViewOn) && !ctx.sceneCullViewOn) {
            link.request.pending = true;
            link.request.resetAll = true;
        }
        if (ctx.sceneCullViewOn) {
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
