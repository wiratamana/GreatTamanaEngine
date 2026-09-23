// End-to-end log endpoint tests - logger-1 campaign, PHASE5
// (PHASE5_SMOKE_TEST_AND_FULL_VALIDATION.md, Step 3.1). Mirrors
// tests/Network/CaptureEndpointsEndToEndTests.cpp's own file-level shape
// (this project's existing precedent for "a SECOND end-to-end test file
// alongside NetworkServerTests.cpp itself, for one specific endpoint
// family" - see that file's own doc comment cited in
// NetworkServerTests.cpp): a real gte::Network::NetworkServer, started on an
// OS-assigned ephemeral port (Start(0), BoundPort()), driven with a real
// httplib::Client - this time against GET /get_logs / POST /clear_logs.
//
// Because GET /get_logs and POST /clear_logs need NO bridge at all (see
// AGENTS.md, "Logging", and Editor/Logger.h's own class comment - Logger is
// its own purpose-built, thread-safe, process-global store, not "engine
// state" reached through the usual bridge rule), the NetworkServer this file
// constructs needs no bridge pointers wired up - the simplest possible
// constructor call, `gte::Network::NetworkServer server;`, mirroring
// NetworkServerTests.cpp's own "seven no-argument NetworkServer server;
// constructions" default-bridge precedent.
//
// Because Logger is process-global static state (PHASE1), and this test
// process is the SAME process as LoggerTests.cpp's (PHASE1) and
// NetworkRoutesTests.cpp's `#if GTE_ENABLE_EDITOR` cases (PHASE3), EVERY
// test body below starts with a fresh gte::Logger::Clear() so test execution
// order never matters and no test observes another test's leftover entries.
//
// This whole file used to be guarded by `#if GTE_ENABLE_EDITOR` - removed by
// editor-core-separation-1 campaign's PHASE8 (GTE_ENABLE_EDITOR no longer
// exists anywhere in this codebase). Logger.cpp is now always compiled
// in (see Editor/Logger.h/.cpp's own PHASE8 history), so this file's own
// gte::Logger::Log()/Clear()/SetCurrentFrame() calls always have real,
// observable behavior.

#include "Editor/Logger.h"
#include "Network/NetworkServer.h"

#include "NetworkTestHelpers.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

namespace gte {
namespace {

using gte::Network::TestHelpers::WaitUntilAcceptingConnections;

// A real gte::Network::NetworkServer, started on an ephemeral port - a
// fresh instance per TEST_F (no shared/global server across tests), mirroring
// CaptureEndpointsEndToEndTests.cpp's own fixture shape. gte::Logger's own
// state is process-global (not owned by this fixture), so every TEST_F body
// itself is responsible for calling gte::Logger::Clear() first - see this
// file's own header comment.
class LogEndpointsEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_server = std::make_unique<Network::NetworkServer>();
        m_server->Start(0);
        ASSERT_TRUE(m_server->IsRunning());
        m_client = std::make_unique<httplib::Client>("127.0.0.1", m_server->BoundPort());
        WaitUntilAcceptingConnections(*m_server, *m_client);
    }

    void TearDown() override
    {
        m_client.reset();
        m_server.reset();
        // Leave no leftover entries behind for whatever test (in this file or
        // any other translation unit sharing this same test binary) happens
        // to run next.
        Logger::Clear();
    }

    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
};

TEST_F(LogEndpointsEndToEndTest, BasicFetchReturnsRealLoggedEntriesRoundTripped)
{
    Logger::Clear();

    Logger::SetCurrentFrame(10);
    GTE_LOG_INFO("Renderer", "renderer entry at frame 10");
    Logger::SetCurrentFrame(11);
    GTE_LOG_WARNING("Jobs", "jobs entry at frame 11");
    GTE_LOG_ERROR("Network", "network entry at frame 11");

    const httplib::Result res = m_client->Get("/get_logs");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["logging_enabled"], true);
    EXPECT_EQ(body["count"], 3);
    ASSERT_EQ(body["entries"].size(), 3u);

    EXPECT_EQ(body["entries"][0]["frame"], 10);
    EXPECT_EQ(body["entries"][0]["level"], "Info");
    EXPECT_EQ(body["entries"][0]["category"], "Renderer");
    EXPECT_EQ(body["entries"][0]["message"], "renderer entry at frame 10");

    EXPECT_EQ(body["entries"][1]["frame"], 11);
    EXPECT_EQ(body["entries"][1]["level"], "Warning");
    EXPECT_EQ(body["entries"][1]["category"], "Jobs");
    EXPECT_EQ(body["entries"][1]["message"], "jobs entry at frame 11");

    EXPECT_EQ(body["entries"][2]["frame"], 11);
    EXPECT_EQ(body["entries"][2]["level"], "Error");
    EXPECT_EQ(body["entries"][2]["category"], "Network");
    EXPECT_EQ(body["entries"][2]["message"], "network entry at frame 11");

    EXPECT_EQ(body["latest_id"], body["entries"][2]["id"]);
}

