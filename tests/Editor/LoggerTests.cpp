// Unit tests for the core Logger module (src/Editor/Logger.h/.cpp) - see
// task_manager/logger-1/PHASE1_CORE_LOGGER_MODULE.md. Logger is
// process-global static state, so every test calls Logger::Clear() at the
// START of its own body (never relying on test execution order) so one
// test's leftover entries never leak into another's assertions. The
// monotonic id counter keeps climbing across tests within the same run, so
// assertions use RELATIVE id ordering, never a specific absolute id value.
//
// Only built when GTE_ENABLE_EDITOR is ON, since Logger's real
// implementation (Logger.cpp) is only compiled into gte_core then (see
// CMakeLists.txt / tests/CMakeLists.txt).

#include "Editor/Logger.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

namespace gte {
namespace {

TEST(LoggerTest, BasicRecordAndQuery_ReturnsAllInAscendingIdOrder)
{
    Logger::Clear();

    Logger::Log(LogLevel::Debug, "CategoryA", "message one");
    Logger::Log(LogLevel::Info, "CategoryB", "message two");
    Logger::Log(LogLevel::Warning, "CategoryC", "message three");

    const std::vector<LogEntry> results = Logger::Query(LogQueryFilter{});
    ASSERT_EQ(results.size(), 3u);

    EXPECT_LT(results[0].id, results[1].id);
    EXPECT_LT(results[1].id, results[2].id);

    EXPECT_EQ(results[0].level, LogLevel::Debug);
    EXPECT_EQ(results[0].category, "CategoryA");
    EXPECT_EQ(results[0].message, "message one");

    EXPECT_EQ(results[1].level, LogLevel::Info);
    EXPECT_EQ(results[1].category, "CategoryB");
    EXPECT_EQ(results[1].message, "message two");

    EXPECT_EQ(results[2].level, LogLevel::Warning);
    EXPECT_EQ(results[2].category, "CategoryC");
    EXPECT_EQ(results[2].message, "message three");
}

TEST(LoggerTest, RingBufferEviction_KeepsOnlyNewestCapacityEntries)
{
    Logger::Clear();

    const std::size_t total = Logger::kCapacity + 5;
    for (std::size_t i = 0; i < total; ++i) {
        Logger::Log(LogLevel::Info, "Category", "entry " + std::to_string(i));
    }

    EXPECT_EQ(Logger::EntryCount(), Logger::kCapacity);

    const std::vector<LogEntry> results = Logger::Query(LogQueryFilter{});
    ASSERT_EQ(results.size(), Logger::kCapacity);
    // The first 5 (indices 0..4) were evicted - the oldest surviving entry
    // is "entry 5", the 6th one ever logged.
    EXPECT_EQ(results.front().message, "entry 5");
}

TEST(LoggerTest, SinceIdCursor_ReturnsOnlyEntriesAfterIt)
{
    Logger::Clear();

    Logger::Log(LogLevel::Info, "Category", "first");
    Logger::Log(LogLevel::Info, "Category", "second");
    Logger::Log(LogLevel::Info, "Category", "third");

    const std::vector<LogEntry> all = Logger::Query(LogQueryFilter{});
    ASSERT_EQ(all.size(), 3u);
    const std::uint64_t secondId = all[1].id;

    LogQueryFilter filter;
    filter.sinceId = secondId;
    const std::vector<LogEntry> afterSecond = Logger::Query(filter);

    ASSERT_EQ(afterSecond.size(), 1u);
    EXPECT_EQ(afterSecond[0].message, "third");
}

TEST(LoggerTest, MinLevelFilter_ReturnsOnlyThatLevelAndAbove)
{
    Logger::Clear();

    Logger::Log(LogLevel::Debug, "Category", "debug msg");
    Logger::Log(LogLevel::Info, "Category", "info msg");
    Logger::Log(LogLevel::Warning, "Category", "warning msg");
    Logger::Log(LogLevel::Error, "Category", "error msg");

    LogQueryFilter filter;
    filter.hasMinLevel = true;
    filter.minLevel = LogLevel::Warning;

    const std::vector<LogEntry> results = Logger::Query(filter);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].level, LogLevel::Warning);
    EXPECT_EQ(results[1].level, LogLevel::Error);
}

TEST(LoggerTest, CategoryFilter_ExactCaseSensitiveMatch)
{
    Logger::Clear();

    Logger::Log(LogLevel::Info, "Renderer", "renderer message");
    Logger::Log(LogLevel::Info, "Jobs", "jobs message");

    LogQueryFilter filter;
    filter.category = "Renderer";
    std::vector<LogEntry> results = Logger::Query(filter);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].message, "renderer message");

    // Confirm case-sensitivity: a differently-cased category does not match.
    filter.category = "renderer";
    results = Logger::Query(filter);
    EXPECT_EQ(results.size(), 0u);
}

TEST(LoggerTest, KeywordFilter_CaseInsensitiveSubstringMatch)
{
    Logger::Clear();

    Logger::Log(LogLevel::Error, "Category", "Something Failed To Load");
    Logger::Log(LogLevel::Info, "Category", "Everything is fine");

    LogQueryFilter filter;
    filter.keyword = "failed";
    const std::vector<LogEntry> results = Logger::Query(filter);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].message, "Something Failed To Load");
}

