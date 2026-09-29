// Tier-1 tests for src/Editor/EditorProjectLifecycleCapability.h/.cpp's new
// IAssetScaffoldingCapability::CreateAssetScaffold() addition
// (editor-core-separation-18 campaign, On-Engine Project Workflow plan,
// BIG-STEP 4, PHASE1 - PHASE1_ASSET_SCAFFOLDING_CAPABILITY_AND_TEMPLATES.md,
// STEP 4).
//
// Mechanically confirmed correction, restated here so it is never silently
// re-litigated: BuildScaffoldFileSpecs()/ReminderMessageForKind()/every
// Build...Content() template builder/ToLowerAscii()/ReplaceAll() all live
// inside an UNNAMED (anonymous) namespace physically inside
// EditorProjectLifecycleCapability.cpp - they have internal linkage and are
// NOT visible/callable from this separate test .cpp file at all. This
// exactly matches this same file's own pre-existing convention
// (BuildGameStubCppContent()/WriteTextFile()/ToTierName() have ZERO direct
// unit tests anywhere in tests/ today - only ever exercised INDIRECTLY,
// through CreateNewProjectAssembly()/OpenProjectAssembly()). Every test
// below exercises the templates/collision logic ONLY through the one real
// public entry point, CreateAssetScaffold().
//
// File location: tests/Editor/ (NOT tests/Core/Plugins/) -
// EditorProjectLifecycleCapability is gte_editor-tier code (physically
// compiled into the gte_editor static library, exactly like every
// Network/*ProjectEndpointEndToEndTests.cpp file that already links against
// it) - tests/Core/Plugins/ is reserved for genuinely gte_core-tier-only
// code needing no gte_editor symbol at all.
//
// Test-isolation hazard (made explicit so it is never silently
// reintroduced): ActiveProjectAssemblyState is a real, process-wide Meyers
// singleton shared by EVERY test in GreatTamanaEngineTests.exe, including
// CreateProjectEndpointEndToEndTests.cpp/OpenProjectEndpointEndToEndTests.cpp,
// both of which already legitimately call SetActive() as a side effect of
// their own successful paths - gtest gives no cross-file test-order
// guarantee, so this file can NEVER assume GetActive().hasActiveProject
// starts false. Every test below captures a "before" snapshot first
// (OpenProjectEndpointEndToEndTests.cpp's own
// OpenProjectOnTier0OrNonexistentNameReturns400AndDoesNotMutateActiveState
// idiom), and the one test that genuinely needs hasActiveProject == false
// explicitly forces it via Clear() then restores "before" afterward.

#include "Editor/ActiveProjectAssemblyState.h"
#include "Editor/EditorProjectLifecycleCapability.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gte {
namespace {

// A unique-per-test-run temp directory, cleaned up (best-effort) at the end
// of each test via its own destructor - never touches the real, production
// Projects/ tree. Mirrors
// tests/Core/Plugins/ProjectAssemblyBuildRunnerTierClassificationTests.cpp's
// own TempOutputDirectory helper for the create/cleanup SHAPE only.
class TempScratchProjectDirectory {
public:
    explicit TempScratchProjectDirectory(const std::string& uniqueSuffix)
        : m_path(std::filesystem::temp_directory_path() / ("gte_asset_scaffold_tests_" + uniqueSuffix))
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path / "Assets");
    }

    ~TempScratchProjectDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    const std::filesystem::path& Path() const { return m_path; }
    std::filesystem::path AssetsPath() const { return m_path / "Assets"; }

private:
    std::filesystem::path m_path;
};

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

// RAII helper restoring ActiveProjectAssemblyState to whatever it was before
// this test touched it - every test uses this so no scratch project name
// or "no active project" state ever leaks into whichever test happens to
// run next in this same binary.
class ActiveProjectStateRestorer {
public:
    ActiveProjectStateRestorer() : m_before(ActiveProjectAssemblyState::Instance().GetActive()) {}
    ~ActiveProjectStateRestorer()
    {
        if (m_before.hasActiveProject) {
            ActiveProjectAssemblyState::Instance().SetActive(m_before.name, m_before.sourceDirectory);
        } else {
            ActiveProjectAssemblyState::Instance().Clear();
        }
    }

private:
    ActiveProjectAssemblyInfo m_before;
};

} // namespace

