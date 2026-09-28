# PHASE3 COMPLETION REPORT — Shared Reconstruction Function & HOOK POINT B: Restore

Campaign: `editor-core-separation-15` (Project Assembly Hot Reload plan, BIG-STEP 4).
Phase file: `PHASE3_SHARED_RECONSTRUCTION_FUNCTION_AND_HOOK_POINT_B_RESTORE.md`.

## What was actually built

All edits described in PHASE3's own plan were made, after re-reading the WHOLE
current body of `Editor::LoadScene()` via `read_file` first (not partial
`read_line`), and re-verifying PHASE1/PHASE2's own completion reports against
the real, current source before building on top of them (`Core::GetGame()`/
`GetAssetDatabase()`/`GetProjectAssemblyHost()`, `PerformProjectAssemblyHotReload()`'s
7-parameter signature, `HotReloadStateSnapshot::document`, and the
`RestoreProjectAssemblyHotReloadState(core, snapshot)` no-op stub were all
confirmed live before any change):

1. **`src/Scene/SceneBuilder.h`** (gte_core-tier):
   - The stale "Deliberately still Renderer-free" header comment (original
     lines 8-15) was corrected to state the TRUE, current fact: this file now
     also owns the LOAD half (`ReconstructSceneFromDocument()`), needs a live
     `Renderer`, and this is not a layering problem since `Renderer` has
     always been gte_core-tier — mirrors Unity's own `SceneManager.LoadScene()`
     being a Player-build capability, not merely an Editor one.
   - Added forward declarations `class Game;`/`class Renderer;` and the new
     function declaration, `ReconstructSceneFromDocument(Game&, Renderer&,
     const SceneDocument&, const AssetDatabase&) -> void`, appended
     immediately after `ClearEntireScene()`, with a full doc comment
     (caller's-own-responsibility note for `ClearEntireScene()`, recipe-aware
     summary, pointer to the original `LoadScene()` body as the algorithm's
     proof, and the "also calls `EnsureDefaultCameraExists()`" guarantee) —
     matching the phase file's own specified comment content.

2. **`src/Scene/SceneBuilder.cpp`** (gte_core-tier):
   - Added the four new `#include`s the phase file specified explicitly
     (`../ECS/Components/Name.h`, `../Game/Game.h`,
     `../Renderer/Primitives/PrimitiveMeshGenerator.h`,
     `../Renderer/Renderer.h`) plus `<functional>` (for
     `std::function<void(std::size_t)> markSubtreeUnresolved`) — matching
     `Editor/SceneIO.cpp`'s own explicit-include discipline rather than
     relying on any transitive include path, exactly as instructed.
   - Added `ReconstructSceneFromDocument()`'s real body: a byte-for-byte
     faithful copy of `Editor::LoadScene()`'s prior body (from
     `Registry& registry = game.GetRegistry();` through, but not including,
     `return true;`), with EXACTLY the three mechanical adjustments the phase
     file specified and nothing else:
     1. `document->` → `document.` throughout (parameter is now a
        `const SceneDocument&`, not a `std::optional`).
     2. The `ClearEntireScene(registry);` call and its surrounding comment
        block were dropped — this responsibility now belongs to each of the
        two real callers.
     3. The `ResolveProjectRootDirectory()`/fresh-`AssetDatabase`-scan block
        was dropped — the function now receives an already-scanned
        `AssetDatabase` as a parameter.
     Every comment, every guard flag (`consumedByRecipe`/
     `alreadyParentedByRecipe`/`childrenOf`/`markSubtreeUnresolved`), and
     every Pass A/B1/B2/B3 step was copied unchanged — no algorithmic
     "improvement" was made during the move. The function ends with the
     same `game.EnsureDefaultCameraExists();` call `LoadScene()` had, and
     returns `void` (the `return true;` was dropped, since the caller
     already knows the document parsed successfully).
   - One tiny, non-behavioral fix made in-flight: a dangling unmatched
     parenthesis in the copied final comment ("... this a no-op in that
     case." → "... this a no-op in that case).") was corrected while
     copying — pure comment punctuation, not a code/behavior change.

3. **`src/Editor/SceneIO.cpp`** (gte_editor-tier): `LoadScene()`'s body was
   replaced with the thin wrapper the phase file specified verbatim: parse
   the file, resolve the project root, scan a fresh `AssetDatabase`, call
   `ClearEntireScene(game.GetRegistry())`, then call
   `ReconstructSceneFromDocument(game, renderer, *document, assetDatabase)`,
   and return `true`. The file's own `#include` list was pruned of
   everything only the now-removed reconstruction body needed (`Name.h`,
   `Transform.h`, `ComponentTypeRegistry.h`, `Registry.h`,
   `TransformHierarchy.h`, `PrimitiveMeshGenerator.h`, `<functional>`) —
   `SaveScene()`'s own body and includes were left completely untouched.

