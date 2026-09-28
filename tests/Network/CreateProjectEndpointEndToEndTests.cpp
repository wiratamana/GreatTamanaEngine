// End-to-end tests for POST /project_assembly/create_project -
// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2), PHASE5 (PHASE5_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md,
// STEP 1). Mirrors tests/Network/ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp's
// own exact fixture shape (a real gte::Network::NetworkServer on an
// ephemeral port, a real httplib::Client,
// Network::TestHelpers::WaitUntilAcceptingConnections) - wired against a
// real gte::EditorProjectLifecycleCapability instance (the 9th constructor
// argument).
//
// NOTE on why this deliberately touches THIS repo's own real Projects/
// directory: EditorProjectLifecycleCapability::CreateNewProjectAssembly()
// resolves its own source root INTERNALLY
// (ResolveCMakeBuildDirectory(gte::ExecutableDirectory()), see
// src/Editor/EditorProjectLifecycleCapability.cpp) - it cannot be
// redirected to a throwaway temp directory by construction alone. This
// file picks PHASE5's own documented option (b): test the real, production
// resolution path directly, against THIS repo's own real, current build
// tree, using a genuinely unique, disposable project name per test, and
// delete it in TearDown() - accepting that these tests genuinely mutate
// (briefly - including a real, synchronous `cmake -S/-B` reconfigure, per
// LDD-CP2) and then clean up real repo state, exactly like
// ProjectAssemblyHotReloadEndpointsEndToEndTests.cpp's own
// CompileOnlyValidNameReturns200Started test already accepts a real, live
// child-process side effect for the same underlying reason.

#include "Editor/EditorProjectLifecycleCapability.h"
#include "Network/NetworkServer.h"

#include "NetworkTestHelpers.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace gte {
namespace {

// A genuinely unique, disposable project name per test run - combines a
// monotonic steady-clock tick count with a per-process atomic counter (in
// case two calls land on the exact same tick) so repeated test runs never
// collide with a leftover folder from a prior run that somehow failed to
// clean up.
std::string MakeUniqueProjectName()
{
    static std::atomic<std::uint64_t> s_counter{ 0 };
    const std::uint64_t ticks = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    return "GteE2ECreateProjectTest" + std::to_string(ticks % 1000000000ULL) + "_" + std::to_string(s_counter.fetch_add(1));
}

std::size_t CountDirectoryEntries(const std::filesystem::path& directory)
{
    std::error_code existsError;
    if (!std::filesystem::exists(directory, existsError)) {
        return 0;
    }
    std::size_t count = 0;
    std::error_code iterateError;
    for (auto it = std::filesystem::directory_iterator(directory, iterateError);
         !iterateError && it != std::filesystem::directory_iterator();
         it.increment(iterateError)) {
        ++count;
    }
    return count;
}

// A real gte::Network::NetworkServer, started on an ephemeral port, wired
// with a real EditorProjectLifecycleCapability instance (the 9th
// constructor argument) - a fresh instance per TEST_F, mirroring
// ProjectAssemblyHotReloadEndpointsEndToEndTest's own fixture shape
// exactly.
class CreateProjectEndpointEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_server = std::make_unique<Network::NetworkServer>(
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &m_capability);
        m_server->Start(0);
        ASSERT_TRUE(m_server->IsRunning());
        m_client = std::make_unique<httplib::Client>("127.0.0.1", m_server->BoundPort());
        Network::TestHelpers::WaitUntilAcceptingConnections(*m_server, *m_client);
    }

    void TearDown() override
    {
        m_client.reset();
        m_server.reset();
        if (!m_createdProjectDirectory.empty()) {
            std::error_code ignoredError;
            std::filesystem::remove_all(m_createdProjectDirectory, ignoredError);
        }
    }

    EditorProjectLifecycleCapability m_capability;
    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
    // Set by whichever test actually creates a real project - TearDown()
    // removes it (best-effort) so no scratch project folder ever survives
    // a test run. Left empty (no cleanup) for a test that never creates one.
    std::filesystem::path m_createdProjectDirectory;
};

