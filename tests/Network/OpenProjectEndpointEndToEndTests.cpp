// End-to-end tests for POST /project_assembly/open_project and GET
// /project_assembly/list_projects - editor-core-separation-17 campaign
// (On-Engine Project Workflow plan, BIG-STEP 3, "Open Project"), PHASE5
// (PHASE5_FULL_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md, Section 3.1).
// Mirrors tests/Network/CreateProjectEndpointEndToEndTests.cpp's own exact
// fixture shape (a real gte::Network::NetworkServer on an ephemeral port, a
// real httplib::Client, Network::TestHelpers::WaitUntilAcceptingConnections)
// wired against a real gte::EditorProjectLifecycleCapability instance (the
// 9th constructor argument) - option (b) from that file's own precedent:
// test the real production resolution path directly, against THIS repo's
// own real, current build tree, using genuinely unique/disposable project
// names created and torn down per-test, never the persistent
// ProjectAssemblyProbe fixture for anything that WRITES to disk.
//
// Tier-0/Tier-1 fixtures are created directly on disk (no HTTP route exists
// to produce those specific tiers) as one-level-deep folders under the same
// real Projects/ source root a genuine POST /project_assembly/create_project
// call resolves to (obtained per-test by actually creating one real,
// disposable project first via that route, then reading its own parent
// directory - exactly the same technique
// CreateProjectEndpointEndToEndTests.cpp::InvalidNamesAreRejectedBeforeAnyFilesystemWrite
// already uses). Tier-2 coverage is simply that same real created project,
// since a freshly create_project'd folder has real Game source but no
// compiled .dll yet - exactly ProjectValidityTier::NotCompiled's own
// definition (PHASE1_TIER_CLASSIFICATION_MODEL.md).
//
// Tier-3/Tier-4 coverage deliberately points at the real, persistent
// ProjectAssemblyProbe fixture instead (read-only from this file's own
// point of view - never deleted/recreated/modified), guarded by a runtime
// skip if ProjectAssemblyProbe_Game.dll does not exist yet in this build's
// own output directory - mirrors this repository's existing, legitimate
// environment-gated skip convention (PHASE5's own strategy file, Section
// 3.1's closing paragraph).
#include "Editor/ActiveProjectAssemblyState.h"
#include "Editor/EditorProjectLifecycleCapability.h"
#include "Editor/ProjectRootPath.h" // gte::ExecutableDirectory()
#include "Core/Plugins/ProjectAssemblyBuildRunner.h" // ResolveProjectAssemblyOutputDirectory()
#include "Network/NetworkServer.h"

#include "NetworkTestHelpers.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gte {
namespace {

// A genuinely unique, disposable project name per test run - combines a
// monotonic steady-clock tick count with a per-process atomic counter (in
// case two calls land on the exact same tick) so repeated test runs never
// collide with a leftover folder from a prior run that somehow failed to
// clean up. Mirrors CreateProjectEndpointEndToEndTests.cpp's own
// MakeUniqueProjectName() exactly, parameterized by a caller-supplied
// prefix so this file's several fixture kinds (Tier-0/Tier-1/Tier-2/
// nonexistent) never collide with each other either.
std::string MakeUniqueProjectName(const std::string& prefix)
{
    static std::atomic<std::uint64_t> s_counter{ 0 };
    const std::uint64_t ticks = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    return prefix + std::to_string(ticks % 1000000000ULL) + "_" + std::to_string(s_counter.fetch_add(1));
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    stream << content;
    return stream.good();
}

// Finds the {"name":..., "tier":...} entry matching `name` inside a real
// GET /project_assembly/list_projects JSON array body - returns an empty
// string (never found) if absent.
std::string FindTierForName(const nlohmann::json& listBody, const std::string& name)
{
    for (const auto& entry : listBody) {
        if (entry.value("name", std::string()) == name) {
            return entry.value("tier", std::string());
        }
    }
    return "";
}

// A real gte::Network::NetworkServer, started on an ephemeral port, wired
// with a real EditorProjectLifecycleCapability instance (the 9th
// constructor argument) - a fresh instance per TEST_F, mirroring
// CreateProjectEndpointEndToEndTest's own fixture shape exactly. This
// capability instance never has SetLoadCommandBridge()/SetEngineReferences()
// called (no live Core/EditorHost exists in a unit test) - exactly like
// CreateProjectEndpointEndToEndTest's own instance - so a Tier-3 ("Compiled")
// open always takes EditorProjectLifecycleCapability::OpenProjectAssembly()'s
// own honest "load command bridge unavailable - marked active only" fallback
// branch rather than genuinely loading a .dll; this is expected and is NOT
// exercised by this file (a real load is only provable against a real,
// running GreatTamanaEditor.exe - see this phase's own live HTTP
// verification, done separately, not inside this test file).
class OpenProjectEndpointEndToEndTest : public ::testing::Test {
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
        for (const auto& directory : m_createdDirectories) {
            std::error_code ignoredError;
            std::filesystem::remove_all(directory, ignoredError);
        }
    }