TEST(AssetScaffoldTemplateTest, NoActiveProjectFailsWithClearMessage)
{
    ActiveProjectStateRestorer restorer;
    ActiveProjectAssemblyState::Instance().Clear();

    EditorProjectLifecycleCapability capability;
    const IAssetScaffoldingCapability::ScaffoldOutcome outcome =
        capability.CreateAssetScaffold(AssetScaffoldKind::RenderPass, "Foo");

    EXPECT_FALSE(outcome.success);
    EXPECT_NE(outcome.errorMessage.find("no active project"), std::string::npos);
    EXPECT_TRUE(outcome.createdFiles.empty());
}

TEST(AssetScaffoldTemplateTest, RenderPassKindCreatesOneFileWithSubstitutedNameAndReminder)
{
    ActiveProjectStateRestorer restorer;
    TempScratchProjectDirectory scratch("RenderPass");
    ActiveProjectAssemblyState::Instance().SetActive("ScratchProject", scratch.Path());

    EditorProjectLifecycleCapability capability;
    const IAssetScaffoldingCapability::ScaffoldOutcome outcome =
        capability.CreateAssetScaffold(AssetScaffoldKind::RenderPass, "Foo");

    ASSERT_TRUE(outcome.success) << outcome.errorMessage;
    ASSERT_EQ(outcome.createdFiles.size(), 1u);
    EXPECT_EQ(outcome.createdFiles[0], "FooRenderPass.cpp");
    EXPECT_FALSE(outcome.reminderMessage.empty());
    EXPECT_NE(outcome.reminderMessage.find("RegisterFooRenderPass"), std::string::npos);

    ASSERT_TRUE(std::filesystem::exists(scratch.AssetsPath() / "FooRenderPass.cpp"));
    const std::string content = ReadFile(scratch.AssetsPath() / "FooRenderPass.cpp");
    EXPECT_EQ(content.find("__NAME__"), std::string::npos);
    EXPECT_NE(content.find("RegisterFooRenderPass"), std::string::npos);
    EXPECT_NE(content.find("\"Foo.RenderPass\""), std::string::npos);
    // Regression proof for the corrected template (PHASE0_MASTER_STRATEGY.md,
    // Section 2) - the real, two-value ProviderScope enum and the real,
    // two-parameter RenderPassProvider lambda signature, never the
    // master-plan file's own non-compiling sketch.
    EXPECT_NE(content.find("gte::rg::ProviderScope::Once"), std::string::npos);
    EXPECT_NE(content.find("std::vector<gte::rg::RenderPassDesc>& outPasses"), std::string::npos);
}

TEST(AssetScaffoldTemplateTest, ComputeShaderKindCreatesTwoFilesWithSubstitutedNameAndReminder)
{
    ActiveProjectStateRestorer restorer;
    TempScratchProjectDirectory scratch("ComputeShader");
    ActiveProjectAssemblyState::Instance().SetActive("ScratchProject", scratch.Path());

    EditorProjectLifecycleCapability capability;
    const IAssetScaffoldingCapability::ScaffoldOutcome outcome =
        capability.CreateAssetScaffold(AssetScaffoldKind::ComputeShader, "Foo");

    ASSERT_TRUE(outcome.success) << outcome.errorMessage;
    ASSERT_EQ(outcome.createdFiles.size(), 2u);
    EXPECT_EQ(outcome.createdFiles[0], "Foo.comp");
    EXPECT_EQ(outcome.createdFiles[1], "FooComputePass.cpp");
    EXPECT_FALSE(outcome.reminderMessage.empty());
    EXPECT_NE(outcome.reminderMessage.find("RegisterFooComputePass"), std::string::npos);

    ASSERT_TRUE(std::filesystem::exists(scratch.AssetsPath() / "Foo.comp"));
    ASSERT_TRUE(std::filesystem::exists(scratch.AssetsPath() / "FooComputePass.cpp"));
    const std::string computeContent = ReadFile(scratch.AssetsPath() / "Foo.comp");
    EXPECT_EQ(computeContent.find("__NAME__"), std::string::npos);
    EXPECT_NE(computeContent.find("#version 450"), std::string::npos);
    const std::string passContent = ReadFile(scratch.AssetsPath() / "FooComputePass.cpp");
    EXPECT_EQ(passContent.find("__NAME__"), std::string::npos);
    EXPECT_NE(passContent.find("RegisterFooComputePass"), std::string::npos);
    EXPECT_NE(passContent.find("g_FooComputePipeline"), std::string::npos);
}

