// End-to-end tests for POST /instantiate_primitive and POST /delete_entity -
// network-impl-3 campaign, Phase 5
// (PHASE5_NETWORK_POST_ROUTES_AND_COMMAND_DISPATCH.md). Mirrors
// tests/Network/CaptureEndpointsEndToEndTests.cpp's own proven shape exactly:
// a real gte::EngineCommandBridge + a real gte::Network::NetworkServer, wired
// together exactly like gte::Application does in production, hit over a REAL
// loopback socket via httplib::Client - with a FAKE, GPU/ECS-free stand-in
// (FakeEngineCommandStandIn, below) standing in for
// Application::Run()'s real per-frame TryPeekPendingCommandRequest()/
// FulfillCommand() calls (and, transitively, for Game::InstantiatePrimitive()/
// DeleteEntityByName() themselves).
//
// There is no live Application/Game/Renderer/Vulkan device anywhere in this
// file - per PHASE5's own "Verification for this phase" section (second-
// iteration review), that GPU-touching half of the campaign (Phase 3's
// Game:: methods) stays in the accepted, documented "Tier 2, no automated
// coverage yet" bucket (AGENTS.md, "Testability & Regression Safety") and is
// instead covered by a manual, real end-to-end gte_send_request smoke test
// against a live running engine instance (see PHASE5_COMPLETION_REPORT.md).
// What IS tested here, for real, is everything this phase actually invented
// that ISN'T ordinary ECS/GPU plumbing: the EngineCommandBridge <->
// NetworkServer cross-thread handshake, the HTTP status-code mapping (200/
// 400/404/503), and the real JSON request/response bodies round-tripping
// over a real socket.
//
// UPDATE (network-impl-5 campaign, PHASE5_TESTS_DOCS_AND_REGRESSION_SAFETY.md):
// extended to also cover POST /set_entity_trs and POST /instantiate_light -
// FakeEngineCommandStandIn's Run() method is now a real switch over all FOUR
// EngineCommandKind values instead of the original two-way if/else. Same
// "deliberately NOT trying to replicate the real Game:: logic, only prove the
// wiring" philosophy applies to the two new kinds too - see
// PHASE5_TESTS_DOCS_AND_REGRESSION_SAFETY.md, Step 3.1.

#include "Application/EngineCommandBridge.h"
#include "Network/NetworkServer.h"

#include "Math/Quat.h"
#include "Math/Vec3.h"

#include "NetworkTestHelpers.h"

#include <httplib.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>

namespace gte {
namespace {

// A named "entity"'s fake, in-memory local Transform state - tracked purely
// so FakeEngineCommandStandIn can prove SetEntityTrs's translation/rotation/
// scale change-tracking + echo-back wiring works, without any real
// Registry/Transform component involved. Defaults mirror Transform.h's own
// defaults (zero position/rotation, unit scale).
struct FakeTransformState {
    Vec3 position{ 0.0f, 0.0f, 0.0f };
    Vec3 rotationEulerDegrees{ 0.0f, 0.0f, 0.0f };
    Vec3 scale{ 1.0f, 1.0f, 1.0f };
};

// The GPU/ECS-free stand-in for Application::Run()'s real per-frame engine-
// command drain. Tracks a small in-memory set of "live" entity names (plus,
// as of network-impl-5, a matching fake Transform per name) purely so a fake
// InstantiatePrimitive/DeleteEntity/SetEntityTrs/InstantiateLight sequence
// can be round-tripped without any real Registry/Game/Renderer involved -
// deliberately NOT trying to replicate Game::InstantiatePrimitive()'s (or
// Game::SetEntityTrs()'s/Game::InstantiateLight()'s) real shape-name
// validation/auto-dedup/parent-lookup/quaternion-math logic (that logic is
// Phase 2/3's own, independently Tier-1-tested responsibility - see
// tests/ECS/EntityQueryTests.cpp, tests/Game/GameEntityCommandsTests.cpp,
// tests/Renderer/PrimitiveMeshGeneratorTests.cpp, and Phase 3's own
// testability notes) - this stand-in only needs to prove the HTTP <-> bridge
// <-> "main thread" wiring itself works.
class FakeEngineCommandStandIn {
public:
    explicit FakeEngineCommandStandIn(EngineCommandBridge& bridge)
        : m_bridge(bridge)
    {
        m_thread = std::thread([this] { Run(); });
    }