    // Creates a real, disposable Tier-2 ("NotCompiled") project via the
    // ALREADY-WORKING POST /project_assembly/create_project route (BIG-STEP
    // 2's own real, working feature) - the one, real, production way to get
    // a genuine Libraries/CMakeLists.txt + Assets/<Name>Game.cpp scaffold
    // with no compiled .dll on disk yet. Registers the created directory
    // for TearDown() cleanup and returns {name, createdSourceDirectory}.
    std::pair<std::string, std::filesystem::path> CreateRealTier2Fixture()
    {
        const std::string name = MakeUniqueProjectName("GteE2EOpenProjectTier2_");
        const httplib::Result res = m_client->Post("/project_assembly/create_project?name=" + name);
        EXPECT_TRUE(res != nullptr);
        if (res == nullptr) {
            return { name, std::filesystem::path{} };
        }
        EXPECT_EQ(res->status, 200);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        const std::filesystem::path createdDirectory = body["created_source_directory"].get<std::string>();
        m_createdDirectories.push_back(createdDirectory);
        return { name, createdDirectory };
    }

    // Creates a bare, empty Tier-0 ("NotAProject") fixture folder directly
    // under `projectsDirectory` - missing Libraries/CMakeLists.txt
    // entirely, exactly ProjectValidityTier::NotAProject's own definition.
    // No HTTP route can produce this tier (create_project always writes a
    // full, valid scaffold), so this file writes it directly, mirroring
    // this phase's own strategy file (Section 3.1, item 1).
    std::filesystem::path CreateTier0Fixture(const std::filesystem::path& projectsDirectory, const std::string& name)
    {
        const std::filesystem::path directory = projectsDirectory / name;
        std::filesystem::create_directories(directory);
        m_createdDirectories.push_back(directory);
        return directory;
    }

    // Creates a Tier-1 ("NotBuildable") fixture folder directly under
    // `projectsDirectory` - has a real Libraries/CMakeLists.txt but no
    // Assets/*.cpp at all, exactly ProjectValidityTier::NotBuildable's own
    // definition.
    std::filesystem::path CreateTier1Fixture(const std::filesystem::path& projectsDirectory, const std::string& name)
    {
        const std::filesystem::path directory = projectsDirectory / name;
        const std::filesystem::path librariesDirectory = directory / "Libraries";
        std::filesystem::create_directories(librariesDirectory);
        EXPECT_TRUE(WriteTextFile(librariesDirectory / "CMakeLists.txt", "gte_add_project(" + name + ")\n"));
        m_createdDirectories.push_back(directory);
        return directory;
    }

    EditorProjectLifecycleCapability m_capability;
    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
    // Every fixture directory this test created - TearDown() removes each
    // one (best-effort) so no scratch project folder ever survives a test
    // run.
    std::vector<std::filesystem::path> m_createdDirectories;
};

