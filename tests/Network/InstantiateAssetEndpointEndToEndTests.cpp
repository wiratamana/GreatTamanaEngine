// End-to-end tests for POST /instantiate_asset - task_manager/stl-parser-2
// campaign, PHASE4 (PHASE4_INSTANTIATE_ASSET_NETWORK_ENDPOINT.md, Step 3.4).
// Mirrors tests/Network/EngineCommandEndpointsEndToEndTests.cpp's EXACT shape
// (confirmed the correct template during this campaign's Iteration 2
// double-check - not ActivateTabEndpointEndToEndTests.cpp, which exercises
// the structurally different EditorUiCommandBridge with its own 404/409
// status-code semantics that don't apply here): a real gte::EngineCommandBridge
// + a real gte::Network::NetworkServer, started on an ephemeral port, hit
// over a REAL loopback socket via httplib::Client - with a small, dedicated,
// GPU/ECS-free stand-in (FakeInstantiateMeshAssetStandIn, below) servicing
// ONLY EngineCommandKind::InstantiateMeshAsset the way Application::Run()'s
// real per-frame TryPeekPendingCommandRequest()/FulfillCommand() drain would
// (and, transitively, for Game::InstantiateMeshAssetFromGtaFile() itself).
// This stand-in is DELIBERATELY its own small, separate class in this file
// rather than reusing EngineCommandEndpointsEndToEndTests.cpp's own
// FakeEngineCommandStandIn (which lives in that file's own anonymous
// namespace and isn't reachable from here) - see this phase document's own
// Step 3.4 for why either approach was acceptable.
//
// There is no live Application/Game/Renderer/Vulkan device anywhere in this
// file - per this codebase's established "Tier 2, no automated coverage yet"
// acceptance for anything genuinely GPU/Renderer-dependent (AGENTS.md,
// "Testability & Regression Safety"; see also PHASE3's own accepted gap for
// Game::InstantiateMeshAssetFromGtaFile() itself), the real spawn pipeline
// stays covered by PHASE4's own manual sanity check (a small already-
// imported Mesh *.gta through a real running engine) plus PHASE5's own real
// terrain.stl smoke test. What IS tested here, for real, is everything this
// phase actually invented that ISN'T ordinary ECS/GPU plumbing: the
// EngineCommandBridge <-> NetworkServer cross-thread handshake (already
// proven generically by the sibling file above, re-proven here for this
// FIFTH EngineCommandKind specifically), the HTTP status-code mapping (200/
// 400/503/504), and the real JSON request/response bodies round-tripping
// over a real socket.

#include "Application/EngineCommandBridge.h"
#include "Network/NetworkServer.h"

#include "NetworkTestHelpers.h"

#include <httplib.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace gte {
namespace {

// The GPU/ECS-free stand-in for Application::Run()'s real per-frame
// InstantiateMeshAsset drain. A tight polling loop on its own dedicated
// thread that fulfills any pending InstantiateMeshAsset request with a
// configurable, pre-set InstantiateMeshAssetOutcome - deliberately NOT
// trying to replicate Game::InstantiateMeshAssetFromGtaFile()'s real
// GPU-upload/ECS-hierarchy behavior (that stays PHASE3's own accepted,
// documented Tier-2 gap) - this stand-in only needs to prove the HTTP <->
// bridge <-> "main thread" wiring itself works, the same philosophy every
// sibling end-to-end test file already establishes (FakeEngineCommandStandIn/
// FakeAssetImportStandIn/FakeEditorUiStandIn).
class FakeInstantiateMeshAssetStandIn {
public:
    explicit FakeInstantiateMeshAssetStandIn(EngineCommandBridge& bridge)
        : m_bridge(bridge)
    {
        m_thread = std::thread([this] { Run(); });
    }

