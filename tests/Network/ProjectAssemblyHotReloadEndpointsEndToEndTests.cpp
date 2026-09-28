// End-to-end tests for the 7 new GET/POST /project_assembly/* routes -
// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1), PHASE3 (PHASE3_HTTP_ROUTES_AND_HOST_WIRING.md, Section 3.7).
// Mirrors tests/Network/LogEndpointsEndToEndTests.cpp's own proven shape for
// the 4 bridge-free OBSERVE routes (a real gte::Network::NetworkServer,
// started on an OS-assigned ephemeral port, driven with a real
// httplib::Client, wired against a real EditorHotReloadDebugCapability
// instance - the 8th constructor argument) and
// tests/Network/EngineCommandEndpointsEndToEndTests.cpp's own
// FakeEngineCommandStandIn precedent for GET /project_assembly/debug/
// scene_snapshot, the ONE route this campaign routes through
// EngineCommandBridge instead (see this campaign's PHASE0 doc, Correction 1).
//
// NOTE on POST /project_assembly/debug/compile_only: this route's real body
// (EditorHotReloadDebugCapability::TriggerCompileOnly()) kicks off a REAL,
// short-lived `cmake --build <buildDir> --target <name>_Game` child process
// on a JobSystem-registered background thread (see
// Core/Plugins/ProjectAssemblyBuildRunner.cpp) - this is INTENTIONAL, not an
// accident: PHASE0_MASTER_STRATEGY.md's own Definition of Done requires this
// route to trigger a REAL compile, and this campaign's own PHASE3 phase
// document (Section 3.7) explicitly asks for a 200/{"started":true}
// assertion here. A deliberately-fake project name is used below
// ("NetworkTestFakeProject") so the real `cmake --build` invocation fails
// almost instantly with a harmless "unknown target"/"no rule to make
// target" line (logged, never asserted on) rather than actually compiling
// anything real - GreatTamanaEngineTests.exe's own JobSystem shutdown
// blocks briefly until that background thread's `cmake` child process
// exits, exactly like it would for any other Project Assembly build.

#include "Application/EngineCommandBridge.h"
#include "Application/ProjectAssemblyHotReloadCommandBridge.h"
#include "Editor/EditorHotReloadDebugCapability.h"
#include "Network/NetworkServer.h"

#include "NetworkTestHelpers.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gte {
namespace {

// A real gte::Network::NetworkServer, started on an ephemeral port, wired
// with a real EditorHotReloadDebugCapability instance (the 8th constructor
// argument) - a fresh instance per TEST_F, mirroring
// LogEndpointsEndToEndTest's own fixture shape exactly. Covers the 4
// bridge-free OBSERVE routes plus both TRIGGER routes (compile_only/
// hot_reload) - none of these need commandBridge at all.
class ProjectAssemblyHotReloadEndpointsEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_server = std::make_unique<Network::NetworkServer>(
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &m_capability);
        m_server->Start(0);
        ASSERT_TRUE(m_server->IsRunning());
        m_client = std::make_unique<httplib::Client>("127.0.0.1", m_server->BoundPort());
        Network::TestHelpers::WaitUntilAcceptingConnections(*m_server, *m_client);
    }

    void TearDown() override
    {
        m_client.reset();
        m_server.reset();
    }

    EditorHotReloadDebugCapability m_capability;
    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
};

TEST_F(ProjectAssemblyHotReloadEndpointsEndToEndTest, HotReloadStatusReturns200WithWellFormedShape)
{
    const httplib::Result res = m_client->Get("/project_assembly/hot_reload/status");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const nlohmann::json body = nlohmann::json::parse(res->body);
    // No production code anywhere calls Set()/Finish() on the underlying
    // ProjectAssemblyHotReloadDebugStatus singleton this campaign - only
    // Core/Plugins/ProjectAssemblyHotReloadDebugStatusTests.cpp's own,
    // separate test file does, for its own isolated coverage - so this
    // route's real, live value is expected to be "Idle" in practice.
    // Asserting the well-formed SHAPE (rather than a brittle cross-test-file
    // ordering assumption) is what this test actually needs to prove: the
    // route/capability/singleton wiring itself works.
    ASSERT_TRUE(body["phase"].is_string());
    ASSERT_TRUE(body["project_name"].is_string());
    ASSERT_TRUE(body["cycle_id"].is_number_unsigned());
    ASSERT_TRUE(body["phase_elapsed_ms"].is_number_unsigned());
    ASSERT_TRUE(body["last_outcome"].is_string());
    ASSERT_TRUE(body["last_error_message"].is_string());
}

