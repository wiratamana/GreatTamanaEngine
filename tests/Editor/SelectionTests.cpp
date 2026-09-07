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

TEST(SelectionTest, DefaultsToNoneWithNoModelPartSelectedEither)
{
    const Selection selection;

    EXPECT_EQ(selection.SelectedModelPartEntity(), kInvalidEntity);
    EXPECT_EQ(selection.SelectedModelPartKind(), ModelPartKind::Bone);
    EXPECT_EQ(selection.SelectedModelPartIndex(), -1);
    EXPECT_FALSE(selection.IsModelPartSelected(kInvalidEntity, ModelPartKind::Bone, -1));
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

TEST(SelectionTest, ClearResetsModelPartFieldsToo)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7);

    selection.Clear();

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::None);
    EXPECT_EQ(selection.SelectedModelPartEntity(), kInvalidEntity);
    EXPECT_EQ(selection.SelectedModelPartKind(), ModelPartKind::Bone);
    EXPECT_EQ(selection.SelectedModelPartIndex(), -1);
}

TEST(SelectionTest, SelectModelPartMakesItTheCurrentModelPartSelectionAndInspectorSource)
{
    Selection selection;

    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7);

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::ModelPart);
    EXPECT_EQ(selection.SelectedModelPartEntity(), (Entity{ 4, 1 }));
    EXPECT_EQ(selection.SelectedModelPartKind(), ModelPartKind::RigidBody);
    EXPECT_EQ(selection.SelectedModelPartIndex(), 7);
    EXPECT_TRUE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7));
}

TEST(SelectionTest, IsModelPartSelectedRequiresAllThreeFieldsToMatchExactly)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7);

    // Different entity, same kind/index.
    EXPECT_FALSE(selection.IsModelPartSelected(Entity{ 5, 1 }, ModelPartKind::RigidBody, 7));
    // Different ModelPartKind, same entity/index.
    EXPECT_FALSE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::Bone, 7));
    // Different index, same entity/kind.
    EXPECT_FALSE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 8));
}

TEST(SelectionTest, SelectModelPartLeavesEntityAndAssetFieldsIntactButUnhighlightsThemImmediately)
{
    Selection selection;
    const Entity entity{ 2, 1 };
    selection.SelectEntity(entity);
    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7);

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::ModelPart);
    EXPECT_EQ(selection.SelectedEntity(), entity);
    EXPECT_EQ(selection.SelectedAssetRelativePath(), "rock.png");
    EXPECT_FALSE(selection.IsEntitySelected(entity));
    EXPECT_FALSE(selection.IsAssetSelected("rock.png"));
}

TEST(SelectionTest, SelectEntityAfterModelPartUnhighlightsItImmediately)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7);

    selection.SelectEntity(Entity{ 9, 1 });

    // The stale Model-Part selection is still "remembered" (accessors still
    // return it, never auto-cleared - matches SelectEntity()/SelectAsset()'s
    // own existing "leave it, just gate visibility" convention), but
    // IsModelPartSelected() is gated on Kind() == ModelPart - no longer
    // highlighted once an entity is on top.
    EXPECT_EQ(selection.SelectedModelPartEntity(), (Entity{ 4, 1 }));
    EXPECT_EQ(selection.SelectedModelPartKind(), ModelPartKind::RigidBody);
    EXPECT_EQ(selection.SelectedModelPartIndex(), 7);
    EXPECT_FALSE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7));
}

TEST(SelectionTest, SelectAssetAfterModelPartUnhighlightsItImmediately)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7);

    selection.SelectAsset("C:/Project/rock.png", "rock.png", /*isDirectory=*/false);

    EXPECT_EQ(selection.SelectedModelPartEntity(), (Entity{ 4, 1 }));
    EXPECT_EQ(selection.SelectedModelPartKind(), ModelPartKind::RigidBody);
    EXPECT_EQ(selection.SelectedModelPartIndex(), 7);
    EXPECT_FALSE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7));
}

TEST(SelectionTest, ClearModelPartIfEntityIsNoOpWhenEntityDoesNotMatch)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7);

    selection.ClearModelPartIfEntity(Entity{ 9, 1 });

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::ModelPart);
    EXPECT_EQ(selection.SelectedModelPartEntity(), (Entity{ 4, 1 }));
    EXPECT_EQ(selection.SelectedModelPartKind(), ModelPartKind::RigidBody);
    EXPECT_EQ(selection.SelectedModelPartIndex(), 7);
}

