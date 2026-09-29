// End-to-end tests for POST /project_assembly/create_asset -
// editor-core-separation-18 campaign (On-Engine Project Workflow plan,
// BIG-STEP 4), PHASE4 (PHASE4_LIVE_VERIFICATION_TESTS_AND_CAMPAIGN_CLOSEOUT.md,
// STEP 3.1). Mirrors tests/Network/CreateProjectEndpointEndToEndTests.cpp's
// own real, existing NetworkServer/httplib::Client/
// WaitUntilAcceptingConnections fixture shape.
//
// Real difference from that file: CreateProjectEndpointEndToEndTests.cpp
// never itself sets ActiveProjectAssemblyState directly (it only calls
// CreateNewProjectAssembly() via HTTP, which sets it as a side effect) -
// this file's own tests call ActiveProjectAssemblyState::Instance().SetActive()/
// Clear() DIRECTLY, so it instead mirrors
// tests/Network/OpenProjectEndpointEndToEndTests.cpp's own "capture a
// before snapshot, never assume a pristine empty state" idiom:
// ActiveProjectAssemblyState is a REAL, process-wide singleton shared by
// EVERY test in GreatTamanaEngineTests.exe, including
// CreateProjectEndpointEndToEndTests.cpp/OpenProjectEndpointEndToEndTests.cpp,
// both of which already legitimately mutate it. This file's own
// "no active project" case explicitly captures a "before" snapshot, calls
// Clear() itself, runs its assertion, then restores the captured "before"
// snapshot afterward (via SetActive() if it had one, or leaving it Clear()'d
// if it didn't) - it never assumes Clear() was already the state on entry,
// and never leaves the singleton in a different state than it found it.
//
// Safe to redirect ActiveProjectAssemblyState at a real, disposable scratch
// temp directory here (unlike CreateProjectEndpointEndToEndTests.cpp's own
// CreateNewProjectAssembly() test, which cannot) - CreateAssetScaffold()
// never resolves its own source root internally, it only ever reads
// ActiveProjectAssemblyState::Instance().GetActive() (see PHASE1's own
// STEP4 reasoning, EditorProjectLifecycleCapability.cpp).
#include "Editor/ActiveProjectAssemblyState.h"
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
#include <fstream>
#include <memory>
#include <string>

namespace gte {
namespace {

// A genuinely unique, disposable scratch directory name per test run -
// mirrors CreateProjectEndpointEndToEndTests.cpp's own
// MakeUniqueProjectName() exactly (monotonic steady-clock tick count + a
// per-process atomic counter), just renamed since this file's own fixture
// is a bare scratch directory, never a real project created through
// CreateNewProjectAssembly()'s own scaffold.
std::string MakeUniqueScratchName(const std::string& prefix)
{
    static std::atomic<std::uint64_t> s_counter{ 0 };
    const std::uint64_t ticks = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    return prefix + std::to_string(ticks % 1000000000ULL) + "_" + std::to_string(s_counter.fetch_add(1));
}

// A real gte::Network::NetworkServer, started on an ephemeral port, wired
// with a real EditorProjectLifecycleCapability instance as BOTH the 9th
// (IProjectLifecycleCapability*) AND 10th (IAssetScaffoldingCapability*)
// constructor arguments (LDD-CA4 - one class, multiple inheritance, two
// base-sub-object pointers into the SAME object) - mirrors
// CreateProjectEndpointEndToEndTest's own fixture shape, extended by one
// argument.
class CreateAssetEndpointEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_server = std::make_unique<Network::NetworkServer>(
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &m_capability, &m_capability);
        m_server->Start(0);
        ASSERT_TRUE(m_server->IsRunning());
        m_client = std::make_unique<httplib::Client>("127.0.0.1", m_server->BoundPort());
        Network::TestHelpers::WaitUntilAcceptingConnections(*m_server, *m_client);

        // Real, disposable scratch project directory - a bare Assets/
        // subfolder is all CreateAssetScaffold() ever reads/writes
        // (active.assetsDirectory), so nothing under Libraries/ is needed
        // for this file's own purposes.
        m_scratchProjectDirectory = std::filesystem::temp_directory_path()
            / MakeUniqueScratchName("GteE2ECreateAssetScratch_");
        m_scratchAssetsDirectory = m_scratchProjectDirectory / "Assets";
        std::filesystem::create_directories(m_scratchAssetsDirectory);
    }