// Scenario 1 (PHASE5 strategy file, Section 3.1, item 1): list_projects
// against fixture tiers.
TEST_F(OpenProjectEndpointEndToEndTest, ListProjectsReturnsCorrectTiersForTier0Tier1Tier2Fixtures)
{
    const auto tier2 = CreateRealTier2Fixture();
    const std::string& tier2Name = tier2.first;
    const std::filesystem::path projectsDirectory = tier2.second.parent_path();
    ASSERT_FALSE(projectsDirectory.empty());

    const std::string tier0Name = MakeUniqueProjectName("GteE2EOpenProjectTier0_");
    CreateTier0Fixture(projectsDirectory, tier0Name);

    const std::string tier1Name = MakeUniqueProjectName("GteE2EOpenProjectTier1_");
    CreateTier1Fixture(projectsDirectory, tier1Name);

    const httplib::Result res = m_client->Get("/project_assembly/list_projects");
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_TRUE(body.is_array());

    EXPECT_EQ(FindTierForName(body, tier0Name), "NotAProject");
    EXPECT_EQ(FindTierForName(body, tier1Name), "NotBuildable");
    EXPECT_EQ(FindTierForName(body, tier2Name), "NotCompiled");
}

// Scenario 2: open_project success on a Tier-2 ("not yet compiled") fixture.
TEST_F(OpenProjectEndpointEndToEndTest, OpenProjectOnTier2FixtureReturns200WithNotYetCompiledStatus)
{
    const auto tier2 = CreateRealTier2Fixture();

    const httplib::Result res = m_client->Post("/project_assembly/open_project?name=" + tier2.first);
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_TRUE(body.contains("load_attempted"));
    EXPECT_EQ(body["load_attempted"], false);
    ASSERT_TRUE(body.contains("status_message"));
    EXPECT_NE(body["status_message"].get<std::string>().find("not yet compiled"), std::string::npos);
}

// Scenario 3: open_project failure on a Tier-0/nonexistent name - HTTP 400,
// a clear error message, and ActiveProjectAssemblyState::GetActive() left
// unaffected. Deliberately compares a "before" and "after" snapshot of the
// SAME process-wide singleton (never a fixed baseline) since this singleton
// is shared across every test in this binary and other tests legitimately
// mutate it - only a RELATIVE "this call changed nothing" check is safe
// here.
TEST_F(OpenProjectEndpointEndToEndTest, OpenProjectOnTier0OrNonexistentNameReturns400AndDoesNotMutateActiveState)
{
    const auto tier2 = CreateRealTier2Fixture();
    const std::filesystem::path projectsDirectory = tier2.second.parent_path();
    ASSERT_FALSE(projectsDirectory.empty());
    const std::string tier0Name = MakeUniqueProjectName("GteE2EOpenProjectTier0_");
    CreateTier0Fixture(projectsDirectory, tier0Name);

    const ActiveProjectAssemblyInfo before = ActiveProjectAssemblyState::Instance().GetActive();

    // A Tier-0 folder that genuinely exists on disk, but is missing
    // Libraries/CMakeLists.txt.
    {
        const httplib::Result res = m_client->Post("/project_assembly/open_project?name=" + tier0Name);
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 400);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        ASSERT_TRUE(body.contains("error"));
        EXPECT_FALSE(body["error"].get<std::string>().empty());
    }
    // A genuinely nonexistent name - no folder at all on disk.
    {
        const std::string nonexistentName = MakeUniqueProjectName("GteE2EOpenProjectNonexistent_");
        const httplib::Result res = m_client->Post("/project_assembly/open_project?name=" + nonexistentName);
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 400);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        ASSERT_TRUE(body.contains("error"));
        EXPECT_FALSE(body["error"].get<std::string>().empty());
    }

    const ActiveProjectAssemblyInfo after = ActiveProjectAssemblyState::Instance().GetActive();
    EXPECT_EQ(before.hasActiveProject, after.hasActiveProject);
    EXPECT_EQ(before.name, after.name);
    EXPECT_EQ(before.sourceDirectory, after.sourceDirectory);
}