TEST(SelectionTest, ClearModelPartIfEntityClearsFieldsAndRevertsKindToNoneWhenModelPartIsOnTop)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7);

    selection.ClearModelPartIfEntity(Entity{ 4, 1 });

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::None);
    EXPECT_EQ(selection.SelectedModelPartEntity(), kInvalidEntity);
    EXPECT_EQ(selection.SelectedModelPartKind(), ModelPartKind::Bone);
    EXPECT_EQ(selection.SelectedModelPartIndex(), -1);
}

TEST(SelectionTest, ClearModelPartIfEntityClearsFieldsButKeepsEntityKindWhenEntityIsOnTop)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7);
    selection.SelectEntity(Entity{ 2, 1 }); // Inspector now shows the entity, not the model part.

    selection.ClearModelPartIfEntity(Entity{ 4, 1 });

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::Entity);
    EXPECT_EQ(selection.SelectedEntity(), (Entity{ 2, 1 }));
    EXPECT_EQ(selection.SelectedModelPartEntity(), kInvalidEntity);
    EXPECT_EQ(selection.SelectedModelPartKind(), ModelPartKind::Bone);
    EXPECT_EQ(selection.SelectedModelPartIndex(), -1);
    EXPECT_FALSE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7));
}

TEST(SelectionTest, SelectModelPartsReplacesEntireSelectionWithGivenSet)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 2);

    selection.SelectModelParts(Entity{ 4, 1 }, ModelPartKind::RigidBody, { 5, 1, 3 });

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::ModelPart);
    // Stored ascending-sorted regardless of insertion order.
    EXPECT_EQ(selection.SelectedModelPartIndices(), (std::vector<int>{ 1, 3, 5 }));
    EXPECT_EQ(selection.SelectedModelPartIndex(), 1); // Lowest element.
    EXPECT_TRUE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 1));
    EXPECT_TRUE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 3));
    EXPECT_TRUE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 5));
    EXPECT_FALSE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 2)); // Replaced, not merged.
}

TEST(SelectionTest, SelectModelPartsDeduplicatesInput)
{
    Selection selection;

    selection.SelectModelParts(Entity{ 1, 1 }, ModelPartKind::RigidBody, { 3, 3, 1, 1, 2 });

    EXPECT_EQ(selection.SelectedModelPartIndices(), (std::vector<int>{ 1, 2, 3 }));
}

TEST(SelectionTest, SelectModelPartsWithEmptySetClearsTheSelectionEntirely)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 7);

    selection.SelectModelParts(Entity{ 4, 1 }, ModelPartKind::RigidBody, {});

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::None);
    EXPECT_EQ(selection.SelectedModelPartEntity(), kInvalidEntity);
    EXPECT_EQ(selection.SelectedModelPartKind(), ModelPartKind::Bone);
    EXPECT_TRUE(selection.SelectedModelPartIndices().empty());
    EXPECT_EQ(selection.SelectedModelPartIndex(), -1);
}

TEST(SelectionTest, ToggleModelPartInSelectionAddsANewIndexToAnExistingCompatibleSelection)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 2);

    selection.ToggleModelPartInSelection(Entity{ 4, 1 }, ModelPartKind::RigidBody, 5);

    EXPECT_EQ(selection.SelectedModelPartIndices(), (std::vector<int>{ 2, 5 }));
    EXPECT_TRUE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 2));
    EXPECT_TRUE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 5));
}

TEST(SelectionTest, ToggleModelPartInSelectionRemovesAnAlreadySelectedIndex)
{
    Selection selection;
    selection.SelectModelParts(Entity{ 4, 1 }, ModelPartKind::RigidBody, { 2, 5 });

    selection.ToggleModelPartInSelection(Entity{ 4, 1 }, ModelPartKind::RigidBody, 2);

    EXPECT_EQ(selection.SelectedModelPartIndices(), (std::vector<int>{ 5 }));
    EXPECT_FALSE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 2));
    EXPECT_TRUE(selection.IsModelPartSelected(Entity{ 4, 1 }, ModelPartKind::RigidBody, 5));
}