    void TearDown() override
    {
        m_client.reset();
        m_server.reset();
        std::error_code ignoredError;
        std::filesystem::remove_all(m_scratchProjectDirectory, ignoredError);
    }

    // Captures ActiveProjectAssemblyState's own "before" snapshot, points
    // it at this test's own real, disposable scratch directory, and
    // returns the captured snapshot so the caller can restore it in its own
    // teardown - mirrors OpenProjectEndpointEndToEndTests.cpp's own
    // "capture a before snapshot, never assume a pristine empty state"
    // idiom exactly.
    ActiveProjectAssemblyInfo ActivateScratchProjectAndCaptureBeforeSnapshot(const std::string& scratchProjectName)
    {
        const ActiveProjectAssemblyInfo before = ActiveProjectAssemblyState::Instance().GetActive();
        ActiveProjectAssemblyState::Instance().SetActive(scratchProjectName, m_scratchProjectDirectory);
        return before;
    }

    // Restores ActiveProjectAssemblyState to whatever `before` snapshot was
    // captured earlier - SetActive() if it had a real active project,
    // Clear() otherwise. Every test in this file MUST call this in its own
    // body (there is no shared TearDown() hook for it, since not every test
    // touches ActiveProjectAssemblyState at all) before returning, so this
    // process-wide singleton is never left mutated for whichever test in
    // this binary happens to run next.
    static void RestoreActiveProjectAssemblyState(const ActiveProjectAssemblyInfo& before)
    {
        if (before.hasActiveProject) {
            ActiveProjectAssemblyState::Instance().SetActive(before.name, before.sourceDirectory);
        } else {
            ActiveProjectAssemblyState::Instance().Clear();
        }
    }

    EditorProjectLifecycleCapability m_capability;
    std::unique_ptr<Network::NetworkServer> m_server;
    std::unique_ptr<httplib::Client> m_client;
    std::filesystem::path m_scratchProjectDirectory;
    std::filesystem::path m_scratchAssetsDirectory;
};

// Scenario 1 (PHASE4 strategy file, STEP 3.1, item 1) - no active project
// set: explicitly Clear() ActiveProjectAssemblyState (capturing/restoring
// the real "before" snapshot per this file's own header comment), then
// confirm 400 + "no active project" message.
TEST_F(CreateAssetEndpointEndToEndTest, NoActiveProjectReturns400WithClearMessage)
{
    const ActiveProjectAssemblyInfo before = ActiveProjectAssemblyState::Instance().GetActive();
    ActiveProjectAssemblyState::Instance().Clear();

    const httplib::Result res = m_client->Post("/project_assembly/create_asset?kind=render_pass&name=X");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_TRUE(body.contains("error"));
    EXPECT_NE(body["error"].get<std::string>().find("no active project"), std::string::npos);

    RestoreActiveProjectAssemblyState(before);
}

// Scenario 2 - invalid `kind` query value: 400, error message names the 3
// valid values. Deliberately does NOT touch ActiveProjectAssemblyState at
// all - the route's own kind-parsing happens BEFORE the capability is even
// consulted (NetworkServer.cpp), so this is true regardless of whatever
// active-project state this test binary happens to be in when it runs.
TEST_F(CreateAssetEndpointEndToEndTest, InvalidKindReturns400NamingTheThreeValidValues)
{
    const httplib::Result res = m_client->Post("/project_assembly/create_asset?kind=not_a_real_kind&name=X");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 400);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    ASSERT_TRUE(body.contains("error"));
    const std::string errorMessage = body["error"].get<std::string>();
    EXPECT_NE(errorMessage.find("render_pass"), std::string::npos);
    EXPECT_NE(errorMessage.find("compute_shader"), std::string::npos);
    EXPECT_NE(errorMessage.find("shader_pair"), std::string::npos);
}

