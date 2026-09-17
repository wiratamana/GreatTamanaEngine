# PHASE3 — JSON Scene Document, Full-Hierarchy Save, and a Working Generic Load

_Part of `task_manager/scene-serialization-2/`. **Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.**_
Depends on: PHASE1, PHASE2 (both must be done first).
Branch: `feature/scene-serialization`.

## Step 1: The Goal

Replace the hand-rolled TEXT scene format with the JSON shape PHASE0's
Appendix A locked in, and make BOTH halves of the round trip work for
**every entity that is NOT a `PrimitiveSource`/`MeshAssetSource` "recipe"
entity** — i.e., after this phase, a Camera, a Light, or a plain empty
Transform+Name node, anywhere in the hierarchy (including nested under other
entities), fully round-trips: exact Transform, exact Name, exact Camera
`nearZ`/`farZ`/etc., exact parent/child structure. `PrimitiveSource`/
`MeshAssetSource` entities are DELIBERATELY, EXPLICITLY handled as a
simplified/partial case in this phase (see 3.5) — PHASE4 is what makes THOSE
fully correct. This split keeps every phase's own compile check green and
every phase's own scope reviewable.

## Step 2: The Situation / The Problem

`src/Scene/SceneDocument.h` today is a flat `struct SceneDocument { std::vector<SceneObjectRecord> objects; }`
with a FIXED set of fields per record (`kind`, `name`, `position`/
`rotation`/`scale`, `primitiveType`, `assetGuid`) — no hierarchy, no generic
component bag. `src/Scene/SceneTextFormat.h/.cpp` is the hand-rolled
`OBJECT`/`key=value`/`END` text grammar. `src/Scene/SceneBuilder.h/.cpp`'s
`BuildSceneDocumentFromRegistry()` only walks ROOT entities with
`PrimitiveSource`/`MeshAssetSource`. `src/Editor/SceneIO.cpp`'s `LoadScene()`
spawns via `Game::CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()`
only, then copies Transform/Name onto the result.

Every entity this engine creates ALWAYS has a `Transform` component (verified
across `Game::CreatePrimitiveEntity()`, `CreateMeshEntityFromGtaFile()`,
`CreateDirectionalLightEntity()`, `EnsureDefaultCameraExists()`) — so walking
`ECS/TransformHierarchy.h`'s `GetChildren(registry, kInvalidEntity)`
RECURSIVELY (every root, then every root's own children via
`GetChildren(registry, thatRoot)`, and so on) reaches literally every entity
in the Registry that this engine has ever created through a normal spawn
path. An entity with no `Transform` at all is out of scope (documented
limitation — this engine has never created one).

## Step 3: The Plan

### 3.1 — Rewrite `src/Scene/SceneDocument.h`

```cpp
#pragma once
#include <nlohmann/json.hpp> // PHASE0 Locked Design Decision #1 - src/Scene/ now legitimately depends on this.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// One serializable entity - see PHASE0_MASTER_STRATEGY.md's Appendix A for
// the full on-disk shape this maps onto 1:1. Deliberately NOT tied to a
// live Entity/Registry - a pure, Tier-1-testable snapshot, same spirit as
// the OLD SceneObjectRecord it replaces.
struct SceneEntityRecord {
    // Index into the OWNING SceneDocument::entities array of this entity's
    // OWN parent, or std::nullopt for a root entity. NEVER a raw Entity
    // handle (those are session-local and meaningless across a save/load
    // round trip) - always a document-local, 0-based array index.
    std::optional<std::size_t> parentIndex;

    std::uint32_t siblingIndex = 0;

    // Empty means "not an asset-recipe root" - see PHASE4 for how/when this
    // gets populated on Save and consumed on Load. Guid::ToString() format
    // (32 lowercase hex chars) when present.
    std::string assetGuid;

    // Generic component bag - one key per registered ComponentTypeRegistry
    // typeName this entity actually has (e.g. "Transform", "Name",
    // "Camera") mapped to that component's own serialized fields. Built/
    // consumed entirely generically by SceneBuilder.cpp below - NEVER
    // hand-parsed field-by-field the way the old SceneTextFormat.cpp was.
    nlohmann::json components = nlohmann::json::object();
};

struct SceneDocument {
    std::vector<SceneEntityRecord> entities;
};

} // namespace gte
```

