// End-to-end tests for POST /import_asset - task_manager/stl-parser-2
// campaign, PHASE2 (PHASE2_IMPORT_ASSET_NETWORK_ENDPOINT.md, Step 3.7).
// Mirrors tests/Network/ActivateTabEndpointEndToEndTests.cpp's EXACT shape: a
// real gte::AssetImportCommandBridge + a real gte::Network::NetworkServer
// bound to an ephemeral port, a small FAKE stand-in thread that services the
// bridge the way Application::Run()'s real per-frame drain loop would (and,
// transitively, for IEditorLayer::ImportExternalAssetIntoProject()/
// ProjectPanel::ImportExternalFile() themselves), real HTTP requests via
// httplib::Client.
//
// There is no live Application/Editor/ProjectPanel/AssetDatabase anywhere in
// this file - per this codebase's established "Tier 2, no automated coverage
// yet" acceptance for anything genuinely Editor/filesystem-dependent
// (AGENTS.md, "Testability & Regression Safety"; see also PHASE1's own
// accepted gap for ProjectPanel::ImportExternalFile() itself), the real
// import pipeline stays covered by this phase's own manual sanity check
// (a small throwaway external file through a real running engine - see
// PHASE2_COMPLETION_REPORT.md) plus PHASE5's own real terrain.stl smoke test.
// What IS tested here, for real, is everything this phase actually invented
// that ISN'T ProjectPanel/AssetDatabase plumbing: the
// AssetImportCommandBridge <-> NetworkServer cross-thread handshake, the HTTP
// status-code mapping (200/400/503/504), and the real JSON request/response
// bodies round-tripping over a real socket.

#include "Application/AssetImportCommandBridge.h"
#include "Network/NetworkServer.h"

#include "NetworkTestHelpers.h"

#include <httplib.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace gte {
namespace {

// The Editor/ProjectPanel/AssetDatabase-free stand-in for Application::Run()'s
// real per-frame /import_asset drain (see this phase document's own Step
// 3.7). A tight polling loop on its own dedicated thread that fulfills any
// pending request with a configurable, pre-set ImportExternalFileOutcome -
// deliberately NOT trying to replicate ProjectPanel::ImportExternalFile()'s
// real filesystem/AssetImporter behavior (that stays PHASE1's own
// code-inspection-verified, PHASE5's own live-smoke-tested responsibility) -
// this stand-in only needs to prove the HTTP <-> bridge <-> "main thread"
// wiring itself works, the same "deliberately NOT trying to replicate the
// real logic" philosophy every sibling end-to-end test file already
// establishes (FakeEditorUiStandIn/FakeEngineCommandStandIn).
class FakeAssetImportStandIn {
public:
    explicit FakeAssetImportStandIn(AssetImportCommandBridge& bridge)
        : m_bridge(bridge)
    {
        m_thread = std::thread([this] { Run(); });
    }

