// Unit tests for Selection (src/Editor/Selection.h) - the single
// gate-keeper for every Hierarchy-entity / Project-asset selection in the
// Editor (see AGENTS.md, "Editor Module Structure" and Selection.h's own
// class comment). Deliberately pure logic with no ImGui/SDL/Vulkan
// dependency at all, so it is Tier-1-testable exactly like the rest of the
// engine's math/ECS coverage (see AGENTS.md, "Testability & Regression
// Safety"). Only actually compiled/linked when GTE_ENABLE_EDITOR is ON,
// since Selection itself is only ever compiled into gte_core then (see the
// root CMakeLists.txt's "Editor Module Structure" - the whole src/Editor/
// folder, not just ImGui-touching files, is gated on it) - see
// tests/CMakeLists.txt.

#include "Editor/Selection.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(SelectionTest, DefaultsToNoneWithNoEntityOrAssetSelected)
{
    const Selection selection;

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::None);
    EXPECT_EQ(selection.SelectedEntity(), kInvalidEntity);
    EXPECT_TRUE(selection.SelectedAssetAbsolutePath().empty());
    EXPECT_TRUE(selection.SelectedAssetRelativePath().empty());
    EXPECT_FALSE(selection.SelectedAssetIsDirectory());
    EXPECT_FALSE(selection.HasAssetSelection());
}

TEST(SelectionTest, SelectEntityMakesItTheCurrentEntitySelectionAndInspectorSource)
{
    Selection selection;
    const Entity entity{ /*index=*/3, /*generation=*/1 };

    selection.SelectEntity(entity);

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::Entity);
    EXPECT_EQ(selection.SelectedEntity(), entity);
    EXPECT_TRUE(selection.IsEntitySelected(entity));
}

TEST(SelectionTest, SelectEntityLeavesAssetFieldsIntactButUnhighlightsThemImmediately)
{
    Selection selection;
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    selection.SelectEntity(Entity{ 7, 1 });

    // Kind() flips to Entity (Inspector now shows the entity), and the
    // underlying Project selection fields are never cleared by picking an
    // entity (SelectedAssetRelativePath()/AbsolutePath() still return what
    // was last picked in Project) - but IsAssetSelected()/
    // HasAssetSelection() must immediately report nothing selected, since
    // both are gated on Kind(). This is the actual bug-fix behavior: no two
    // panels may ever show a highlight at the same time (see Selection.h's
    // class comment) - ProjectPanel must never keep its own separate
    // "am I still highlighted" state to defeat this.
    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::Entity);
    EXPECT_EQ(selection.SelectedAssetRelativePath(), "rock.png");
    EXPECT_EQ(selection.SelectedAssetAbsolutePath(), "C:/Project/rock.png");
    EXPECT_FALSE(selection.IsAssetSelected("rock.png"));
    EXPECT_FALSE(selection.HasAssetSelection());
}

TEST(SelectionTest, SelectAssetMakesItTheCurrentAssetSelectionAndInspectorSource)
{
    Selection selection;

    selection.SelectAsset("C:/Project/Textures", "Textures", /*isDirectory=*/true);

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::Asset);
    EXPECT_EQ(selection.SelectedAssetAbsolutePath(), "C:/Project/Textures");
    EXPECT_EQ(selection.SelectedAssetRelativePath(), "Textures");
    EXPECT_TRUE(selection.SelectedAssetIsDirectory());
    EXPECT_TRUE(selection.IsAssetSelected("Textures"));
    EXPECT_TRUE(selection.HasAssetSelection());
}

TEST(SelectionTest, SelectAssetLeavesEntityFieldIntactButUnhighlightsItImmediately)
{
    Selection selection;
    const Entity entity{ 2, 1 };
    selection.SelectEntity(entity);

    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    // Kind() flips to Asset, and SelectedEntity() still returns the entity
    // (the underlying field is never cleared by picking a Project asset),
    // but IsEntitySelected() must immediately report it not selected, since
    // it's gated on Kind() - HierarchyPanel must never keep its own
    // separate "am I still highlighted" state to defeat this.
    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::Asset);
    EXPECT_EQ(selection.SelectedEntity(), entity);
    EXPECT_FALSE(selection.IsEntitySelected(entity));
}