Delete the OLD `SceneObjectKind`/`SceneObjectRecord` enum+struct entirely —
every caller is updated in this same phase (see 3.3/3.4 below), so there is
no dangling reference left after this file changes.

### 3.2 — Replace `SceneTextFormat.h/.cpp` with `src/Scene/SceneJsonFormat.h/.cpp`

Delete `SceneTextFormat.h`/`SceneTextFormat.cpp` and
`tests/Scene/SceneTextFormatTests.cpp` in THIS phase (not deferred to
Phase 6) — keeping a dead, unused old format around while also building its
replacement in the same phase only invites the two to drift/confuse a
future reader. `tests/CMakeLists.txt`'s reference to
`Scene/SceneTextFormatTests.cpp` must be removed in the SAME commit, or the
build breaks.

New `src/Scene/SceneJsonFormat.h`:

```cpp
#pragma once
#include "SceneDocument.h"
#include <optional>
#include <string>

namespace gte {

// Bumped from SceneTextFormat.h's old kSceneTextFormatVersion (which was 1)
// - PHASE0's Locked Design Decision #5 explicitly accepts breaking any old
// .gtscene file written by that older format; this is a NEW, unrelated
// format version number, not a continuation of the old one's numbering.
inline constexpr int kSceneJsonFormatVersion = 2;

// Renders `document` as pretty-printed JSON text (2-space indent, via
// nlohmann::json::dump(2) - human-diffable/hand-editable, matching this
// engine's existing "a *.gtscene file is meant to be inspectable" spirit).
// Always succeeds - there is no invalid SceneDocument value.
std::string SerializeSceneDocument(const SceneDocument& document);

// Parses text previously produced by SerializeSceneDocument() (or ANY text
// conforming to the shape below) back into a SceneDocument. Returns
// std::nullopt (never throws) for:
//   - text that isn't valid JSON at all,
//   - a top-level value that isn't a JSON object,
//   - a missing or non-integer "gtscene_version", or one that doesn't
//     exactly equal kSceneJsonFormatVersion (no partial/best-effort
//     migration from version 1 - see PHASE0's Locked Design Decision #5),
//   - a missing or non-array "entities",
//   - any entity whose "components" key is present but not a JSON object,
//   - any entity whose "parent" is present, non-null, but not a
//     non-negative integer, OR is an integer that is out of range for
//     `document.entities.size()` at the time deserialization finishes (a
//     forward OR backward reference to a real array slot is fine - order
//     doesn't matter - but a reference to an index that doesn't exist at
//     all is rejected as malformed, the whole document fails).
// "sibling_index" defaults to 0 if absent (never fails parsing on its
// own). "asset_guid" defaults to "" if absent. Every OTHER key inside one
// entity object (besides parent/sibling_index/asset_guid/components) is
// silently ignored, forward-compatible.
std::optional<SceneDocument> DeserializeSceneDocument(const std::string& text);

} // namespace gte
```

Implement `SceneJsonFormat.cpp` using `nlohmann::json` directly (parse via
`nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false)` — check
`.is_discarded()` rather than a try/catch, matching `nlohmann::json`'s own
documented no-throw parsing mode, OR wrap a throwing `parse()` call in
`try`/`catch(const nlohmann::json::exception&)` — pick ONE convention and
use it consistently; check what `NetworkRoutes.cpp` already does for JSON
parsing and match it for consistency across the codebase). Validate the
exact shape described in the doc comment above, field by field, returning
`std::nullopt` immediately on the FIRST problem found — mirror
`SceneTextFormat.cpp`'s old "reject the whole file rather than guess"
discipline exactly, just against JSON structure instead of a line grammar.

