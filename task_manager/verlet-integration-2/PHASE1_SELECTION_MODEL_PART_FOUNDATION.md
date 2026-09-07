# PHASE1 — Selection: Model-Part Selection Foundation (`src/Editor/Selection.h/.cpp`) (v2)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit C/D). Depends on: nothing (this
is the foundation phase — pure data-model change). Produces: an extended
`Selection` class capable of representing "a bone/rigid-body/joint of some
model, belonging to some entity, is currently selected", with the exact same
"single gate-keeper, mutually-exclusive `Kind()`" shape `Entity`/`Asset`
selection already has. No ImGui/BoneViewerWindow/InspectorPanel code is
touched in this phase — it must compile and pass its own tests in total
isolation, exactly like `PHASE1_CORE_VERLET_PHYSICS_FOUNDATION.md` did for
the earlier campaign.

**v2 note:** adds one new mutator, `Selection::ClearModelPartIfEntity(Entity)`
(section 3.2/3.3 below), that v1 was missing — see
`PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)", finding #1, for the
full rationale: without it, PHASE3's `BoneViewerWindow` had no way to keep a
stale Model-Part selection from silently surviving across a genuine reload of
the same entity's underlying model data. Everything else in this document is
unchanged from v1.

## Step 1: The Goal

Give `Selection` a THIRD kind, alongside the existing `Entity`/`Asset`, that
can uniquely identify "part index `N`, of kind `Bone`/`RigidBody`/`Joint`,
belonging to model-root entity `E`" — general enough to represent any of the
three categories the Bone Viewer (Phase 3) will show, without needing a
fourth/fifth kind later. This must preserve, byte-for-byte, every existing
behavioral guarantee `Selection` already has and is already tested for
(`tests/Editor/SelectionTests.cpp`):

- Exactly one thing is ever "on top" (`Kind()`) at a time — selecting a
  model part must unhighlight whatever Entity/Asset was selected before, and
  vice versa, without ever clearing the OTHER kind's own underlying fields
  (same "leave the other fields untouched, just gate visibility on `Kind()`"
  contract `SelectEntity()`/`SelectAsset()` already have with each other).
- `Selection` stays plain data + small pure mutators, zero ImGui/SDL/Vulkan
  dependency, Tier-1-testable exactly as it is today.

## Step 2: The Situation / The Problem

`src/Editor/Selection.h` today defines:

```cpp
enum class InspectorSelectionKind {
    None,
    Entity,
    Asset,
};
```

and `Selection` holds `m_kind` plus an `Entity m_entity` (for `Entity`) and
three fields for `Asset` (`m_assetAbsolutePath`/`m_assetRelativePath`/
`m_assetIsDirectory`). There is no way to represent "a sub-part of an
entity's own model" at all — see `PHASE0_MASTER_STRATEGY.md`, Culprit C/D,
for exactly why `BoneViewerWindow` (today) and `InspectorPanel` (Phase 4)
both need this.

`GizmoOperation` (`TransformGizmo.h`) is the existing precedent for "a free
enum, not nested in a class, used unqualified by every panel that needs it" —
`Selection.h`'s own comment on `InspectorSelectionKind` explicitly calls out
following that same convention; the new `ModelPartKind` enum below must do
the same.

## Step 3: The Plan

### 3.1 `Selection.h` — new enum + extended `InspectorSelectionKind`

Add, right after the existing `InspectorSelectionKind` enum (before the
`Selection` class itself):

```cpp
// Which of the three categories a ModelPart selection (see
// InspectorSelectionKind::ModelPart below) refers to - the Bone Viewer's
// own "Bones / Rigid Bodies / Joints" dropdown (BoneViewerWindow.h,
// PHASE3_BONE_VIEWER_VIEW_MODE_DROPDOWN_AND_GIZMO.md) picks exactly one of
// these to display/select from at a time. A free enum (not nested in
// Selection), same convention as GizmoOperation (TransformGizmo.h) and
// InspectorSelectionKind above, so every panel can write
// `ModelPartKind::Bone` unqualified.
enum class ModelPartKind {
    Bone,
    RigidBody,
    Joint,
};
```

Extend `InspectorSelectionKind` with a fourth value, appended at the end
(never renumber/reorder the existing three — nothing in this codebase
serializes this enum to disk, but appending-only is still this codebase's
standing convention for every enum with external meaning, see
`AssetTypes.h`'s own "only ever APPEND, never renumber" rule):

```cpp
enum class InspectorSelectionKind {
    None,
    Entity,
    Asset,
    ModelPart,
};
```

Update the enum's own doc comment (directly above it) to mention the new
kind in the same style as the existing two, e.g. append a sentence: *"Also
becomes `ModelPart` when a bone/rigid-body/joint is selected inside the Bone
Viewer window (`BoneViewerWindow.h`) — only ever reachable the same way
`Asset` is, when `GTE_ENABLE_PROJECT_PANEL` is ON, since that window is only
ever compiled then."*

### 3.2 `Selection.h` — new fields + methods on `Selection`

Add three new private fields, mirroring the shape of the existing `Asset`
fields exactly:

```cpp
Entity m_modelPartEntity = kInvalidEntity;
ModelPartKind m_modelPartKind = ModelPartKind::Bone;
int m_modelPartIndex = -1;
```

Add the public mutator, placed right after `SelectAsset()`'s own doc comment
block, matching its documentation depth/style exactly:

```cpp
// Makes "part `partIndex` of kind `partKind`, belonging to `owningEntity`'s
// own model" the current Model-Part selection and the current Inspector
// source (Kind() becomes ModelPart) - the Hierarchy/entity selection field
// AND the Project/asset selection fields are left untouched (SelectedEntity()/
// SelectedAssetAbsolutePath() etc. still return whatever was last picked in
// Hierarchy/Project), but since Kind() is now ModelPart,
// IsEntitySelected()/IsAssetSelected() below immediately report nothing
// selected - exactly the same "leave the other fields untouched, just gate
// visibility on Kind()" contract SelectEntity()/SelectAsset() already have
// with each other. `owningEntity` is the model's ROOT entity (the one
// carrying MeshAssetSource - see ECS/Components/MeshAssetSource.h) - NOT
// necessarily the same as whatever SelectedEntity() currently returns.
// `partIndex` is an index into whichever array `partKind` names
// (SkeletonData::bones / PhysicsData::rigidBodies / PhysicsData::joints -
// see Assets/PhysicsData.h/SkeletonData.h) - meaningless on its own without
// also knowing `owningEntity`'s own model and `partKind`, which is exactly
// why all three are stored together rather than as three independently-
// settable fields.
void SelectModelPart(Entity owningEntity, ModelPartKind partKind, int partIndex);
```

Add the three plain accessors, grouped with the existing ones:

```cpp
Entity SelectedModelPartEntity() const { return m_modelPartEntity; }
ModelPartKind SelectedModelPartKind() const { return m_modelPartKind; }
int SelectedModelPartIndex() const { return m_modelPartIndex; }
```

Add the highlight-gating query, mirroring `IsEntitySelected()`/
`IsAssetSelected()` exactly (ALL THREE fields must match, not just the
index — a rigid body at index 2 must never appear selected while a bone at
index 2 is what's actually selected, and vice versa; and a part belonging to
a DIFFERENT entity must never appear selected either, e.g. two entities
spawned from different models that both happen to have a bone index 2):

```cpp
// True if `owningEntity`/`partKind`/`partIndex` are EXACTLY the current
// Model-Part selection AND Kind() is ModelPart - this is the ONLY thing
// BoneViewerWindow's tree-row/gizmo-dot rendering may use to decide whether
// to highlight a row/dot; it must never keep its own separate "is this row
// selected" state (see Selection.h's own class comment, and
// PHASE0_MASTER_STRATEGY.md's Culprit C). Mirrors IsEntitySelected()/
// IsAssetSelected() exactly, extended to three fields instead of one/two
// since a bare index alone is ambiguous across both `partKind` and
// `owningEntity`.
bool IsModelPartSelected(Entity owningEntity, ModelPartKind partKind, int partIndex) const;
```

**(v2 addition)** Add one more public mutator, right after
`IsModelPartSelected()`'s own declaration, mirroring `ClearAssetIfPath()`'s
own existing "clear only if it currently matches" shape exactly (see
`PHASE0_MASTER_STRATEGY.md`'s "Revision Notes (v2)", finding #1, for the full
motivating scenario):

```cpp
// Clears the Model-Part selection fields ONLY if they currently refer to
// `owningEntity` exactly (regardless of whatever partKind/partIndex they
// currently hold) - a no-op otherwise. Mirrors ClearAssetIfPath()'s own
// "clear only if it currently matches" shape exactly, just keyed on the
// owning Entity rather than a path string (a bone/rigid-body/joint index is
// meaningless without also knowing which entity's model it belongs to - see
// SelectModelPart()'s own doc comment above). If the current Inspector
// source (Kind()) is ModelPart at the moment this matches, it also reverts
// to None (the Inspector then shows nothing, rather than stale info for a
// part that may no longer mean the same thing against freshly reloaded
// data) - if Kind() is Entity/Asset, it stays that way (this never touches
// those fields). Used by BoneViewerWindow whenever `owningEntity`'s own
// underlying model data genuinely reloads (a different file, or the same
// file with a newer mtime) - a stale index from the PREVIOUS load means
// nothing against the newly (re)loaded data, exactly the same reasoning
// BoneViewerWindow's own (now-deleted) private `m_selectedBoneIndex` used
// to reset to -1 for on every reload.
void ClearModelPartIfEntity(Entity owningEntity);
```

Update `Selection`'s own class-level doc comment to mention the third kind
in its final paragraph (the one about "a future selectable 'thing' ...
should extend this same class"), replacing/extending it to note this is
exactly that extension having happened.

### 3.3 `Selection.cpp` — implementation

```cpp
void Selection::SelectModelPart(Entity owningEntity, ModelPartKind partKind, int partIndex)
{
    m_modelPartEntity = owningEntity;
    m_modelPartKind = partKind;
    m_modelPartIndex = partIndex;
    m_kind = InspectorSelectionKind::ModelPart;
}