TEST(AssetScaffoldTemplateTest, ShaderPairKindCreatesTwoFilesWithNoReminderAndNoCompanionCpp)
{
    ActiveProjectStateRestorer restorer;
    TempScratchProjectDirectory scratch("ShaderPair");
    ActiveProjectAssemblyState::Instance().SetActive("ScratchProject", scratch.Path());

    EditorProjectLifecycleCapability capability;
    const IAssetScaffoldingCapability::ScaffoldOutcome outcome =
        capability.CreateAssetScaffold(AssetScaffoldKind::ShaderPair, "Foo");

    ASSERT_TRUE(outcome.success) << outcome.errorMessage;
    ASSERT_EQ(outcome.createdFiles.size(), 2u);
    EXPECT_EQ(outcome.createdFiles[0], "Foo.vert");
    EXPECT_EQ(outcome.createdFiles[1], "Foo.frag");
    EXPECT_TRUE(outcome.reminderMessage.empty());

    ASSERT_TRUE(std::filesystem::exists(scratch.AssetsPath() / "Foo.vert"));
    ASSERT_TRUE(std::filesystem::exists(scratch.AssetsPath() / "Foo.frag"));
    EXPECT_FALSE(std::filesystem::exists(scratch.AssetsPath() / "Foo.cpp"));
    const std::string vertContent = ReadFile(scratch.AssetsPath() / "Foo.vert");
    EXPECT_NE(vertContent.find("gl_Position"), std::string::npos);
    const std::string fragContent = ReadFile(scratch.AssetsPath() / "Foo.frag");
    EXPECT_NE(fragContent.find("outColor"), std::string::npos);
}

TEST(AssetScaffoldTemplateTest, SameNameSameKindCalledTwiceRejectsSecondCallAndLeavesOriginalFileUntouched)
{
    ActiveProjectStateRestorer restorer;
    TempScratchProjectDirectory scratch("DuplicateSameCase");
    ActiveProjectAssemblyState::Instance().SetActive("ScratchProject", scratch.Path());

    EditorProjectLifecycleCapability capability;
    const IAssetScaffoldingCapability::ScaffoldOutcome first =
        capability.CreateAssetScaffold(AssetScaffoldKind::RenderPass, "Foo");
    ASSERT_TRUE(first.success) << first.errorMessage;
    const std::string originalContent = ReadFile(scratch.AssetsPath() / "FooRenderPass.cpp");
    const std::filesystem::file_time_type originalMtime =
        std::filesystem::last_write_time(scratch.AssetsPath() / "FooRenderPass.cpp");

    const IAssetScaffoldingCapability::ScaffoldOutcome second =
        capability.CreateAssetScaffold(AssetScaffoldKind::RenderPass, "Foo");

    EXPECT_FALSE(second.success);
    EXPECT_NE(second.errorMessage.find("already exists"), std::string::npos);
    EXPECT_TRUE(second.createdFiles.empty());

    // The original file's content/mtime genuinely never changed - a real,
    // mechanical proof, not an assumed side effect.
    EXPECT_EQ(ReadFile(scratch.AssetsPath() / "FooRenderPass.cpp"), originalContent);
    EXPECT_EQ(std::filesystem::last_write_time(scratch.AssetsPath() / "FooRenderPass.cpp"), originalMtime);
}

TEST(AssetScaffoldTemplateTest, SameNameDifferentCaseCalledTwiceRejectsSecondCallCaseInsensitively)
{
    ActiveProjectStateRestorer restorer;
    TempScratchProjectDirectory scratch("DuplicateDifferentCase");
    ActiveProjectAssemblyState::Instance().SetActive("ScratchProject", scratch.Path());

    EditorProjectLifecycleCapability capability;
    const IAssetScaffoldingCapability::ScaffoldOutcome first =
        capability.CreateAssetScaffold(AssetScaffoldKind::ComputeShader, "Foo");
    ASSERT_TRUE(first.success) << first.errorMessage;

    // "foo" vs. "Foo" - a different-case variant of the same base name -
    // must ALSO be rejected (case-insensitive collision, LDD-CA# in
    // PHASE0_MASTER_STRATEGY.md / risk register item 5).
    const IAssetScaffoldingCapability::ScaffoldOutcome second =
        capability.CreateAssetScaffold(AssetScaffoldKind::ComputeShader, "foo");

    EXPECT_FALSE(second.success);
    EXPECT_NE(second.errorMessage.find("already exists"), std::string::npos);
    EXPECT_TRUE(second.createdFiles.empty());
}

} // namespace gte
