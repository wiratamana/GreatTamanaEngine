# PHASE4 — `src/Scene/SceneBuilder.h/.cpp`: Registry ⇄ `SceneDocument` Bridge (v2)

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`.
Depends on PHASE2 (`PrimitiveSource`, `DestroyEntityAndDescendants()`) and
PHASE3 (`SceneDocument`, and — as of v2 — its guarantee that every
deserialized `Asset` record already carries a valid `Guid`). PHASE5 depends
on this phase's `BuildSceneDocumentFromRegistry()`/
`ClearSerializableSceneObjects()`.

**Revision note (v2):** the second-iteration audit (see
`PHASE0_MASTER_STRATEGY.md`) found that `BuildSceneDocumentFromRegistry()`'s
use of `AssetDatabase::FindByPath()` quietly depends on a path-normalization
invariant that v1 never wrote down. Section 3.1's doc comment and section
3.4's test list below now call this out explicitly and add one test case
for it; nothing else in this phase changes from v1 — the actual
`SceneBuilder.h/.cpp` code in section 3.1/3.2 is byte-for-byte identical to
v1 (no code-level bug was found, only a missing piece of documentation/test
coverage for an assumption the code already correctly relies on).

## Step 1: The Goal (Where are we going?)

Build the ECS-facing half of scene serialization: turn a live `Registry`'s
current content into a `SceneDocument` (for Save), and clear exactly the
entities this feature owns out of a `Registry` (the first step of Load,
before new entities are spawned from a freshly-parsed `SceneDocument`).
Still **zero Renderer dependency** — this stays Tier-1-testable, exactly
like PHASE3, by taking a `Registry&` plus a `const AssetDatabase&` (both
already Renderer-free types).

## Step 2: The Situation / The Problem (Where are we now?)

- `ECS/TransformHierarchy.h`'s `GetChildren(registry, kInvalidEntity)`
  (pre-existing) is exactly how `Panels/HierarchyPanel.cpp` already
  enumerates every ROOT-level entity today — the correct, established way
  to find "every top-level scene object," reused here rather than
  reinvented.
- `ECS/Components/PrimitiveSource.h` (PHASE2) and
  `ECS/Components/MeshAssetSource.h` (pre-existing) are the two tags that
  identify which of the two serializable kinds a root entity is; an entity
  with NEITHER (e.g. the default Camera from PHASE1) must be skipped
  entirely — it is not part of this feature's serialization scope.
- `src/Assets/AssetDatabase.h`'s `FindByPath(const std::filesystem::path&)`
  and `FindByGuid(const Guid&)` (both pre-existing, confirmed by direct
  inspection) are exactly the two lookups needed: `FindByPath()` at Save
  time (turn a `MeshAssetSource::gtaPath` into its Guid) and `FindByGuid()`
  at Load time (turn a `SceneObjectRecord::assetGuid` back into an absolute
  path) — the latter is used by PHASE5, not this phase, but the former is
  used directly here.
- **Correctness invariant (v2 — confirmed, not a bug, but load-bearing and
  previously undocumented): `AssetDatabase::FindByPath()`'s own
  implementation (`src/Assets/AssetDatabase.cpp`) re-normalizes whatever
  path it's given through `std::filesystem::absolute()` before comparing
  against its internal `m_pathToIndex` map, and `RefreshFromDirectory()`
  populates that SAME map by running every discovered `*.gta`'s path
  through the identical `std::filesystem::absolute()` normalization.**
  This means `FindByPath(meshAssetSource->gtaPath)` below only reliably
  resolves when `meshAssetSource->gtaPath` (originally captured from
  whatever absolute path string was passed into
  `Game::CreateMeshEntityFromGtaFile()` — today, always
  `Panels/ProjectPanel.cpp`'s drag-and-drop payload, itself built from
  `SDL_GetBasePath()`-derived, already-absolute path segments) refers to
  the exact same file `std::filesystem::absolute()` would resolve it to —
  true today for every real call site, but not an accident a future call
  site is free to break (e.g. a future caller that hands
  `CreateMeshEntityFromGtaFile()` a path containing a `.`/`..` segment, a
  different-cased drive letter, or a forward-slash-only path on Windows
  should not silently produce a `MeshAssetSource::gtaPath` that this
  bridge — or `SceneIO.cpp`'s Save path re-scan — then fails to resolve).
  See section 3.4's new test case, which exercises exactly this path
  through a real temp-directory `AssetDatabase`, the same way this phase's
  existing tests already do.
- `ECS/TransformHierarchy.h::DestroyEntityAndDescendants()` (PHASE2) is
  exactly the primitive `ClearSerializableSceneObjects()` below needs.

## Step 3: The Plan (A very detailed strategy)

### 3.1 — `src/Scene/SceneBuilder.h` (new file)

```cpp
#pragma once