New Tier-1 tests: `tests/Scene/SceneJsonFormatTests.cpp` (replaces the
deleted `SceneTextFormatTests.cpp`, added to `tests/CMakeLists.txt` in its
place) covering: empty document round-trip, one entity with no parent/no
components round-trip, a 3-level-deep parent chain (root -> child ->
grandchild, by index) round-trip, an entity with an arbitrary
`components` bag (e.g. `{"Transform":{...},"Camera":{...}}`) round-trip,
rejection of wrong/missing `gtscene_version`, rejection of a `parent` index
that is out of range, rejection of non-array `entities`, rejection of a
non-object `components`, and tolerance of an unrecognized extra top-level
key (forward-compat).

### 3.3 — Rewrite `src/Scene/SceneBuilder.h/.cpp` — the SAVE half

`BuildSceneDocumentFromRegistry(Registry&, const AssetDatabase&)` keeps its
existing signature (still needed for the Guid-resolution step - see
PHASE4's own note on why `AssetDatabase` is still a parameter even though
this phase's own save logic doesn't yet DO anything special for
`MeshAssetSource` - PHASE4 is what actually uses it; this phase wires the
parameter through unchanged so PHASE4 doesn't need to touch this function's
signature again).

Algorithm — a recursive pre-order walk building a FLAT array with
already-resolved parent INDICES as it goes:

```cpp
namespace {
void WalkEntityRecursive(Registry& registry, Entity entity, std::optional<std::size_t> parentIndex,
    std::vector<Entity>& outEntityOrder, SceneDocument& outDocument)
{
    const std::size_t myIndex = outDocument.entities.size();
    SceneEntityRecord record;
    record.parentIndex = parentIndex;
    if (const Transform* t = registry.TryGetComponent<Transform>(entity); t != nullptr) {
        record.siblingIndex = t->siblingIndex;
    }
    // Generic component capture - THE key new piece of logic in this whole
    // campaign: walk EVERY registered component type, in
    // AllSortedByTypeName() order, and if this entity has it, serialize
    // it. No per-component-type branch anywhere in this function.
    for (const ComponentTypeDescriptor& descriptor : ComponentTypeRegistry::Instance().AllSortedByTypeName()) {
        const void* component = descriptor.tryGetConstComponent(registry, entity);
        if (component == nullptr) { continue; }
        nlohmann::json fields = nlohmann::json::object();
        for (const FieldDescriptor& field : descriptor.fields) {
            field.writeJson(component, fields);
        }
        record.components[descriptor.typeName] = std::move(fields);
    }

    outEntityOrder.push_back(entity);
    outDocument.entities.push_back(std::move(record));

    for (const Entity child : GetChildren(registry, entity)) {
        WalkEntityRecursive(registry, child, myIndex, outEntityOrder, outDocument);
    }
}
}

SceneDocument BuildSceneDocumentFromRegistry(Registry& registry, const AssetDatabase& assetDatabase)
{
    SceneDocument document;
    std::vector<Entity> entityOrder; // index-aligned with document.entities - PHASE4 reuses this for asset_guid resolution.
    for (const Entity root : GetChildren(registry, kInvalidEntity)) {
        WalkEntityRecursive(registry, root, std::nullopt, entityOrder, document);
    }
    // PHASE4 inserts the MeshAssetSource -> asset_guid resolution pass HERE,
    // using `entityOrder`/`assetDatabase` together with the already-built
    // `document` - this phase deliberately leaves that step out (every
    // entity, including one with a MeshAssetSource, is still captured
    // generically above with whatever fields ARE registered for it today -
    // which is none, since MeshAssetSource itself is not registered, see
    // PHASE2 - so an asset-spawned root's OWN record still gets its
    // Transform/Name captured correctly by this phase; only its
    // asset-recipe re-linking is deferred).
    return document;
}
```

`ClearSerializableSceneObjects()` is INTENTIONALLY NOT touched in this
phase — PHASE4 replaces it with `ClearEntireScene()`. For THIS phase, leave
the existing function and its existing (narrower) behavior in place so
`Editor/SceneIO.cpp`'s `LoadScene()` (updated in 3.4 below) still has
something to call; PHASE4 swaps it out.