TEST(LoggerTest, FrameRangeFilter_InclusiveBoundaries)
{
    Logger::Clear();

    Logger::SetCurrentFrame(10);
    Logger::Log(LogLevel::Info, "Category", "frame10");
    Logger::SetCurrentFrame(20);
    Logger::Log(LogLevel::Info, "Category", "frame20");
    Logger::SetCurrentFrame(30);
    Logger::Log(LogLevel::Info, "Category", "frame30");
    Logger::SetCurrentFrame(0);

    LogQueryFilter filter;
    filter.hasFrameMin = true;
    filter.frameMin = 10;
    filter.hasFrameMax = true;
    filter.frameMax = 20;

    const std::vector<LogEntry> results = Logger::Query(filter);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].message, "frame10");
    EXPECT_EQ(results[1].message, "frame20");
}

TEST(LoggerTest, LimitFilter_KeepsNewestNInAscendingOrder)
{
    Logger::Clear();

    for (int i = 0; i < 10; ++i) {
        Logger::Log(LogLevel::Info, "Category", "entry " + std::to_string(i));
    }

    LogQueryFilter filter;
    filter.limit = 3;
    const std::vector<LogEntry> results = Logger::Query(filter);

    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].message, "entry 7");
    EXPECT_EQ(results[1].message, "entry 8");
    EXPECT_EQ(results[2].message, "entry 9");
    EXPECT_LT(results[0].id, results[1].id);
    EXPECT_LT(results[1].id, results[2].id);
}

TEST(LoggerTest, ClearSemantics_IdCounterNeverResetsOrReuses)
{
    Logger::Clear();

    Logger::Log(LogLevel::Info, "Category", "before clear 1");
    Logger::Log(LogLevel::Info, "Category", "before clear 2");
    const std::uint64_t latestBeforeClear = Logger::LatestEntryId();

    Logger::Clear();
    EXPECT_EQ(Logger::EntryCount(), 0u);

    Logger::Log(LogLevel::Info, "Category", "after clear");
    const std::vector<LogEntry> results = Logger::Query(LogQueryFilter{});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_GT(results[0].id, latestBeforeClear);
}

TEST(LoggerTest, TryParseLogLevelAndToString_RoundTripAndRejectInvalid)
{
    LogLevel parsed{};

    EXPECT_TRUE(TryParseLogLevel("debug", &parsed));
    EXPECT_EQ(parsed, LogLevel::Debug);
    EXPECT_STREQ(ToString(parsed), "Debug");

    EXPECT_TRUE(TryParseLogLevel("INFO", &parsed));
    EXPECT_EQ(parsed, LogLevel::Info);
    EXPECT_STREQ(ToString(parsed), "Info");

    EXPECT_TRUE(TryParseLogLevel("Warning", &parsed));
    EXPECT_EQ(parsed, LogLevel::Warning);
    EXPECT_STREQ(ToString(parsed), "Warning");

    EXPECT_TRUE(TryParseLogLevel("eRRoR", &parsed));
    EXPECT_EQ(parsed, LogLevel::Error);
    EXPECT_STREQ(ToString(parsed), "Error");

    LogLevel untouched = LogLevel::Warning;
    EXPECT_FALSE(TryParseLogLevel("not-a-level", &untouched));
    EXPECT_EQ(untouched, LogLevel::Warning);
}

TEST(LoggerTest, LogMacrosReachLoggerWithCorrectLevelCategoryMessage)
{
    Logger::Clear();

    GTE_LOG_DEBUG("MacroDebugCategory", "macro debug message");
    GTE_LOG_INFO("MacroInfoCategory", "macro info message");
    GTE_LOG_WARNING("MacroWarningCategory", "macro warning message");
    GTE_LOG_ERROR("MacroErrorCategory", "macro error message");

    const std::vector<LogEntry> results = Logger::Query(LogQueryFilter{});
    ASSERT_EQ(results.size(), 4u);

    EXPECT_EQ(results[0].level, LogLevel::Debug);
    EXPECT_EQ(results[0].category, "MacroDebugCategory");
    EXPECT_EQ(results[0].message, "macro debug message");

    EXPECT_EQ(results[1].level, LogLevel::Info);
    EXPECT_EQ(results[1].category, "MacroInfoCategory");
    EXPECT_EQ(results[1].message, "macro info message");

    EXPECT_EQ(results[2].level, LogLevel::Warning);
    EXPECT_EQ(results[2].category, "MacroWarningCategory");
    EXPECT_EQ(results[2].message, "macro warning message");

    EXPECT_EQ(results[3].level, LogLevel::Error);
    EXPECT_EQ(results[3].category, "MacroErrorCategory");
    EXPECT_EQ(results[3].message, "macro error message");
}

TEST(LoggerTest, ConcurrentLogging_NoCrashAndStrictlyAscendingUniqueIds)
{
    Logger::Clear();

    constexpr int kThreadCount = 8;
    constexpr int kLogsPerThread = 200;

    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([t]() {
            const std::string category = "Thread" + std::to_string(t);
            for (int i = 0; i < kLogsPerThread; ++i) {
                Logger::Log(LogLevel::Info, category, "message " + std::to_string(i));
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_LE(Logger::EntryCount(), Logger::kCapacity);

    const std::vector<LogEntry> results = Logger::Query(LogQueryFilter{});
    ASSERT_FALSE(results.empty());
    for (std::size_t i = 1; i < results.size(); ++i) {
        EXPECT_GT(results[i].id, results[i - 1].id);
    }
}

} // namespace
} // namespace gte
