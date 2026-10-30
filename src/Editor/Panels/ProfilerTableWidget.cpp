#include "ProfilerTableWidget.h"

#include "../ProfilerPanelData.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace gte {

namespace {

constexpr ImVec4 kGreyText(0.62f, 0.62f, 0.62f, 1.0f);
constexpr ImVec4 kTitleText(0.95f, 0.85f, 0.45f, 1.0f);
constexpr ImU32 kLaneHeaderBg = IM_COL32(48, 52, 64, 255);
constexpr ImU32 kBarBackground = IM_COL32(40, 40, 46, 255);
constexpr ImU32 kBarMain = IM_COL32(77, 166, 255, 255);
constexpr ImU32 kBarWorkers = IM_COL32(60, 200, 190, 255);
constexpr ImU32 kBarGpu = IM_COL32(255, 158, 51, 255);
constexpr ImU32 kBarWaiting = IM_COL32(130, 130, 130, 255);
constexpr ImU32 kFoldGlyph = IM_COL32(220, 220, 220, 255);
constexpr float kIndentPixels = 14.0f;
constexpr float kFoldPixels = 14.0f;

using Profiling::ScopeLane;

const char* LaneLabel(ScopeLane lane)
{
    switch (lane) {
    case ScopeLane::Main: return "Main thread (CPU)";
    case ScopeLane::Workers: return "Helper threads (CPU)";
    case ScopeLane::Gpu: return "Graphics card (GPU)";
    }
    return "";
}

ImU32 LaneColor(ScopeLane lane)
{
    switch (lane) {
    case ScopeLane::Main: return kBarMain;
    case ScopeLane::Workers: return kBarWorkers;
    case ScopeLane::Gpu: return kBarGpu;
    }
    return kBarWaiting;
}

const char* OriginLabel(Profiling::ScopeOrigin origin)
{
    switch (origin) {
    case Profiling::ScopeOrigin::Engine: return "Engine";
    case Profiling::ScopeOrigin::Project: return "Your project";
    case Profiling::ScopeOrigin::Unknown: break;
    }
    return "Unknown";
}

int IdOf(std::uint64_t key)
{
    return static_cast<int>(key ^ (key >> 32));
}

bool HasTime(const TableRowView& view)
{
    return view.row.lane != ScopeLane::Gpu || view.row.status == Profiling::SampleStatus::Present
        || view.row.status == Profiling::SampleStatus::Partial;
}

// GPU rows that were not measured print their status word, never a number.
std::string TimeText(const TableRowView& view, double milliseconds)
{
    if (!HasTime(view)) {
        return FormatGpuTimingLine(view.row.status, 0.0);
    }
    const std::string text = FormatDuration(milliseconds);
    return view.row.lane == ScopeLane::Gpu && view.row.status == Profiling::SampleStatus::Partial ? "~" + text : text;
}

void DrawLaneHeader(ScopeLane lane, double totalMs)
{
    ImGui::TableNextRow();
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kLaneHeaderBg);
    ImGui::PushID(-1 - static_cast<int>(lane));
    ImGui::TableSetColumnIndex(1);

    const float lineHeight = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 6.0f, p.y + lineHeight * 0.5f), 5.0f, LaneColor(lane));
    ImGui::Dummy(ImVec2(kFoldPixels, lineHeight));
    ImGui::SameLine(0.0f, 0.0f);
    if (totalMs > 0.0) {
        ImGui::Text("%s   %s", LaneLabel(lane), FormatDuration(totalMs).c_str());
    } else {
        ImGui::TextUnformatted(LaneLabel(lane));
    }
    ImGui::PopID();
}

