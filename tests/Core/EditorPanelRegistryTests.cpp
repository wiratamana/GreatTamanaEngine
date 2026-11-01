// Tests for the process-wide editor window registry. It has no reset, so every test uses its own unique window names.

#include "Core/EditorPanelRegistry.h"
#include "Editor/EditorWindow.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace gte {
namespace {

class FakeWindow final : public EditorWindow {
public:
    explicit FakeWindow(const char* name) : EditorWindow(name, WindowDock::Bottom) {}

protected:
    void OnGui(EditorFrame& /*frame*/) override {}
};

bool Contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

TEST(EditorPanelRegistryTest, RegisteredWindowIsKnownListedAndFindable)
{
    FakeWindow window("Test_Window_Alpha");
    EditorPanelRegistry& registry = EditorPanelRegistry::Instance();
    registry.RegisterWindow(window.Name(), &window);

    EXPECT_TRUE(registry.IsKnownName("Test_Window_Alpha"));
    EXPECT_TRUE(Contains(registry.AllNames(), "Test_Window_Alpha"));
    EXPECT_EQ(registry.FindWindow("Test_Window_Alpha"), &window);

    registry.UnregisterWindow("Test_Window_Alpha");
}

TEST(EditorPanelRegistryTest, WindowsKeepRegistrationOrder)
{
    FakeWindow first("Test_Window_Order_First");
    FakeWindow second("Test_Window_Order_Second");
    EditorPanelRegistry& registry = EditorPanelRegistry::Instance();
    registry.RegisterWindow(first.Name(), &first);
    registry.RegisterWindow(second.Name(), &second);

    const std::vector<std::string> names = registry.AllNames();
    const auto firstAt = std::find(names.begin(), names.end(), "Test_Window_Order_First");
    const auto secondAt = std::find(names.begin(), names.end(), "Test_Window_Order_Second");
    ASSERT_NE(firstAt, names.end());
    ASSERT_NE(secondAt, names.end());
    EXPECT_LT(firstAt, secondAt);

    registry.UnregisterWindow("Test_Window_Order_First");
    registry.UnregisterWindow("Test_Window_Order_Second");
}

TEST(EditorPanelRegistryTest, DuplicateNameIsRefusedAndTheFirstWindowWins)
{
    FakeWindow first("Test_Window_Duplicate");
    FakeWindow second("Test_Window_Duplicate");
    EditorPanelRegistry& registry = EditorPanelRegistry::Instance();
    registry.RegisterWindow(first.Name(), &first);
    registry.RegisterWindow(second.Name(), &second);

    EXPECT_EQ(registry.FindWindow("Test_Window_Duplicate"), &first);
    const std::vector<std::string> names = registry.AllNames();
    EXPECT_EQ(std::count(names.begin(), names.end(), "Test_Window_Duplicate"), 1);

    registry.UnregisterWindow("Test_Window_Duplicate");
}

TEST(EditorPanelRegistryTest, NullWindowAndEmptyNameAreRefused)
{
    EditorPanelRegistry& registry = EditorPanelRegistry::Instance();
    FakeWindow window("Test_Window_Unnamed");
    registry.RegisterWindow("Test_Window_Null", nullptr);
    registry.RegisterWindow("", &window);

    EXPECT_FALSE(registry.IsKnownName("Test_Window_Null"));
    EXPECT_FALSE(registry.IsKnownName(""));
}

TEST(EditorPanelRegistryTest, UnregisterRemovesTheNameAndAllowsRegisteringItAgain)
{
    FakeWindow first("Test_Window_Reload");
    EditorPanelRegistry& registry = EditorPanelRegistry::Instance();
    registry.RegisterWindow(first.Name(), &first);
    registry.UnregisterWindow("Test_Window_Reload");
    EXPECT_FALSE(registry.IsKnownName("Test_Window_Reload"));

    FakeWindow second("Test_Window_Reload");
    registry.RegisterWindow(second.Name(), &second);
    EXPECT_EQ(registry.FindWindow("Test_Window_Reload"), &second);

    registry.UnregisterWindow("Test_Window_Reload");
}

TEST(EditorPanelRegistryTest, UnregisterOfAnUnknownNameIsSafe)
{
    EXPECT_NO_FATAL_FAILURE(EditorPanelRegistry::Instance().UnregisterWindow("Test_Window_Never_Registered"));
    EXPECT_FALSE(EditorPanelRegistry::Instance().IsKnownName("Test_Window_Never_Registered"));
}

TEST(EditorPanelRegistryTest, NamesAreCaseSensitive)
{
    FakeWindow window("Test_Window_Case");
    EditorPanelRegistry& registry = EditorPanelRegistry::Instance();
    registry.RegisterWindow(window.Name(), &window);

    EXPECT_TRUE(registry.IsKnownName("Test_Window_Case"));
    EXPECT_FALSE(registry.IsKnownName("test_window_case"));

    registry.UnregisterWindow("Test_Window_Case");
}

TEST(EditorPanelRegistryTest, UnregisterEditorWindowOnlyRemovesTheRegisteredObject)
{
    FakeWindow owner("Test_Window_Owner");
    FakeWindow impostor("Test_Window_Owner");
    RegisterEditorWindow(owner);
    RegisterEditorWindow(impostor);

    UnregisterEditorWindow(impostor);
    EXPECT_EQ(EditorPanelRegistry::Instance().FindWindow("Test_Window_Owner"), &owner);

    UnregisterEditorWindow(owner);
    EXPECT_FALSE(EditorPanelRegistry::Instance().IsKnownName("Test_Window_Owner"));
}

// Other test files in this binary need the built-in window names to be known, as they are in the editor.
class SeedWindowNamesEnvironment : public ::testing::Environment {
public:
    void SetUp() override
    {
        for (const char* name :
            { "Hierarchy", "Inspector", "Scene", "Game", "Memory", "Profiler", "Render Graph", "Jobs", "Log" }) {
            m_windows.push_back(std::make_unique<FakeWindow>(name));
            RegisterEditorWindow(*m_windows.back());
        }
    }

    void TearDown() override
    {
        for (const std::unique_ptr<FakeWindow>& window : m_windows) {
            UnregisterEditorWindow(*window);
        }
    }

private:
    std::vector<std::unique_ptr<FakeWindow>> m_windows;
};

::testing::Environment* const g_seedWindowNamesEnvironment =
    ::testing::AddGlobalTestEnvironment(new SeedWindowNamesEnvironment());

} // namespace
} // namespace gte