    ~FakeInstantiateMeshAssetStandIn()
    {
        m_stop.store(true);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    FakeInstantiateMeshAssetStandIn(const FakeInstantiateMeshAssetStandIn&) = delete;
    FakeInstantiateMeshAssetStandIn& operator=(const FakeInstantiateMeshAssetStandIn&) = delete;

    // Gating (mirrors every sibling end-to-end test file's own SetGated) is
    // what lets a request stay genuinely pending on purpose - used by the
    // timeout/already-pending tests below.
    void SetGated(bool gated) { m_gated.store(gated); }

    // Configures what the NEXT fulfilled request will report - defaults to
    // a plausible happy-path spawn, overridden per-test as needed.
    void SetNextOutcome(const InstantiateMeshAssetOutcome& outcome)
    {
        std::lock_guard<std::mutex> lock(m_outcomeMutex);
        m_nextOutcome = outcome;
    }

private:
    void Run()
    {
        while (!m_stop.load()) {
            if (!m_gated.load()) {
                if (const std::optional<EngineCommandRequest> request = m_bridge.TryPeekPendingCommandRequest()) {
                    EngineCommandResult result;
                    result.kind = request->kind;
                    if (request->kind == EngineCommandKind::InstantiateMeshAsset) {
                        std::lock_guard<std::mutex> lock(m_outcomeMutex);
                        result.instantiateMeshAsset = m_nextOutcome;
                    }
                    m_bridge.FulfillCommand(result);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    EngineCommandBridge& m_bridge;
    std::atomic<bool> m_gated{ false };
    std::atomic<bool> m_stop{ false };
    std::mutex m_outcomeMutex;
    InstantiateMeshAssetOutcome m_nextOutcome = [] {
        InstantiateMeshAssetOutcome outcome;
        outcome.success = true;
        outcome.entityIndex = 7;
        outcome.entityGeneration = 0;
        outcome.resolvedName = "terrain";
        return outcome;
    }();
    std::thread m_thread;
};

// A real gte::EngineCommandBridge + a real gte::Network::NetworkServer
// (captureBridge deliberately left nullptr - this route never touches it),
// started on an ephemeral port, wired together exactly like gte::Application
// does in production.
class InstantiateAssetEndpointEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_standIn = std::make_unique<FakeInstantiateMeshAssetStandIn>(m_bridge);
        m_server = std::make_unique<Network::NetworkServer>(nullptr, &m_bridge);
        m_server->Start(0);
        ASSERT_TRUE(m_server->IsRunning());
        m_client = std::make_unique<httplib::Client>("127.0.0.1", m_server->BoundPort());
        Network::TestHelpers::WaitUntilAcceptingConnections(*m_server, *m_client);
    }

    void TearDown() override
    {
        m_client.reset();
        m_server.reset();
        m_standIn.reset();
    }

    EngineCommandBridge m_bridge;
    std::unique_ptr<FakeInstantiateMeshAssetStandIn> m_standIn;
    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
};

TEST_F(InstantiateAssetEndpointEndToEndTest, ValidRequestFulfilledWithSuccessReturns200WithExactBody)
{
    const httplib::Result res = m_client->Post(
        "/instantiate_asset", R"({"gta_path":"C:\\Project\\terrain.gta"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["entity"]["index"], 7);
    EXPECT_EQ(parsed["entity"]["generation"], 0);
    EXPECT_EQ(parsed["name"], "terrain");
}

TEST_F(InstantiateAssetEndpointEndToEndTest, ValidRequestFulfilledWithFailureReturns400)
{
    InstantiateMeshAssetOutcome outcome;
    outcome.success = false;
    outcome.errorMessage = "gta_path does not resolve to a valid Mesh asset";
    m_standIn->SetNextOutcome(outcome);

    const httplib::Result res = m_client->Post(
        "/instantiate_asset", R"({"gta_path":"C:\\does\\not\\exist.gta"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
    EXPECT_EQ(parsed["error"], "gta_path does not resolve to a valid Mesh asset");
}

TEST_F(InstantiateAssetEndpointEndToEndTest, MissingGtaPathReturns400)
{
    const httplib::Result res = m_client->Post("/instantiate_asset", R"({})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

TEST_F(InstantiateAssetEndpointEndToEndTest, MalformedJsonReturns400)
{
    const httplib::Result res = m_client->Post("/instantiate_asset", "{not valid json", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
}

TEST_F(InstantiateAssetEndpointEndToEndTest, SecondConcurrentRequestReturns503Immediately)
{
    m_standIn->SetGated(true);

    std::thread firstRequester([this] {
        httplib::Client firstClient("127.0.0.1", m_server->BoundPort());
        firstClient.Post("/instantiate_asset", R"({"gta_path":"C:\\first.gta"})", "application/json");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto start = std::chrono::steady_clock::now();
    const httplib::Result second =
        m_client->Post("/instantiate_asset", R"({"gta_path":"C:\\second.gta"})", "application/json");
    const auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_TRUE(second != nullptr);
    EXPECT_EQ(second->status, 503);
    EXPECT_LT(elapsed, std::chrono::milliseconds(1000));

    m_standIn->SetGated(false);
    firstRequester.join();
}

// Per this phase document's own Section 3.3 note, and every sibling
// end-to-end test file's own precedent: never wait out EngineCommandBridge's
// real 3000ms default production timeout over a real HTTP round trip - call
// SubmitAndWait() directly with a per-call SHORT override instead, bypassing
// HTTP entirely. This is still a full, honest regression proof of the exact
// behavior the HTTP route forwards verbatim (see NetworkServer.cpp's
// "/instantiate_asset" handler) - timedOut -> HTTP 504 - without needing a
// slow, real ~3-second round trip in the test suite.
TEST_F(InstantiateAssetEndpointEndToEndTest, TimedOutSubmissionIsWhatTheHttpRouteMapsToA504)
{
    m_standIn->SetGated(true);

    EngineCommandRequest request;
    request.kind = EngineCommandKind::InstantiateMeshAsset;
    request.instantiateMeshAsset.absoluteGtaPath = "C:\\whatever.gta";
    const EngineCommandBridge::SubmitResult result = m_bridge.SubmitAndWait(request, 100);

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.result.has_value());

    m_standIn->SetGated(false);
}

// A NetworkServer constructed with commandBridge == nullptr (mirrors
// production's own "no engine-state-touching routes wired up" contract, see
// NetworkServer.h's own constructor doc comment) must respond 503 for POST
// /instantiate_asset rather than crashing.
TEST(InstantiateAssetEndpointNullBridgeTests, InstantiateAssetReturns503WhenBridgeIsNull)
{
    Network::NetworkServer server; // Every bridge defaults to nullptr.
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());
    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    const httplib::Result res =
        client.Post("/instantiate_asset", R"({"gta_path":"C:\\some\\file.gta"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 503);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);

    server.Stop();
}

} // namespace
} // namespace gte
