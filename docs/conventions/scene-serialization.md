# Scene Serialization

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The `scene-serialization-1` campaign (`task_manager/scene-serialization-1/`,
`PHASE0_MASTER_STRATEGY.md`) gave the engine its first real Save/Load loop - a
narrow one, only covering `PrimitiveSource`/`MeshAssetSource`-tagged ROOT
entities, backed by a hand-rolled TEXT grammar (`SceneTextFormat.h/.cpp`). The
`scene-serialization-2` campaign (`task_manager/scene-serialization-2/`,
`PHASE0_MASTER_STRATEGY.md`, six phases) REPLACED that narrow system wholesale
with a genuinely generic one: a hand-rolled field-reflection layer
(`src/ECS/Reflection/`), a JSON on-disk format, full-hierarchy save/load
(every entity, not just tagged roots), and two new HTTP endpoints. This
document describes the system as it exists TODAY, post-`scene-serialization-2`
- see that campaign's own `PHASEn_COMPLETION_REPORT.md` files for the full,
phase-by-phase history of how it got here.

## Module layout

- **`src/ECS/Reflection/`** - the generic, opt-in field-reflection core, with
  NO dependency on any specific `ECS/Components/*.h` type:
  - `ComponentTypeDescriptor.h` - `FieldDescriptor` (a field's name + a pair of
    type-erased `writeJson`/`readJson` `std::function`s) and
    `ComponentTypeDescriptor` (a component type's own name + type-erased
    `hasComponent`/`tryGetConstComponent`/`tryGetMutableComponent`/
    `ensureDefaultComponent` callbacks + its `fields` list).
  - `ComponentTypeRegistry.h/.inl/.cpp` - the process-wide table of every
    registered reflectable component type (`Find()`/`AllSortedByTypeName()`),
    plus the generic `RegisterComponentType<T>(typeName, fields)` template
    helper that builds the four type-erased callbacks over a real
    `Registry`. `Instance()` self-bootstraps on first call (see below) - no
    external call site anywhere in `Game`/`Application` is needed to
    register anything.
  - `ReflectFieldMacros.h` - `GTE_REFLECT_FIELD(ComponentType, member)` and
    `GTE_REFLECT_ENUM_FIELD(ComponentType, member, ToStringFn, TryParseFn)`,
    the two macros an actual component registration is built from. Both
    generated `readJson` lambdas treat a missing JSON key as "keep the
    current value" and never throw - any `nlohmann::json` exception is
    caught and turned into a clean `false` + non-empty `errorMessage`.
  - `MathJsonAdapters.h` - free `to_json`/`from_json` ADL functions for
    `Vec3`/`Quat` (each a plain 3/4-element JSON array).
  - `BuiltinComponentReflection.cpp` - **the one place a real, engine-owned
    component type's fields are actually registered.** `RegisterBuiltinComponentReflections()`
    is called exactly once, lazily, the first time anything anywhere calls
    `ComponentTypeRegistry::Instance()` (set the "already bootstrapped" flag
    BEFORE calling it, not after - `RegisterComponentType<T>()` itself calls
    `Instance()` again internally for every type it registers, and that
    nested re-entrant call must see the flag already set or it would recurse
    forever).
