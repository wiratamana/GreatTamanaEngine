#include "ProfilerGraphWidget.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gte {

namespace {

constexpr ImU32 kBackground = IM_COL32(24, 24, 28, 255);
constexpr ImU32 kBorder = IM_COL32(70, 70, 80, 255);
constexpr ImU32 kGuideLine = IM_COL32(110, 110, 120, 120);
constexpr ImU32 kGuideText = IM_COL32(150, 150, 160, 255);
constexpr ImU32 kCursorLine = IM_COL32(255, 255, 255, 90);
constexpr ImVec4 kWarnText(1.0f, 0.75f, 0.2f, 1.0f);
constexpr float kLineThickness = 1.5f;
constexpr float kSwatchSize = 10.0f;

ImU32 ToU32(const GraphColor& c)
{
    return IM_COL32(static_cast<int>(c.r * 255.0f + 0.5f), static_cast<int>(c.g * 255.0f + 0.5f),
        static_cast<int>(c.b * 255.0f + 0.5f), 255);
}

ImVec4 ToVec4(const GraphColor& c)
{
    return ImVec4(c.r, c.g, c.b, 1.0f);
}

void DrawSwatch(const GraphColor& color)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float offset = (ImGui::GetTextLineHeight() - kSwatchSize) * 0.5f;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(p.x, p.y + offset), ImVec2(p.x + kSwatchSize, p.y + offset + kSwatchSize), ToU32(color));
    ImGui::Dummy(ImVec2(kSwatchSize, ImGui::GetTextLineHeight()));
    ImGui::SameLine(0.0f, 4.0f);
}

bool* BaseFlag(GraphVisibility& visibility, std::size_t index)
{
    return index == 0 ? &visibility.frame : index == 1 ? &visibility.cpuBusy : &visibility.gpu;
}

void DrawLegend(const GraphModel& model, GraphVisibility& visibility, std::vector<TrackedKey>& tracked)
{
    for (std::size_t i = 0; i < model.series.size(); ++i) {
        const GraphSeries& series = model.series[i];
        ImGui::PushID(static_cast<int>(i));
        if (i != 0) {
            ImGui::SameLine(0.0f, 12.0f);
        }
        DrawSwatch(series.color);
        if (series.isBase) {
            ImGui::Checkbox(series.label.c_str(), BaseFlag(visibility, i));
        } else {
            ImGui::TextUnformatted(series.label.c_str());
            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::SmallButton("x")) {
                RemoveTracked(tracked, series.key);
            }
        }
        ImGui::PopID();
    }
}

bool Usable(const GraphModel& model, const GraphSeries& series)
{
    return series.visible && (series.isBase || !model.historyOutOfSync);
}

} // namespace

void DrawProfilerGraph(const GraphModel& model, GraphVisibility& visibility, std::vector<TrackedKey>& tracked, float height)
{
    const std::size_t count = model.frameIndices.size();
    if (count == 0 || model.series.empty()) {
        ImGui::TextDisabled("Waiting for profiler data...");
        return;
    }

    DrawLegend(model, visibility, tracked);

    const float width = std::max(ImGui::GetContentRegionAvail().x, 50.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + height);
    ImGui::InvisibleButton("##frameGraph", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();

    const float axisMax = std::max(model.axisMaxMs, 1.0f);
    const auto xAt = [&](std::size_t i) {
        return count > 1 ? p0.x + (width - 1.0f) * static_cast<float>(i) / static_cast<float>(count - 1) : p0.x;
    };
    const auto yAt = [&](float value) {
        const float t = std::clamp(value / axisMax, 0.0f, 1.0f);
        return p1.y - 1.0f - t * (height - 2.0f);
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(p0, p1, kBackground);
    drawList->AddRect(p0, p1, kBorder);
    drawList->PushClipRect(p0, p1, true);

    char label[32];
    std::snprintf(label, sizeof(label), "%.1f ms", static_cast<double>(axisMax));
    drawList->AddText(ImVec2(p0.x + 4.0f, p0.y + 2.0f), kGuideText, label);
    const auto drawGuide = [&](double ms) {
        const float y = yAt(static_cast<float>(ms));
        drawList->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), kGuideLine);
        std::snprintf(label, sizeof(label), "%.1f", ms);
        drawList->AddText(ImVec2(p1.x - 36.0f, y - ImGui::GetTextLineHeight()), kGuideText, label);
    };
    if (model.guideLow) {
        drawGuide(Profiling::kGraphGuideLowMs);
    }
    if (model.guideHigh) {
        drawGuide(Profiling::kGraphGuideHighMs);
    }

    for (const GraphSeries& series : model.series) {
        if (!Usable(model, series)) {
            continue;
        }
        const std::size_t n = std::min(series.values.size(), count);
        const ImU32 color = ToU32(series.color);
        for (std::size_t i = 0; i < n; ++i) {
            const float value = series.values[i];
            if (!std::isfinite(value)) {
                continue;
            }
            const bool hasPrev = i > 0 && std::isfinite(series.values[i - 1]);
            const bool hasNext = i + 1 < n && std::isfinite(series.values[i + 1]);
            if (hasPrev) {
                drawList->AddLine(ImVec2(xAt(i - 1), yAt(series.values[i - 1])), ImVec2(xAt(i), yAt(value)), color, kLineThickness);
            } else if (!hasNext) {
                drawList->AddCircleFilled(ImVec2(xAt(i), yAt(value)), 1.5f, color);
            }
        }
    }
    drawList->PopClipRect();

    if (hovered) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - p0.x) / std::max(width - 1.0f, 1.0f), 0.0f, 1.0f);
        const std::size_t index = static_cast<std::size_t>(t * static_cast<float>(count - 1) + 0.5f);
        const float x = xAt(index);
        drawList->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), kCursorLine);

        ImGui::BeginTooltip();
        ImGui::Text("Frame %llu", static_cast<unsigned long long>(model.frameIndices[index]));
        for (const GraphSeries& series : model.series) {
            if (!Usable(model, series) || index >= series.values.size()) {
                continue;
            }
            const float value = series.values[index];
            if (std::isfinite(value)) {
                ImGui::TextColored(ToVec4(series.color), "%s: %.2f ms", series.label.c_str(), static_cast<double>(value));
            } else {
                ImGui::TextColored(ToVec4(series.color), "%s: no data", series.label.c_str());
            }
        }
        ImGui::EndTooltip();
    }

    if (model.historyOutOfSync) {
        ImGui::TextColored(kWarnText, "History is out of sync: ticked rows are hidden for now.");
    }
}

} // namespace gte
