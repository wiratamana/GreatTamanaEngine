// Tier-1 tests for src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp's new
// ProjectValidityTier enum / ClassifyProjectAssemblyFolder() addition
// (editor-core-separation-17 campaign, On-Engine Project Workflow plan,
// BIG-STEP 3, PHASE1 - PHASE1_TIER_CLASSIFICATION_MODEL.md). A genuinely
// new file (no existing sibling to extend) - mirrors
// ProjectAssemblyBuildRunnerSourceRootTests.cpp's own exact
// TempOutputDirectory/WriteFile() helper style rather than re-inventing it.

#include "Core/Plugins/ProjectAssemblyBuildRunner.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gte {
namespace {

// A unique-per-test-run temp directory, cleaned up (best-effort) at the end
// of each test via its own destructor - never touches the real, production
// build/ tree.
class TempOutputDirectory {
public:
    explicit TempOutputDirectory(const std::string& uniqueSuffix)
        : m_path(std::filesystem::temp_directory_path() / ("gte_tier_classification_tests_" + uniqueSuffix))
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TempOutputDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    const std::filesystem::path& Path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

void WriteFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << content;
}

} // namespace

TEST(ProjectAssemblyBuildRunnerTierClassificationTest, NotAProjectWhenCandidateFolderIsCompletelyEmpty)
{
    TempOutputDirectory candidate("Case1_EmptyFolder");
    TempOutputDirectory output("Case1_Output");
    EXPECT_EQ(ClassifyProjectAssemblyFolder(candidate.Path(), output.Path(), {}), ProjectValidityTier::NotAProject);
}

TEST(ProjectAssemblyBuildRunnerTierClassificationTest, NotAProjectWhenLibrariesExistsButHasNoCMakeLists)
{
    TempOutputDirectory candidate("Case2_LibrariesNoCMakeLists");
    TempOutputDirectory output("Case2_Output");
    std::filesystem::create_directories(candidate.Path() / "Libraries");
    EXPECT_EQ(ClassifyProjectAssemblyFolder(candidate.Path(), output.Path(), {}), ProjectValidityTier::NotAProject);
}

TEST(ProjectAssemblyBuildRunnerTierClassificationTest, NotBuildableWhenAssetsFolderIsMissingEntirely)
{
    TempOutputDirectory candidate("Case3_NoAssets");
    TempOutputDirectory output("Case3_Output");
    WriteFile(candidate.Path() / "Libraries" / "CMakeLists.txt", "gte_add_project(Case3)\n");
    EXPECT_EQ(ClassifyProjectAssemblyFolder(candidate.Path(), output.Path(), {}), ProjectValidityTier::NotBuildable);
}

TEST(ProjectAssemblyBuildRunnerTierClassificationTest, NotBuildableWhenAssetsFolderExistsButIsEmpty)
{
    TempOutputDirectory candidate("Case4_EmptyAssets");
    TempOutputDirectory output("Case4_Output");
    WriteFile(candidate.Path() / "Libraries" / "CMakeLists.txt", "gte_add_project(Case4)\n");
    std::filesystem::create_directories(candidate.Path() / "Assets");
    EXPECT_EQ(ClassifyProjectAssemblyFolder(candidate.Path(), output.Path(), {}), ProjectValidityTier::NotBuildable);
}

TEST(ProjectAssemblyBuildRunnerTierClassificationTest, NotCompiledWhenRealGameSourceExistsButNoMatchingDll)
{
    TempOutputDirectory candidate("Case5_NotCompiled");
    TempOutputDirectory output("Case5_Output");
    WriteFile(candidate.Path() / "Libraries" / "CMakeLists.txt", "gte_add_project(Case5_NotCompiled)\n");
    WriteFile(candidate.Path() / "Assets" / "SomethingGame.cpp", "// game source\n");
    EXPECT_EQ(ClassifyProjectAssemblyFolder(candidate.Path(), output.Path(), {}), ProjectValidityTier::NotCompiled);
}