4. **`src/Core/Plugins/ProjectAssemblyHotReload.h`/`.cpp`** (gte_core-tier):
   `RestoreProjectAssemblyHotReloadState()` (HOOK POINT B) gained its real
   body, exactly as the phase file specified: a new `Renderer&` parameter, a
   new `const std::filesystem::path& projectRootDirectory` parameter (named
   in the header, commented out as `/*projectRootDirectory*/` in the `.cpp`
   definition since the body deliberately does not use it — matching this
   same file's own established unused-parameter convention, e.g.
   `Core& /*core*/` on the old stub), a call to
   `ClearEntireScene(core.GetGame().GetRegistry())` followed by
   `ReconstructSceneFromDocument(core.GetGame(), renderer, snapshot.document,
   core.GetAssetDatabase())`, and a `GTE_LOG_INFO` reporting the restored
   entity count. `PerformProjectAssemblyHotReload()`'s own call site was
   updated from `RestoreProjectAssemblyHotReloadState(core, snapshot);` to
   `RestoreProjectAssemblyHotReloadState(core, renderer, snapshot,
   projectRootDirectory);` — `renderer`/`projectRootDirectory` were both
   already in scope at that exact call site (the former is this whole
   function's own second parameter; the latter was threaded through by
   PHASE1). No other line of `PerformProjectAssemblyHotReload()`'s own
   control flow was touched.

5. **`docs/conventions/scene-serialization.md`**: updated exactly the
   sentences this Step's own code move invalidated, and nothing else:
   - The `SceneBuilder.h/.cpp` bullet's opening line no longer claims "still
     Renderer-free" — it now states the file needs a live `Renderer`,
     explains why that is not a layering problem, and names both real
     callers of `ReconstructSceneFromDocument()`.
   - A new sub-bullet documents `ReconstructSceneFromDocument()` itself
     (signature, what it does, what it deliberately does NOT do — clear the
     scene or scan an `AssetDatabase`).
   - The `Editor/SceneIO.h/.cpp` bullet's `LoadScene()` description now states
     it is a thin wrapper, naming its four remaining steps.
   - The "Recipe-spawn reconciliation" section's opening sentence now
     attributes the algorithm to `Scene/SceneBuilder.cpp`'s
     `ReconstructSceneFromDocument()`, not `Editor/SceneIO.cpp`'s
     `LoadScene()` alone.
   - The file's own unrelated, pre-existing `GTE_ENABLE_EDITOR` staleness was
     deliberately left untouched, exactly as the phase file instructed — it
     predates this campaign and is out of scope here.

## Deviations from the plan

None of substance. Every cited line number in the phase file (and in
`SceneIO.cpp`'s own body, referenced as "lines 55-386") had drifted slightly
by the time of actual editing — re-confirmed live via `read_file` before every
edit, exactly as the phase file's own instructions required ("re-confirm them"
— Section 2.3/PHASE3 preamble). One tiny, deliberately-flagged, non-behavioral
fix was made while copying the algorithm body: a dangling unmatched
parenthesis in one comment (see item 2 above) — this is a comment-only
correction, not an algorithmic change, and is called out here explicitly per
this phase's own "fix bugs, if any are found, only as their own,
separately-justified, clearly-called-out change" rule (this is not a "bug" in
the algorithm itself, just a typo in a comment describing it, but is
disclosed anyway for full transparency).

## New gaps found

None beyond what PHASE0/PHASE3 already documented as explicitly out of scope
for this phase (a genuinely runtime-mutated custom-component test — PHASE4's
job; `AssetDatabase` unification; scoped per-project-only restore; a new
GPU-dependent test for `ReconstructSceneFromDocument()` itself, which the
phase file explicitly says NOT to add).

## Verification performed

1. **Incremental build** (`cmake --build build`, working directory the repo
   root): succeeded cleanly on the first attempt after all edits — only the
   affected files and their direct dependents were rebuilt/relinked
   (`gte_core`, `gte_editor`, `GreatTamanaEditor.exe`,
   `GreatTamanaEngineTests.exe`, and both `ProjectAssemblyProbe` `.dll`s). No
   new warnings.
2. **Full existing scene-serialization Tier-1/Tier-2 test suite**
   (`tests/GreatTamanaEngineTests.exe --gtest_filter=*Scene*`): **58 of 58
   tests passed**, with **zero changes to any test file's own body**
   (`SceneBuilderTests.cpp`, `SceneJsonFormatTests.cpp`,
   `SceneRoundTripIntegrationTests.cpp`, plus every other test whose name
   happens to contain "Scene" — `ProjectAssemblyHotReloadSceneSnapshot*`,
   `SceneGridMathTest`, `EditorCameraTest`, `ComputeAerialPerspectiveComposite*`,
   `ParseScenePathRequestTests`, `BuildScenePathResponseJsonTests`). Per the
   phase file's own explicit note, `SceneRoundTripIntegrationTests.cpp`
   reproduces the Pass A/B1/B2/B3 sequence directly (minus recipe spawning,
   which needs a live `Renderer`) against a plain `Registry` and was
   confirmed to still pass unchanged — the correct, permanent regression
   guard for the underlying algorithm.