    ~FakeEngineCommandStandIn()
    {
        m_stop.store(true);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    FakeEngineCommandStandIn(const FakeEngineCommandStandIn&) = delete;
    FakeEngineCommandStandIn& operator=(const FakeEngineCommandStandIn&) = delete;

    // Gating (mirrors FakeMainThreadStandIn::SetGated in
    // CaptureEndpointsEndToEndTests.cpp) is what lets
    // SecondConcurrentCommandReturns503Immediately hold a first request
    // genuinely pending on purpose.
    void SetGated(bool gated) { m_gated.store(gated); }

private:
    void Run()
    {
        // Only ever touched from this one background thread - the test's
        // own main thread never reaches into m_liveNames/m_transforms/
        // m_nextIndex directly, so no additional synchronization is needed
        // beyond EngineCommandBridge's own internal mutex.
        std::set<std::string> liveNames;
        std::map<std::string, FakeTransformState> transforms;
        std::uint32_t nextIndex = 1;

        while (!m_stop.load()) {
            if (!m_gated.load()) {
                if (const std::optional<EngineCommandRequest> request = m_bridge.TryPeekPendingCommandRequest()) {
                    EngineCommandResult result;
                    result.kind = request->kind;
                    switch (request->kind) {
                    case EngineCommandKind::InstantiatePrimitive: {
                        result.instantiatePrimitive.success = true;
                        result.instantiatePrimitive.entityIndex = nextIndex++;
                        result.instantiatePrimitive.entityGeneration = 0;
                        result.instantiatePrimitive.resolvedName = request->instantiatePrimitive.requestedName;
                        liveNames.insert(request->instantiatePrimitive.requestedName);
                        transforms[request->instantiatePrimitive.requestedName] = FakeTransformState{};
                        break;
                    }
                    case EngineCommandKind::DeleteEntity: {
                        const auto it = liveNames.find(request->deleteEntity.name);
                        if (it != liveNames.end()) {
                            result.deleteEntity.success = true;
                            result.deleteEntity.deletedEntityIndex = 1;
                            result.deleteEntity.deletedEntityGeneration = 0;
                            liveNames.erase(it);
                            transforms.erase(request->deleteEntity.name);
                        } else {
                            result.deleteEntity.success = false;
                            result.deleteEntity.errorMessage = "no live entity named '" + request->deleteEntity.name + "'";
                        }
                        break;
                    }
                    case EngineCommandKind::SetEntityTrs: {
                        const std::string& name = request->setEntityTrs.name;
                        const auto it = transforms.find(name);
                        if (it == transforms.end()) {
                            result.setEntityTrs.success = false;
                            result.setEntityTrs.entityNotFound = true;
                            result.setEntityTrs.errorMessage = "no live entity named '" + name + "'";
                        } else {
                            FakeTransformState& state = it->second;
                            if (request->setEntityTrs.hasTranslation) {
                                state.position = request->setEntityTrs.translation;
                                result.setEntityTrs.translationChanged = true;
                            }
                            if (request->setEntityTrs.hasRotationEulerDegrees) {
                                state.rotationEulerDegrees = request->setEntityTrs.rotationEulerDegrees;
                                result.setEntityTrs.rotationChanged = true;
                            }
                            if (request->setEntityTrs.hasScale) {
                                state.scale = request->setEntityTrs.scale;
                                result.setEntityTrs.scaleChanged = true;
                            }
                            result.setEntityTrs.success = true;
                            result.setEntityTrs.entityNotFound = false;
                            result.setEntityTrs.entityIndex = 1;
                            result.setEntityTrs.entityGeneration = 0;
                            result.setEntityTrs.resultingPosition = state.position;
                            result.setEntityTrs.resultingRotation = Quat::FromEulerDegrees(
                                state.rotationEulerDegrees.x, state.rotationEulerDegrees.y, state.rotationEulerDegrees.z);
                            result.setEntityTrs.resultingScale = state.scale;
                        }
                        break;
                    }
                    case EngineCommandKind::InstantiateLight: {
                        std::string lowerLightType = request->instantiateLight.lightType;
                        std::transform(lowerLightType.begin(), lowerLightType.end(), lowerLightType.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        if (!lowerLightType.empty() && lowerLightType != "directional") {
                            result.instantiateLight.success = false;
                            result.instantiateLight.errorMessage = "unsupported light_type '"
                                + request->instantiateLight.lightType + "' - only 'directional' is currently supported";
                        } else {
                            result.instantiateLight.success = true;
                            result.instantiateLight.entityIndex = nextIndex++;
                            result.instantiateLight.entityGeneration = 0;
                            result.instantiateLight.resolvedName = request->instantiateLight.requestedName;
                            liveNames.insert(request->instantiateLight.requestedName);
                            transforms[request->instantiateLight.requestedName] = FakeTransformState{};
                        }
                        break;
                    }
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
    std::thread m_thread;
};

// A real gte::EngineCommandBridge + a real gte::Network::NetworkServer
// (commandBridge wired, captureBridge deliberately left nullptr - these two
// routes never touch it), started on an ephemeral port, wired together
// exactly like gte::Application does in production.
class EngineCommandEndpointsEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_standIn = std::make_unique<FakeEngineCommandStandIn>(m_bridge);
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
    std::unique_ptr<FakeEngineCommandStandIn> m_standIn;
    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
};

TEST_F(EngineCommandEndpointsEndToEndTest, InstantiatePrimitiveValidPayloadReturns200AndSuccess)
{
    const httplib::Result res = m_client->Post("/instantiate_primitive",
        R"({"shape":"sphere","name":"NetTestSphere","world_position":{"x":1,"y":2,"z":3}})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");

    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["name"], "NetTestSphere");
}

TEST_F(EngineCommandEndpointsEndToEndTest, InstantiatePrimitiveMalformedJsonReturns400)
{
    const httplib::Result res = m_client->Post("/instantiate_primitive", "{not valid json", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

TEST_F(EngineCommandEndpointsEndToEndTest, DeleteEntityForUnknownNameReturns404)
{
    const httplib::Result res =
        m_client->Post("/delete_entity", R"({"name":"DoesNotExist"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

TEST_F(EngineCommandEndpointsEndToEndTest, DeleteEntityMalformedJsonReturns400)
{
    const httplib::Result res = m_client->Post("/delete_entity", "{not valid json", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
}

// The strongest regression proof this whole campaign's cross-thread plumbing
// actually works end to end: instantiate a uniquely-named primitive, then
// delete that exact name, confirming 200 both times (PHASE5's own
// "Verification for this phase" section calls this out explicitly).
TEST_F(EngineCommandEndpointsEndToEndTest, FullInstantiateThenDeleteRoundTripBothReturn200)
{
    const httplib::Result instantiateRes =
        m_client->Post("/instantiate_primitive", R"({"shape":"cube","name":"RoundTripCube"})", "application/json");
    ASSERT_TRUE(instantiateRes != nullptr);
    EXPECT_EQ(instantiateRes->status, 200);
    const nlohmann::json instantiateBody = nlohmann::json::parse(instantiateRes->body);
    EXPECT_EQ(instantiateBody["success"], true);

    const httplib::Result deleteRes =
        m_client->Post("/delete_entity", R"({"name":"RoundTripCube"})", "application/json");
    ASSERT_TRUE(deleteRes != nullptr);
    EXPECT_EQ(deleteRes->status, 200);
    const nlohmann::json deleteBody = nlohmann::json::parse(deleteRes->body);
    EXPECT_EQ(deleteBody["success"], true);
}

TEST_F(EngineCommandEndpointsEndToEndTest, SecondConcurrentCommandReturns503Immediately)
{
    // Gate the stand-in so the FIRST request below stays genuinely pending
    // until this test un-gates it at the very end - mirrors
    // CaptureEndpointsEndToEndTests.cpp's own
    // SecondConcurrentRequestOfSameKindGets503, generalized here to the
    // single GLOBAL slot this bridge uses (PHASE0_MASTER_STRATEGY.md's
    // Locked Design Decision #5) - the two concurrent requests below are
    // deliberately of DIFFERENT kinds, proving the collision is bridge-wide,
    // not per-kind.
    m_standIn->SetGated(true);

    std::thread firstRequester([this] {
        httplib::Client firstClient("127.0.0.1", m_server->BoundPort());
        firstClient.Post("/instantiate_primitive", R"({"shape":"cube","name":"Gated"})", "application/json");
    });

    // Give the first request a moment to actually register as pending.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const auto start = std::chrono::steady_clock::now();
    const httplib::Result second = m_client->Post("/delete_entity", R"({"name":"Whatever"})", "application/json");
    const auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_TRUE(second != nullptr);
    EXPECT_EQ(second->status, 503);
    // Must have returned promptly - well before any timeout, proving this
    // was the immediate "already pending" rejection, not a real wait.
    EXPECT_LT(elapsed, std::chrono::milliseconds(1000));

    m_standIn->SetGated(false);
    firstRequester.join();
}

// Per this phase document's own precedent (CaptureEndpointsEndToEndTests.cpp's
// TimeoutSurfacesAs504): never wait out EngineCommandBridge's real 3-second
// default production timeout over a real HTTP round trip - call
// SubmitAndWait() directly with a per-call SHORT override instead, bypassing
// HTTP entirely. This is still a full, honest regression proof of the exact
// behavior the HTTP route forwards verbatim (see NetworkServer.cpp's
// "/instantiate_primitive"/"/delete_entity" handlers) - timedOut -> HTTP 504
// - without needing a slow, real ~3-second round trip in the test suite.
TEST_F(EngineCommandEndpointsEndToEndTest, TimedOutSubmissionIsWhatTheHttpRouteMapsToA504)
{
    m_standIn->SetGated(true);

    EngineCommandRequest request;
    request.kind = EngineCommandKind::DeleteEntity;
    request.deleteEntity.name = "Whatever";
    const EngineCommandBridge::SubmitResult result = m_bridge.SubmitAndWait(request, 100);

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.result.has_value());

    m_standIn->SetGated(false);
}

// A NetworkServer constructed with commandBridge == nullptr (mirrors
// production's own "no engine-state-touching routes wired up" contract, see
// NetworkServer.h's own constructor doc comment) must respond 503 for ALL
// FOUR engine-command routes rather than crashing - extended by
// network-impl-5 (PHASE5_TESTS_DOCS_AND_REGRESSION_SAFETY.md, Step 3.2) to
// also cover the two new routes this campaign added.
TEST(EngineCommandEndpointsNoBridgeTests, CommandBridgeNullptrReturns503ForAllFourRoutes)
{
    Network::NetworkServer server; // Both captureBridge/commandBridge default to nullptr.
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());

    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    const httplib::Result instantiateRes =
        client.Post("/instantiate_primitive", R"({"shape":"cube","name":"X"})", "application/json");
    ASSERT_TRUE(instantiateRes != nullptr);
    EXPECT_EQ(instantiateRes->status, 503);

    const httplib::Result deleteRes = client.Post("/delete_entity", R"({"name":"X"})", "application/json");
    ASSERT_TRUE(deleteRes != nullptr);
    EXPECT_EQ(deleteRes->status, 503);

    const httplib::Result setTrsRes =
        client.Post("/set_entity_trs", R"({"name":"X"})", "application/json");
    ASSERT_TRUE(setTrsRes != nullptr);
    EXPECT_EQ(setTrsRes->status, 503);

    const httplib::Result instantiateLightRes =
        client.Post("/instantiate_light", R"({"name":"X"})", "application/json");
    ASSERT_TRUE(instantiateLightRes != nullptr);
    EXPECT_EQ(instantiateLightRes->status, 503);

    server.Stop();
}

// --- network-impl-5 campaign
// (PHASE5_TESTS_DOCS_AND_REGRESSION_SAFETY.md, Step 3.2) - new end-to-end
// coverage for POST /set_entity_trs and POST /instantiate_light.

TEST_F(EngineCommandEndpointsEndToEndTest, SetEntityTrsValidPayloadReturns200AndReflectsChanges)
{
    const httplib::Result instantiateRes = m_client->Post("/instantiate_light",
        R"({"light_type":"directional","name":"TrsTestLight"})", "application/json");
    ASSERT_TRUE(instantiateRes != nullptr);
    EXPECT_EQ(instantiateRes->status, 200);

    const httplib::Result res = m_client->Post("/set_entity_trs",
        R"({"name":"TrsTestLight","rotation_euler_degrees":{"x":30,"y":90,"z":0}})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["changed"]["rotation"], true);
    EXPECT_EQ(parsed["changed"]["translation"], false);
    EXPECT_EQ(parsed["changed"]["scale"], false);
    ASSERT_TRUE(parsed.contains("transform"));
    EXPECT_TRUE(parsed["transform"].contains("rotation_euler_degrees"));
    EXPECT_TRUE(parsed["transform"].contains("rotation_quaternion"));
}

TEST_F(EngineCommandEndpointsEndToEndTest, SetEntityTrsMalformedJsonReturns400)
{
    const httplib::Result res = m_client->Post("/set_entity_trs", "{not valid json", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

TEST_F(EngineCommandEndpointsEndToEndTest, SetEntityTrsUnknownNameReturns404)
{
    const httplib::Result res =
        m_client->Post("/set_entity_trs", R"({"name":"DoesNotExist"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

TEST_F(EngineCommandEndpointsEndToEndTest, SetEntityTrsNoOpRequestStillReturns200WithCurrentTransform)
{
    const httplib::Result instantiateRes = m_client->Post("/instantiate_light",
        R"({"name":"NoOpTrsLight"})", "application/json");
    ASSERT_TRUE(instantiateRes != nullptr);
    EXPECT_EQ(instantiateRes->status, 200);

    const httplib::Result res =
        m_client->Post("/set_entity_trs", R"({"name":"NoOpTrsLight"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["changed"]["translation"], false);
    EXPECT_EQ(parsed["changed"]["rotation"], false);
    EXPECT_EQ(parsed["changed"]["scale"], false);
    ASSERT_TRUE(parsed.contains("transform"));
}

TEST_F(EngineCommandEndpointsEndToEndTest, InstantiateLightValidPayloadReturns200AndSuccess)
{
    const httplib::Result res = m_client->Post("/instantiate_light",
        R"({"light_type":"directional","name":"NetTestSun","world_position":{"x":0,"y":5,"z":0}})",
        "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["name"], "NetTestSun");
}

TEST_F(EngineCommandEndpointsEndToEndTest, InstantiateLightMalformedJsonReturns400)
{
    const httplib::Result res = m_client->Post("/instantiate_light", "{not valid json", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

TEST_F(EngineCommandEndpointsEndToEndTest, InstantiateLightMissingNameReturns400)
{
    const httplib::Result res = m_client->Post("/instantiate_light",
        R"({"light_type":"directional"})", "application/json");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

// A round-trip mirroring FullInstantiateThenDeleteRoundTripBothReturn200
// above, extended to also exercise SetEntityTrs in the middle - directly
// proves the campaign's own three-command lifecycle (spawn a light, rotate
// it, delete it) works end to end through the real bridge/server wiring.
TEST_F(EngineCommandEndpointsEndToEndTest, FullInstantiateLightThenSetTrsThenDeleteRoundTripAllReturn200)
{
    const httplib::Result instantiateRes = m_client->Post("/instantiate_light",
        R"({"light_type":"directional","name":"RoundTripLight"})", "application/json");
    ASSERT_TRUE(instantiateRes != nullptr);
    EXPECT_EQ(instantiateRes->status, 200);
    const nlohmann::json instantiateBody = nlohmann::json::parse(instantiateRes->body);
    EXPECT_EQ(instantiateBody["success"], true);

    const httplib::Result setTrsRes = m_client->Post("/set_entity_trs",
        R"({"name":"RoundTripLight","rotation_euler_degrees":{"x":10,"y":20,"z":30}})", "application/json");
    ASSERT_TRUE(setTrsRes != nullptr);
    EXPECT_EQ(setTrsRes->status, 200);
    const nlohmann::json setTrsBody = nlohmann::json::parse(setTrsRes->body);
    EXPECT_EQ(setTrsBody["success"], true);

    const httplib::Result deleteRes =
        m_client->Post("/delete_entity", R"({"name":"RoundTripLight"})", "application/json");
    ASSERT_TRUE(deleteRes != nullptr);
    EXPECT_EQ(deleteRes->status, 200);
    const nlohmann::json deleteBody = nlohmann::json::parse(deleteRes->body);
    EXPECT_EQ(deleteBody["success"], true);
}

} // namespace
} // namespace gte