    ~FakeAssetImportStandIn()
    {
        m_stop.store(true);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    FakeAssetImportStandIn(const FakeAssetImportStandIn&) = delete;
    FakeAssetImportStandIn& operator=(const FakeAssetImportStandIn&) = delete;

    // Gating (mirrors every sibling end-to-end test file's own SetGated) is
    // what lets a request stay genuinely pending on purpose - used by the
    // timeout/already-pending tests below.
    void SetGated(bool gated) { m_gated.store(gated); }

    // Configures what the NEXT fulfilled request will report - defaults to
    // a plausible happy-path STL import, overridden per-test as needed.
    void SetNextOutcome(const ImportExternalFileOutcome& outcome)
    {
        std::lock_guard<std::mutex> lock(m_outcomeMutex);
        m_nextOutcome = outcome;
    }

private:
    void Run()
    {
        while (!m_stop.load()) {
            if (!m_gated.load()) {
                if (const std::optional<AssetImportCommandRequest> request = m_bridge.TryPeekPendingCommandRequest()) {
                    AssetImportCommandResult result;
                    result.kind = request->kind;
                    {
                        std::lock_guard<std::mutex> lock(m_outcomeMutex);
                        result.importExternalFile = m_nextOutcome;
                    }
                    m_bridge.FulfillCommand(result);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    AssetImportCommandBridge& m_bridge;
    std::atomic<bool> m_gated{ false };
    std::atomic<bool> m_stop{ false };
    std::mutex m_outcomeMutex;
    ImportExternalFileOutcome m_nextOutcome = [] {
        ImportExternalFileOutcome outcome;
        outcome.projectAvailable = true;
        outcome.success = true;
        outcome.message = "imported OK";
        outcome.finalRelativePath = "terrain.gta";
        outcome.finalAbsolutePath = "C:/Project/terrain.gta";
        outcome.guid = "12345678-1234-1234-1234-123456789012";
        outcome.convertedToMeshAsset = true;
        outcome.meshSourceFormat = "stl";
        outcome.meshVertexCount = 3136374;
        outcome.meshTriangleCount = 1045458;
        return outcome;
    }();
    std::thread m_thread;
};

// A real gte::AssetImportCommandBridge + a real gte::Network::NetworkServer
// (captureBridge/commandBridge/uiCommandBridge/frameDebuggerCommandBridge
// deliberately left nullptr - /import_asset never touches any of them),
// started on an ephemeral port, wired together exactly like gte::Application
// does in production.
class ImportAssetEndpointEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_standIn = std::make_unique<FakeAssetImportStandIn>(m_bridge);
        m_server = std::make_unique<Network::NetworkServer>(nullptr, nullptr, nullptr, nullptr, &m_bridge);
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

    AssetImportCommandBridge m_bridge;
    std::unique_ptr<FakeAssetImportStandIn> m_standIn;
    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
};

TEST_F(ImportAssetEndpointEndToEndTest, ValidRequestFulfilledWithSuccessReturns200WithExactBody)
{
    const httplib::Result res =
        m_client->Post("/import_asset", R"({"source_path":"C:\\reference\\terrain.stl"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["message"], "imported OK");
    EXPECT_EQ(parsed["final_relative_path"], "terrain.gta");
    EXPECT_EQ(parsed["final_absolute_path"], "C:/Project/terrain.gta");
    EXPECT_EQ(parsed["guid"], "12345678-1234-1234-1234-123456789012");
    EXPECT_EQ(parsed["converted_to_mesh_asset"], true);
    EXPECT_EQ(parsed["mesh_source_format"], "stl");
    EXPECT_EQ(parsed["mesh_vertex_count"], 3136374);
    EXPECT_EQ(parsed["mesh_triangle_count"], 1045458);
}

TEST_F(ImportAssetEndpointEndToEndTest, ValidRequestFulfilledWithFailureReturns400)
{
    ImportExternalFileOutcome outcome;
    outcome.projectAvailable = true;
    outcome.success = false;
    outcome.message = "source file does not exist";
    m_standIn->SetNextOutcome(outcome);

    const httplib::Result res =
        m_client->Post("/import_asset", R"({"source_path":"C:\\does\\not\\exist.stl"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
    EXPECT_EQ(parsed["error"], "source file does not exist");
}

TEST_F(ImportAssetEndpointEndToEndTest, ProjectPanelUnavailableReturns503)
{
    ImportExternalFileOutcome outcome;
    outcome.projectAvailable = false;
    m_standIn->SetNextOutcome(outcome);

    const httplib::Result res =
        m_client->Post("/import_asset", R"({"source_path":"C:\\some\\file.stl"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 503);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

TEST_F(ImportAssetEndpointEndToEndTest, MissingSourcePathReturns400)
{
    const httplib::Result res = m_client->Post("/import_asset", R"({})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

TEST_F(ImportAssetEndpointEndToEndTest, MalformedJsonReturns400)
{
    const httplib::Result res = m_client->Post("/import_asset", "{not valid json", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
}

TEST_F(ImportAssetEndpointEndToEndTest, SecondConcurrentRequestReturns503Immediately)
{
    m_standIn->SetGated(true);

    std::thread firstRequester([this] {
        httplib::Client firstClient("127.0.0.1", m_server->BoundPort());
        firstClient.Post("/import_asset", R"({"source_path":"C:\\first.stl"})", "application/json");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto start = std::chrono::steady_clock::now();
    const httplib::Result second =
        m_client->Post("/import_asset", R"({"source_path":"C:\\second.stl"})", "application/json");
    const auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_TRUE(second != nullptr);
    EXPECT_EQ(second->status, 503);
    EXPECT_LT(elapsed, std::chrono::milliseconds(1000));

    m_standIn->SetGated(false);
    firstRequester.join();
}

// Per this phase document's own precedent (ActivateTabEndpointEndToEndTests.cpp's
// own ActivateTabReturns504WhenNeverFulfilled): never wait out
// AssetImportCommandBridge's real 120000ms default production timeout over a
// real HTTP round trip - call SubmitAndWait() directly with a per-call SHORT
// override instead, bypassing HTTP entirely. This is still a full, honest
// regression proof of the exact behavior the HTTP route forwards verbatim
// (see NetworkServer.cpp's "/import_asset" handler) - timedOut -> HTTP 504 -
// without needing a slow, real ~120-second round trip in the test suite.
TEST_F(ImportAssetEndpointEndToEndTest, TimedOutSubmissionIsWhatTheHttpRouteMapsToA504)
{
    m_standIn->SetGated(true);

    AssetImportCommandRequest request;
    request.kind = AssetImportCommandKind::ImportExternalFile;
    request.importExternalFile.sourceAbsolutePath = "C:\\whatever.stl";
    const AssetImportCommandBridge::SubmitResult result = m_bridge.SubmitAndWait(request, 100);

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.result.has_value());

    m_standIn->SetGated(false);
}

// A NetworkServer constructed with assetImportCommandBridge == nullptr
// (mirrors production's own "no asset-import-touching routes wired up"
// contract, see NetworkServer.h's own constructor doc comment) must respond
// 503 for POST /import_asset rather than crashing.
TEST(ImportAssetEndpointNullBridgeTests, ImportAssetReturns503WhenBridgeIsNull)
{
    Network::NetworkServer server; // Every bridge defaults to nullptr.
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());
    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    const httplib::Result res =
        client.Post("/import_asset", R"({"source_path":"C:\\some\\file.stl"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 503);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);

    server.Stop();
}

} // namespace
} // namespace gte