3. **Live verification #1 — byte-for-byte Ctrl+S/Ctrl+O round-trip**, against
   a real running `GreatTamanaEditor.exe` (`run_app_background` →
   `gte_send_request` → `stop_app_background`):
   - `POST /instantiate_primitive` spawned `CubeParent` (a Cube at world
     `(1,2,3)`), then `SphereChild` (a Sphere at world `(5,6,7)`, parented
     under `CubeParent` — resolving to local `(4,4,4)`).
   - `GET /project_assembly/debug/scene_snapshot` (reference, before save):
     3 entities — Camera, `CubeParent` (root, position `(1,2,3)`),
     `SphereChild` (parent index 1, local position `(4,4,4)`).
   - `POST /save_scene` (empty body → `DefaultScenePath()`): `200`,
     `resolved_path` under `build/Project/TestScene.gtscene`.
   - `POST /load_scene` (empty body → same default path): `200`.
   - `GET /project_assembly/debug/scene_snapshot` (after load): the SAME 3
     entities, with byte-for-byte-identical Name/Transform/Camera field
     values and the SAME parent/child relationship (`SphereChild`'s parent
     resolved to `CubeParent`'s new entity index, local position still
     exactly `(4,4,4)`) — only the raw entity array order/index numbers
     differed, which is expected and harmless (`Entity` handles are
     session-local, never serialized directly, per this system's own
     pre-existing design).
4. **Live verification #2 — HOOK POINT B isolation test**:
   - `POST /instantiate_primitive` spawned `HotReloadProbeCone` (a Cone) at a
     distinctive world position, `(123, 456, 789)`.
   - `GET /project_assembly/debug/scene_snapshot` confirmed it present at
     exactly that position, unparented.
   - `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` (no source
     change): `200`, `"last_outcome":"Success"`.
   - `GET /project_assembly/debug/scene_snapshot` again: `HotReloadProbeCone`
     is STILL present, at the EXACT same position `(123, 456, 789)`, still
     unparented — together with `CubeParent`/`SphereChild`/Camera from
     verification #1, all four entities survived the freeze → capture →
     unload → recompile → reload → restore cycle with their data intact,
     each simply reassigned a fresh `Entity` index (expected per LDD-HR8).
   - `GET /get_swapchain` confirmed normal rendering afterward, no crash, no
     corruption — the Hierarchy panel visibly lists `HotReloadProbeCone`,
     `CubeParent` (with its child), and the Camera.
   - The background `GreatTamanaEditor.exe` process was cleanly stopped via
     `stop_app_background` once verification was complete.

## Definition of Done — checked against the phase file's own list

- [x] `ReconstructSceneFromDocument()` exists in `Scene/SceneBuilder.h/.cpp`
      (gte_core-tier), compiles, and its own body is a faithful, unmodified
      copy of `LoadScene()`'s own prior recipe-aware logic (Pass A/B1/B2/B3
      + `markSubtreeUnresolved` + the final `EnsureDefaultCameraExists()`
      call), minus the file-I/O/`AssetDatabase`-scan/`ClearEntireScene()`
      responsibilities now left to each caller.
- [x] `Editor::LoadScene()` is a thin wrapper calling the shared function;
      Step 2's own mandatory regression proof (byte-for-byte Ctrl+S/Ctrl+O
      round-trip, plus the FULL existing scene-serialization test suite
      unchanged and passing) is complete.
- [x] `SceneBuilder.h`'s own stale "Deliberately still Renderer-free" header
      comment is corrected to describe the TRUE, current state.
- [x] HOOK POINT B's real body exists, compiles, and Step 4's own isolation
      test passes live: a runtime-spawned primitive's own Transform data
      genuinely survives a real hot-reload cycle.
- [x] `PerformProjectAssemblyHotReload()`'s own call sites for both hook
      functions are updated to the new signatures; the enclosing
      orchestrator's own CONTROL FLOW is otherwise completely unchanged.
- [x] `docs/conventions/scene-serialization.md`'s "Module layout"/"Recipe-
      spawn reconciliation" sections no longer attribute this logic to
      `Editor/SceneIO.cpp` alone — both now name `Scene/SceneBuilder.cpp`'s
      `ReconstructSceneFromDocument()` as its real, current home.

PHASE3 is complete. PHASE4 (Probe Fixture: A Real Marker Entity + the one
narrow testing-only mutation route) may begin.
