// Tests for the pure state of EditorWindow. Draw() needs a live ImGui context and is checked live over HTTP.

#include "Editor/EditorWindow.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

class ProbeWindow final : public EditorWindow {
public:
    ProbeWindow(const char* name, WindowDock dock, bool open) : EditorWindow(name, dock, open) {}

protected:
    void OnGui(EditorFrame& /*frame*/) override {}
};

TEST(EditorWindowTest, OpenStateFollowsTheConstructorArgument)
{
    const ProbeWindow open("Open", WindowDock::Bottom, true);
    const ProbeWindow closed("Closed", WindowDock::Floating, false);
    EXPECT_TRUE(open.IsOpen());
    EXPECT_FALSE(closed.IsOpen());
}

TEST(EditorWindowTest, ShowOpensAndCloseCloses)
{
    ProbeWindow window("Probe", WindowDock::Floating, false);
    window.Show();
    EXPECT_TRUE(window.IsOpen());
    window.Close();
    EXPECT_FALSE(window.IsOpen());
}

TEST(EditorWindowTest, AClosedWindowIsNeverVisible)
{
    ProbeWindow window("Probe", WindowDock::Center, true);
    EXPECT_TRUE(window.IsVisible());
    window.Close();
    EXPECT_FALSE(window.IsVisible());
}

TEST(EditorWindowTest, NameAndDefaultDockAreKept)
{
    const ProbeWindow window("Render Graph", WindowDock::Floating, true);
    EXPECT_STREQ(window.Name(), "Render Graph");
    EXPECT_EQ(window.DefaultDock(), WindowDock::Floating);
}

TEST(WindowNameLessTest, IgnoresCaseAndOrdersByLetter)
{
    EXPECT_TRUE(WindowNameLess("atmosphere", "Bone Viewer"));
    EXPECT_FALSE(WindowNameLess("Bone Viewer", "atmosphere"));
    EXPECT_FALSE(WindowNameLess("Game", "game"));
    EXPECT_FALSE(WindowNameLess("game", "Game"));
}

TEST(WindowNameLessTest, AShorterPrefixComesFirst)
{
    EXPECT_TRUE(WindowNameLess("Scene", "SceneLighting"));
    EXPECT_FALSE(WindowNameLess("SceneLighting", "Scene"));
}

} // namespace
} // namespace gte
