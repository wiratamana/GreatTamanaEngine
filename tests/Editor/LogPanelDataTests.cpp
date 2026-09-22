// Unit tests for the Editor "Log" panel's data-shaping logic
// (src/Editor/LogPanelData.h) - deliberately pure (no ImGui/live Logger::
// state involved at all; every test builds its own plain LogEntry values),
// so it's Tier-1-testable exactly like JobsPanelData.h/ProfilerPanelData.h
// despite living under src/Editor/ - see AGENTS.md, "Testability &
// Regression Safety". Only built when GTE_ENABLE_EDITOR is ON, since
// LogPanelData.h/.cpp are only compiled into gte_core then (see the root
// CMakeLists.txt's "Editor Module Structure") - the same "zero-touch when
// off" rule already applied to tests/Editor/JobsPanelDataTests.cpp.

#include "Editor/LogPanelData.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

LogEntry MakeEntry(std::uint64_t id, LogLevel level, const char* category, const char* message,
    std::uint64_t frameNumber = 0, double timestampSeconds = 0.0)
{
    LogEntry entry;
    entry.id = id;
    entry.frameNumber = frameNumber;
    entry.timestampSeconds = timestampSeconds;
    entry.level = level;
    entry.category = category;
    entry.message = message;
    return entry;
}

TEST(LogPanelDataTest, BuildLogQueryFilterCarriesCategoryAndKeywordThrough)
{
    LogPanelFilterState state;
    state.categoryFilter = "Renderer";
    state.keywordFilter = "warning";

    const LogQueryFilter filter = BuildLogQueryFilter(state, /*sinceId=*/42);
    EXPECT_EQ(filter.category, "Renderer");
    EXPECT_EQ(filter.keyword, "warning");
    EXPECT_EQ(filter.sinceId, 42u);
}

TEST(LogPanelDataTest, BuildLogQueryFilterNeverSetsMinLevel)
{
    // Level filtering is resolved entirely by FilterByEnabledLevels()
    // below, never delegated to Logger::Query()'s own single ordinal
    // threshold - see LogPanelData.h's own doc comment for why.
    LogPanelFilterState state;
    state.showDebug = false;
    state.showInfo = false;
    state.showWarning = true;
    state.showError = true;

    const LogQueryFilter filter = BuildLogQueryFilter(state, /*sinceId=*/0);
    EXPECT_FALSE(filter.hasMinLevel);
}

TEST(LogPanelDataTest, BuildLogQueryFilterReflectsSinceIdVerbatim)
{
    const LogQueryFilter filter = BuildLogQueryFilter(LogPanelFilterState{}, /*sinceId=*/12345);
    EXPECT_EQ(filter.sinceId, 12345u);
}

TEST(LogPanelDataTest, FilterByEnabledLevelsAppliesArbitrarySubsetNotAThreshold)
{
    // Debug and Error enabled, Info/Warning disabled - a combination no
    // single ordinal "minimum level" threshold could ever express. This is
    // the one test that most directly proves that property.
    std::vector<LogEntry> entries;
    entries.push_back(MakeEntry(1, LogLevel::Debug, "A", "debug message"));
    entries.push_back(MakeEntry(2, LogLevel::Info, "A", "info message"));
    entries.push_back(MakeEntry(3, LogLevel::Warning, "A", "warning message"));
    entries.push_back(MakeEntry(4, LogLevel::Error, "A", "error message"));

    LogPanelFilterState state;
    state.showDebug = true;
    state.showInfo = false;
    state.showWarning = false;
    state.showError = true;

    const std::vector<LogEntry> result = FilterByEnabledLevels(entries, state);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].id, 1u);
    EXPECT_EQ(result[1].id, 4u);
}

TEST(LogPanelDataTest, FilterByEnabledLevelsPreservesOriginalRelativeOrder)
{
    std::vector<LogEntry> entries;
    entries.push_back(MakeEntry(1, LogLevel::Debug, "A", "one"));
    entries.push_back(MakeEntry(2, LogLevel::Debug, "A", "two"));
    entries.push_back(MakeEntry(3, LogLevel::Warning, "A", "three")); // excluded
    entries.push_back(MakeEntry(4, LogLevel::Debug, "A", "four"));

    LogPanelFilterState state;
    state.showDebug = true;
    state.showInfo = true;
    state.showWarning = false;
    state.showError = true;

    const std::vector<LogEntry> result = FilterByEnabledLevels(entries, state);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].id, 1u);
    EXPECT_EQ(result[1].id, 2u);
    EXPECT_EQ(result[2].id, 4u);
}

TEST(LogPanelDataTest, FilterByEnabledLevelsAllDisabledReturnsEmpty)
{
    std::vector<LogEntry> entries;
    entries.push_back(MakeEntry(1, LogLevel::Debug, "A", "one"));
    entries.push_back(MakeEntry(2, LogLevel::Error, "A", "two"));

    LogPanelFilterState state;
    state.showDebug = false;
    state.showInfo = false;
    state.showWarning = false;
    state.showError = false;

    const std::vector<LogEntry> result = FilterByEnabledLevels(entries, state);
    EXPECT_TRUE(result.empty());
}

TEST(LogPanelDataTest, ColorForLevelReturnsFourDistinctColors)
{
    const LevelColor debug = ColorForLevel(LogLevel::Debug);
    const LevelColor info = ColorForLevel(LogLevel::Info);
    const LevelColor warning = ColorForLevel(LogLevel::Warning);
    const LevelColor error = ColorForLevel(LogLevel::Error);

    auto sameColor = [](const LevelColor& a, const LevelColor& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };

    EXPECT_FALSE(sameColor(debug, info));
    EXPECT_FALSE(sameColor(debug, warning));
    EXPECT_FALSE(sameColor(debug, error));
    EXPECT_FALSE(sameColor(info, warning));
    EXPECT_FALSE(sameColor(info, error));
    EXPECT_FALSE(sameColor(warning, error));
}

TEST(LogPanelDataTest, FormatLogEntryLineProducesExpectedShape)
{
    const LogEntry entry = MakeEntry(1, LogLevel::Warning, "Renderer", "value=42", /*frameNumber=*/7,
        /*timestampSeconds=*/12.345);
    EXPECT_EQ(FormatLogEntryLine(entry), "[12.345s][Frame 7][Warning][Renderer] value=42");
}

} // namespace
} // namespace gte