TEST_F(LogEndpointsEndToEndTest, SinceIdCursorReturnsOnlyLaterEntries)
{
    Logger::Clear();

    GTE_LOG_INFO("Renderer", "first entry");
    GTE_LOG_INFO("Renderer", "second entry");
    GTE_LOG_INFO("Renderer", "third entry");

    const httplib::Result firstRes = m_client->Get("/get_logs");
    ASSERT_TRUE(firstRes != nullptr);
    ASSERT_EQ(firstRes->status, 200);
    const nlohmann::json firstBody = nlohmann::json::parse(firstRes->body);
    ASSERT_EQ(firstBody["entries"].size(), 3u);
    // Read the real id of the 2nd entry back from the response itself -
    // never hard-code an assumed id (PHASE5's own Step 3.1 instruction).
    const std::uint64_t secondEntryId = firstBody["entries"][1]["id"].get<std::uint64_t>();

    const httplib::Result cursorRes = m_client->Get("/get_logs?since_id=" + std::to_string(secondEntryId));
    ASSERT_TRUE(cursorRes != nullptr);
    EXPECT_EQ(cursorRes->status, 200);
    const nlohmann::json cursorBody = nlohmann::json::parse(cursorRes->body);
    ASSERT_EQ(cursorBody["entries"].size(), 1u);
    EXPECT_EQ(cursorBody["entries"][0]["message"], "third entry");
}

TEST_F(LogEndpointsEndToEndTest, MinLevelFilterIsCaseInsensitiveAndInclusiveUpward)
{
    Logger::Clear();

    GTE_LOG_DEBUG("Renderer", "debug entry");
    GTE_LOG_INFO("Renderer", "info entry");
    GTE_LOG_WARNING("Renderer", "warning entry");
    GTE_LOG_ERROR("Renderer", "error entry");

    for (const std::string& casing : { std::string("warning"), std::string("Warning"), std::string("WARNING") }) {
        const httplib::Result res = m_client->Get("/get_logs?min_level=" + casing);
        ASSERT_TRUE(res != nullptr);
        ASSERT_EQ(res->status, 200);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        ASSERT_EQ(body["entries"].size(), 2u) << "casing was: " << casing;
        EXPECT_EQ(body["entries"][0]["message"], "warning entry");
        EXPECT_EQ(body["entries"][1]["message"], "error entry");
    }
}

TEST_F(LogEndpointsEndToEndTest, CategoryFilterMatchesExactlyAndUnknownCategoryIsEmptyNotAnError)
{
    Logger::Clear();

    GTE_LOG_INFO("Renderer", "renderer entry");
    GTE_LOG_INFO("Jobs", "jobs entry");

    const httplib::Result res = m_client->Get("/get_logs?category=Jobs");
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->status, 200);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_EQ(body["entries"].size(), 1u);
    EXPECT_EQ(body["entries"][0]["category"], "Jobs");
    EXPECT_EQ(body["entries"][0]["message"], "jobs entry");

    const httplib::Result unknownRes = m_client->Get("/get_logs?category=NoSuchCategory");
    ASSERT_TRUE(unknownRes != nullptr);
    EXPECT_EQ(unknownRes->status, 200);
    const nlohmann::json unknownBody = nlohmann::json::parse(unknownRes->body);
    EXPECT_EQ(unknownBody["count"], 0);
    EXPECT_EQ(unknownBody["entries"].size(), 0u);
}

TEST_F(LogEndpointsEndToEndTest, KeywordFilterIsCaseInsensitiveSubstringMatch)
{
    Logger::Clear();

    GTE_LOG_INFO("Renderer", "a message containing DistinctiveSubstring right here");
    GTE_LOG_INFO("Renderer", "an unrelated message");

    const httplib::Result res = m_client->Get("/get_logs?keyword=distinctivesubstring");
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->status, 200);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_EQ(body["entries"].size(), 1u);
    EXPECT_EQ(body["entries"][0]["message"], "a message containing DistinctiveSubstring right here");
}

TEST_F(LogEndpointsEndToEndTest, FrameMinMaxRangeIsInclusive)
{
    Logger::Clear();

    Logger::SetCurrentFrame(5);
    GTE_LOG_INFO("Renderer", "frame 5 entry");
    Logger::SetCurrentFrame(6);
    GTE_LOG_INFO("Renderer", "frame 6 entry");
    Logger::SetCurrentFrame(7);
    GTE_LOG_INFO("Renderer", "frame 7 entry");
    Logger::SetCurrentFrame(8);
    GTE_LOG_INFO("Renderer", "frame 8 entry");

    const httplib::Result res = m_client->Get("/get_logs?frame_min=6&frame_max=7");
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->status, 200);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_EQ(body["entries"].size(), 2u);
    EXPECT_EQ(body["entries"][0]["message"], "frame 6 entry");
    EXPECT_EQ(body["entries"][1]["message"], "frame 7 entry");
}