TEST(ProjectAssemblyBuildRunnerTierClassificationTest, CompiledWhenMatchingGameDllExistsAndIsNotLoaded)
{
    TempOutputDirectory candidate("Case6_Compiled");
    TempOutputDirectory output("Case6_Output");
    const std::string name = candidate.Path().filename().string();
    WriteFile(candidate.Path() / "Libraries" / "CMakeLists.txt", "gte_add_project(" + name + ")\n");
    WriteFile(candidate.Path() / "Assets" / "SomethingGame.cpp", "// game source\n");
    WriteFile(output.Path() / (name + "_Game.dll"), "");
    EXPECT_EQ(ClassifyProjectAssemblyFolder(candidate.Path(), output.Path(), {}), ProjectValidityTier::Compiled);
}

TEST(ProjectAssemblyBuildRunnerTierClassificationTest, AlreadyLoadedWhenMatchingGameDllFileNameIsInLoadedList)
{
    TempOutputDirectory candidate("Case7_AlreadyLoaded");
    TempOutputDirectory output("Case7_Output");
    const std::string name = candidate.Path().filename().string();
    WriteFile(candidate.Path() / "Libraries" / "CMakeLists.txt", "gte_add_project(" + name + ")\n");
    WriteFile(candidate.Path() / "Assets" / "SomethingGame.cpp", "// game source\n");
    WriteFile(output.Path() / (name + "_Game.dll"), "");
    const std::vector<std::string> loadedDllFileNames{ name + "_Game.dll" };
    EXPECT_EQ(ClassifyProjectAssemblyFolder(candidate.Path(), output.Path(), loadedDllFileNames), ProjectValidityTier::AlreadyLoaded);
}

TEST(ProjectAssemblyBuildRunnerTierClassificationTest, NotAProjectWhenCandidateFolderDoesNotExistAtAllAndNeverThrows)
{
    TempOutputDirectory output("Case8_Output");
    const std::filesystem::path neverCreated = std::filesystem::temp_directory_path() / "gte_tier_classification_tests_Case8_NeverCreated_DoesNotExist";
    std::error_code ignored;
    std::filesystem::remove_all(neverCreated, ignored); // Guarantee it really does not exist.
    EXPECT_NO_THROW({
        EXPECT_EQ(ClassifyProjectAssemblyFolder(neverCreated, output.Path(), {}), ProjectValidityTier::NotAProject);
    });
}

// Load-bearing case (PHASE1_TIER_CLASSIFICATION_MODEL.md, 3.3, case 9) - a
// .cpp file nested several folders deep under Assets/ (not directly inside
// it) must still count as real, buildable Game source, exactly mirroring
// gte_add_project()'s own GLOB_RECURSE (cmake/GteProject.cmake) - a plain,
// non-recursive scan would wrongly report NotBuildable here.
TEST(ProjectAssemblyBuildRunnerTierClassificationTest, NotCompiledWhenOnlyANestedGameSourceFileExistsRecursiveCase)
{
    TempOutputDirectory candidate("Case9_NestedGameSource");
    TempOutputDirectory output("Case9_Output");
    WriteFile(candidate.Path() / "Libraries" / "CMakeLists.txt", "gte_add_project(Case9_NestedGameSource)\n");
    WriteFile(candidate.Path() / "Assets" / "Sub" / "SomethingGame.cpp", "// nested game source\n");
    EXPECT_EQ(ClassifyProjectAssemblyFolder(candidate.Path(), output.Path(), {}), ProjectValidityTier::NotCompiled);
}

// Load-bearing case (PHASE1_TIER_CLASSIFICATION_MODEL.md, 3.3, case 10) -
// the ONLY .cpp file present lives under an "/Editor/" path segment, which
// gte_add_project() buckets into EDITOR_SOURCES, never GAME_SOURCES - this
// must classify as NotBuildable (no real Game source), never NotCompiled.
TEST(ProjectAssemblyBuildRunnerTierClassificationTest, NotBuildableWhenOnlyEditorOnlySourceExistsEditorAwareCase)
{
    TempOutputDirectory candidate("Case10_EditorOnlySource");
    TempOutputDirectory output("Case10_Output");
    WriteFile(candidate.Path() / "Libraries" / "CMakeLists.txt", "gte_add_project(Case10_EditorOnlySource)\n");
    WriteFile(candidate.Path() / "Assets" / "Editor" / "SomethingEditorTool.cpp", "// editor-only source\n");
    EXPECT_EQ(ClassifyProjectAssemblyFolder(candidate.Path(), output.Path(), {}), ProjectValidityTier::NotBuildable);
}

} // namespace gte