### 3.4 — Rewrite `Editor/SceneIO.cpp`'s `LoadScene()` — the GENERIC (simplified) LOAD half

This is the phase's other major deliverable. Replace the spawn-dispatch
loop with a two-pass generic reconstruction that does NOT yet special-case
`PrimitiveSource`/`MeshAssetSource` (PHASE4 adds that special-casing on top,
in the SAME two functions, without needing a third pass):

```cpp
bool LoadScene(Game& game, Renderer& renderer)
{
    // ... unchanged: read DefaultScenePath(), DeserializeSceneDocument(),
    // return false on any failure, exactly like today ...

    Registry& registry = game.GetRegistry();
    ClearSerializableSceneObjects(registry); // PHASE4 replaces this call with ClearEntireScene(registry).

    std::vector<Entity> resultEntities(document->entities.size(), kInvalidEntity);

    // Pass A - create every entity as a bare entity (PHASE4 replaces THIS
    // loop body with the recipe-aware version - Primitive/Asset entities
    // still just become bare entities in Phase 3, which is the
    // "intentionally simplified" gap this phase's Step 1 already calls
    // out: they will have NO MeshRenderer/visible mesh yet after only this
    // phase, but WILL have their correct Transform/Name/any-other-reflected-
    // field once Pass B below runs).
    for (std::size_t i = 0; i < document->entities.size(); ++i) {
        resultEntities[i] = registry.CreateEntity();
    }

    // Pass B1 - wire hierarchy FIRST (before applying Transform values -
    // see PHASE0's own note on why order matters: SetParent()'s
    // worldPositionStays=true would otherwise RECOMPUTE local
    // position/rotation/scale and CLOBBER whatever Pass B2 is about to
    // write). worldPositionStays=false here is what avoids that.
    for (std::size_t i = 0; i < document->entities.size(); ++i) {
        const SceneEntityRecord& record = document->entities[i];
        if (record.parentIndex.has_value()) {
            const Entity parentEntity = resultEntities[*record.parentIndex];
            SetParent(registry, resultEntities[i], parentEntity, /*worldPositionStays=*/false);
        }
    }

    // Pass B2 - generically apply every reflected component's fields -
    // this is what actually restores Transform/Name/Camera/etc to their
    // EXACT saved values, regardless of whatever Pass A/B1 left them as.
    for (std::size_t i = 0; i < document->entities.size(); ++i) {
        const SceneEntityRecord& record = document->entities[i];
        const Entity entity = resultEntities[i];
        for (auto it = record.components.begin(); it != record.components.end(); ++it) {
            const std::string& typeName = it.key();
            const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find(typeName);
            if (descriptor == nullptr) { continue; } // Unknown component type - forward-compat, silently skipped.
            descriptor->ensureDefaultComponent(registry, entity);
            void* component = descriptor->tryGetMutableComponent(registry, entity);
            for (const FieldDescriptor& field : descriptor->fields) {
                std::string errorMessage;
                if (!field.readJson(component, it.value(), errorMessage)) {
                    // A single malformed FIELD does not fail the whole
                    // Load - log/ignore and keep that field at whatever
                    // ensureDefaultComponent()'s default left it as. This
                    // matches this engine's general "degrade gracefully"
                    // convention rather than SceneTextFormat.cpp's old
                    // "any malformed field fails the WHOLE document"
                    // behavior - deliberately relaxed here because a
                    // single bad float in an otherwise-huge scene file
                    // should not lose everything else in it. Document this
                    // choice explicitly in a code comment at this exact
                    // spot.
                }
            }
        }
    }

    // Pass B3 - restore sibling ordering, per parent group, ascending by
    // saved siblingIndex (SetSiblingIndex() renumbers the WHOLE sibling
    // group each call, so process every record whose parent is the SAME,
    // in ascending siblingIndex order, calling SetSiblingIndex once per
    // entity - best-effort, matching GetChildren()'s own documented
    // "ties broken by creation/dense-storage order" tolerance elsewhere in
    // this engine, not a pixel-perfect ordering guarantee).
    for (std::size_t i = 0; i < document->entities.size(); ++i) {
        SetSiblingIndex(registry, resultEntities[i], document->entities[i].siblingIndex);
    }

    return true;
}
```

