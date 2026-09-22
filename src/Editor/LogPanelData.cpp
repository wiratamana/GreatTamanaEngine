#include "LogPanelData.h"

#include <cstdio>

namespace gte {

LogQueryFilter BuildLogQueryFilter(const LogPanelFilterState& state, std::uint64_t sinceId)
{
    LogQueryFilter filter;
    filter.sinceId = sinceId;
    // hasMinLevel is deliberately left false - see this file's own header
    // comment (LogPanelData.h) for why level filtering is resolved
    // entirely by FilterByEnabledLevels() below instead of a single
    // Logger-side ordinal threshold.
    filter.category = state.categoryFilter;
    filter.keyword = state.keywordFilter;
    return filter;
}

std::vector<LogEntry> FilterByEnabledLevels(const std::vector<LogEntry>& entries, const LogPanelFilterState& state)
{
    std::vector<LogEntry> result;
    result.reserve(entries.size());
    for (const LogEntry& entry : entries) {
        bool enabled = false;
        switch (entry.level) {
            case LogLevel::Debug:
                enabled = state.showDebug;
                break;
            case LogLevel::Info:
                enabled = state.showInfo;
                break;
            case LogLevel::Warning:
                enabled = state.showWarning;
                break;
            case LogLevel::Error:
                enabled = state.showError;
                break;
        }
        if (enabled) {
            result.push_back(entry);
        }
    }
    return result;
}

LevelColor ColorForLevel(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Debug:
            return LevelColor{ 0.65f, 0.65f, 0.65f, 1.0f }; // Gray - low-signal detail.
        case LogLevel::Info:
            return LevelColor{ 0.85f, 0.85f, 0.85f, 1.0f }; // Near-white - normal informational text.
        case LogLevel::Warning:
            return LevelColor{ 0.95f, 0.80f, 0.30f, 1.0f }; // Amber - matches this codebase's own warning color
                                                             // convention elsewhere (e.g. scene IO status text).
        case LogLevel::Error:
            return LevelColor{ 1.0f, 0.40f, 0.40f, 1.0f }; // Red - matches this codebase's own error color
                                                            // convention elsewhere (e.g. scene IO status text).
    }
    return LevelColor{ 1.0f, 1.0f, 1.0f, 1.0f };
}

std::string FormatLogEntryLine(const LogEntry& entry)
{
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "[%.3fs][Frame %llu][%s][%s] %s", entry.timestampSeconds,
        static_cast<unsigned long long>(entry.frameNumber), ToString(entry.level), entry.category.c_str(),
        entry.message.c_str());
    return std::string(buffer);
}

} // namespace gte
