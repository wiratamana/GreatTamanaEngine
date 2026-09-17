# PHASE0 — MASTER STRATEGY: Generic Scene Serialization v2 + Save/Load Network Endpoints

_Campaign folder: `task_manager/scene-serialization-2/`. This file is the
ORCHESTRATOR for every other `PHASEn_*.md` file in this same folder. Read this
file FIRST, always. Every child phase file links back to this one and must be
read in numeric order — PHASE2 assumes PHASE1 already landed, etc._

Branch: stay on `feature/scene-serialization` for this entire campaign (every
phase). Do not create a new branch per phase.

---

## Step 1: The Goal (Where are we going?)

The engine already has a first, working Save/Load loop
(`task_manager/scene-serialization-1/`, see `docs/conventions/scene-serialization.md`
and `AGENTS.md`'s "Scene Serialization" section). It is real, but narrow. This
campaign has three concrete deliverables:

1. **Two new HTTP endpoints, `POST /save_scene` and `POST /load_scene`** — a
   thin network bridge to the EXISTING save/load system (`Editor/SceneIO.h`'s
   `SaveScene()`/`LoadScene()`), exactly the same shape as every other
   ECS-mutating endpoint this engine already has (`/instantiate_primitive`,
   `/instantiate_asset`, ...). An AI/tool caller must be able to trigger a
   save or load over HTTP instead of only via Ctrl+S/Ctrl+O.
2. **Fix the two concrete, reported serialization gaps**:
   - Entity Transform (translation/rotation/scale) is NOT reliably preserved
     for every kind of entity today (only for `PrimitiveSource`/`MeshAssetSource`
     ROOT entities — see Step 2 below for exactly why).
   - Other component properties — the reported concrete example is
     `Camera::nearZ`/`farZ` (also `fovYDegrees`) — are never serialized at all
     today, because `Camera` entities are entirely out of this feature's scope.
3. **Make the WHOLE system scale like Unity's `[SerializeField]`**: a future
   contributor adding a brand-new component type must be able to make it
   save/load-capable by registering its own fields ONCE, in a small, obvious
   place — never by editing three different existing files (`SceneDocument.h`,
   `SceneBuilder.cpp`, `SceneTextFormat.cpp`) by hand for every single new
   component, which is what today's design requires.

None of this is a "nice to have" wrapped around the existing system — item 3
requires a genuine new sub-system (a small, hand-rolled C++ field-reflection
layer), which items 1 and 2 fall out of as natural consequences once it
exists. This is why this campaign is 6 phases, not 2.

---

## Step 2: The Situation / The Problem (Where are we now?)

Investigated directly against the real code (`src/Scene/`, `src/ECS/`,
`src/Editor/SceneIO.*`, `src/Network/`, `src/Application/EngineCommandBridge.*`)
before writing this strategy. Exact findings:

- **`Scene/SceneBuilder.cpp`'s `BuildSceneDocumentFromRegistry()` only ever
  walks ROOT entities (`GetChildren(registry, kInvalidEntity)`), and only ever
  emits a record for a root that carries `PrimitiveSource` OR
  `MeshAssetSource`.** Every other root — the engine's own default `Camera`
  entity, a future `DirectionalLight`/"Sun" entity, or a plain empty
  Transform+Name node — is silently skipped. Transform IS actually copied
  into the record for the roots that DO qualify (`record.position/rotation/scale
  = transform->position/rotation/scale`) and DOES round-trip today for THOSE
  roots — so "Transform never serializes" is only true for the (very common,
  since Camera/Light are exactly what's excluded) entities this feature
  doesn't even attempt today. Fixing this properly means widening scope to
  every entity, not patching one `if` statement.
- **A multi-part imported mesh's own CHILD "submesh part" entities are never
  individually serialized** — only the root's Guid+Transform is saved; every
  child is destroyed and freshly re-derived from the asset again at Load
  time (`scene-serialization-1`'s own documented Design Decision #3). A
  hand-tweaked child part Transform does not survive a save/load round trip.
- **`Camera`/`DirectionalLight` components are never touched by this feature
  at all** — hence `nearZ`/`farZ`/`fovYDegrees` (and light color/lux) are
  never serialized, exactly the second reported gap.
- **The on-disk format (`Scene/SceneTextFormat.h/.cpp`) is a hand-rolled,
  fixed-schema TEXT format**: a literal `OBJECT`/`kind=`/`position=`/`END`
  grammar with a hardcoded set of recognized keys. Adding one new
  serializable field today means: (1) add a field to
  `SceneObjectRecord` (`SceneDocument.h`), (2) add a `text += "key=..."` line
  to `SerializeSceneDocument()`, (3) add an `else if (key == "...")` parse
  branch to `DeserializeSceneDocument()`, (4) update
  `BuildSceneDocumentFromRegistry()`/`Editor/SceneIO.cpp`'s `LoadScene()` to
  actually copy the new field to/from the live component. FOUR hand-edited
  spots, in three different files, for every single new field, on every
  single new component, forever. This is precisely the scaling problem the
  user flagged (contrast with Unity's `[SerializeField]`, which needs zero
  changes anywhere else once a field is marked).
- **There is no reflection of any kind in this engine's ECS today.**
  `Registry`/`ComponentStorage<T>`/`IComponentPool` (`src/ECS/`) are
  deliberately minimal — `IComponentPool` only has `Remove()`/`Has()`, no
  hook for "describe your own fields". This is a real gap this campaign must
  fill with NEW code, not something to work around.
- **There is no HTTP endpoint for save/load at all today** — only the
  Editor's `File > Save Scene`/`File > Open Scene` menu items
  (`Editor/DockLayout.cpp`) call `Editor/SceneIO.h`'s `SaveScene()`/
  `LoadScene()` directly, in-process. `SceneIO.h` is compiled ONLY when
  `GTE_ENABLE_EDITOR` is ON (it depends on `Editor/ProjectRootPath.h`).
- **`nlohmann::json` is already vendored** (`cmake/FetchJson.cmake`) and used
  today, but ONLY inside `src/Network/NetworkRoutes.cpp` for parsing
  untrusted HTTP request bodies — never as an on-disk file format, and never
  included from any `src/Scene/` or `src/ECS/` header. This campaign
  DELIBERATELY widens that boundary (an explicit, reviewed decision — see
  "Locked Design Decisions" below), because a hand-rolled generic reflection
  system genuinely needs a naturally-nestable value format, and re-inventing
  that from scratch when a correct, already-vendored one exists would be
  pure waste.

### Locked Design Decisions (from user consultation — do not re-litigate)

1. **On-disk scene format switches from the hand-rolled TEXT grammar to
   JSON.** The file keeps the same name/extension (`TestScene.gtscene`,
   `Editor/SceneIO.h`'s `DefaultScenePath()`) — only the BYTES inside it
   change shape. `nlohmann::json` is now a legitimate dependency of
   `src/Scene/` (and the new `src/ECS/Reflection/` module) — this
   supersedes `docs/conventions/networking.md`'s older, narrower framing of
   `nlohmann::json` as "only for parsing untrusted network input"; Phase 6
   updates that doc comment to describe the widened scope accurately.
2. **Scope widens to EVERY entity in the Registry, with full parent/child
   hierarchy preserved** — not just `PrimitiveSource`/`MeshAssetSource`
   roots. This supersedes `scene-serialization-1`'s own Design Decision #3
   ("a multi-part asset's child part Transforms are never preserved") —
   Phase 4 changes this specific, documented behavior on purpose.
3. **A genuine, generic, opt-in field-reflection system is built** — each
   component type registers a small list of its own fields (name + a
   to/from-JSON conversion) in ONE place; a fully generic engine walks that
   list for every entity that has that component. A future component becomes
   serializable by adding ~10 lines to the reflection registration, and nothing
   in `src/Scene/` or `src/Network/` ever needs to change again for it.
4. **`POST /save_scene` / `POST /load_scene` accept an OPTIONAL `path` JSON
   field.** Omitted/empty means "use the existing hardcoded default"
   (`Editor/SceneIO.h`'s `DefaultScenePath()` — unchanged, still what
   Ctrl+S/Ctrl+O use). A caller-supplied path is used EXACTLY as given
   (absolute, or resolved against the engine process's own working
   directory if relative) — the SAME "caller's responsibility, no sandboxing"
   convention `POST /instantiate_asset`'s `gta_path` field already
   established (see `docs/conventions/networking.md`), for consistency.
5. **Breaking the old `.gtscene` TEXT format is explicitly OK.** The format
   version is bumped; an old file written by `scene-serialization-1` will no
   longer parse. This is accepted (WIP feature, no real users of the old
   format), not something any phase needs to migrate/support.
6. **`save_scene`/`load_scene` follow the EXACT SAME `GTE_ENABLE_EDITOR`-off
   precedent `POST /import_asset` already established**: when the Editor
   module isn't compiled in, the route responds `503` with a clear message —
   never a compile error, never a crash, never a silent no-op with `200`.

### Cross-Phase Invariants (true at the end of EVERY phase, not just the last)

- **The engine must compile after every phase**, in both a
  `GTE_ENABLE_EDITOR=ON` and `GTE_ENABLE_EDITOR=OFF` configuration where a
  phase touches anything outside `src/Editor/`. A fast compile check (not a
  full rebuild/regression run) closes out every phase except the last.
- **Never crash, never throw across a phase's own new public API.** Every
  existing "degrade gracefully" precedent in this codebase (an unresolvable
  parent name, a moved asset file, a malformed JSON body, ...) must be
  matched by this campaign's new code — a malformed/missing/older-version
  scene file must fail `LoadScene()` cleanly (return `false`, touch nothing),
  never partially apply, never crash.
- **No component that references a live GPU resource by handle
  (`MeshRenderer`'s `MeshHandle`/`PipelineHandle`/`TextureHandle`) is EVER
  registered in the generic reflection system.** A handle is a
  session-local, opaque resource-pool index (see `Renderer/ResourcePool.h`)
  — it is never meaningful across a save/load round trip. `MeshRenderer` is
  always reconstructed by re-running the existing spawn helpers
  (`Game::CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()`), driven
  by the (separately, explicitly handled) `PrimitiveSource`/`MeshAssetSource`
  "recipe" tag components — never by generically reflecting `MeshRenderer`
  itself. Getting this wrong (reflecting a handle field) would silently
  corrupt every future session's GPU resource pool. See PHASE2's own
  "Explicitly Out Of Scope" list for the full, per-component reasoning.
- **`AGENTS.md`'s Testability rule applies**: every new Tier-1-testable
  module (the reflection core, the new JSON scene format) gets its own
  `tests/` coverage in the SAME phase that introduces it — not deferred to
  Phase 6 "if there's time".

---

## Step 3: The Plan (Phase Index)

Each phase below is its own `PHASEn_*.md` file in this same folder. Read each
one fully before starting it — do not skip ahead. Each phase's own file
contains the actual step-by-step implementation detail; this index is only a
map.

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_REFLECTION_CORE_AND_MATH_JSON_ADAPTERS.md` | New `src/ECS/Reflection/` module: `FieldDescriptor`/`ComponentTypeDescriptor`/`ComponentTypeRegistry`, the `GTE_REFLECT_FIELD`/`GTE_REFLECT_ENUM_FIELD` macros, and `Vec3`/`Quat` JSON adapters. No engine behavior changes yet — this is pure new infrastructure, fully unit-tested against a throwaway test-only component. |
| 2 | `PHASE2_BUILTIN_COMPONENT_REFLECTION_REGISTRATION.md` | Registers `Transform`, `Name`, `Camera`, `DirectionalLight`, `PrimitiveSource` into the Phase 1 registry. Documents, explicitly, WHY `MeshRenderer`/`MeshAssetSource`/`SkeletalAnimator`/`DynamicChainRig`/`ResolvedAnimationPose` are deliberately NOT registered. Still no change to `src/Scene/`/`src/Editor/` — this closes the "which components are reflectable" question before any file format touches it. |
| 3 | `PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md` | Replaces `SceneDocument.h`'s flat `objects` list with a hierarchy-aware, generic-component-bag shape; replaces `SceneTextFormat.h/.cpp` with `SceneJsonFormat.h/.cpp`; rewrites `BuildSceneDocumentFromRegistry()` to walk the ENTIRE registry (every entity, full parent/child tree) generically. Also lands a WORKING (if intentionally simplified) generic `LoadScene()` — every entity round-trips generically EXCEPT `PrimitiveSource`/`MeshAssetSource` "recipe" entities, which is Phase 4's job. The engine compiles and a plain Camera/Light/empty-node save+load round trip already works after this phase. |
| 4 | `PHASE4_RECIPE_SPAWN_RECONCILIATION_AND_LOAD_CORRECTNESS.md` | Fixes the deferred gap: `PrimitiveSource`/`MeshAssetSource` entities are reconstructed via the EXISTING `Game::CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()` spawn helpers (so their real GPU `MeshRenderer` comes back correctly), then RECONCILED against the saved document so their (and their children's) Transform/Name are restored to the exact saved values — the two-pass algorithm this phase documents in full. Also replaces `ClearSerializableSceneObjects()` with a simpler "destroy everything" `ClearEntireScene()`, and fixes `Game::EnsureDefaultCameraExists()`'s one-shot guard so the scene never ends up cameraless after a Load. |
| 5 | `PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md` | `POST /save_scene`/`POST /load_scene` — two new `EngineCommandKind` values on the existing `EngineCommandBridge`, `NetworkRoutes.h/.cpp` parsing/response-building, `NetworkServer.cpp` route registration, `EngineCommandDispatch.cpp` dispatch (with the `GTE_ENABLE_EDITOR`-off 503 path), `docs/conventions/networking.md` update. |
| 6 | `PHASE6_TESTS_DOCS_CLEANUP_AND_FULL_REGRESSION.md` | Deletes the now-dead `SceneTextFormat.*`/its old test file, updates every stale doc (`docs/conventions/scene-serialization.md`, `AGENTS.md`'s summary, `TODO.md`), adds/rewrites every remaining Tier-1 test, and — ONLY in this last phase — runs the FULL build + `ctest` regression pass. |

Every phase file ends with its own "Definition of Done" checklist and a
reminder to write a `PHASEn_COMPLETION_REPORT.md` (mirroring
`scene-serialization-1`'s own precedent) and commit.

---

## Appendix A: Final On-Disk Format Shape (locked, referenced by Phases 3/4/6)

```jsonc
{
  "gtscene_version": 2,
  "entities": [
    {
      // null for a root entity; otherwise the 0-based index of this
      // entity's OWN parent within this SAME "entities" array. Order in
      // the array carries no other meaning - a parent may appear before OR
      // after any of its children.
      "parent": null,
      "sibling_index": 0,
      // OPTIONAL. Present ONLY for a root entity that was spawned from a
      // MeshAssetSource "recipe" (see PHASE4) - the stable AssetDatabase
      // Guid (32 lowercase hex chars, Guid::ToString()'s own format) this
      // root's *.gta file resolves to. Absent for every other entity.
      "asset_guid": "0123456789abcdef0123456789abcdef",
      // Generic, arbitrary component bag - one key per REGISTERED
      // component type (see PHASE1/PHASE2's ComponentTypeRegistry) that
      // this entity actually has. An unrecognized key (e.g. a NEWER
      // engine build's component this OLDER build doesn't know about) is
      // silently ignored at Load time - forward-compatible, matching every
      // other "unrecognized input" precedent in this codebase.
      "components": {
        "Transform": { "position": [0.0, 0.0, -5.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0] },
        "Name": { "value": "Main Camera" },
        "Camera": { "fovYDegrees": 60.0, "nearZ": 0.1, "farZ": 1000.0, "active": true }
      }
    }
  ]
}
```

## Appendix B: Full-Text Cross-Reference Index

- Existing code read/understood in full before writing this strategy:
  `src/Scene/SceneDocument.h`, `SceneBuilder.h/.cpp`, `SceneTextFormat.h/.cpp`,
  `src/Editor/SceneIO.h/.cpp`, `src/ECS/Registry.h`, `ComponentStorage.h`,
  `Entity.h`, `TransformHierarchy.h`, `EntityQuery.h`, `EntityManager.h`,
  `src/ECS/Components/{Transform,Camera,DirectionalLight,Name,PrimitiveSource,
  MeshAssetSource,MeshRenderer,SkeletalAnimator,DynamicChainRig,
  ResolvedAnimationPose}.h`, `src/Game/Game.h/.cpp` (in particular
  `EnsureDefaultCameraExists()`), `src/Game/EngineCommandResults.h`,
  `src/Application/EngineCommandBridge.h`, `src/Application/
  EngineCommandDispatch.cpp`, `src/Network/NetworkRoutes.h`,
  `src/Network/NetworkServer.cpp` (in particular the `/import_asset` route's
  `GTE_ENABLE_EDITOR`-off `503` handling, which Phase 5 mirrors),
  `src/Assets/AssetTypes.h` (`Guid`), `docs/conventions/scene-serialization.md`,
  `docs/conventions/networking.md`, `AGENTS.md`.
- Every phase file below assumes the reader has NOT independently re-read all
  of the above — each phase file re-states exactly what it needs, inline.