bool Selection::IsModelPartSelected(Entity owningEntity, ModelPartKind partKind, int partIndex) const
{
    return m_kind == InspectorSelectionKind::ModelPart && m_modelPartEntity == owningEntity
        && m_modelPartKind == partKind && m_modelPartIndex == partIndex;
}

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

Update `Selection::Clear()` to also reset the three new fields to their
defaults (`kInvalidEntity`/`ModelPartKind::Bone`/`-1`), appended after the
existing asset-field resets, same style.

`SelectEntity()`/`SelectAsset()` themselves need **no changes** — they
already only ever touch their own fields + `m_kind`, which is exactly the
"leave every other kind's fields untouched" behavior this phase needs from
them too (verified directly by the new tests in 3.4 below).

### 3.4 Tests — extend `tests/Editor/SelectionTests.cpp`

Add these `TEST(SelectionTest, ...)` cases, in the same terse/direct style
as the existing ones (no new test file — this is a natural extension of the
existing one, same class under test):

- `DefaultsToNoneWithNoModelPartSelectedEither` — extend the existing
  `DefaultsToNoneWithNoEntityOrAssetSelected` test (or add a sibling) to also
  assert `selection.SelectedModelPartEntity() == kInvalidEntity`,
  `selection.SelectedModelPartKind() == ModelPartKind::Bone`,
  `selection.SelectedModelPartIndex() == -1`, and
  `!selection.IsModelPartSelected(kInvalidEntity, ModelPartKind::Bone, -1)`
  (a default/never-selected state must never accidentally read as "selected"
  against its own default values).
