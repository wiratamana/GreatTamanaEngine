#pragma once

#include "Logger.h"

#include <string>
#include <vector>

// task_manager/logger-1 campaign, PHASE4 (Editor "Log" Panel UI) - the
// panel's own pure, ImGui-free filter state + data-shaping logic. Follows
// JobsPanelData.h/ProfilerPanelData.h/MemoryPanelData.h's own template
// exactly (plain reshape/format functions, Tier-1-tested despite living
// under src/Editor/ - see AGENTS.md, "Testability & Regression Safety").
// Panels/LogPanel.cpp is the thin ImGui-facing wrapper around these.

namespace gte {

// The "Log" panel's own cross-frame filter/UI state - a plain data struct
// so it can be unit-tested independently of ImGui (mirrors JobsPanel.h's
// own m_paused/m_frozenPoints style, just factored out as its own named
// type here since there's more of it).
struct LogPanelFilterState {
    bool showDebug = true;
    bool showInfo = true;
    bool showWarning = true;
    bool showError = true;
    std::string categoryFilter; // Empty = any. Exact, case-sensitive match (mirrors Logger's own convention).
    std::string keywordFilter;  // Empty = any. Case-insensitive substring match on the message.
    bool autoScroll = true;
};

// Translates `state`'s category/keyword text filters into a
// Logger::LogQueryFilter's matching fields (leaving `hasMinLevel`
// deliberately false/unset - see FilterByEnabledLevels() below for why
// level filtering is NOT expressed as a single Logger-side min-level
// threshold here) plus `sinceId` (0 to fetch the whole current buffer, or
// a caller-supplied cursor for an incremental re-query).
LogQueryFilter BuildLogQueryFilter(const LogPanelFilterState& state, std::uint64_t sinceId);

// Logger::Query() only supports a single ordinal "minimum level"
// threshold - but this panel's UI is FOUR INDEPENDENT per-level
// checkboxes (e.g. "show Debug and Error but not Info/Warning" is a
// valid, arbitrary combination no single threshold can express). This
// function applies that arbitrary subset AFTER Logger::Query() has
// already applied every other filter - it is the ONE place this
// distinction is resolved, so LogPanel.cpp itself never needs to touch
// LogEntry::level directly.
std::vector<LogEntry> FilterByEnabledLevels(const std::vector<LogEntry>& entries, const LogPanelFilterState& state);

// Plain RGBA (0..1 float) - LogPanel.cpp is the only place this gets
// turned into an ImVec4, keeping this function itself ImGui-free (same
// rule ProfilerPanelData.h's own functions already follow).
struct LevelColor {
    float r;
    float g;
    float b;
    float a;
};
LevelColor ColorForLevel(LogLevel level) noexcept;

// "[12.345s][Frame 42][Warning][Renderer] message text" - the exact,
// single, shared line format both LogPanel.cpp's rendering AND this
// file's own test use, so they can never silently drift apart (mirrors
// ProfilerPanelData.h's FormatDuration()/FormatFrameTimeSummary()
// precedent).
std::string FormatLogEntryLine(const LogEntry& entry);

} // namespace gte
