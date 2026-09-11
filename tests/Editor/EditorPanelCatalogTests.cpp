#include "Editor/EditorPanelCatalog.h"

#include <gtest/gtest.h>

// NOTE: unlike every OTHER tests/Editor/*.cpp file, this test is registered
// in tests/CMakeLists.txt's MAIN, unconditional GTE_TEST_SOURCES list, NOT
// inside the if(GTE_ENABLE_EDITOR) block - EditorPanelCatalog.h compiles and
// must be tested in every build configuration, since it is consumed by
// src/Network/NetworkRoutes.cpp, which always compiles regardless of
// GTE_ENABLE_EDITOR (see network-impl-7 campaign, PHASE1).

namespace gte {
namespace {

TEST(EditorPanelCatalogTest, KnownNamesAreRecognized)
{
    EXPECT_TRUE(IsKnownEditorPanelName("Hierarchy"));
    EXPECT_TRUE(IsKnownEditorPanelName("Inspector"));
    EXPECT_TRUE(IsKnownEditorPanelName("Scene"));
    EXPECT_TRUE(IsKnownEditorPanelName("Game"));
    EXPECT_TRUE(IsKnownEditorPanelName("Memory"));
    EXPECT_TRUE(IsKnownEditorPanelName("Profiler"));
    EXPECT_TRUE(IsKnownEditorPanelName("Render Graph"));
    EXPECT_TRUE(IsKnownEditorPanelName("Jobs"));
    EXPECT_TRUE(IsKnownEditorPanelName("Atmosphere"));
}

TEST(EditorPanelCatalogTest, IsCaseSensitive)
{
    EXPECT_TRUE(IsKnownEditorPanelName("Profiler"));
    EXPECT_FALSE(IsKnownEditorPanelName("profiler"));
    EXPECT_FALSE(IsKnownEditorPanelName("PROFILER"));
}

TEST(EditorPanelCatalogTest, RejectsUnknownNames)
{
    EXPECT_FALSE(IsKnownEditorPanelName("NotARealTab"));
    EXPECT_FALSE(IsKnownEditorPanelName(""));
    EXPECT_FALSE(IsKnownEditorPanelName("Console"));
}

// Every literal in kKnownEditorPanelNames must round-trip through
// IsKnownEditorPanelName() as true - a loop-driven test so adding a new
// panel name later automatically gets covered with no test-file edit needed.
TEST(EditorPanelCatalogTest, EveryCatalogEntryRoundTripsAsKnown)
{
    for (std::size_t i = 0; i < kKnownEditorPanelNameCount; ++i) {
        EXPECT_TRUE(IsKnownEditorPanelName(kKnownEditorPanelNames[i]))
            << "Catalog entry '" << kKnownEditorPanelNames[i] << "' did not round-trip";
    }
}

} // namespace
} // namespace gte