- `SelectModelPartMakesItTheCurrentModelPartSelectionAndInspectorSource` —
  `SelectModelPart(Entity{4,1}, ModelPartKind::RigidBody, 7)` then assert
  `Kind() == ModelPart`, the three accessors return exactly those values,
  and `IsModelPartSelected(Entity{4,1}, ModelPartKind::RigidBody, 7)` is
  true.
- `IsModelPartSelectedRequiresAllThreeFieldsToMatchExactly` — after the same
  `SelectModelPart(...)` call above, assert `IsModelPartSelected()` returns
  `false` when: the entity differs (same kind/index), the `ModelPartKind`
  differs (same entity/index — e.g. `Bone` vs. `RigidBody` at the same
  numeric index 7), or the index differs (same entity/kind) — three
  separate assertions, each changing exactly one of the three fields, to
  pin down that this is a genuine three-way AND, not e.g. an accidental
  index-only check.
- `SelectModelPartLeavesEntityAndAssetFieldsIntactButUnhighlightsThemImmediately`
  — mirrors `SelectEntityLeavesAssetFieldsIntactButUnhighlightsThemImmediately`
  exactly: first `SelectEntity(...)` AND `SelectAsset(...)`, then
  `SelectModelPart(...)`; assert `Kind() == ModelPart`,
  `SelectedEntity()`/`SelectedAssetRelativePath()` still return their prior
  values unchanged, but `IsEntitySelected(...)`/`IsAssetSelected(...)` for
  those same prior values now both return `false`.
- `SelectEntityAfterModelPartUnhighlightsItImmediately` (and the `SelectAsset`
  equivalent) — mirrors the existing cross-kind tests exactly, just with
  `ModelPart` as the "previously on top" kind instead of `Asset`: after
  `SelectModelPart(...)` then `SelectEntity(...)`, assert
  `SelectedModelPartEntity()`/`Kind()`/`Index()` still return the SAME
  stale values as before (never auto-cleared — matches the existing
  "leave it, just gate visibility" convention), but
  `IsModelPartSelected(...)` for those exact stale values now returns
  `false`.
- `ClearResetsModelPartFieldsToo` — extend the existing `ClearResetsEverythingToDefaults`
  test (or add a sibling) to also `SelectModelPart(...)` before calling
  `Clear()`, then assert the three model-part fields are back to their
  documented defaults and `Kind() == None`.