TEST(SelectionTest, ToggleModelPartInSelectionRemovingTheLastIndexClearsTheSelectionEntirely)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 2);

    selection.ToggleModelPartInSelection(Entity{ 4, 1 }, ModelPartKind::RigidBody, 2);

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::None);
    EXPECT_TRUE(selection.SelectedModelPartIndices().empty());
}

TEST(SelectionTest, ToggleModelPartInSelectionStartsAFreshSelectionWhenTheCurrentOneIsIncompatible)
{
    Selection selection;
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 2);

    // Different owningEntity - not an extension of the existing selection.
    selection.ToggleModelPartInSelection(Entity{ 9, 1 }, ModelPartKind::RigidBody, 5);
    EXPECT_EQ(selection.SelectedModelPartEntity(), (Entity{ 9, 1 }));
    EXPECT_EQ(selection.SelectedModelPartIndices(), (std::vector<int>{ 5 }));

    // Different partKind on the SAME entity - also not an extension.
    selection.SelectModelPart(Entity{ 4, 1 }, ModelPartKind::RigidBody, 2);
    selection.ToggleModelPartInSelection(Entity{ 4, 1 }, ModelPartKind::Bone, 0);
    EXPECT_EQ(selection.SelectedModelPartKind(), ModelPartKind::Bone);
    EXPECT_EQ(selection.SelectedModelPartIndices(), (std::vector<int>{ 0 }));
}

// v2 addition (task_manager/verlet-integration-4/PHASE0_MASTER_STRATEGY.md's
// Revision Notes, finding #4) - the existing test directly above only ever
// starts from a PRE-EXISTING but INCOMPATIBLE ModelPart selection (a
// different owningEntity/partKind). Neither "nothing selected at all"
// (Kind() == None) nor "an Entity/Asset selection is currently on top" was
// ever independently exercised, even though both take the exact same
// `m_kind != InspectorSelectionKind::ModelPart` branch in the real
// implementation.
TEST(SelectionTest, ToggleModelPartInSelectionStartsAFreshSelectionWhenNothingOrAnUnrelatedKindWasSelected)
{
    Selection freshSelection;
    ASSERT_EQ(freshSelection.Kind(), InspectorSelectionKind::None);

    freshSelection.ToggleModelPartInSelection(Entity{ 3, 1 }, ModelPartKind::RigidBody, 4);

    EXPECT_EQ(freshSelection.Kind(), InspectorSelectionKind::ModelPart);
    EXPECT_EQ(freshSelection.SelectedModelPartIndices(), (std::vector<int>{ 4 }));

    Selection entitySelection;
    entitySelection.SelectEntity(Entity{ 7, 1 });
    ASSERT_EQ(entitySelection.Kind(), InspectorSelectionKind::Entity);

    entitySelection.ToggleModelPartInSelection(Entity{ 3, 1 }, ModelPartKind::RigidBody, 4);

    EXPECT_EQ(entitySelection.Kind(), InspectorSelectionKind::ModelPart);
    EXPECT_EQ(entitySelection.SelectedModelPartIndices(), (std::vector<int>{ 4 }));
    // The pre-existing Entity selection field is left untouched (same
    // "leave the other fields, just gate visibility on Kind()" contract
    // every other Selection mutator already has).
    EXPECT_EQ(entitySelection.SelectedEntity(), (Entity{ 7, 1 }));
}

TEST(SelectionTest, ClearModelPartIfEntityClearsAMultiElementSelectionEntirely)
{
    Selection selection;
    selection.SelectModelParts(Entity{ 4, 1 }, ModelPartKind::RigidBody, { 1, 2, 3 });

    selection.ClearModelPartIfEntity(Entity{ 4, 1 });

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::None);
    EXPECT_TRUE(selection.SelectedModelPartIndices().empty());
}

TEST(SelectionTest, ClearResetsAMultiElementModelPartSelectionToo)
{
    Selection selection;
    selection.SelectModelParts(Entity{ 4, 1 }, ModelPartKind::RigidBody, { 1, 2, 3 });

    selection.Clear();

    EXPECT_EQ(selection.Kind(), InspectorSelectionKind::None);
    EXPECT_TRUE(selection.SelectedModelPartIndices().empty());
    EXPECT_EQ(selection.SelectedModelPartIndex(), -1);
}

} // namespace
} // namespace gte