TEST(SelectionTest, IsEntitySelectedIsFalseWhenAnAssetIsCurrentlyOnTop)
{
    Selection selection;
    const Entity entity{ 5, 1 };
    selection.SelectEntity(entity);
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    // The entity is still "remembered" (SelectedEntity() still returns it),
    // but IsEntitySelected() is gated on Kind() == Entity - Hierarchy no
    // longer highlights it once an asset is on top.
    EXPECT_EQ(selection.SelectedEntity(), entity);
    EXPECT_FALSE(selection.IsEntitySelected(entity));
}

TEST(SelectionTest, IsAssetSelectedIsFalseWhenAnEntityIsCurrentlyOnTop)
{
    Selection selection;
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);
    selection.SelectEntity(Entity{ 9, 1 });

    // The regression this test guards against: Project's own row highlight
    // must disappear the instant an entity becomes the active selection -
    // IsAssetSelected() is gated on Kind() == Asset for exactly this reason.
    EXPECT_FALSE(selection.IsAssetSelected("rock.png"));
    EXPECT_FALSE(selection.HasAssetSelection());
}

TEST(SelectionTest, HasAssetSelectionIsFalseForTheProjectRootItself)
{
    Selection selection;

    // An empty relativePath means the Project root itself (see SelectAsset()'s
    // doc comment) - selected/highlighted like any other row, but never a
    // valid "Delete Selected" target.
    selection.SelectAsset("C:/Project", "", /*isDirectory=*/true);

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::Asset);
    EXPECT_TRUE(selection.IsAssetSelected(""));
    EXPECT_FALSE(selection.HasAssetSelection());
}

TEST(SelectionTest, ClearAssetIfPathIsNoOpWhenPathDoesNotMatch)
{
    Selection selection;
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    selection.ClearAssetIfPath("other.png");

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::Asset);
    EXPECT_EQ(selection.SelectedAssetRelativePath(), "rock.png");
}

TEST(SelectionTest, ClearAssetIfPathClearsFieldsAndRevertsKindToNoneWhenAssetIsOnTop)
{
    Selection selection;
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    selection.ClearAssetIfPath("rock.png");

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::None);
    EXPECT_TRUE(selection.SelectedAssetAbsolutePath().empty());
    EXPECT_TRUE(selection.SelectedAssetRelativePath().empty());
    EXPECT_FALSE(selection.SelectedAssetIsDirectory());
}

TEST(SelectionTest, ClearAssetIfPathClearsFieldsButKeepsEntityKindWhenEntityIsOnTop)
{
    Selection selection;
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);
    const Entity entity{ 4, 1 };
    selection.SelectEntity(entity); // Inspector now shows the entity, not the asset.

    selection.ClearAssetIfPath("rock.png");

    // The asset fields are still cleared (nothing left pointing at a
    // deleted item), but Kind() stays Entity - clearing a stale Project
    // selection must never yank the Inspector away from an entity it's
    // currently showing.
    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::Entity);
    EXPECT_EQ(selection.SelectedEntity(), entity);
    EXPECT_TRUE(selection.SelectedAssetRelativePath().empty());
    EXPECT_FALSE(selection.IsAssetSelected("rock.png"));
}

TEST(SelectionTest, ClearResetsEverythingToDefaults)
{
    Selection selection;
    selection.SelectEntity(Entity{ 1, 1 });
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    selection.Clear();

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::None);
    EXPECT_EQ(selection.SelectedEntity(), kInvalidEntity);
    EXPECT_TRUE(selection.SelectedAssetAbsolutePath().empty());
    EXPECT_TRUE(selection.SelectedAssetRelativePath().empty());
    EXPECT_FALSE(selection.SelectedAssetIsDirectory());
}

} // namespace
} // namespace gte