#include "../Assets/AssetDatabase.h"
#include "../ECS/Registry.h"
#include "SceneDocument.h"

namespace gte {

// The ECS-facing bridge between a live Registry and a plain SceneDocument
// (Scene/SceneDocument.h) - the SAVE half (BuildSceneDocumentFromRegistry())
// and the "make room for a fresh Load" half (ClearSerializableSceneObjects())
// of task_manager/scene-serialization-1. Deliberately still Renderer-free -
// spawning new entities from a SceneDocument (the rest of "Load") needs a
// live Renderer (Game::CreatePrimitiveEntity()/CreateMeshEntityFromGtaFile()
// both do), so THAT half lives in Editor/SceneIO.h instead, not here -
// keeping this file Tier-1-testable exactly like Scene/SceneTextFormat.h.
//
// Only ROOT entities (Transform::parent == kInvalidEntity, per
// ECS/TransformHierarchy.h's GetChildren(registry, kInvalidEntity)) are ever
// considered - a reparented primitive/asset root living somewhere deeper in
// the hierarchy is a documented, out-of-scope limitation (see
// task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md, "What We
// Will NOT Do"). A multi-part asset's own CHILD "submesh part" entities
// (spawned by Game::CreateMeshEntityFromGtaFile() underneath its root) are
// NEVER individually walked/serialized here - only the root's own Guid +
// Transform is captured; every child is discarded on save and fully
// re-derived fresh from the asset again at load time (see PHASE0's Design
// Decision #3).
//
// CORRECTNESS INVARIANT (v2): resolving a MeshAssetSource::gtaPath below via
// `assetDatabase.FindByPath()` only works when that stored path and
// `assetDatabase`'s own RefreshFromDirectory()-populated index agree on
// what "the same file" looks like as a string - both normalize via
// std::filesystem::absolute() independently (see AssetDatabase.cpp), so a
// MeshAssetSource::gtaPath captured via any means OTHER than an
// already-absolute, already-`SDL_GetBasePath()`-rooted path (today's only
// real source - Panels/ProjectPanel.cpp's drag-and-drop payload) is not
// guaranteed to resolve. This is true today by construction, not by luck,
// but is not re-derived/re-checked anywhere in this file itself - see
// task_manager/scene-serialization-1/PHASE4_SCENE_BUILDER_REGISTRY_ASSETDATABASE_BRIDGE.md's
// own "Correctness invariant" note for the full reasoning, and keep it in
// mind before changing how any caller constructs the path handed to
// Game::CreateMeshEntityFromGtaFile().

// Walks every root entity in `registry` and returns a SceneDocument
// describing exactly the ones this feature knows how to serialize:
//   - a root entity carrying PrimitiveSource -> a SceneObjectKind::Primitive
//     record (primitiveType copied verbatim from the component).
//   - a root entity carrying MeshAssetSource -> a SceneObjectKind::Asset
//     record, ONLY if `assetDatabase.FindByPath(gtaPath)` actually resolves
//     to a tracked asset (its Guid is copied into the record) - an
//     asset-spawned root whose source *.gta file is no longer tracked by
//     `assetDatabase` (moved/deleted/never imported through it) is SKIPPED
//     entirely, since it has no stable Guid to serialize a reference by.
//   - any other root entity (no PrimitiveSource AND no MeshAssetSource -
//     e.g. the engine's own default Camera entity, see PHASE1) is SKIPPED
//     entirely - it is not part of this feature's serialization scope.
// Every included record's position/rotation/scale is copied directly from
// that root entity's own Transform component (guaranteed present - every
// entity Instantiate() creates always gets one), and its `name` is copied
// from a Name component if present, otherwise left empty. Entities are
// visited in GetChildren()'s own iteration order (creation order, unless a
// Remove() elsewhere has since reshuffled it) - the resulting SceneDocument's
// `objects` order is therefore stable but not independently meaningful.
SceneDocument BuildSceneDocumentFromRegistry(Registry& registry, const AssetDatabase& assetDatabase);

// Destroys every root entity (and, via DestroyEntityAndDescendants(), all
// of its descendants) that BuildSceneDocumentFromRegistry() above would
// have included in a SceneDocument - i.e. every root carrying
// PrimitiveSource OR MeshAssetSource. Everything else (the default Camera
// entity, or any future entity kind this feature doesn't know about) is
// left completely untouched. Call this BEFORE spawning entities from a
// freshly-loaded SceneDocument (see Editor/SceneIO.h's LoadScene()) so a
// Load genuinely REPLACES this feature's own prior content rather than
// merging into it, without ever silently discarding something (like the
// Camera) this feature never owned in the first place.
void ClearSerializableSceneObjects(Registry& registry);

} // namespace gte
```

### 3.2 — `src/Scene/SceneBuilder.cpp` (new file)

```cpp
#include "SceneBuilder.h"

