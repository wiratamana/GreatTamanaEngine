#include "LogPanel.h"

#include "../EditorContext.h"

#include <imgui.h>

#include <cstdio>

namespace gte {

void LogPanel::Build(EditorContext& /*ctx*/)
{
    ImGui::Begin("Log");

    // Filter row: four independent per-level toggles, same line.
    ImGui::Checkbox("Debug", &m_filterState.showDebug);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &m_filterState.showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warning", &m_filterState.showWarning);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &m_filterState.showError);

    // This codebase does NOT vendor misc/cpp/imgui_stdlib.h, so
    // ImGui::InputText() cannot bind directly to a std::string - mirrors
    // Panels/InspectorPanel.cpp's own entity "Name" field idiom: a local,
    // fixed-size char buffer, refilled from the std::string every frame
    // BEFORE the InputText() call, copied back only when a real edit
    // happened this frame (InputText() returned true).
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
    // Called DIRECTLY, no bridge - same justification as GET /clear_logs
    // (PHASE3): this runs on the main thread anyway, exactly like every
    // other Editor panel's own direct engine calls.
    if (ImGui::Button("Clear")) {
        Logger::Clear();
    }

    // Always re-queries the FULL current buffer every frame - at only up
    // to Logger::kCapacity (2000) entries this is cheap; there is no need
    // for this panel itself to use an incremental since_id cursor the way
    // an external HTTP poller does.
    const LogQueryFilter filter = BuildLogQueryFilter(m_filterState, /*sinceId=*/0);
    const std::vector<LogEntry> matched = FilterByEnabledLevels(Logger::Query(filter), m_filterState);

    ImGui::Text("%zu entries", matched.size());

    ImGui::BeginChild("LogScrollRegion", ImVec2(0.0f, 0.0f), true);
    if (matched.empty()) {
        // Logger::IsEnabled() is always true wherever this panel's own
        // code runs (this panel only exists when GTE_ENABLE_EDITOR is ON,
        // and Logger has no independent enable switch of its own beyond
        // that - see PHASE4_EDITOR_LOG_PANEL_UI.md, Step 2) - so there is
        // no "logging disabled in this build" state to distinguish here,
        // unlike e.g. ProfilerPanel's own compiled-out empty message.
        ImGui::TextDisabled("No log entries yet.");
    } else {
        // Standard Dear ImGui "Console" idiom (imgui_demo.cpp): check
        // whether the scroll position was already at the bottom BEFORE
        // this frame's new content is added, so a user who has manually
        // scrolled up to read older entries never gets yanked back down.
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

    ImGui::End();
}

} // namespace gte
