# PHASE4 Completion Report — Editor Save to Asset Persistence

campaign: `verlet-integration-11` (see `PHASE0_MASTER_STRATEGY.md`)
phase: `PHASE4_EDITOR_SAVE_TO_ASSET_PERSISTENCE.md`
status: **DONE** — compiles cleanly (`gte_core` + `GreatTamanaEngineTests`), ready for PHASE5.

## What was implemented

Followed the phase strategy document (`PHASE4_EDITOR_SAVE_TO_ASSET_PERSISTENCE.md`)
step by step:

1. **`src/Game/Physics/DynamicChainPhysicsPersistence.h`** (new file) —
   declares `SaveJointPhysicsOverridesToGtaFile(absoluteGtaPath, chains,
   outErrorMessage = nullptr)`, exactly as specified: pure w.r.t. `chains`,
   no ImGui/Editor dependency, doc comment covering the "full snapshot, not
   a diff" semantics and the "never fabricate a fresh RigFileData on
   decode failure" safety rule.
2. **`src/Game/Physics/DynamicChainPhysicsPersistence.cpp`** (new file) —
   implemented exactly as specified: `ReadGtaFile()` -> verify
   `AssetType::Mesh` -> `DecodeRigDataFromBytes()` (refusing to proceed on
   any of these three failing) -> flattens every joint of every chain into
   one `JointPhysicsOverride` each (keyed by `jointBoneIndices`, mirroring
   `ApplyJointPhysicsOverrides()`'s own PHASE2 key convention) -> replaces
   only `RigFileData::jointPhysicsOverrides` -> `EncodeRigDataToBytes()` ->
   `WriteGtaFile()` with every other header field/payload byte threaded
   straight through from the just-read `gta` unchanged. Reused the exact
   same `Utf8PathFromGamePath()` UTF-8-safe path helper convention
   `MeshAssetGpuCatalog.cpp`/`MaterialTextureGpuCache.cpp` already
   establish (duplicated locally in its own anonymous namespace, per that
   precedent's own stated reasoning for not sharing it yet).
3. **`src/Editor/Panels/InspectorPanel.cpp`** — wired the new "Save Joint
   Physics to Asset" button into the existing "Dynamic Chain Physics"
   section, inside the `if (DynamicChainRig* rig = ...)` block, right after
   the existing Enabled/Freeze checkboxes and their `TextDisabled`
   explanation, and before the `DynamicChainRigCache::ModelEntry* model =
   ...` lookup — exactly the placement the strategy document specifies.
   Two function-local `static` variables
   (`s_lastJointPhysicsSaveSucceeded`/`s_lastJointPhysicsSaveError`) hold
   the button's transient success/failure feedback text, per the strategy
   document's definitive (not left-open) answer to v1's one open question.
   On a successful save, calls PHASE3's
   `meshInstantiationSystem.RefreshCachedJointPhysicsOverridesFromDisk(rig->meshGtaPath)`
   as the final step, closing the "later in the same session" gap
   `PHASE0_MASTER_STRATEGY.md` (Step 2.5) describes. Added
   `#include "../../Game/Physics/DynamicChainPhysicsPersistence.h"`
   alongside the existing `PhysicsSystem.h`/`MeshInstantiationSystem.h`
   includes.

## An additional, necessary fix beyond the strategy document's own text

The strategy document's Step 3.3 assumed `meshInstantiationSystem` was
already in scope at the exact call site it describes. In the ACTUAL current
source tree, the "Dynamic Chain Physics" section lives inside a helper
function, `BuildEntityInspector(Registry&, EditorContext&, [BoneViewerWindow&,]
PhysicsSystem&)` — a function PHASE3 had NOT threaded `MeshInstantiationSystem&`
through, even though PHASE3 already added it to both outer
`BuildInspectorPanel()` signatures. This was caught immediately by the
mandated "Fast Compile Check" (a `'meshInstantiationSystem' was not declared
in this scope` error), not discovered by manual inspection alone — confirming
why this task's workflow always requires an actual compile before declaring a
phase done. Fixed by:

- Adding `MeshInstantiationSystem& meshInstantiationSystem` as a new
  parameter to BOTH `BuildEntityInspector()` overloads (the
  `GTE_ENABLE_PROJECT_PANEL` ON/OFF branches), mirroring exactly how PHASE3
  already added the same parameter to both `BuildInspectorPanel()`
  overloads.
- Updating both of `BuildInspectorPanel()`'s own call sites to
  `BuildEntityInspector()` to forward `meshInstantiationSystem` through.

This is a small, mechanical, purely-additive signature change (no behavior
change to any existing call), not a deviation from the strategy document's
actual design — the save button itself, its placement, and its logic are
implemented exactly as specified.

## Tests added

- **New file — `tests/Game/Physics/DynamicChainPhysicsPersistenceTests.cpp`**
  (Tier 1-adjacent: real disk I/O against a temp file, no GPU/SDL/ImGui
  involved, mirroring `tests/Assets/GtaFileTests.cpp`'s own precedent),
  exactly the four cases the strategy document specifies:
  - `SavesOverridesWithoutDisturbingAnyOtherData` — builds a real, on-disk
    Mesh `*.gta` (3-bone skeleton, 1 material, a real triangle mesh
    payload), saves overrides for 2 joints, and asserts: every header field
    (Guid/Flags/version/Type) is unchanged, the mesh payload is
    byte-for-byte unchanged, skeleton/skin-weight/material counts are
    unchanged, and the new `jointPhysicsOverrides` list matches exactly
    what was passed in.
  - `FailsGracefullyWhenFileDoesNotExist` — a missing file returns `false`
    with a non-empty error message, writes nothing.
  - `FailsGracefullyOnANonMeshAssetType` — an `AssetType::Texture` `*.gta`
    is refused outright.
  - `OverwritingASecondTimeReplacesRatherThanAccumulatingOverrides` —
    saving twice against the same joint REPLACES the override rather than
    appending a second entry.

## Build registration

- `CMakeLists.txt` — added `src/Game/Physics/DynamicChainPhysicsPersistence.cpp`/
  `.h` to `gte_core`'s source list, right after
  `src/Game/Physics/DynamicChainRigCache.h`.
- `tests/CMakeLists.txt` — added
  `Game/Physics/DynamicChainPhysicsPersistenceTests.cpp` to
  `GTE_TEST_SOURCES`, right before `Game/Physics/DynamicChainRigCacheTests.cpp`.

Registered now (not deferred to PHASE5), matching the precedent PHASE2/PHASE3
already set, since this phase's own "Fast Compile Check" workflow step needs
every referenced symbol/test file to actually be buildable.

## Compile verification

Ran a fast, targeted build (not a full project build/regression run, per this
task's instructions):

```
cmake --build build --target gte_core                 -> SUCCESS (0 errors, after the BuildEntityInspector() fix above)
cmake --build build --target GreatTamanaEngineTests    -> SUCCESS (0 errors)
```

Both the engine core static library and the full test binary link cleanly.
Also ran a targeted (not full-suite) test pass covering every test this
campaign has touched so far, rather than running the complete `ctest`
regression this task's workflow explicitly defers to PHASE5:

```
tests\GreatTamanaEngineTests.exe --gtest_filter=*JointPhysicsOverride*:*DynamicChainPhysicsPersistence*:*RigFile*:*MeshAssetGpuCatalog*
-> 22/22 PASSED
```

This covers the new PHASE4 tests, PHASE1's `RigFileTest`/`AssetImporterTest`,
PHASE2's `JointPhysicsOverrideApplicationTest`, and PHASE3's
`MeshAssetGpuCatalogJointOverrideRefreshTest` — all still green, confirming
this phase's changes didn't disturb anything earlier phases already proved.

## What this phase deliberately does NOT do (unchanged from the strategy doc)

- No confirmation modal, undo support, or autosave/dirty-tracking — a single
  explicit button click.
- No "Revert to Saved"/"Reload from Asset" button.
- Does not touch `AssetImporter.cpp`/`PmxLoader.cpp` — this save path only
  ever re-writes an already-imported Mesh `*.gta`, never re-imports from a
  source `.pmx`.
- Does not implement `RefreshCachedJointPhysicsOverridesFromDisk()` itself —
  that's entirely PHASE3's; this phase only adds the one call to it.
- Full `ctest` regression run and end-to-end round-trip tests are
  deliberately deferred to PHASE5, per this task's own workflow rules.

## Next step

**PHASE5_END_TO_END_ROUND_TRIP_REGRESSION_AND_BUILD_REGISTRATION.md** — the
final phase: verify every new file across PHASE1-4 is correctly registered
(already true today — this phase and PHASE3 both registered their own files
proactively), add the full end-to-end round-trip test (detect -> edit ->
save -> fresh `PhysicsSystem` -> reload -> observe saved values) plus the
same-session second-spawn regression test, and run the full build/regression
suite per that phase's own explicit instruction to do so.
