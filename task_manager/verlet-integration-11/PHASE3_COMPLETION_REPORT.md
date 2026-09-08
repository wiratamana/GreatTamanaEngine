# PHASE3 Completion Report — Same-Session Cache Freshness Infrastructure

campaign: `verlet-integration-11` (see `PHASE0_MASTER_STRATEGY.md`)
phase: `PHASE3_SAME_SESSION_CACHE_FRESHNESS_INFRASTRUCTURE.md`
status: **DONE** — compiles cleanly (`gte_core` + `GreatTamanaEngineTests`), ready for PHASE4.

## What was implemented

Followed the phase strategy document
(`PHASE3_SAME_SESSION_CACHE_FRESHNESS_INFRASTRUCTURE.md`) step by step:

1. **`src/Game/Instantiation/MeshAssetGpuCatalog.h`** — added
   `bool RefreshCachedJointPhysicsOverridesFromDisk(const std::string& absoluteGtaPath);`
   as a new public method, right after the existing `TryGetSkinnedMeshData()`
   declaration, with the exact doc comment the strategy document specifies.
2. **`src/Game/Instantiation/MeshAssetGpuCatalog.cpp`** — implemented it
   exactly as specified: a plain `m_skinnedMeshCache.find()` short-circuit
   (returns `false`, touches nothing, for a path never cached this session),
   then `ReadGtaFile()` + `DecodeRigDataFromBytes()` (returning `false` and
   leaving the existing cached entry untouched on any read/decode failure —
   never destructive), and finally replacing only
   `found->second.jointPhysicsOverrides` in place with the freshly-decoded
   `RigFileData::jointPhysicsOverrides` — never touching mesh geometry,
   skeleton, skin weights, morphs, materials, or any GPU resource. No new
   includes were needed (`AssetTypes.h`/`GtaFile.h`/`RigFile.h` and the
   anonymous-namespace `Utf8PathFromGamePath()` helper were already present
   from `EnsureMeshAsset()`).
3. **`src/Game/Instantiation/MeshInstantiationSystem.h`** — added a one-line
   forwarding accessor, `RefreshCachedJointPhysicsOverridesFromDisk()`,
   mirroring `TryGetSkinnedMeshData()`'s exact shape (deliberately NOT
   `const`, since it mutates the underlying cache).
4. **`src/Game/Game.h`** — added `GetMeshInstantiationSystem()`, mirroring
   `GetPhysicsSystem()`'s exact "Editor observes/acts through a public
   accessor" shape and placement (right after it).
5. **`src/Editor/Panels/InspectorPanel.h`** — added a
   `class MeshInstantiationSystem;` forward declaration alongside the
   existing ones, and a new `MeshInstantiationSystem& meshInstantiationSystem`
   parameter to BOTH `BuildInspectorPanel()` overloads (present in every
   signature, not gated behind `GTE_ENABLE_PROJECT_PANEL`, exactly like
   `physicsSystem` already is) — updated the doc comment to explain why.
6. **`src/Editor/Panels/InspectorPanel.cpp`** — added
   `#include "../../Game/Instantiation/MeshInstantiationSystem.h"` alongside
   the existing `PhysicsSystem.h` include, and updated both
   `BuildInspectorPanel()` definitions' parameter lists to match the header
   exactly. The parameter is threaded through but not yet used anywhere
   inside the function body — that's PHASE4's job (the one call site that
   actually needs it, right after a successful save).
7. **`src/Editor/ImGuiEditorLayer.cpp`** — updated both call sites (the
   `GTE_ENABLE_PROJECT_PANEL` ON/OFF branches) to pass
   `game.GetMeshInstantiationSystem()` alongside the existing
   `game.GetPhysicsSystem()`.

## Tests added

- **New file — `tests/Game/Instantiation/MeshAssetGpuCatalogJointOverrideRefreshTests.cpp`**
  (Tier 1, zero Renderer/GPU dependency): `RefreshingAPathNeverCachedThisSessionIsANoOpThatReturnsFalse`
  — proves the one branch that is genuinely testable without a live Renderer
  (an empty `MeshAssetGpuCatalog` with nothing ever cached) returns `false`
  and needs no real `*.gta` file at all.