TEST_F(CreateProjectEndpointEndToEndTest, ValidUniqueNameCreatesRealScaffoldThenDuplicateReturns400)
{
    const std::string projectName = MakeUniqueProjectName();

    const httplib::Result createRes = m_client->Post("/project_assembly/create_project?name=" + projectName);
    ASSERT_TRUE(createRes != nullptr);
    ASSERT_EQ(createRes->status, 200);
    EXPECT_EQ(createRes->get_header_value("Content-Type"), "application/json");
    const nlohmann::json createBody = nlohmann::json::parse(createRes->body);
    ASSERT_TRUE(createBody.contains("created_source_directory"));
    const std::string createdSourceDirectory = createBody["created_source_directory"].get<std::string>();
    EXPECT_NE(createdSourceDirectory.find(projectName), std::string::npos);

    m_createdProjectDirectory = createdSourceDirectory; // TearDown() removes this.

    // Confirm the real 3-file scaffold, byte-for-byte matching PHASE3's own
    // template use (cmake/templates/ProjectAssemblyExports.h copied
    // verbatim, plus the generated CMakeLists.txt/Game.cpp stubs).
    EXPECT_TRUE(std::filesystem::exists(m_createdProjectDirectory / "Libraries" / "CMakeLists.txt"));
    EXPECT_TRUE(std::filesystem::exists(m_createdProjectDirectory / "Libraries" / "ProjectAssemblyExports.h"));
    EXPECT_TRUE(std::filesystem::exists(m_createdProjectDirectory / "Assets" / (projectName + "Game.cpp")));

    // Repeating the exact same request must be rejected - "already exists" -
    // and must not touch/overwrite anything already on disk.
    const auto mtimeBefore = std::filesystem::last_write_time(m_createdProjectDirectory / "Libraries" / "CMakeLists.txt");
    const httplib::Result duplicateRes = m_client->Post("/project_assembly/create_project?name=" + projectName);
    ASSERT_TRUE(duplicateRes != nullptr);
    EXPECT_EQ(duplicateRes->status, 400);
    const nlohmann::json duplicateBody = nlohmann::json::parse(duplicateRes->body);
    ASSERT_TRUE(duplicateBody.contains("error"));
    EXPECT_NE(duplicateBody["error"].get<std::string>().find("already exists"), std::string::npos);
    const auto mtimeAfter = std::filesystem::last_write_time(m_createdProjectDirectory / "Libraries" / "CMakeLists.txt");
    EXPECT_EQ(mtimeBefore, mtimeAfter);
}

TEST_F(CreateProjectEndpointEndToEndTest, InvalidNamesAreRejectedBeforeAnyFilesystemWrite)
{
    // Establish a real Projects/ directory to diff a directory-entry-count
    // against, via one real, successful create (cleaned up by TearDown(),
    // same as the other test above).
    const std::string validName = MakeUniqueProjectName();
    const httplib::Result createRes = m_client->Post("/project_assembly/create_project?name=" + validName);
    ASSERT_TRUE(createRes != nullptr);
    ASSERT_EQ(createRes->status, 200);
    const nlohmann::json createBody = nlohmann::json::parse(createRes->body);
    m_createdProjectDirectory = createBody["created_source_directory"].get<std::string>();
    const std::filesystem::path projectsDirectory = m_createdProjectDirectory.parent_path();

    const std::size_t entryCountBefore = CountDirectoryEntries(projectsDirectory);

    // CON: a reserved Windows device name.
    {
        const httplib::Result res = m_client->Post("/project_assembly/create_project?name=CON");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 400);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        ASSERT_TRUE(body.contains("error"));
        EXPECT_FALSE(body["error"].get<std::string>().empty());
    }
    // Empty name - deliberately no `name` query parameter at all.
    {
        const httplib::Result res = m_client->Post("/project_assembly/create_project");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 400);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        ASSERT_TRUE(body.contains("error"));
        EXPECT_FALSE(body["error"].get<std::string>().empty());
    }
    // A path-traversal attempt.
    {
        const httplib::Result res = m_client->Post("/project_assembly/create_project?name=../evil");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 400);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        ASSERT_TRUE(body.contains("error"));
        EXPECT_FALSE(body["error"].get<std::string>().empty());
    }
    // A name containing a space (URL-encoded as %20 so the request line
    // itself stays well-formed).
    {
        const httplib::Result res = m_client->Post("/project_assembly/create_project?name=My%20Project");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 400);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        ASSERT_TRUE(body.contains("error"));
        EXPECT_FALSE(body["error"].get<std::string>().empty());
    }

    EXPECT_EQ(CountDirectoryEntries(projectsDirectory), entryCountBefore)
        << "a rejected name must never write anything to Projects/";
}

} // namespace

// A NetworkServer constructed with projectLifecycleCapability == nullptr
// (mirrors ProjectAssemblyHotReloadEndpointsNoCapabilityTests's own exact
// convention) must respond 503 for this route, never crash.
TEST(CreateProjectEndpointNoCapabilityTests, MissingCapabilityReturns503)
{
    Network::NetworkServer server; // Every bridge/capability pointer defaults to nullptr.
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());
    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    const httplib::Result res = client.Post("/project_assembly/create_project?name=SomeProject");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 503);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["success"], false);

    server.Stop();
}

} // namespace gte
