# PHASE1 — Selection: Multi-Object Model-Part Selection (`src/Editor/Selection.h/.cpp`) (v2)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit A — the actual blocking root
cause). Depends on: nothing (this is the campaign's first phase). Produces:
`Selection` can hold and report MANY currently-selected Model-Part indices at
once, with every pre-existing single-selection call site in the codebase
(`BoneViewerWindow.cpp`, `InspectorPanel.cpp`, `tests/Editor/SelectionTests.cpp`)
continuing to compile and behave identically to today.

**v2 revision note**: unchanged from v1 except one added test case closing a
Tier-1 coverage gap found during the v2 self-audit (Step 3.8's new
`ToggleModelPartInSelectionStartsAFreshSelectionWhenNothingOrAnUnrelatedKindWasSelected`
test) and a small wording correction to `ToggleModelPartInSelection()`'s own
doc comment (it is used for Ctrl-click's "extend by one item" semantics only
— Shift-click's own genuine contiguous range select, added in Phase 3's v2
revision, is built entirely on top of `SelectModelParts()` instead, never on
this method — see `PHASE0_MASTER_STRATEGY.md`'s Revision Notes, finding #2).
Every field name/signature/line citation below was re-verified against the
live source tree and is still byte-for-byte accurate.

## Step 1: The Goal

1. `Selection` stores a de-duplicated, ascending-sorted SET of currently
   selected Model-Part indices (`std::vector<int>`) instead of a single `int`.
2. A new `SelectModelParts(Entity, ModelPartKind, std::vector<int>)` REPLACES
   the entire current Model-Part selection with the given set in one call —
   what a "Select All (Group)"/"Select All (Branch)" button (Phase 3) needs,
   and also what Phase 3's own new Shift-click contiguous range select needs
   (it computes the range as a plain `std::vector<int>` and hands the WHOLE
   thing to this method, exactly like the two toolbar buttons do — never
   through `ToggleModelPartInSelection()` below). Passing an empty set is
   treated as a genuine CLEAR (mirrors `ClearModelPartIfEntity()`'s own
   "revert `Kind()` to `None` if ModelPart was on top" behavior) — never a
   "ModelPart selected but with nothing in it" limbo state.
3. A new `ToggleModelPartInSelection(Entity, ModelPartKind, int)` ADDS the
   index to the current set if absent, or REMOVES it if already present —
   Ctrl-click's own "extend an existing selection by exactly one item"
   semantics (Windows Explorer/Unity style), needed by Phase 3's click-handler
   wiring for every view mode (Bone/Rigid Body/Joint alike). The Bone tree
   specifically (`BoneViewerWindow.cpp`'s `RenderBoneTreeNode()`) also routes
   Shift-click through this SAME method rather than a genuine range select —
   see `PHASE0_MASTER_STRATEGY.md`'s "What We Will NOT Do" for why a bone's
   raw array index has no meaningful linear "range" the way a flat Rigid
   Body/Joint row does.
4. A new `SelectedModelPartIndices()` returns the full current set
   (`const std::vector<int>&`), ascending-sorted, empty when nothing is
   selected — the new source of truth for "how many/which," and also what
   Phase 3's v2 `hasSeed` fix uses to correctly require "exactly one" rather
   than merely "the lowest one is in range" (see `PHASE0_MASTER_STRATEGY.md`'s
   Revision Notes, finding #1).
5. `SelectModelPart(Entity, ModelPartKind, int)` (the existing single-index
   method) becomes a one-line convenience wrapper: replace the whole set with
   exactly `{ partIndex }` — every existing call site
   (`BoneViewerWindow.cpp`'s plain-click paths) keeps compiling and behaving
   exactly as before, with zero call-site changes required in this phase.
6. `SelectedModelPartIndex()` (the existing single-index accessor) becomes
   "the lowest-numbered index in the current set, or -1 if empty" — every
   existing single-selection reader (`InspectorPanel.cpp`'s
   `BuildModelPartInspector()`, not touched until Phase 4) keeps compiling
   and, for the single-selection case every one of THOSE callers is still
   exercised through until Phase 3 lands, behaves byte-for-byte identically
   to today (a one-element set's only element IS its lowest element).
7. `IsModelPartSelected(Entity, ModelPartKind, int)` changes from an exact
   `==` comparison against one stored value to a membership test against the
   current set — the ONE change that makes every existing per-part
   highlight/reveal loop in `BoneViewerWindow.cpp` automatically start
   correctly multi-highlighting once Phase 3's buttons populate a multi-item
   set, with no changes needed to those loops themselves (see
   `PHASE0_MASTER_STRATEGY.md`'s Culprit list, point 7).
8. `ClearModelPartIfEntity()` and `Clear()` both clear the WHOLE set (not
   just one slot) — unchanged in spirit, updated for the new storage type.

## Step 2: The Situation / The Problem

`src/Editor/Selection.h` (today, lines 206-218) stores:

```cpp
private:
    InspectorSelectionKind m_kind = InspectorSelectionKind::None;

    Entity m_entity = kInvalidEntity;

    std::string m_assetAbsolutePath;
    std::string m_assetRelativePath;
    bool m_assetIsDirectory = false;

    Entity m_modelPartEntity = kInvalidEntity;
    ModelPartKind m_modelPartKind = ModelPartKind::Bone;
    int m_modelPartIndex = -1;
};
```

`src/Editor/Selection.cpp` implements `SelectModelPart()`/
`ClearModelPartIfEntity()`/`Clear()`/`IsModelPartSelected()` entirely in terms
of that one `m_modelPartIndex` int (see lines 33-54 and 83-87 — already read
in full during this campaign's investigation). `Selection.h` does not
currently `#include <vector>` at all (only `<string>`), and `Selection.cpp`
does not `#include <algorithm>` — both are needed for the new
sort/unique/find-based set operations below.

## Step 3: The Plan

### 3.1 `Selection.h` — new include

Add, alongside the existing `#include <string>` (today, line 004):

```cpp
#include <algorithm>
#include <string>
#include <vector>
```

(`<algorithm>` is needed here too since `SelectedModelPartIndices()` itself
stays a plain accessor, but the class's public API doc comments below
reference `std::find`-style membership checks that a future caller might
reasonably want to perform against the returned vector — harmless to include
defensively alongside `<vector>`; the mandatory new includes are `<vector>`
in this header and `<algorithm>` in the `.cpp`, listed again in 3.4 below.)

### 3.2 `Selection.h` — new/changed method declarations

Replace the existing `SelectModelPart()` declaration (today, lines 113-131)
and the three accessors right after it (today, lines 164-166) with:

```cpp
    // Makes "part `partIndex` of kind `partKind`, belonging to `owningEntity`'s
    // own model" the ENTIRE current Model-Part selection (replacing whatever
    // was selected before, exactly like clicking a single row/dot always
    // has) and the current Inspector source (Kind() becomes ModelPart) - a
    // thin single-element convenience wrapper over SelectModelParts() below
    // (`SelectModelParts(owningEntity, partKind, { partIndex })`), kept so
    // every pre-existing single-click call site
    // (BoneViewerWindow.cpp's RenderBoneTreeNode()/RenderFlatPartRow()/direct
    // viewport-dot click) keeps compiling and behaving exactly as before.
    // See SelectModelParts() below for the multi-object version (task_manager/
    // verlet-integration-4/PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md).
    void SelectModelPart(Entity owningEntity, ModelPartKind partKind, int partIndex);

    // Replaces the ENTIRE current Model-Part selection with the exact set of
    // `indices` (de-duplicated and ascending-sorted internally - callers may
    // pass them in any order, with duplicates) and makes it the current
    // Inspector source (Kind() becomes ModelPart) - the multi-object
    // equivalent of SelectModelPart() above. Used by BoneViewerWindow's
    // "Select All (Group)"/"Select All (Branch)" toolbar buttons AND its own
    // genuine Shift-click contiguous range select (see task_manager/
    // verlet-integration-4/PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md)
    // to make MANY rigid bodies the current selection at once. An EMPTY
    // `indices` is treated as a genuine CLEAR of the Model-Part selection -
    // exactly like ClearModelPartIfEntity() (Kind() reverts to None if
    // ModelPart was currently on top) - "select all matching" that matched
    // nothing must never leave a stale "ModelPart selected, holding zero
    // parts" limbo state that IsModelPartSelected() would then have to
    // special-case separately.
    void SelectModelParts(Entity owningEntity, ModelPartKind partKind, std::vector<int> indices);

    // Adds `index` to the CURRENT Model-Part selection set if it is not
    // already present, or REMOVES it if it is - Ctrl-click's own "extend the
    // existing selection by exactly one item" semantics (Windows Explorer/
    // Unity style), as opposed to SelectModelPart()/SelectModelParts() above,
    // both of which always REPLACE the whole set. Also used by the Bone
    // tree's own Shift-click (BoneViewerWindow.cpp's RenderBoneTreeNode()) -
    // deliberately identical to its own Ctrl-click there, since a bone's raw
    // array index has no meaningful linear "range" the way a flat Rigid
    // Body/Joint row does (a genuine Shift-click range select for those two
    // is built on top of SelectModelParts() above instead - see PHASE3's own
    // FlatListRangeSelection.h). If the CURRENT selection does not already
    // belong to this exact `owningEntity`/`partKind` pair (Kind() is not
    // ModelPart yet, or it's a ModelPart selection for a DIFFERENT
    // entity/kind), this call first behaves exactly like
    // `SelectModelPart(owningEntity, partKind, index)` - a fresh Ctrl-click
    // on an unrelated part/entity/kind always starts a brand new
    // single-element selection rather than silently mixing incompatible
    // selections together. If, after toggling, the resulting set is empty
    // (the user Ctrl-clicked the last remaining selected item), this reverts
    // Kind() to None exactly like SelectModelParts({}) would.
    void ToggleModelPartInSelection(Entity owningEntity, ModelPartKind partKind, int index);
```

Replace the three accessors (today, lines 164-166):

```cpp
    Entity SelectedModelPartEntity() const { return m_modelPartEntity; }
    ModelPartKind SelectedModelPartKind() const { return m_modelPartKind; }
    int SelectedModelPartIndex() const { return m_modelPartIndex; }
```

with:

```cpp
    Entity SelectedModelPartEntity() const { return m_modelPartEntity; }
    ModelPartKind SelectedModelPartKind() const { return m_modelPartKind; }

    // The lowest-numbered currently-selected Model-Part index, or -1 if
    // none is selected - a thin single-element convenience accessor over
    // SelectedModelPartIndices() below, kept so every pre-existing
    // single-selection reader (InspectorPanel.cpp's single-part property
    // sheet, before task_manager/verlet-integration-4/
    // PHASE4_INSPECTOR_MULTI_SELECTION_SUMMARY.md's own update) keeps
    // compiling and, for the single-selection case, returns byte-for-byte
    // the same value as before this phase (a one-element set's only
    // element IS its lowest element). NOTE: this is deliberately NOT "is
    // there exactly one selected" - a caller that needs to distinguish
    // "exactly one selected" from "several selected, this is merely the
    // lowest of them" (e.g. BoneViewerWindow.cpp's "Select All (...)"
    // button seed check - see PHASE3's own hasSeed fix, task_manager/
    // verlet-integration-4/PHASE0_MASTER_STRATEGY.md's Revision Notes,
    // finding #1) MUST also check SelectedModelPartIndices().size() == 1,
    // never rely on this accessor alone for that purpose.
    int SelectedModelPartIndex() const { return m_modelPartIndices.empty() ? -1 : m_modelPartIndices.front(); }

    // Every currently-selected Model-Part index, ascending-sorted, for
    // whichever (owningEntity, partKind) SelectedModelPartEntity()/
    // SelectedModelPartKind() currently report - empty if nothing is
    // selected. This is the SOURCE OF TRUTH for "how many/which" parts are
    // currently selected - SelectedModelPartIndex() above is only ever sugar
    // over this list's first (lowest) element.
    const std::vector<int>& SelectedModelPartIndices() const { return m_modelPartIndices; }
```

### 3.3 `Selection.h` — private storage

Replace (today, line 217):

```cpp
    int m_modelPartIndex = -1;
```

with:

```cpp
    std::vector<int> m_modelPartIndices;
```

(No other private field changes — `m_modelPartEntity`/`m_modelPartKind` stay
exactly as they are; only "the index" becomes "the indices.")

### 3.4 `Selection.cpp` — new include

Add, at the top (today, only `#include "Selection.h"` at line 000):

```cpp
#include "Selection.h"

#include <algorithm>
```

(Needed for `std::sort`/`std::unique`/`std::find` below.)

### 3.5 `Selection.cpp` — replace `SelectModelPart()`/add `SelectModelParts()`/`ToggleModelPartInSelection()`

Replace the existing `SelectModelPart()` definition (today, lines 33-39):

```cpp
void Selection::SelectModelPart(Entity owningEntity, ModelPartKind partKind, int partIndex)
{
    m_modelPartEntity = owningEntity;
    m_modelPartKind = partKind;
    m_modelPartIndex = partIndex;
    m_kind = InspectorSelectionKind::ModelPart;
}
```

with:

```cpp
void Selection::SelectModelPart(Entity owningEntity, ModelPartKind partKind, int partIndex)
{
    SelectModelParts(owningEntity, partKind, std::vector<int>{ partIndex });
}

void Selection::SelectModelParts(Entity owningEntity, ModelPartKind partKind, std::vector<int> indices)
{
    // Canonicalize the stored set (sorted + de-duplicated) - membership
    // checks in IsModelPartSelected() would be correct either way, but a
    // predictable, stable order is what lets any future/ Phase 4 UI that
    // ITERATES SelectedModelPartIndices() (e.g. an Inspector "N selected"
    // name list) show a consistent, non-jumbled order across frames.
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    m_modelPartEntity = owningEntity;
    m_modelPartKind = partKind;
    m_modelPartIndices = std::move(indices);

    if (m_modelPartIndices.empty()) {
        // Selecting an empty set is a genuine CLEAR, not a "ModelPart
        // selected with nothing in it" limbo state - mirrors
        // ClearModelPartIfEntity()'s own "Kind() reverts to None" behavior
        // exactly, and means IsModelPartSelected() never needs to special-
        // case an empty set separately from "Kind() isn't ModelPart at all."
        m_modelPartEntity = kInvalidEntity;
        m_modelPartKind = ModelPartKind::Bone;
        if (m_kind == InspectorSelectionKind::ModelPart) {
            m_kind = InspectorSelectionKind::None;
        }
        return;
    }

    m_kind = InspectorSelectionKind::ModelPart;
}

void Selection::ToggleModelPartInSelection(Entity owningEntity, ModelPartKind partKind, int index)
{
    if (m_kind != InspectorSelectionKind::ModelPart || m_modelPartEntity != owningEntity
        || m_modelPartKind != partKind) {
        // No existing COMPATIBLE selection to extend (nothing selected yet,
        // a different entity/model was selected, or a different partKind
        // e.g. Bone vs RigidBody) - a fresh Ctrl-click always starts a brand
        // new one-element selection instead of mixing incompatible
        // selections together, exactly like a plain click already does.
        SelectModelPart(owningEntity, partKind, index);
        return;
    }

    const auto it = std::find(m_modelPartIndices.begin(), m_modelPartIndices.end(), index);
    if (it != m_modelPartIndices.end()) {
        m_modelPartIndices.erase(it); // Already selected - Ctrl/Shift-clicking it again removes it.
    } else {
        m_modelPartIndices.push_back(index);
        std::sort(m_modelPartIndices.begin(), m_modelPartIndices.end());
    }

    if (m_modelPartIndices.empty()) {
        // The last remaining selected item was just toggled off - same
        // "empty set means a real clear" contract as SelectModelParts().
        m_modelPartEntity = kInvalidEntity;
        m_modelPartKind = ModelPartKind::Bone;
        m_kind = InspectorSelectionKind::None;
    }
}
```

### 3.6 `Selection.cpp` — `ClearModelPartIfEntity()`/`Clear()`/`IsModelPartSelected()`

Replace (today, lines 41-54):

```cpp
void Selection::ClearModelPartIfEntity(Entity owningEntity)
{
    if (m_modelPartEntity != owningEntity) {
        return;
    }

    m_modelPartEntity = kInvalidEntity;
    m_modelPartKind = ModelPartKind::Bone;
    m_modelPartIndex = -1;

    if (m_kind == InspectorSelectionKind::ModelPart) {
        m_kind = InspectorSelectionKind::None;
    }
}
```

with (only the middle three lines change):

```cpp
void Selection::ClearModelPartIfEntity(Entity owningEntity)
{
    if (m_modelPartEntity != owningEntity) {
        return;
    }

    m_modelPartEntity = kInvalidEntity;
    m_modelPartKind = ModelPartKind::Bone;
    m_modelPartIndices.clear();

    if (m_kind == InspectorSelectionKind::ModelPart) {
        m_kind = InspectorSelectionKind::None;
    }
}
```

In `Clear()` (today, lines 56-66), replace `m_modelPartIndex = -1;` with
`m_modelPartIndices.clear();` — no other line in that function changes.

Replace `IsModelPartSelected()` (today, lines 83-87):

```cpp
bool Selection::IsModelPartSelected(Entity owningEntity, ModelPartKind partKind, int partIndex) const
{
    return m_kind == InspectorSelectionKind::ModelPart && m_modelPartEntity == owningEntity
        && m_modelPartKind == partKind && m_modelPartIndex == partIndex;
}
```

with:

```cpp
bool Selection::IsModelPartSelected(Entity owningEntity, ModelPartKind partKind, int partIndex) const
{
    if (m_kind != InspectorSelectionKind::ModelPart || m_modelPartEntity != owningEntity
        || m_modelPartKind != partKind) {
        return false;
    }
    return std::find(m_modelPartIndices.begin(), m_modelPartIndices.end(), partIndex) != m_modelPartIndices.end();
}
```

### 3.7 `Selection.h` — class comment update

The class comment above `class Selection` (today, lines 62-73) ends with:
"This class was extended once already, from Entity/Asset to also cover
ModelPart ... any future selectable "thing" should extend it the same way
rather than adding a new ad hoc field elsewhere." Append one sentence noting
this second extension, so a future reader sees the precedent continued:

```
// ... any future selectable "thing" should extend it the same way rather
// than adding a new ad hoc field elsewhere. Extended a second time in
// task_manager/verlet-integration-4/PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md
// to let the ModelPart selection hold MANY indices at once (a
// std::vector<int> instead of one int) - SelectModelPart()/
// SelectedModelPartIndex() stayed as single-element convenience wrappers so
// every pre-existing single-selection call site kept compiling unchanged.
```

### 3.8 `tests/Editor/SelectionTests.cpp` — updates and additions

Two EXISTING tests construct a `Selection` and inspect `SelectedModelPartIndex()`
directly after `SelectModelPart(...)` — these keep passing unchanged (a
one-element set's front element is that element), so no edits are required to
`SelectModelPartMakesItTheCurrentModelPartSelectionAndInspectorSource`,
`IsModelPartSelectedRequiresAllThreeFieldsToMatchExactly`,
`SelectModelPartLeavesEntityAndAssetFieldsIntactButUnhighlightsThemImmediately`,
`SelectEntityAfterModelPartUnhighlightsItImmediately`,
`SelectAssetAfterModelPartUnhighlightsItImmediately`,
`ClearModelPartIfEntity*`, or `ClearResetsModelPartFieldsToo` — verify this by
running the suite before adding anything new (see Step 5).

Add these new `TEST(SelectionTest, ...)` cases to the same file (after the
existing Model-Part tests, before the closing `} // namespace`):

```cpp
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
```

(`#include <vector>` is already implicitly available in the test file via
`Selection.h`'s own new `<vector>` include from Step 3.1 above — no separate
test-file include change is needed, `std::vector<int>` is usable directly.)

## Step 4: What We Will NOT Do

- We will **not** touch `BoneViewerWindow.cpp` or `InspectorPanel.cpp` in this
  phase — every existing call site there uses `SelectModelPart()`/
  `IsModelPartSelected()`/`SelectedModelPartIndex()` with the exact same
  signatures as before, so nothing else needs to change yet. Phase 3 and
  Phase 4 pick up from here.
- We will **not** add any ordering/priority concept to the stored set (e.g.
  "which one was selected most recently") beyond "ascending sorted" — the
  Inspector's Phase 4 summary view iterates the whole set rather than relying
  on insertion order, so no such concept is needed. Phase 3's v2 Shift-click
  range select does not need this either - it computes its own range as a
  plain `std::vector<int>` externally (see `FlatListRangeSelection.h`) and
  hands the WHOLE thing to `SelectModelParts()` in one call.
- We will **not** implement a genuine "range select" primitive inside
  `Selection` itself - `SelectModelParts()` is already sufficiently general
  (any caller can compute any specific set of indices it wants and hand it
  over as one call); the actual range-BUILDING math (Phase 3's v2 addition)
  belongs in its own small, reusable, Tier-1-tested module instead
  (`src/Editor/FlatListRangeSelection.h`), not inside `Selection`, which
  stays a pure "what is currently selected" gate-keeper with no opinion on
  HOW a caller arrived at a particular set.
- We will **not** change `SelectEntity()`/`SelectAsset()`/`ClearAssetIfPath()`/
  `IsEntitySelected()`/`IsAssetSelected()`/`HasAssetSelection()` at all — none
  of them touch Model-Part state, and none are in scope per
  `PHASE0_MASTER_STRATEGY.md`'s "What We Will NOT Do".

## Step 5: Their Role

Implementer checklist for this phase:

1. Apply 3.1-3.7 to `Selection.h`/`Selection.cpp` exactly as written.
2. Add the new tests from 3.8 to `tests/Editor/SelectionTests.cpp`, including
   the v2-added `ToggleModelPartInSelectionStartsAFreshSelectionWhenNothingOrAnUnrelatedKindWasSelected` case.
3. Build `gte_core` + `GreatTamanaEngineTests` (both `GTE_ENABLE_EDITOR=ON`,
   the switch `Selection` itself is already gated behind — see
   `tests/CMakeLists.txt` line ~1203) and confirm ALL `SelectionTest.*` cases
   pass, both the pre-existing ones (verifying zero regression on the
   single-selection behavior every other panel still relies on) and the new
   ones added in 3.8.
4. Confirm `BoneViewerWindow.cpp`/`InspectorPanel.cpp` still compile
   unchanged against the new `Selection.h` (they should — no signature
   changed for any method they call) before moving on to Phase 2.