- **`src/Scene/`** - the plain-data/format layer, still fully Tier-1-testable
  (no ECS/Renderer/filesystem dependency of its own):
  - `SceneDocument.h` - `SceneEntityRecord` (`parentIndex: std::optional<std::size_t>`
    - a document-local array index, never a raw `Entity` handle;
    `siblingIndex`; `assetGuid` - a `Guid::ToString()`, present only for a
    root spawned from a `MeshAssetSource` "recipe"; `components: nlohmann::json`
    - a generic bag, one key per registered component type this entity
    actually has) and `SceneDocument` (`std::vector<SceneEntityRecord> entities`).
  - `SceneJsonFormat.h/.cpp` - `kSceneJsonFormatVersion = 2`,
    `SerializeSceneDocument()`/`DeserializeSceneDocument()`. The on-disk file
    (still `*.gtscene`, still `Editor/SceneIO.h`'s hardcoded
    `DefaultScenePath()`) is now plain JSON, not the old hand-rolled TEXT
    grammar - see PHASE0's Appendix A for the exact JSON shape. Never throws;
    a malformed/missing/wrong-version file makes `DeserializeSceneDocument()`
    return `std::nullopt`, never a partial parse.
  - **`SceneTextFormat.h/.cpp` and its old `tests/Scene/SceneTextFormatTests.cpp`
    were DELETED by `scene-serialization-2` Phase 3** - if you remember that
    name from an older reading of this codebase, it is gone; `SceneJsonFormat.h/.cpp`
    is its full replacement.
  - `SceneBuilder.h/.cpp` - the ECS-facing bridge, still Renderer-free:
    - `BuildSceneDocumentFromRegistry(Registry&, const AssetDatabase&)` - the
      SAVE half. Recursively walks **every root entity and every one of its
      descendants** (`ECS/TransformHierarchy.h`'s `GetChildren()`), not just
      `PrimitiveSource`/`MeshAssetSource`-tagged roots - this supersedes
      `scene-serialization-1`'s old root-only, tag-only scope entirely. Each
      visited entity's own `components` bag is built ENTIRELY GENERICALLY: for
      every type in `ComponentTypeRegistry::Instance().AllSortedByTypeName()`
      that entity actually has, its `tryGetConstComponent`/`writeJson` is
      called - there is no per-component-type branch anywhere in this
      function, so a future component becomes part of a saved scene
      automatically the moment it registers itself (see "Adding a new
      serializable component field" below). A second pass then resolves
      `assetGuid` for every entity with a live `MeshAssetSource`, via
      `assetDatabase.FindByPath(gtaPath)`.
    - `ClearEntireScene(Registry&)` - unconditionally destroys **every** root
      entity (and, via `DestroyEntityAndDescendants()`, every descendant) -
      i.e. genuinely empties the whole Registry. This REPLACES the old,
      narrower `ClearSerializableSceneObjects()` (which only ever destroyed a
      `PrimitiveSource`/`MeshAssetSource`-tagged root, silently leaving e.g.
      the default Camera untouched) - there is no longer any entity kind this
      feature does not own, so there is nothing left to selectively preserve.
- **`src/Editor/SceneIO.h/.cpp`** (`GTE_ENABLE_EDITOR`-only) - the
  Game/Renderer/filesystem-touching glue:
  - `SaveScene(Game&, const std::filesystem::path&)` / `SaveScene(Game&)` (the
    zero-argument overload forwards to the first with `DefaultScenePath()`,
    unchanged behavior - still what `Editor/DockLayout.cpp`'s Ctrl+S calls).
  - `LoadScene(Game&, Renderer&, const std::filesystem::path&)` /
    `LoadScene(Game&, Renderer&)` (same forwarding shape - still what Ctrl+O
    calls). `LoadScene()`'s reconstruction is the "recipe-spawn
    reconciliation" algorithm - see below.

## Full hierarchy, every entity - not just tagged roots

**Every entity in the Registry is now walked and saved, with its full parent/
child hierarchy preserved** (`parentIndex` = the document-local array index of
the entity that was visited as this one's own `Transform::parent`, or
`std::nullopt` for a root). This supersedes TWO things `scene-serialization-1`
used to do:

- Its old "only a ROOT entity, and only when it carries `PrimitiveSource` OR
  `MeshAssetSource`" scope - a plain empty Transform+Name node, the default
  `Camera` entity, or a future `DirectionalLight` "Sun" entity are all now
  saved and restored too, exactly like a tagged root.
- Its old Design Decision #3 ("a multi-part asset's own child 'submesh part'
  entities are never individually serialized - always freshly re-derived from
  the asset on Load") - a hand-tweaked child part Transform now DOES survive a
  save/load round trip, via the recipe-spawn reconciliation algorithm below.

## Recipe-spawn reconciliation (how a `PrimitiveSource`/`MeshAssetSource` root reloads correctly)

A generic field-copy alone cannot correctly restore a `MeshRenderer` (it holds
live, session-local GPU resource handles - see "What is never reflected"
below) or a multi-part imported mesh's own child "part" entities (freshly
re-derived from the source `*.gta` every time, since that's the only way to
get a REAL, GPU-backed mesh back). `Editor/SceneIO.cpp`'s `LoadScene()` solves
this with a recipe-aware, multi-pass reconstruction (see that file's own
extensive inline comments for the full algorithm, including every edge case
and the exact guard flags used):

1. **`ClearEntireScene()`** wipes the whole Registry first, so the by-Name
   reconciliation below can never observe a stale, pre-Load entity.
2. **Pass A** creates every entity: a record with a `"PrimitiveSource"` key
   spawns via `Game::CreatePrimitiveEntity()` (a real, GPU-backed primitive);
   a record with a resolvable `assetGuid` spawns via
   `Game::CreateMeshEntityFromGtaFile()` (a real, GPU-backed multi-part mesh),
   immediately followed by a **by-Name, first-unused-match reconciliation**
   between that root's own saved children and the live "part" children the
   spawn helper just created - this is what lets a hand-edited (including
   unnamed) child part's Transform survive the round trip. An unresolvable
   `assetGuid` (moved/deleted asset) marks that whole saved subtree as
   unreachable, matching `scene-serialization-1`'s own per-object skip
   behavior. Everything else (Camera/Light/an empty node/an orphaned child)
   becomes a bare entity with a default `Transform` added up front (needed so
   Pass B1's `SetParent()` - which requires a `Transform` on both sides - can
   actually succeed).
3. **Pass B1** wires up parent/child hierarchy (`SetParent(..., worldPositionStays=false)`),
   skipping only an asset root's own successfully-reconciled child (already
   correctly parented by the spawn helper itself) - a reparented recipe ROOT
   still gets its own saved parent applied normally.
4. **Pass B2** applies every entity's saved `components` fields generically,
   via `ComponentTypeRegistry` - this is what restores a hand-edited child
   part's Transform, and every plain entity's Name/Camera/etc fields. A
   `PrimitiveSource` key is skipped for an already-recipe-spawned entity
   (already correct); everything else always applies.
5. **Pass B3** restores each entity's saved sibling index.
6. `Game::EnsureDefaultCameraExists()` is called once more, right before
   returning `true` - harmless/idempotent when a Camera record was present,
   and guarantees a Camera exists immediately rather than only on the next
   rendered frame (this method's own guard checks the LIVE `Camera` component
   count, not a one-shot bool, so it self-heals if a Camera is ever destroyed
   later too).

A malformed/missing/wrong-version scene file makes `LoadScene()` return
`false` and touch nothing - never a partial apply, never a crash.

## Adding a new serializable component field (the whole point of this system)

A future component becomes save/load-capable by registering its own fields
ONCE, in `src/ECS/Reflection/BuiltinComponentReflection.cpp`'s
`RegisterBuiltinComponentReflections()` (or a similarly-shaped `.cpp` file
that function calls out to):

```cpp
RegisterComponentType<MyComponent>("MyComponent", {
    GTE_REFLECT_FIELD(MyComponent, someFloatField),
    GTE_REFLECT_ENUM_FIELD(MyComponent, someEnumField, ToString, TryParseMyEnumName),
});
```

Nothing in `src/Scene/` or `src/Network/` ever needs to change again for it -
`BuildSceneDocumentFromRegistry()`/`LoadScene()` both walk the registry
generically and pick up any newly-registered type automatically. Contrast
this with the OLD `scene-serialization-1` system, which needed a hand-edited
`SceneObjectKind` enumerator, a new `SceneTextFormat.cpp` key, AND a new
branch in both `BuildSceneDocumentFromRegistry()`/`LoadScene()`'s spawn
dispatch for every single new field, forever.

## What is deliberately never reflected (and why)

Five real component types are explicitly NOT registered in
`BuiltinComponentReflection.cpp` - this reasoning is written into that file's
own comments too, so a future contributor does not "helpfully" re-add one of
these without re-reading why it was excluded:

- **`MeshRenderer`** (`MeshHandle`/`PipelineHandle`/`TextureHandle`) - every
  field is a live, session-local, opaque GPU-resource-pool index
  (`Renderer/ResourcePool.h`). Serializing one and reading it back in a LATER
  session would silently reference whatever unrelated resource happens to
  occupy that pool slot this time - a serious, silent-corruption-class bug.
  `MeshRenderer` is always rebuilt fresh by re-running
  `Game::CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()` (see the
  recipe-spawn reconciliation above), never restored from saved data.
- **`MeshAssetSource`** (`gtaPath: std::string`) - a raw filesystem path is
  not a stable enough reference across machines/after a project is moved.
  This is handled instead by resolving it, at Save time, through
  `AssetDatabase` to a stable `Guid`, stored at the `SceneEntityRecord` level
  (`assetGuid`), NOT inside the generic `components` bag.
- **`SkeletalAnimator`** (`meshGtaPath, animationGtaPath: std::string; frame,
  speed: float; playing, loop: bool`) - technically all plain data and COULD
  be safely field-reflected in principle, but deliberately deferred: correctly
  restoring it also needs re-running `Game::PlayAnimationOnEntity()`'s own
  cache-registration side effects (bone-name resolution against the model's
  skeleton), not just a field copy. See `TODO.md`'s "Scene Serialization"
  entry for this well-scoped future follow-up.
- **`DynamicChainRig`** (live physics simulation state - particle positions
  mid-verlet-integration) - restoring stale simulation state from a save file
  would look like a physics glitch on the very next frame, not a feature. A
  freshly loaded scene always starts this component's physics from its own
  natural rest-pose recomputation, exactly like a freshly SPAWNED model does.
- **`ResolvedAnimationPose`** (`pose: std::vector<BoneLocalOffset>`) - purely
  derived, per-frame-recomputed data (written EXCLUSIVELY by
  `AnimationSystem::EvaluatePoses()`, always overwriting wholesale) - nothing
  here is ever meaningful to persist.

`Transform` itself is only ever reflected for its `position`/`rotation`/
`scale` fields - NEVER its `parent`/`siblingIndex` (both `Entity`-shaped or
session-local; hierarchy is instead captured at the `SceneEntityRecord` level
via `parentIndex`/`siblingIndex`, kept visually distinct from plain component
data in the saved JSON).

## Network endpoints

`POST /save_scene` and `POST /load_scene` expose this whole system over the
embedded HTTP server, with an optional `path` JSON field (omitted/empty means
"use `DefaultScenePath()`", same as Ctrl+S/Ctrl+O) - see
[docs/conventions/networking.md](networking.md) for the full request/response
shape, status-code mapping, and the `GTE_ENABLE_EDITOR`-off `503` precedent
both routes follow.

## `nlohmann::json` scope

As of `scene-serialization-2`, `nlohmann::json` is a legitimate dependency of
`src/Scene/` and `src/ECS/Reflection/` - not merely a tool for parsing
untrusted HTTP request bodies anymore (see
[docs/conventions/networking.md](networking.md) for the historical context of
why it was first vendored). It is now this engine's actual on-disk scene file
format AND the value type every reflected component field is serialized
into/out of.