- **(v2 addition)** `ClearModelPartIfEntityIsNoOpWhenEntityDoesNotMatch` —
  mirrors `ClearAssetIfPathIsNoOpWhenPathDoesNotMatch` exactly:
  `SelectModelPart(Entity{4,1}, ModelPartKind::RigidBody, 7)`, then
  `ClearModelPartIfEntity(Entity{9,1})`; assert `Kind()` is still `ModelPart`
  and the three accessors still return the original values, untouched.
- **(v2 addition)**
  `ClearModelPartIfEntityClearsFieldsAndRevertsKindToNoneWhenModelPartIsOnTop`
  — mirrors `ClearAssetIfPathClearsFieldsAndRevertsKindToNoneWhenAssetIsOnTop`
  exactly: `SelectModelPart(Entity{4,1}, ModelPartKind::RigidBody, 7)`, then
  `ClearModelPartIfEntity(Entity{4,1})`; assert `Kind() == None` and the three
  accessors are back to their documented defaults
  (`kInvalidEntity`/`ModelPartKind::Bone`/`-1`).
- **(v2 addition)**
  `ClearModelPartIfEntityClearsFieldsButKeepsEntityKindWhenEntityIsOnTop` —
  mirrors `ClearAssetIfPathClearsFieldsButKeepsEntityKindWhenEntityIsOnTop`
  exactly: `SelectModelPart(Entity{4,1}, ModelPartKind::RigidBody, 7)`, then
  `SelectEntity(Entity{2,1})` (Inspector now shows the entity, not the model
  part), then `ClearModelPartIfEntity(Entity{4,1})`; assert `Kind()` stays
  `Entity` and `SelectedEntity()` still returns `Entity{2,1}` (this must never
  touch the entity selection), but the three model-part accessors are still
  reset to their documented defaults and
  `IsModelPartSelected(Entity{4,1}, ModelPartKind::RigidBody, 7)` now returns
  `false` (proves the fields really were cleared even though `Kind()` had
  already moved on to `Entity` before `ClearModelPartIfEntity()` was called —
  the exact scenario PHASE3's reload-safety fix depends on: the Bone Viewer
  may reload a DIFFERENT entity's model, entirely unrelated to whatever the
  stale Model-Part selection still points at, while `Kind()` is already
  `Entity`, and that stale selection must still be cleared correctly).

## Step 4: What We Will NOT Do

- We will **not** add a `HasModelPartSelection()` convenience method (unlike
  `HasAssetSelection()`) — nothing in Phase 3/4 needs a "is there currently a
  valid, non-empty model-part selection to act on" gate beyond checking
  `Kind() == ModelPart` directly (there is no "empty index means root"
  special case here the way an empty `relativePath` means the Project root
  for `Asset`) — avoid adding unrequested API surface.
- We will **not** validate `partIndex`/`owningEntity` in `SelectModelPart()`
  itself (e.g. checking the entity is alive, or the index is in range) —
  `Selection` stays pure, dumb data storage with zero `Registry`/ECS
  dependency, exactly like it is today for `Entity`/`Asset` (`SelectEntity()`
  doesn't check `registry.IsAlive()` either) — validation is entirely the
  caller's (Phase 3's click handler, Phase 4's Inspector renderer)
  responsibility, at read time.
- We will **not** add any notion of "which model/asset path" directly into
  `Selection` itself (e.g. a stored `gtaPath` string) — `owningEntity` is
  enough; any reader that needs the model's own file path re-resolves it via
  that entity's `MeshAssetSource` component at read time (see Phase 4),
  keeping `Selection` free of any `MeshAssetSource`/Assets-layer dependency.

## Step 5: Their Role

Implementer checklist for this phase:

1. Edit `src/Editor/Selection.h`: add `ModelPartKind`, extend
   `InspectorSelectionKind`, add the three new fields + `SelectModelPart()`
   declaration + three accessors + `IsModelPartSelected()` declaration +
   **(v2)** `ClearModelPartIfEntity()` declaration to `Selection`, per
   3.1/3.2.
2. Edit `src/Editor/Selection.cpp`: implement `SelectModelPart()`/
   `IsModelPartSelected()`/**(v2)** `ClearModelPartIfEntity()`, extend
   `Clear()`, per 3.3.
3. Extend `tests/Editor/SelectionTests.cpp` with every case in 3.4
   (including the three **(v2)** `ClearModelPartIfEntity*` cases).
4. Build `gte_core` + `GreatTamanaEngineTests` and confirm every existing
   `SelectionTest.*` case still passes UNCHANGED (this phase must be a
   behavior-preserving extension for Entity/Asset, not just "new tests
   pass") alongside every new one — Phase 2/3/4 all depend on this exact,
   fully-correct API surface compiling and behaving as documented before
   they can be started.
