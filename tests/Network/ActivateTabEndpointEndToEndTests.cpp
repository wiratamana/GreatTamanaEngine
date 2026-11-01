// End-to-end tests for GET /activate_tab and GET /list_tabs over a real loopback socket.
// A fake stand-in drains the EditorUiCommandBridge in place of the editor layer, so no ImGui is involved.

#include "Application/EditorUiCommandBridge.h"
#include "Core/EditorPanelRegistry.h"
#include "Editor/EditorWindow.h"
#include "Network/NetworkServer.h"

#include "NetworkTestHelpers.h"

#include <httplib.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gte {
namespace {

// Fulfills every pending bridge request with a preset outcome on its own thread.
class FakeEditorUiStandIn {
public:
    explicit FakeEditorUiStandIn(EditorUiCommandBridge& bridge)
        : m_bridge(bridge)
    {
        m_thread = std::thread([this] { Run(); });
    }

    ~FakeEditorUiStandIn()
    {
        m_stop.store(true);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    FakeEditorUiStandIn(const FakeEditorUiStandIn&) = delete;
    FakeEditorUiStandIn& operator=(const FakeEditorUiStandIn&) = delete;

    // While gated, requests stay pending (used by the timeout test).
    void SetGated(bool gated) { m_gated.store(gated); }

    // Outcome reported for the next fulfilled request. Defaults to success with an existing tab.
    void SetNextOutcome(bool success, bool tabExists)
    {
        std::lock_guard<std::mutex> lock(m_outcomeMutex);
        m_nextSuccess = success;
        m_nextTabExists = tabExists;
    }

private:
    void Run()
    {
        while (!m_stop.load()) {
            if (!m_gated.load()) {
                if (const auto pending = m_bridge.TryPeekPendingCommand()) {
                    const EditorUiCommandRequest* const request = &pending->request;
                    EditorUiCommandResult result;
                    result.kind = request->kind;
                    {
                        std::lock_guard<std::mutex> lock(m_outcomeMutex);
                        result.activateTab.success = m_nextSuccess;
                        result.activateTab.tabExists = m_nextTabExists;
                    }
                    m_bridge.FulfillCommand(pending->id, result);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    EditorUiCommandBridge& m_bridge;
    std::atomic<bool> m_gated{ false };
    std::atomic<bool> m_stop{ false };
    std::mutex m_outcomeMutex;
    bool m_nextSuccess = true;
    bool m_nextTabExists = true;
    std::thread m_thread;
};

class ListTabsProbeWindow final : public EditorWindow {
public:
    ListTabsProbeWindow() : EditorWindow("Test_ListTabs_Panel_Omega", WindowDock::Bottom) {}

protected:
    void OnGui(EditorFrame& /*frame*/) override {}
};

struct ListTabsProbeRegistration {
    ListTabsProbeWindow window;
    ListTabsProbeRegistration() { RegisterEditorWindow(window); }
    ~ListTabsProbeRegistration() { UnregisterEditorWindow(window); }
};

// A real bridge and a real NetworkServer on an ephemeral port, wired like the application does.
class ActivateTabEndpointEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_standIn = std::make_unique<FakeEditorUiStandIn>(m_bridge);
        m_server = std::make_unique<Network::NetworkServer>(nullptr, nullptr, &m_bridge);
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

    EditorUiCommandBridge m_bridge;
    std::unique_ptr<FakeEditorUiStandIn> m_standIn;
    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
};

TEST_F(ActivateTabEndpointEndToEndTest, ActivateTabSucceedsWhenMainThreadReportsTabExists)
{
    m_standIn->SetNextOutcome(true, true);

    const httplib::Result res = m_client->Get("/activate_tab?name=Profiler");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], true);
    EXPECT_EQ(parsed["activated_tab"], "Profiler");
}

TEST_F(ActivateTabEndpointEndToEndTest, ActivateTabReturns409WhenMainThreadReportsTabDoesNotExistYet)
{
    m_standIn->SetNextOutcome(false, false);

    const httplib::Result res = m_client->Get("/activate_tab?name=Profiler");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 409);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

TEST_F(ActivateTabEndpointEndToEndTest, ActivateTabReturns400ForMissingName)
{
    const httplib::Result res = m_client->Get("/activate_tab");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
}

TEST_F(ActivateTabEndpointEndToEndTest, ActivateTabReturns504WhenNeverFulfilled)
{
    // Calls SubmitAndWait() with a short timeout directly, so the test does not wait out the 3 s production timeout.
    m_standIn->SetGated(true);

    EditorUiCommandRequest request;
    request.kind = EditorUiCommandKind::ActivateTab;
    request.activateTab.tabName = "Profiler";
    const EditorUiCommandBridge::SubmitResult result = m_bridge.SubmitAndWait(request, 100);

    EXPECT_FALSE(result.alreadyPending);
    EXPECT_TRUE(result.timedOut);
    EXPECT_FALSE(result.result.has_value());

    m_standIn->SetGated(false);
}

// GET /list_tabs reflects the live registry.
TEST_F(ActivateTabEndpointEndToEndTest, ListTabsReturnsEveryRegisteredPanelName)
{
    ListTabsProbeRegistration probe;

    const httplib::Result res = m_client->Get("/list_tabs");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    ASSERT_TRUE(parsed.contains("tabs"));
    const std::vector<std::string>& allNames = EditorPanelRegistry::Instance().AllNames();
    ASSERT_EQ(parsed["tabs"].size(), allNames.size());
    for (std::size_t i = 0; i < allNames.size(); ++i) {
        EXPECT_EQ(parsed["tabs"][i], allNames[i]);
    }
    bool foundTestPanel = false;
    for (const auto& tab : parsed["tabs"]) {
        if (tab == "Test_ListTabs_Panel_Omega") {
            foundTestPanel = true;
        }
    }
    EXPECT_TRUE(foundTestPanel);
}

// An unknown name must answer 404 at once, without touching a bridge that nothing services.
TEST(ActivateTabEndpointNoStandInTests, ActivateTabReturns404ForAnUnknownNameWithoutTouchingTheBridge)
{
    EditorUiCommandBridge bridge;
    Network::NetworkServer server(nullptr, nullptr, &bridge);
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());
    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    const auto start = std::chrono::steady_clock::now();
    const httplib::Result res = client.Get("/activate_tab?name=NotARealTab");
    const auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);
    EXPECT_LT(elapsed, std::chrono::milliseconds(1000));

    server.Stop();
}

// A server without an editor UI bridge answers 503 for a known name instead of crashing.
TEST(ActivateTabEndpointNullBridgeTests, ActivateTabReturns503WhenBridgeIsNull)
{
    Network::NetworkServer server;
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());
    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    const httplib::Result res = client.Get("/activate_tab?name=Profiler");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 503);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);

    server.Stop();
}

// An unknown name is a 404, never a 503, even with a null bridge.
TEST(ActivateTabEndpointNullBridgeTests, ActivateTabReturns404NotServiceUnavailableForUnknownNameEvenWithNullBridge)
{
    Network::NetworkServer server;
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());
    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    const httplib::Result res = client.Get("/activate_tab?name=NotARealTab");

    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 404);
    const nlohmann::json parsed = nlohmann::json::parse(res->body);
    EXPECT_EQ(parsed["success"], false);

    server.Stop();
}

} // namespace
} // namespace gte