Note precisely WHERE Phase 4 will graft its own changes on, so this phase's
own implementer writes code that is easy to extend rather than needing a
rewrite: Pass A's per-record loop body is the ONLY place Phase 4 changes
(to special-case `PrimitiveSource`/`MeshAssetSource` records and their
already-spawned children) — Pass B1/B2/B3 stay conceptually the same, just
skipping any record Pass A already fully resolved via a recipe spawn
(Phase 4 adds a `processedByRecipe` tracking structure Pass B2/B3 both
consult). Write this as an explicit code comment in `SceneIO.cpp` at the
end of Pass A, e.g. `// PHASE4 TODO: replace this loop's body with a
recipe-aware version - see task_manager/scene-serialization-2/PHASE4_....md`.

### 3.5 — What is DELIBERATELY still broken/incomplete after this phase (write into `PHASE3_COMPLETION_REPORT.md`, not silently discovered later)

- A `PrimitiveSource`/`MeshAssetSource`-tagged entity round-trips its
  Transform/Name/`PrimitiveSource` (the enum tag) correctly, but has NO
  `MeshRenderer` after Load — i.e. it will not be visible. This is
  EXPECTED and is exactly what PHASE4 fixes.
- `ClearSerializableSceneObjects()` is unchanged, so a plain/Camera/Light
  entity that this phase now WOULD serialize is nonetheless NOT cleared
  before a fresh Load — meaning a repeated Load can accumulate duplicate
  Camera/Light entities. Acceptable ONLY as this phase's own temporary
  state — PHASE4 fixes this by introducing `ClearEntireScene()`. Do not
  treat this as "phase 3 is broken" — it is an intentionally incomplete,
  clearly-labeled intermediate state per PHASE0's own phase-index summary.

### 3.6 — Update the pre-existing `tests/Scene/SceneBuilderTests.cpp` (this phase's own type deletion breaks it otherwise)

`tests/Scene/SceneBuilderTests.cpp` (from `scene-serialization-1`) directly
constructs/reads `SceneObjectRecord`/`SceneObjectKind`/`document.objects` and
calls `ClearSerializableSceneObjects()`. Since 3.1 above DELETES
`SceneObjectRecord`/`SceneObjectKind` outright, this pre-existing test file
WILL NOT COMPILE anymore unless it is rewritten in this SAME phase — never
defer this to Phase 6, since a broken test file would already fail THIS
phase's own "fast compile check" Definition-of-Done item (a test binary that
fails to compile is not a passing compile check). Rewrite it in place (same
file, same overall test names/spirit where still applicable) against the NEW
`BuildSceneDocumentFromRegistry()`/`SceneDocument`/`SceneEntityRecord` shape
from 3.1/3.3 above:

- `PrimitiveRootProducesOnePrimitiveRecord`/
  `AssetRootWithTrackedPathProducesOneAssetRecord`/
  `AssetRootResolvesViaNormalizedEquivalentPath`/
  `MultipleIndependentRootsAllResolveCorrectly` — rewrite each to assert
  against the new `document.entities[i].components["PrimitiveSource"]`/
  `["Transform"]`/`["Name"]` generic JSON bag (inspecting the raw
  `nlohmann::json` directly is fine — no need to round-trip back through
  `ComponentTypeRegistry` just for a save-side test) instead of the old
  `SceneObjectRecord::kind`/`primitiveType`/`assetGuid`/`name` fields. This
  phase does NOT yet populate `asset_guid` at all (that is PHASE4's own 3.1)
  — do not assert anything about it here yet.
- `AssetRootWithUntrackedPathIsSkipped` — RENAME/REWRITE: an untracked-path
  asset root's record is NO LONGER skipped/omitted entirely the way
  `scene-serialization-1` used to (see 3.3's own note on this exact
  behavior change) — its Transform/Name are still captured generically, only
  its (not-yet-implemented-this-phase) `asset_guid` stays empty. Rename this
  test (e.g. `AssetRootWithUntrackedPathStillProducesARecordWithNoGuid`) and
  assert a record IS present, with an empty/absent `asset_guid`, instead of
  asserting `document.entities` is empty.
- `RootWithNeitherTagIsSkipped` — RENAME/REWRITE: a Camera-only root is no
  longer skipped at all (PHASE0's Locked Design Decision #2 — every entity
  is now in scope). Rename to something like
  `PlainCameraRootProducesACameraRecordToo` and invert the assertion: a
  record IS produced, with a `"Camera"`/`"Transform"` components entry.
  `document.objects` itself no longer exists (renamed to `document.entities`
  by 3.1) so this test needs a full rewrite regardless of the rename.
- `ChildEntityIsNeverIndependentlyVisited` — RENAME/REWRITE: a child entity
  IS now independently visited/serialized (as its own `SceneEntityRecord`
  with a non-null `parentIndex`) — assert the OPPOSITE of the old test:
  `document.entities` has (at least) 2 entries, and the child's own entry's
  `parentIndex` correctly points at the root's own array index. Consider
  renaming to `ChildEntityIsSerializedWithParentIndex`.
- The three `ClearSerializableSceneObjects()`-named tests
  (`ClearDestroysPrimitiveAndAssetRootsPlusTheirChildren`/
  `ClearLeavesUntaggedEntitiesUntouched`/`ClearOnEmptyRegistryIsASafeNoOp`) —
  leave these calling `ClearSerializableSceneObjects()` UNCHANGED in THIS
  phase (3.3 above deliberately does not touch that function yet). PHASE4 is
  what replaces this function with `ClearEntireScene()`, and PHASE4's own
  Section 3.3 is responsible for updating THESE SAME three tests then (see
  that phase's own note) — do not jump ahead and "fix" them here.
- Update this file's own top-of-file doc comment, and its counterpart
  description block in `tests/CMakeLists.txt` (search for
  `Scene/SceneBuilderTests.cpp`), so neither keeps describing the OLD
  flat/root-only/skip-untagged-roots behavior once this phase lands.

## Definition of Done

- [ ] `SceneDocument.h` rewritten to the hierarchy-aware, generic-component
      shape; old `SceneObjectKind`/`SceneObjectRecord` fully removed.
- [ ] `SceneTextFormat.h/.cpp` and `tests/Scene/SceneTextFormatTests.cpp`
      deleted; `SceneJsonFormat.h/.cpp` and
      `tests/Scene/SceneJsonFormatTests.cpp` added and passing.
- [ ] `SceneBuilder.cpp`'s save half walks the ENTIRE hierarchy recursively
      and captures every registered component generically.
- [ ] `Editor/SceneIO.cpp`'s `LoadScene()` reconstructs hierarchy + applies
      every generic component field correctly for non-recipe entities.
- [ ] `tests/Scene/SceneBuilderTests.cpp` (pre-existing, from
      `scene-serialization-1`) is rewritten per 3.6 above — it compiles and
      passes against the NEW `SceneDocument`/`SceneEntityRecord` shape, with
      zero remaining reference to `SceneObjectRecord`/`SceneObjectKind`/
      `document.objects`.
- [ ] A manual/local sanity check (not yet the full regression suite — that
      is Phase 6): save a scene containing a Camera with a non-default
      `nearZ`/`farZ`, plus a plain empty Transform+Name child node parented
      under it, reload, and confirm both values AND the parent/child
      relationship are restored exactly.
- [ ] `PHASE3_COMPLETION_REPORT.md` explicitly documents the 3.5 "still
      broken" list so PHASE4 isn't a surprise.
- [ ] Fast compile check passes (both `GTE_ENABLE_EDITOR=ON`; a
      `GTE_ENABLE_EDITOR=OFF` check of `src/Scene/` alone, which never
      depended on Editor, should also still pass since this phase doesn't
      touch that boundary).
- [ ] `git add`/`git commit`.