- The OTHER branch (an already-cached path, refresh actually replacing the
  field) requires a real Renderer/GPU device to populate the cache in the
  first place via `EnsureMeshAsset()`/`Resolve()` — `MeshAssetGpuCatalog`'s
  own existing class doc comment already states this plainly ("Tier 2, no
  automated coverage yet"), an accepted, pre-existing, project-wide bucket
  per `AGENTS.md`'s testing tiers, not something this phase is expected to
  close. Left for manual Editor verification once PHASE4's save button
  exists to actually exercise it end-to-end (see PHASE5's own cross-check
  step) — the exact checklist from the strategy document (spawn twice, edit +
  save on one, spawn a third from the same path in the same session, confirm
  it shows the saved values) is recorded there for PHASE5 to run.

## Build registration

Registered the new test file now (not deferred to PHASE5), matching the
precedent PHASE2 already set for its own new files, since a phase's own
"Fast Compile Check" workflow step needs every referenced symbol/test file to
actually be buildable:

- `tests/CMakeLists.txt` — added
  `Game/Instantiation/MeshAssetGpuCatalogJointOverrideRefreshTests.cpp` to
  `GTE_TEST_SOURCES`, right after `Game/EntityInstantiatorTests.cpp`.

No `CMakeLists.txt` (engine sources) change was needed — every `.h`/`.cpp`
file touched this phase (`MeshAssetGpuCatalog.h/.cpp`,
`MeshInstantiationSystem.h`, `Game.h`, `InspectorPanel.h/.cpp`,
`ImGuiEditorLayer.cpp`) was already a registered build source.

## Compile verification

Ran a fast, targeted build (not a full project build/regression run, per this
task's instructions):

```
cmake --build build --target gte_core                 -> SUCCESS (0 errors)
cmake --build build --target GreatTamanaEngineTests    -> SUCCESS (0 errors)
```

Both the engine core static library and the full test binary link cleanly.
Also ran a targeted (not full-suite) test pass to sanity-check the new
wiring and every other test this campaign has touched so far, rather than
running the complete `ctest` regression this task's workflow explicitly
defers to a later phase:

```
tests\GreatTamanaEngineTests.exe --gtest_filter=*JointPhysicsOverride*:*RigFile*:*AssetImporter*:MeshAssetGpuCatalog*
-> 32/32 PASSED
```

This covers the new PHASE3 test, every PHASE1 (`RigFileTest`/`AssetImporterTest`)
test, and every PHASE2 (`JointPhysicsOverrideApplicationTest`) test — all
still green, confirming this phase's changes didn't disturb anything earlier
phases already proved.

## What this phase deliberately does NOT do (unchanged from the strategy doc)

- Does not call `RefreshCachedJointPhysicsOverridesFromDisk()` anywhere in
  production code yet — `SaveJointPhysicsOverridesToGtaFile()` doesn't exist
  until PHASE4, which is also where the one real call site is added.
- Does not change `MeshAssetGpuCatalog::EnsureMeshAsset()` itself in any way
  — the FIRST load of any given path was already correct (PHASE1).
- Does not add any new Editor UI/button — `InspectorPanel.cpp`'s function
  bodies are otherwise completely unchanged this phase; only the new
  parameter was threaded through the two signatures.
- Does not attempt to refresh/invalidate `m_meshAssetCache` (the GPU
  `MeshAssetPart` list) or anything skeleton/skin-weight/morph/material-
  related — out of scope, since this campaign never changes any of that
  data.

## Next step

**PHASE4_EDITOR_SAVE_TO_ASSET_PERSISTENCE.md** — add
`SaveJointPhysicsOverridesToGtaFile()`
(`src/Game/Physics/DynamicChainPhysicsPersistence.h/.cpp`), wire a new "Save
Joint Physics to Asset" button into `Panels/InspectorPanel.cpp`'s "Dynamic
Chain Physics" section, and call this phase's
`meshInstantiationSystem.RefreshCachedJointPhysicsOverridesFromDisk()` as the
final step of a successful save.