TEST_F(LogEndpointsEndToEndTest, LimitReturnsNewestNAndAcceptsButClampsAnOverlargeLimit)
{
    Logger::Clear();

    for (int i = 0; i < 10; ++i) {
        GTE_LOG_INFO("Renderer", "entry " + std::to_string(i));
    }

    const httplib::Result limitedRes = m_client->Get("/get_logs?limit=3");
    ASSERT_TRUE(limitedRes != nullptr);
    ASSERT_EQ(limitedRes->status, 200);
    const nlohmann::json limitedBody = nlohmann::json::parse(limitedRes->body);
    ASSERT_EQ(limitedBody["entries"].size(), 3u);
    // Newest 3 (7, 8, 9), still returned oldest-first among themselves.
    EXPECT_EQ(limitedBody["entries"][0]["message"], "entry 7");
    EXPECT_EQ(limitedBody["entries"][1]["message"], "entry 8");
    EXPECT_EQ(limitedBody["entries"][2]["message"], "entry 9");

    // A limit larger than Logger::kCapacity (2000) must still succeed (200,
    // not 400), silently capped rather than rejected - PHASE3's own
    // documented clamping behavior.
    const httplib::Result overlargeRes = m_client->Get("/get_logs?limit=999999");
    ASSERT_TRUE(overlargeRes != nullptr);
    EXPECT_EQ(overlargeRes->status, 200);
    const nlohmann::json overlargeBody = nlohmann::json::parse(overlargeRes->body);
    EXPECT_EQ(overlargeBody["entries"].size(), 10u);
}

TEST_F(LogEndpointsEndToEndTest, MalformedQueryParametersReturn400WithGenericErrorShape)
{
    Logger::Clear();

    const std::string badQueries[] = {
        "/get_logs?since_id=notanumber",
        "/get_logs?min_level=bogus",
        "/get_logs?frame_min=notanumber",
        "/get_logs?frame_max=notanumber",
        "/get_logs?limit=notanumber",
        "/get_logs?since_id=-5",
    };

    for (const std::string& query : badQueries) {
        const httplib::Result res = m_client->Get(query);
        ASSERT_TRUE(res != nullptr) << "query was: " << query;
        EXPECT_EQ(res->status, 400) << "query was: " << query;
        const nlohmann::json body = nlohmann::json::parse(res->body);
        EXPECT_EQ(body["success"], false) << "query was: " << query;
        ASSERT_TRUE(body.contains("error")) << "query was: " << query;
        EXPECT_FALSE(body["error"].get<std::string>().empty()) << "query was: " << query;
    }
}

TEST_F(LogEndpointsEndToEndTest, ClearLogsEmptiesBufferAndIdNeverResets)
{
    Logger::Clear();

    GTE_LOG_INFO("Renderer", "one");
    GTE_LOG_INFO("Renderer", "two");
    GTE_LOG_INFO("Renderer", "three");

    const httplib::Result clearRes = m_client->Post("/clear_logs");
    ASSERT_TRUE(clearRes != nullptr);
    EXPECT_EQ(clearRes->status, 200);
    const nlohmann::json clearBody = nlohmann::json::parse(clearRes->body);
    EXPECT_EQ(clearBody["success"], true);
    EXPECT_EQ(clearBody["cleared_count"], 3);

    const httplib::Result afterClearRes = m_client->Get("/get_logs");
    ASSERT_TRUE(afterClearRes != nullptr);
    ASSERT_EQ(afterClearRes->status, 200);
    const nlohmann::json afterClearBody = nlohmann::json::parse(afterClearRes->body);
    EXPECT_EQ(afterClearBody["count"], 0);

    GTE_LOG_INFO("Renderer", "after clear");
    const httplib::Result afterLogRes = m_client->Get("/get_logs");
    ASSERT_TRUE(afterLogRes != nullptr);
    ASSERT_EQ(afterLogRes->status, 200);
    const nlohmann::json afterLogBody = nlohmann::json::parse(afterLogRes->body);
    ASSERT_EQ(afterLogBody["entries"].size(), 1u);
    // The id keeps counting up from wherever it already was - never reset by
    // Clear() - so it must be strictly greater than 3 (the highest id
    // assigned before this test's own Clear() call above).
    EXPECT_GT(afterLogBody["entries"][0]["id"].get<std::uint64_t>(), 3u);
}

TEST_F(LogEndpointsEndToEndTest, CombinedFiltersComposeAsLogicalAnd)
{
    Logger::Clear();

    Logger::SetCurrentFrame(1);
    GTE_LOG_WARNING("Renderer", "renderer warning at frame 1");
    Logger::SetCurrentFrame(2);
    GTE_LOG_WARNING("Renderer", "renderer warning at frame 2");
    Logger::SetCurrentFrame(2);
    GTE_LOG_INFO("Renderer", "renderer info at frame 2");
    Logger::SetCurrentFrame(2);
    GTE_LOG_WARNING("Jobs", "jobs warning at frame 2");

    // min_level=warning AND category=Renderer AND frame_min=2 AND
    // frame_max=2 - only "renderer warning at frame 2" satisfies all four.
    const httplib::Result res =
        m_client->Get("/get_logs?min_level=warning&category=Renderer&frame_min=2&frame_max=2");
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->status, 200);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_EQ(body["entries"].size(), 1u);
    EXPECT_EQ(body["entries"][0]["message"], "renderer warning at frame 2");
}

} // namespace
} // namespace gte
