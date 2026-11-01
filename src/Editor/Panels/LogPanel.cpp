#include "LogPanel.h"

#include "../EditorFrame.h"

#include <imgui.h>

#include <cstdio>

namespace gte {

void LogPanel::OnGui(EditorFrame& /*frame*/)
{
    ImGui::Checkbox("Debug", &m_filterState.showDebug);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &m_filterState.showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warning", &m_filterState.showWarning);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &m_filterState.showError);

    // InputText needs a char buffer: refill it from the string, copy back only on a real edit.
    char categoryBuffer[128];
    std::snprintf(categoryBuffer, sizeof(categoryBuffer), "%s", m_filterState.categoryFilter.c_str());
    if (ImGui::InputText("Category", categoryBuffer, sizeof(categoryBuffer))) {
        m_filterState.categoryFilter = categoryBuffer;
    }
    ImGui::SameLine();
    char keywordBuffer[128];
    std::snprintf(keywordBuffer, sizeof(keywordBuffer), "%s", m_filterState.keywordFilter.c_str());
    if (ImGui::InputText("Keyword", keywordBuffer, sizeof(keywordBuffer))) {
        m_filterState.keywordFilter = keywordBuffer;
    }

    ImGui::Checkbox("Auto-scroll", &m_filterState.autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        Logger::Clear();
    }

    // The buffer is small (Logger::kCapacity), so the full list is re-queried every frame.
    const LogQueryFilter filter = BuildLogQueryFilter(m_filterState, /*sinceId=*/0);
    const std::vector<LogEntry> matched = FilterByEnabledLevels(Logger::Query(filter), m_filterState);

    ImGui::Text("%zu entries", matched.size());

    ImGui::BeginChild("LogScrollRegion", ImVec2(0.0f, 0.0f), true);
    if (matched.empty()) {
        ImGui::TextDisabled("No log entries yet.");
    } else {
        // Sampled before new rows are added, so a user who scrolled up is not pulled back down.
        const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
        for (const LogEntry& entry : matched) {
            const LevelColor color = ColorForLevel(entry.level);
            ImGui::TextColored(ImVec4(color.r, color.g, color.b, color.a), "%s", FormatLogEntryLine(entry).c_str());
        }
        if (m_filterState.autoScroll && wasAtBottom) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

} // namespace gte