#include "../ECS/Components/MeshAssetSource.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/PrimitiveSource.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/TransformHierarchy.h"

namespace gte {

SceneDocument BuildSceneDocumentFromRegistry(Registry& registry, const AssetDatabase& assetDatabase)
{
    SceneDocument document;

    for (const Entity entity : GetChildren(registry, kInvalidEntity)) {
        const Transform* transform = registry.TryGetComponent<Transform>(entity);
        if (transform == nullptr) {
            continue; // Should never happen in practice - every Instantiate()'d entity gets one.
        }

        SceneObjectRecord record;
        record.position = transform->position;
        record.rotation = transform->rotation;
        record.scale = transform->scale;
        if (const Name* name = registry.TryGetComponent<Name>(entity); name != nullptr) {
            record.name = name->value;
        }

        if (const PrimitiveSource* primitiveSource = registry.TryGetComponent<PrimitiveSource>(entity);
            primitiveSource != nullptr) {
            record.kind = SceneObjectKind::Primitive;
            record.primitiveType = primitiveSource->type;
            document.objects.push_back(record);
        } else if (const MeshAssetSource* meshAssetSource = registry.TryGetComponent<MeshAssetSource>(entity);
            meshAssetSource != nullptr) {
            const AssetRecord* asset = assetDatabase.FindByPath(meshAssetSource->gtaPath);
            if (asset == nullptr) {
                continue; // Not (or no longer) a tracked asset - no stable Guid to serialize by.
            }
            record.kind = SceneObjectKind::Asset;
            record.assetGuid = asset->guid;
            document.objects.push_back(record);
        }
        // else: neither tag present (e.g. the default Camera entity) - not
        // part of this feature's serialization scope, skipped.
    }

    return document;
}

void ClearSerializableSceneObjects(Registry& registry)
{
    // Snapshot roots BEFORE destroying anything - GetChildren(kInvalidEntity)
    // reads live Transform data that DestroyEntityAndDescendants() below
    // mutates as it goes.
    const std::vector<Entity> roots = GetChildren(registry, kInvalidEntity);
    for (const Entity root : roots) {
        const bool isPrimitive = registry.HasComponent<PrimitiveSource>(root);
        const bool isAsset = registry.HasComponent<MeshAssetSource>(root);
        if (isPrimitive || isAsset) {
            DestroyEntityAndDescendants(registry, root);
        }
    }
}

} // namespace gte
```

### 3.3 — `CMakeLists.txt` wiring

Append to the same `src/Scene/*` group PHASE3 already added (right after
`src/Scene/SceneTextFormat.cpp`):

```
    src/Scene/SceneBuilder.h
    src/Scene/SceneBuilder.cpp
```

Still in the main, unconditional source list — this file has no
`GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` dependency at all (it only
needs `Registry`/`AssetDatabase`, both always compiled).

### 3.4 — Tests: `tests/Scene/SceneBuilderTests.cpp` (new file)

Register it in `tests/CMakeLists.txt`'s unconditional `GTE_TEST_SOURCES`
list (right after `Scene/SceneTextFormatTests.cpp`), plus its own
descriptive paragraph in the taxonomy comment block, same convention as
PHASE3's own test file. This file needs a REAL temporary directory (to
build a real `AssetDatabase` against real `*.gta` files) — follow the exact
same pattern already established by `tests/Assets/AssetDatabaseTests.cpp`/
`tests/Editor/ModelRigCacheTests.cpp` (a temp dir created in `SetUp()`,
removed in `TearDown()`, `WriteGtaFile()` used to seed a fake tracked
asset).

Cover, at minimum, for `BuildSceneDocumentFromRegistry()`:
- A hand-built `Registry` with one root entity carrying `Transform` +
  `PrimitiveSource{ PrimitiveType::Sphere }` + `Name{ "MySphere" }` produces
  a `SceneDocument` with exactly one record: `kind == Primitive`,
  `primitiveType == Sphere`, `name == "MySphere"`, and
  position/rotation/scale matching the entity's `Transform` exactly.
- A root entity carrying `Transform` + `MeshAssetSource{ "<real temp
  path>.gta" }`, where a real `AssetDatabase` has ALREADY been
  `RefreshFromDirectory()`'d against a temp directory containing that exact
  `.gta` file (written via `WriteGtaFile()` with a known `Guid`), produces
  exactly one record: `kind == Asset`, `assetGuid` equal to that known Guid.
- **(v2) Path-normalization invariant**: the SAME test above, but
  constructing `MeshAssetSource::gtaPath` from a DELIBERATELY
  differently-spelled-but-equivalent form of the exact same temp file's
  path (e.g. built by joining path segments with the platform separator via
  `std::filesystem::path` operator/ the same way `SDL_GetBasePath()`-derived
  code does today, rather than a raw hand-concatenated string) still
  resolves correctly via `FindByPath()` — proves this bridge's reliance on
  `AssetDatabase`'s own `std::filesystem::absolute()`-based normalization
  (see this phase's "Correctness invariant" note above) actually holds, not
  just for the single literal string `RefreshFromDirectory()` happened to
  produce itself.
- A root entity carrying `MeshAssetSource` whose `gtaPath` does **not**
  resolve via `assetDatabase.FindByPath()` (e.g. the file was never
  imported/tracked) is **skipped** — the resulting document has zero
  records for it.
- A root entity carrying **neither** `PrimitiveSource` nor `MeshAssetSource`
  (e.g. only `Transform` + `Camera`) is **skipped** — proves the Camera
  entity (or any future untagged entity) is correctly excluded.
- A CHILD entity (parented under an asset-spawned root, mirroring
  `Game::CreateMeshEntityFromGtaFile()`'s own multi-part shape) is **never**
  independently visited/produces its own record, even if it happens to also
  carry a `MeshRenderer` — only its root is (or isn't) represented.
- Multiple independent root entities (a mix of Primitive/Asset/skipped
  kinds) all resolve correctly in one call, each producing (or correctly
  not producing) its own record.

Cover, at minimum, for `ClearSerializableSceneObjects()`:
- A `Registry` with one `PrimitiveSource`-tagged root (with a child entity
  under it) and one `MeshAssetSource`-tagged root (with two children under
  it): after calling `ClearSerializableSceneObjects()`, every one of those
  entities (both roots AND every one of their children) reports
  `registry.IsAlive() == false`.
- The SAME `Registry`, additionally holding a Camera-only entity (no
  `PrimitiveSource`/`MeshAssetSource`): after the same call, that Camera
  entity is still `registry.IsAlive() == true` and its `Transform`/`Camera`
  components are unchanged.
- An empty `Registry` (nothing to clear) — a safe no-op, no crash.

## Step 4: What We Will NOT Do (Focus)

- We will **not** have `BuildSceneDocumentFromRegistry()`/
  `ClearSerializableSceneObjects()` touch anything beyond ROOT-level
  entities — see this file's own doc comment above and PHASE0's Design
  Decision #3.
- We will **not** have this module perform ANY filesystem I/O (reading/
  writing the actual `.gtscene` file) or construct/scan an `AssetDatabase`
  itself — both remain the caller's responsibility (a test builds its own
  temp-dir `AssetDatabase`; PHASE5's `Editor/SceneIO.cpp` builds the real
  one against the real Project folder).
- We will **not** give this module any awareness of `Renderer`/`Game` —
  keeping it Tier-1-testable is the entire point of splitting it out from
  PHASE5's glue.
- We will **not** (v2) add our own path-normalization/canonicalization layer
  in this file — the invariant documented above is `AssetDatabase`'s own
  contract to keep; this phase only documents/tests that it's relied upon
  correctly, it does not duplicate that logic here.