// Scenario 3 - a real active project (this test's own scratch directory),
// one successful scaffold call per kind (3 cases), asserting the exact
// expected created_files list and that each file genuinely exists on disk
// with the expected substituted content.
TEST_F(CreateAssetEndpointEndToEndTest, OneSuccessfulScaffoldPerKindWritesExpectedRealFiles)
{
    const ActiveProjectAssemblyInfo before =
        ActivateScratchProjectAndCaptureBeforeSnapshot("GteE2ECreateAssetScratchProject");

    // RenderPass - ONE file, byte-for-byte matching PHASE1's real template
    // shape (the corrected ProviderScope::Once / two-parameter
    // RenderPassProvider lambda - this campaign's own headline risk-
    // register item).
    {
        const httplib::Result res = m_client->Post("/project_assembly/create_asset?kind=render_pass&name=Foo");
        ASSERT_TRUE(res != nullptr);
        ASSERT_EQ(res->status, 200);
        EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");
        const nlohmann::json body = nlohmann::json::parse(res->body);
        ASSERT_TRUE(body.contains("created_files"));
        const std::vector<std::string> createdFiles = body["created_files"].get<std::vector<std::string>>();
        ASSERT_EQ(createdFiles.size(), 1u);
        EXPECT_EQ(createdFiles[0], "FooRenderPass.cpp");
        ASSERT_TRUE(body.contains("reminder_message"));
        const std::string reminderMessage = body["reminder_message"].get<std::string>();
        EXPECT_NE(reminderMessage.find("RegisterFooRenderPass(core)"), std::string::npos);

        const std::filesystem::path filePath = m_scratchAssetsDirectory / "FooRenderPass.cpp";
        ASSERT_TRUE(std::filesystem::exists(filePath));
        std::ifstream stream(filePath, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content.find("__NAME__"), std::string::npos);
        EXPECT_NE(content.find("RegisterFooRenderPass(gte::Core& core)"), std::string::npos);
        EXPECT_NE(content.find("\"Foo.RenderPass\""), std::string::npos);
        EXPECT_NE(content.find("gte::rg::ProviderScope::Once"), std::string::npos);
        EXPECT_NE(content.find("const gte::rg::RenderPassFrameContext& frame, std::vector<gte::rg::RenderPassDesc>& outPasses"),
            std::string::npos);
    }

    // ComputeShader - TWO files (.comp + ComputePass.cpp).
    {
        const httplib::Result res = m_client->Post("/project_assembly/create_asset?kind=compute_shader&name=Bar");
        ASSERT_TRUE(res != nullptr);
        ASSERT_EQ(res->status, 200);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        const std::vector<std::string> createdFiles = body["created_files"].get<std::vector<std::string>>();
        ASSERT_EQ(createdFiles.size(), 2u);
        EXPECT_EQ(createdFiles[0], "Bar.comp");
        EXPECT_EQ(createdFiles[1], "BarComputePass.cpp");
        EXPECT_NE(body["reminder_message"].get<std::string>().find("RegisterBarComputePass(core)"), std::string::npos);
        EXPECT_TRUE(std::filesystem::exists(m_scratchAssetsDirectory / "Bar.comp"));
        EXPECT_TRUE(std::filesystem::exists(m_scratchAssetsDirectory / "BarComputePass.cpp"));
    }

    // ShaderPair - TWO files (.vert + .frag), no companion .cpp, no
    // reminder message.
    {
        const httplib::Result res = m_client->Post("/project_assembly/create_asset?kind=shader_pair&name=Baz");
        ASSERT_TRUE(res != nullptr);
        ASSERT_EQ(res->status, 200);
        const nlohmann::json body = nlohmann::json::parse(res->body);
        const std::vector<std::string> createdFiles = body["created_files"].get<std::vector<std::string>>();
        ASSERT_EQ(createdFiles.size(), 2u);
        EXPECT_EQ(createdFiles[0], "Baz.vert");
        EXPECT_EQ(createdFiles[1], "Baz.frag");
        EXPECT_TRUE(body["reminder_message"].get<std::string>().empty());
        EXPECT_TRUE(std::filesystem::exists(m_scratchAssetsDirectory / "Baz.vert"));
        EXPECT_TRUE(std::filesystem::exists(m_scratchAssetsDirectory / "Baz.frag"));
    }

    RestoreActiveProjectAssemblyState(before);
}