void DrawTooltip(const TableRowView& view)
{
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextColored(kTitleText, "%s", view.title.c_str());
    ImGui::TextWrapped("%s", view.explanation.c_str());
    if (!view.hint.empty()) {
        ImGui::TextWrapped("%s", view.hint.c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kGreyText);
    ImGui::Text("Code name: %s", view.rawName.c_str());
    ImGui::Text("Origin: %s", OriginLabel(view.origin));
    if (!view.sourceText.empty()) {
        ImGui::Text("Source: %s", view.sourceText.c_str());
    }
    if (!view.featureTitle.empty()) {
        ImGui::Text("Render feature: %s", view.featureTitle.c_str());
    }
    if (HasTime(view)) {
        ImGui::Text("Longest call: %s", FormatDuration(view.maxMs).c_str());
    }
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void DrawNameCell(const TableRowView& v, TableDrawState& state)
{
    const float base = ImGui::GetCursorPosX();
    if (v.row.depth > 0) {
        ImGui::SetCursorPosX(base + static_cast<float>(v.row.depth) * kIndentPixels);
    }

    ImGui::BeginGroup();
    const float lineHeight = ImGui::GetTextLineHeight();
    if (state.tree && v.row.hasChildren) {
        const bool folded = std::find(state.collapsed.begin(), state.collapsed.end(), v.row.pathKey) != state.collapsed.end();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##fold", ImVec2(kFoldPixels, lineHeight))) {
            if (folded) {
                state.collapsed.erase(std::remove(state.collapsed.begin(), state.collapsed.end(), v.row.pathKey), state.collapsed.end());
            } else {
                state.collapsed.push_back(v.row.pathKey);
            }
        }
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 3.0f, p.y), kFoldGlyph, folded ? "+" : "-");
    } else {
        ImGui::Dummy(ImVec2(kFoldPixels, lineHeight));
    }
    ImGui::SameLine(0.0f, 0.0f);

    const std::string& primary = state.technicalNames ? v.rawName : v.title;
    const std::string& secondary = state.technicalNames ? v.title : v.rawName;
    if (v.waiting) {
        ImGui::PushStyleColor(ImGuiCol_Text, kGreyText);
    }
    ImGui::TextUnformatted(primary.c_str());
    if (v.waiting) {
        ImGui::PopStyleColor();
    }
    if (!secondary.empty() && secondary != primary) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", secondary.c_str());
    }
    if (v.waiting) {
        ImGui::SameLine();
        ImGui::TextDisabled("(waiting)");
    }
    ImGui::EndGroup();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        DrawTooltip(v);
    }
}

void DrawGraphTick(const TableRowView& view, TableDrawState& state)
{
    if (view.isGpuFeature) {
        return;
    }
    const TrackedKey key{ view.row.lane, view.row.name };
    bool ticked = IsTracked(state.tracked, key);
    if (ImGui::Checkbox("##graph", &ticked)) {
        if (!ticked) {
            RemoveTracked(state.tracked, key);
        } else if (!AddTracked(state.tracked, key)) {
            state.limitRefused = true;
        }
    }
}

void DrawBar(const TableRowView& view)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const float height = ImGui::GetTextLineHeight();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(p, ImVec2(p.x + width, p.y + height), kBarBackground);
    if (view.percentKnown) {
        const float fraction = std::clamp(static_cast<float>(view.percentOfBase) / 100.0f, 0.0f, 1.0f);
        const ImU32 color = view.waiting ? kBarWaiting : LaneColor(view.row.lane);
        drawList->AddRectFilled(p, ImVec2(p.x + width * fraction, p.y + height), color);
    }
    ImGui::Dummy(ImVec2(width, height));
}

void DrawRow(const TableRowView& view, TableDrawState& state)
{
    ImGui::TableNextRow();
    ImGui::PushID(IdOf(view.row.pathKey));

    ImGui::TableSetColumnIndex(0);
    DrawGraphTick(view, state);

    ImGui::TableSetColumnIndex(1);
    DrawNameCell(view, state);

    ImGui::TableSetColumnIndex(2);
    ImGui::Text("%u", view.row.calls);

    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(TimeText(view, view.totalMs).c_str());

    ImGui::TableSetColumnIndex(4);
    ImGui::TextUnformatted(TimeText(view, view.selfMs).c_str());

    ImGui::TableSetColumnIndex(5);
    if (view.percentKnown) {
        ImGui::Text("%.1f%%", view.percentOfBase);
    } else {
        ImGui::TextUnformatted("-");
    }

    ImGui::TableSetColumnIndex(6);
    DrawBar(view);

    ImGui::PopID();
}

} // namespace

void DrawProfilerTable(const TableModel& model, TableDrawState& state, float height)
{
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("ProfilerScopeTable", 7, tableFlags, ImVec2(0.0f, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 84.0f);
    ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthFixed, 84.0f);
    ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableSetupColumn("Bar", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableHeadersRow();

    bool haveLane = false;
    ScopeLane lane = ScopeLane::Main;
    for (const TableRowView& view : model.rows) {
        if (!haveLane || view.row.lane != lane) {
            haveLane = true;
            lane = view.row.lane;
            DrawLaneHeader(lane, model.laneTotalMs[static_cast<std::size_t>(lane)]);
        }
        DrawRow(view, state);
    }
    ImGui::EndTable();
}

} // namespace gte
