// Tier-1 tests for src/Core/LogSink.h/.cpp (editor-core-separation-1
// campaign, PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md) - the global,
// registrable ILogSink mechanism GTE_LOG_* (Core/Logging.h) routes through
// instead of calling gte::Logger::Log() directly.
//
// InstallLogSink()/IsLogSinkInstalled()/LogToActiveSink() are process-global
// state (the exact same shape as Logger itself - see Editor/LoggerTests.cpp's
// own header comment on why every test calls Logger::Clear() at the start of
// its own body). Every test below restores the REAL sink
// (gte::LoggerLogSink::Instance()) in TearDown(), regardless of pass/fail, so
// later tests elsewhere in this SAME test binary that rely on GTE_LOG_*
// reaching the real Logger ring buffer (Editor/LoggerTests.cpp,
// Network/NetworkRoutesTests.cpp, Network/LogEndpointsEndToEndTests.cpp) are
// never left with a stale fake/null sink installed by this file.
//
// This file ALSO registers the ONE global gtest Environment that installs
// the real sink for the ENTIRE test binary at process start (see the bottom
// of this file) - every one of those other test files' own GTE_LOG_* call
// sites depends on this running first. Registered via a namespace-scope
// initializer, since this test binary uses gtest's own default main()
// (gtest_main), never a custom main() this project could call
// ::testing::AddGlobalTestEnvironment() from directly - every namespace-scope
// static's initializer runs before main() begins regardless of which
// translation unit it lives in, and gtest itself guarantees every registered
// Environment's SetUp() runs before any TEST()'s body.

#include "Core/LogSink.h"
#include "Editor/Logger.h" // LoggerLogSink - defined unconditionally regardless of GTE_ENABLE_EDITOR, see that file's own class comment.

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace gte {
namespace {

// A small, test-only ILogSink recording every call verbatim - proves
// LogToActiveSink() actually reaches WHATEVER sink is installed, never
// hardcoded to the real Logger.
class RecordingLogSink : public ILogSink {
public:
    struct Recorded {
        LogLevel level;
        std::string category;
        std::string message;
    };

    void Log(LogLevel level, std::string_view category, std::string_view message) override
    {
        entries.push_back(Recorded{ level, std::string(category), std::string(message) });
    }

    std::vector<Recorded> entries;
};

class LogSinkTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        // Always restore the real sink other test files in this binary
        // depend on - see this file's own header comment.
        InstallLogSink(&LoggerLogSink::Instance());
    }
};

TEST_F(LogSinkTest, NoSinkInstalled_IsLogSinkInstalledIsFalseAndLogToActiveSinkIsANoOp)
{
    InstallLogSink(nullptr);
    EXPECT_FALSE(IsLogSinkInstalled());
    // Must not crash/throw with no sink installed.
    LogToActiveSink(LogLevel::Info, "Category", "message");
}

TEST_F(LogSinkTest, InstallLogSink_MakesIsLogSinkInstalledTrue)
{
    RecordingLogSink sink;
    InstallLogSink(&sink);
    EXPECT_TRUE(IsLogSinkInstalled());
}

TEST_F(LogSinkTest, LogToActiveSink_RoutesLevelCategoryMessageToTheInstalledSinkVerbatim)
{
    RecordingLogSink sink;
    InstallLogSink(&sink);

    LogToActiveSink(LogLevel::Warning, "SomeCategory", "some message");

    ASSERT_EQ(sink.entries.size(), 1u);
    EXPECT_EQ(sink.entries[0].level, LogLevel::Warning);
    EXPECT_EQ(sink.entries[0].category, "SomeCategory");
    EXPECT_EQ(sink.entries[0].message, "some message");
}

TEST_F(LogSinkTest, InstallLogSink_NullptrUninstallsAndLogToActiveSinkBecomesANoOpAgain)
{
    RecordingLogSink sink;
    InstallLogSink(&sink);
    ASSERT_TRUE(IsLogSinkInstalled());

    InstallLogSink(nullptr);
    EXPECT_FALSE(IsLogSinkInstalled());

    LogToActiveSink(LogLevel::Error, "Category", "message");
    EXPECT_TRUE(sink.entries.empty());
}

TEST_F(LogSinkTest, InstallLogSink_SecondCallWithDifferentSinkIsLastWriteWins)
{
    RecordingLogSink first;
    RecordingLogSink second;
    InstallLogSink(&first);
    InstallLogSink(&second);

    LogToActiveSink(LogLevel::Debug, "Category", "message");

    EXPECT_TRUE(first.entries.empty());
    ASSERT_EQ(second.entries.size(), 1u);
}

TEST_F(LogSinkTest, GTE_LOG_MacrosDoNotEvaluateMessageExpressionWhenNoSinkIsInstalled)
{
    InstallLogSink(nullptr);

    bool messageExpressionEvaluated = false;
    auto buildMessage = [&]() -> std::string {
        messageExpressionEvaluated = true;
        return "should never be built";
    };

    GTE_LOG_INFO("Category", buildMessage());

    EXPECT_FALSE(messageExpressionEvaluated);
}

TEST_F(LogSinkTest, GTE_LOG_MacrosRouteThroughLogToActiveSinkWhenASinkIsInstalled)
{
    RecordingLogSink sink;
    InstallLogSink(&sink);

    GTE_LOG_ERROR("SomeCategory", "an error message");

    ASSERT_EQ(sink.entries.size(), 1u);
    EXPECT_EQ(sink.entries[0].level, LogLevel::Error);
    EXPECT_EQ(sink.entries[0].category, "SomeCategory");
    EXPECT_EQ(sink.entries[0].message, "an error message");
}

} // namespace
} // namespace gte

namespace {

class InstallRealLoggerSinkEnvironment : public ::testing::Environment {
public:
    void SetUp() override
    {
        ::gte::InstallLogSink(&::gte::LoggerLogSink::Instance());
    }
};

::testing::Environment* const g_installRealLoggerSinkEnvironment =
    ::testing::AddGlobalTestEnvironment(new InstallRealLoggerSinkEnvironment());

} // namespace