// Scenario 4 - same name, same kind, called twice: second call 400,
// "already exists", and the ORIGINAL file's content/mtime provably
// unchanged (a real, mechanical proof "zero files touched" on rejection).
TEST_F(CreateAssetEndpointEndToEndTest, SameNameSameKindCalledTwiceRejectsSecondCallAndLeavesOriginalFileUntouched)
{
    const ActiveProjectAssemblyInfo before =
        ActivateScratchProjectAndCaptureBeforeSnapshot("GteE2ECreateAssetScratchProjectDup");

    const httplib::Result firstRes = m_client->Post("/project_assembly/create_asset?kind=render_pass&name=Dup");
    ASSERT_TRUE(firstRes != nullptr);
    ASSERT_EQ(firstRes->status, 200);

    const std::filesystem::path filePath = m_scratchAssetsDirectory / "DupRenderPass.cpp";
    ASSERT_TRUE(std::filesystem::exists(filePath));
    const auto mtimeBefore = std::filesystem::last_write_time(filePath);
    std::ifstream beforeStream(filePath, std::ios::binary);
    const std::string contentBefore((std::istreambuf_iterator<char>(beforeStream)), std::istreambuf_iterator<char>());

    const httplib::Result secondRes = m_client->Post("/project_assembly/create_asset?kind=render_pass&name=Dup");
    ASSERT_TRUE(secondRes != nullptr);
    EXPECT_EQ(secondRes->status, 400);
    const nlohmann::json secondBody = nlohmann::json::parse(secondRes->body);
    ASSERT_TRUE(secondBody.contains("error"));
    EXPECT_NE(secondBody["error"].get<std::string>().find("already exists"), std::string::npos);

    const auto mtimeAfter = std::filesystem::last_write_time(filePath);
    std::ifstream afterStream(filePath, std::ios::binary);
    const std::string contentAfter((std::istreambuf_iterator<char>(afterStream)), std::istreambuf_iterator<char>());
    EXPECT_EQ(mtimeBefore, mtimeAfter);
    EXPECT_EQ(contentBefore, contentAfter);

    RestoreActiveProjectAssemblyState(before);
}

// Scenario 5 - same name, different case, called twice: second call ALSO
// 400 (case-insensitive collision, mirrors Windows' own filesystem
// case-insensitivity).
TEST_F(CreateAssetEndpointEndToEndTest, SameNameDifferentCaseCalledTwiceRejectsSecondCallCaseInsensitively)
{
    const ActiveProjectAssemblyInfo before =
        ActivateScratchProjectAndCaptureBeforeSnapshot("GteE2ECreateAssetScratchProjectCase");

    const httplib::Result firstRes = m_client->Post("/project_assembly/create_asset?kind=render_pass&name=CaseTest");
    ASSERT_TRUE(firstRes != nullptr);
    ASSERT_EQ(firstRes->status, 200);

    const httplib::Result secondRes = m_client->Post("/project_assembly/create_asset?kind=render_pass&name=casetest");
    ASSERT_TRUE(secondRes != nullptr);
    EXPECT_EQ(secondRes->status, 400);
    const nlohmann::json secondBody = nlohmann::json::parse(secondRes->body);
    ASSERT_TRUE(secondBody.contains("error"));
    EXPECT_NE(secondBody["error"].get<std::string>().find("already exists"), std::string::npos);

    RestoreActiveProjectAssemblyState(before);
}

} // namespace

// A NetworkServer constructed with assetScaffoldingCapability == nullptr
// (mirrors CreateProjectEndpointNoCapabilityTests's own exact convention)
// must respond 503 for this route, never crash.
TEST(CreateAssetEndpointNoCapabilityTests, MissingCapabilityReturns503)
{
    Network::NetworkServer server; // Every bridge/capability pointer defaults to nullptr.
    server.Start(0);
    ASSERT_TRUE(server.IsRunning());
    httplib::Client client("127.0.0.1", server.BoundPort());
    Network::TestHelpers::WaitUntilAcceptingConnections(server, client);

    const httplib::Result res = client.Post("/project_assembly/create_asset?kind=render_pass&name=SomeAsset");
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->status, 503);
    const nlohmann::json body = nlohmann::json::parse(res->body);
    EXPECT_EQ(body["success"], false);

    server.Stop();
}

} // namespace gte