// Scenario 4: all 4 invalid-name shapes BIG-STEP 2's own test already
// proved (CreateProjectEndpointEndToEndTests.cpp,
// InvalidNamesAreRejectedBeforeAnyFilesystemWrite) rejected before any
// state mutation.
TEST_F(OpenProjectEndpointEndToEndTest, OpenProjectRejectsAllFourInvalidNameShapesBeforeAnyStateMutation)
{
    const ActiveProjectAssemblyInfo before = ActiveProjectAssemblyState::Instance().GetActive();

    // "My Project" is URL-encoded as %20 up front so the request line itself
    // stays well-formed - mirrors CreateProjectEndpointEndToEndTests.cpp's
    // own exact convention. An empty name is represented by omitting the
    // query string entirely.
    const std::vector<std::string> invalidNameQueryStrings = {
        "?name=..%2Fevil",
        "?name=CON",
        "?name=My%20Project",
        "", // no `name` query parameter at all.
    };
    for (const std::string& queryString : invalidNameQueryStrings) {
        const httplib::Result res = m_client->Post("/project_assembly/open_project" + queryString);
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 400) << "queryString='" << queryString << "'";
        const nlohmann::json body = nlohmann::json::parse(res->body);
        ASSERT_TRUE(body.contains("error"));
        EXPECT_FALSE(body["error"].get<std::string>().empty());
    }

    const ActiveProjectAssemblyInfo after = ActiveProjectAssemblyState::Instance().GetActive();
    EXPECT_EQ(before.hasActiveProject, after.hasActiveProject);
    EXPECT_EQ(before.name, after.name);
    EXPECT_EQ(before.sourceDirectory, after.sourceDirectory);
}

// Tier-3/Tier-4 coverage - deliberately points at the real, persistent
// ProjectAssemblyProbe fixture (read-only from this test's own point of
// view - never deleted/recreated/modified), guarded by a runtime skip if
// this build's own output directory does not yet contain a compiled
// ProjectAssemblyProbe_Game.dll - mirrors this repository's existing,
// legitimate environment-gated skip convention.
TEST_F(OpenProjectEndpointEndToEndTest, OpenProjectOnRealCompiledProbeMarksItActiveWithoutTouchingItsFiles)
{
    const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
    const std::filesystem::path probeDllPath = outputDirectory / "ProjectAssemblyProbe_Game.dll";
    if (!std::filesystem::exists(probeDllPath)) {
        GTEST_SKIP() << "ProjectAssemblyProbe_Game.dll does not exist yet at " << probeDllPath.string()
                     << " - this build's own output directory has not compiled the probe fixture yet";
    }

    const httplib::Result listRes = m_client->Get("/project_assembly/list_projects");
    ASSERT_TRUE(listRes != nullptr);
    ASSERT_EQ(listRes->status, 200);
    const nlohmann::json listBody = nlohmann::json::parse(listRes->body);
    const std::string probeTier = FindTierForName(listBody, "ProjectAssemblyProbe");
    EXPECT_TRUE(probeTier == "Compiled" || probeTier == "AlreadyLoaded")
        << "unexpected tier for ProjectAssemblyProbe: '" << probeTier << "'";

    const httplib::Result openRes = m_client->Post("/project_assembly/open_project?name=ProjectAssemblyProbe");
    ASSERT_TRUE(openRes != nullptr);
    EXPECT_EQ(openRes->status, 200);
    const nlohmann::json openBody = nlohmann::json::parse(openRes->body);
    ASSERT_TRUE(openBody.contains("status_message"));
    EXPECT_FALSE(openBody["status_message"].get<std::string>().empty());

    const ActiveProjectAssemblyInfo active = ActiveProjectAssemblyState::Instance().GetActive();
    EXPECT_TRUE(active.hasActiveProject);
    EXPECT_EQ(active.name, "ProjectAssemblyProbe");
}

} // namespace

// A NetworkServer constructed with projectLifecycleCapability == nullptr
// (mirrors CreateProjectEndpointNoCapabilityTests's own exact convention)
// must respond 503 for BOTH new routes, never crash.
TEST(OpenProjectEndpointNoCapabilityTests, MissingCapabilityReturns503ForBothRoutes)
{
    Network::NetworkServer server; // Every bridge/capability pointer defaults to nullptr.
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());
    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    {
        const httplib::Result res = client.Post("/project_assembly/open_project?name=SomeProject");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 503);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        EXPECT_EQ(body["success"], false);
    }
    {
        const httplib::Result res = client.Get("/project_assembly/list_projects");
        ASSERT_TRUE(res != nullptr);
        EXPECT_EQ(res->status, 503);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        EXPECT_EQ(body["success"], false);
    }

    server.Stop();
}

} // namespace gte