TEST_F(ProjectAssemblyHotReloadEndpointsEndToEndTest, LedgerMissingNameQueryParamReturns400)
{
    const httplib::Result res = m_client->Get("/project_assembly/debug/ledger");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["success"], false);
    ASSERT_TRUE(body.contains("error"));
    EXPECT_FALSE(body["error"].get<std::string>().empty());
}

TEST_F(ProjectAssemblyHotReloadEndpointsEndToEndTest, LedgerReturnsHonestEmptyPlaceholderListsForAnyProjectName)
{
    const httplib::Result res = m_client->Get("/project_assembly/debug/ledger?name=SomeProject");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["project_name"], "SomeProject");
    ASSERT_TRUE(body["render_pass_names"].is_array());
    EXPECT_TRUE(body["render_pass_names"].empty());
    ASSERT_TRUE(body["panel_names"].is_array());
    EXPECT_TRUE(body["panel_names"].empty());
    ASSERT_TRUE(body["component_type_names"].is_array());
    EXPECT_TRUE(body["component_type_names"].empty());
}

TEST_F(ProjectAssemblyHotReloadEndpointsEndToEndTest, LoadedAssembliesReturnsHonestEmptyPlaceholderList)
{
    const httplib::Result res = m_client->Get("/project_assembly/debug/loaded_assemblies");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_TRUE(body["dll_file_names"].is_array());
    EXPECT_TRUE(body["dll_file_names"].empty());
}

// Genuinely REAL, live data (not a placeholder) - see
// IHotReloadDebugCapability::GetRegisteredComponentTypeNames()'s own doc
// comment. The exact type-name set is confirmed real/registered by
// tests/ECS/Reflection/BuiltinComponentReflectionTests.cpp - this test only
// needs to prove the route/capability wiring surfaces that same real data,
// not re-derive the full list.
TEST_F(ProjectAssemblyHotReloadEndpointsEndToEndTest, ComponentTypesReturnsRealNonEmptyListIncludingBuiltins)
{
    const httplib::Result res = m_client->Get("/project_assembly/debug/component_types");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_TRUE(body["type_names"].is_array());
    EXPECT_FALSE(body["type_names"].empty());

    const std::vector<std::string> typeNames = body["type_names"].get<std::vector<std::string>>();
    for (const std::string& expected : { "Transform", "Name", "Camera", "DirectionalLight", "PrimitiveSource" }) {
        EXPECT_NE(std::find(typeNames.begin(), typeNames.end(), expected), typeNames.end())
            << "expected component type name not found: " << expected;
    }
}

TEST_F(ProjectAssemblyHotReloadEndpointsEndToEndTest, CompileOnlyMissingNameQueryParamReturns400)
{
    const httplib::Result res = m_client->Post("/project_assembly/debug/compile_only");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["success"], false);
}

TEST_F(ProjectAssemblyHotReloadEndpointsEndToEndTest, CompileOnlyValidNameReturns200Started)
{
    // See this file's own header comment for why "NetworkTestFakeProject" is
    // deliberately a name with no real Project Assembly behind it.
    const httplib::Result res = m_client->Post("/project_assembly/debug/compile_only?name=NetworkTestFakeProject");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["started"], true);
}

TEST_F(ProjectAssemblyHotReloadEndpointsEndToEndTest, HotReloadMissingNameQueryParamReturns400)
{
    const httplib::Result res = m_client->Post("/project_assembly/hot_reload");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["success"], false);
}

// --- POST /project_assembly/hot_reload - the two new tests replacing the
// now-obsolete HotReloadValidNameReturns501NotImplemented (editor-core-
// separation-14 campaign, BIG-STEP 3, PHASE4, Section 3.6 - that route
// never answers 501 again once PHASE4's real route-handler body lands).

// This file's own existing m_capability fixture (above) never calls
// SetHotReloadCommandBridge() (there is no EditorHost in this network-only
// fixture) - so TriggerHotReload() hits its own m_hotReloadCommandBridge ==
// nullptr guard (PHASE3) and returns false immediately, proving the route's
// OWN new 503 branch (PHASE4, NetworkServer.cpp), never the deleted 501 one.
TEST_F(ProjectAssemblyHotReloadEndpointsEndToEndTest, HotReloadValidNameReturns503WhenBridgeIsNotWired)
{
    const httplib::Result res = m_client->Post("/project_assembly/hot_reload?name=SomeProject");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 503);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["success"], false);
    ASSERT_TRUE(body.contains("error"));
    EXPECT_FALSE(body["error"].get<std::string>().empty());
}

