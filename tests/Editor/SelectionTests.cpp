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

TEST(SelectionTest, SelectEntityLeavesAnyExistingAssetSelectionUntouched)
{
    Selection selection;
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    selection.SelectEntity(Entity{ 7, 1 });

    // Kind() flips to Entity (Inspector now shows the entity), but the
    // Project selection itself is never cleared by picking an entity - see
    // Selection.h's class comment (mirrors Unity's own behavior).
    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::Entity);
    EXPECT_EQ(selection.SelectedAssetRelativePath(), "rock.png");
    EXPECT_EQ(selection.SelectedAssetAbsolutePath(), "C:/Project/rock.png");
    EXPECT_TRUE(selection.IsAssetSelected("rock.png"));
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
}

TEST(SelectionTest, SelectAssetLeavesAnyExistingEntitySelectionUntouched)
{
    Selection selection;
    const Entity entity{ 2, 1 };
    selection.SelectEntity(entity);

    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    // Kind() flips to Asset, but the entity selection itself is never
    // cleared by picking a Project asset - SelectedEntity() still returns
    // it (IsEntitySelected() is separately gated on Kind(), see the next
    // test below).
    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::Asset);
    EXPECT_EQ(selection.SelectedEntity(), entity);
}

TEST(SelectionTest, IsEntitySelectedIsFalseWhenAnAssetIsCurrentlyOnTop)
{
    Selection selection;
    const Entity entity{ 5, 1 };
    selection.SelectEntity(entity);
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    // The entity is still "remembered" (SelectedEntity() still returns it),
    // but IsEntitySelected() is gated on Kind() == Entity - Hierarchy no
    // longer highlights it once an asset is on top, matching Unity.
    EXPECT_EQ(selection.SelectedEntity(), entity);
    EXPECT_FALSE(selection.IsEntitySelected(entity));
}

TEST(SelectionTest, IsAssetSelectedStaysTrueRegardlessOfWhichKindIsCurrentlyOnTop)
{
    Selection selection;
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);
    selection.SelectEntity(Entity{ 9, 1 });

    // Project's own row highlight is deliberately NOT gated on Kind() - see
    // Selection.h's IsAssetSelected() doc comment.
    EXPECT_TRUE(selection.IsAssetSelected("rock.png"));
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