// Mirrors FakeSceneSnapshotStandIn's own precedent below in this same file
// exactly: a small background thread that loops calling
// ProjectAssemblyHotReloadCommandBridge::TryPeekPendingProjectName()/
// FulfillPending() - it deliberately NEVER calls the real
// PerformProjectAssemblyHotReload(), exactly like FakeSceneSnapshotStandIn
// never calls the real BuildSceneSnapshotJson(). Proves the
// capability<->bridge<->route wiring succeeds end-to-end with a real 200
// response and a well-formed status JSON body, without needing a real
// EditorHost/compile/reload at all - that end-to-end proof is PHASE5's own
// live verification job, not this Tier-1-adjacent test's.
class FakeHotReloadStandIn {
public:
    explicit FakeHotReloadStandIn(ProjectAssemblyHotReloadCommandBridge& bridge)
        : m_bridge(bridge)
    {
        m_thread = std::thread([this] { Run(); });
    }

    ~FakeHotReloadStandIn()
    {
        m_stop.store(true);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    FakeHotReloadStandIn(const FakeHotReloadStandIn&) = delete;
    FakeHotReloadStandIn& operator=(const FakeHotReloadStandIn&) = delete;

private:
    void Run()
    {
        while (!m_stop.load()) {
            if (m_bridge.TryPeekPendingProjectName().has_value()) {
                m_bridge.FulfillPending();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    ProjectAssemblyHotReloadCommandBridge& m_bridge;
    std::atomic<bool> m_stop{ false };
    std::thread m_thread;
};

class ProjectAssemblyHotReloadTriggerEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Unlike the OTHER capability-fixture class in this file, this one
        // DOES call SetHotReloadCommandBridge() - that is the entire point
        // of this test.
        m_capability.SetHotReloadCommandBridge(m_bridge);
        m_standIn = std::make_unique<FakeHotReloadStandIn>(m_bridge);
        m_server = std::make_unique<Network::NetworkServer>(
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &m_capability);
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

    ProjectAssemblyHotReloadCommandBridge m_bridge;
    EditorHotReloadDebugCapability m_capability;
    std::unique_ptr<FakeHotReloadStandIn> m_standIn;
    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
};

TEST_F(ProjectAssemblyHotReloadTriggerEndToEndTest, HotReloadValidNameReturns200WithRealStatusBodyWhenBridgeIsServiced)
{
    const httplib::Result res = m_client->Post("/project_assembly/hot_reload?name=SomeProject");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    // The real GetHotReloadStatus() body shape (this is the SAME builder
    // GET /project_assembly/hot_reload/status already uses) - not asserting
    // on a specific phase/outcome value since FakeHotReloadStandIn never
    // calls the real orchestrator (see this class's own doc comment above),
    // only proving the wiring itself produced a well-formed response.
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_TRUE(body["phase"].is_string());
    ASSERT_TRUE(body["project_name"].is_string());
    ASSERT_TRUE(body["cycle_id"].is_number_unsigned());
    ASSERT_TRUE(body["phase_elapsed_ms"].is_number_unsigned());
    ASSERT_TRUE(body["last_outcome"].is_string());
    ASSERT_TRUE(body["last_error_message"].is_string());
}

// --- GET /project_assembly/debug/scene_snapshot - the ONE OBSERVE route
// routed through EngineCommandBridge instead of a direct capability call
// (this campaign's PHASE0 doc, Correction 1). Mirrors
// EngineCommandEndpointsEndToEndTests.cpp's own FakeEngineCommandStandIn
// precedent: a GPU/ECS/Game-free stand-in thread that only answers
// EngineCommandKind::GetSceneSnapshot with a small, fixed, fake
// GetSceneSnapshotOutcome - this proves the bridge/route wiring, never
// EditorHotReloadDebugCapability::BuildSceneSnapshotJson()'s own real body
// (that stays covered only by PHASE4's manual live smoke test, matching
// this campaign's own accepted Tier-2-adjacent scope for anything touching
// a real Game&).
class FakeSceneSnapshotStandIn {
public:
    explicit FakeSceneSnapshotStandIn(EngineCommandBridge& bridge)
        : m_bridge(bridge)
    {
        m_thread = std::thread([this] { Run(); });
    }

    ~FakeSceneSnapshotStandIn()
    {
        m_stop.store(true);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    FakeSceneSnapshotStandIn(const FakeSceneSnapshotStandIn&) = delete;
    FakeSceneSnapshotStandIn& operator=(const FakeSceneSnapshotStandIn&) = delete;

private:
    void Run()
    {
        while (!m_stop.load()) {
            if (const std::optional<EngineCommandRequest> request = m_bridge.TryPeekPendingCommandRequest()) {
                EngineCommandResult result;
                result.kind = request->kind;
                if (request->kind == EngineCommandKind::GetSceneSnapshot) {
                    result.getSceneSnapshot.success = true;
                    result.getSceneSnapshot.editorAvailable = true;
                    result.getSceneSnapshot.sceneJson = R"({"entities":[],"fake":"scene_snapshot_stand_in"})";
                }
                m_bridge.FulfillCommand(result);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    EngineCommandBridge& m_bridge;
    std::atomic<bool> m_stop{ false };
    std::thread m_thread;
};

class ProjectAssemblyHotReloadSceneSnapshotEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_standIn = std::make_unique<FakeSceneSnapshotStandIn>(m_bridge);
        // hotReloadDebugCapability (8th argument) is deliberately left
        // nullptr here - scene_snapshot never consults it (see
        // NetworkServer.cpp's own route lambda, which only captures
        // commandBridge).
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
    std::unique_ptr<FakeSceneSnapshotStandIn> m_standIn;
    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
};

TEST_F(ProjectAssemblyHotReloadSceneSnapshotEndToEndTest, ValidRequestReturns200WithRawSceneJsonBody)
{
    const httplib::Result res = m_client->Get("/project_assembly/debug/scene_snapshot");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    // The response body is the RAW GetSceneSnapshotOutcome::sceneJson string,
    // set directly (no wrapping "success"/"data" envelope) - see
    // NetworkRoutes.h's own doc comment on why this route has no dedicated
    // Build*ResponseJson() function.
    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["fake"], "scene_snapshot_stand_in");
}

TEST(ProjectAssemblyHotReloadSceneSnapshotNoBridgeTests, MissingBridgeReturns503)
{
    Network::NetworkServer server; // Every bridge/capability pointer defaults to nullptr.
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());
    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    const httplib::Result res = client.Get("/project_assembly/debug/scene_snapshot");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 503);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["success"], false);

    server.Stop();
}

// A NetworkServer constructed with every bridge/capability pointer
// defaulted to nullptr (mirrors production's own "nothing wired up"
// contract) must respond 503 for every one of the 7 routes this campaign
// adds, never crash - mirrors LogEndpointsNullCapabilityTests's own exact
// convention (tests/Network/LogEndpointsEndToEndTests.cpp). A valid `name`
// query parameter is supplied for the 3 routes that need one (ledger/
// compile_only/hot_reload), so the 400 (missing name) validation path never
// masks the 503 (no capability) path this test actually wants to exercise -
// see NetworkServer.cpp's own route lambdas, where the `name` check always
// runs BEFORE the capability nullptr check.
TEST(ProjectAssemblyHotReloadEndpointsNoCapabilityTests, EveryRouteReturns503WhenNothingIsWiredUp)
{
    Network::NetworkServer server; // Every bridge/capability pointer defaults to nullptr.
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());
    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    {
        const httplib::Result res = client.Get("/project_assembly/hot_reload/status");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 503);
    }
    {
        const httplib::Result res = client.Get("/project_assembly/debug/ledger?name=SomeProject");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 503);
    }
    {
        const httplib::Result res = client.Get("/project_assembly/debug/loaded_assemblies");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 503);
    }
    {
        const httplib::Result res = client.Get("/project_assembly/debug/component_types");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 503);
    }
    {
        const httplib::Result res = client.Get("/project_assembly/debug/scene_snapshot");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 503);
    }
    {
        const httplib::Result res = client.Post("/project_assembly/debug/compile_only?name=SomeProject");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 503);
    }
    {
        const httplib::Result res = client.Post("/project_assembly/hot_reload?name=SomeProject");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 503);
    }

    server.Stop();
}

} // namespace
} // namespace gte
