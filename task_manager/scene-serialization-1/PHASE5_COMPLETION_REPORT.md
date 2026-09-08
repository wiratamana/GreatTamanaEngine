# PHASE5 — Completion Report: `Editor/ProjectRootPath.h/.cpp` + `Editor/SceneIO.h/.cpp`

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`
for the full plan and `PHASE5_EDITOR_SCENE_IO_AND_PROJECT_ROOT_HELPER.md` for
this phase's own detailed work order. This phase is now **complete**.

## What was done

Followed the phase document's plan exactly — the code is byte-for-byte
identical to the spec:

1. **`src/Editor/ProjectRootPath.h`** (new file) — declares
   `ResolveProjectRootDirectory()`, the shared, unconditionally-
   `GTE_ENABLE_EDITOR`-compiled (NOT gated behind the separate
   `GTE_ENABLE_PROJECT_PANEL` switch) helper that resolves "the Project
   folder next to the .exe".
2. **`src/Editor/ProjectRootPath.cpp`** (new file) — implements it via
   `SDL_GetBasePath()` (UTF-8, via the `std::u8string` constructor so Windows
   native narrow-encoding is never involved) `/ "Project"`, falling back to
   `"./"` if SDL can't determine the executable's own directory.
3. **`src/Editor/Panels/ProjectPanel.cpp`** — refactored
   `ProjectPanel::ProjectPanel()`'s body to call
   `ResolveProjectRootDirectory()` instead of its own private
   `SDL_GetBasePath()`/`Utf8ToPath()` computation, removing the duplication.
   Added `#include "../ProjectRootPath.h"`; removed the now-unused
   `#include <SDL3/SDL.h>` (confirmed no other `SDL_*` call remains anywhere
   else in this file).
4. **`src/Editor/SceneIO.h`** (new file) — declares `DefaultScenePath()`
   (`ResolveProjectRootDirectory() / "TestScene.gtscene"`), `SaveScene(Game&)`,
   and `LoadScene(Game&, Renderer&)`, with the full doc comments from the
   phase spec (including the "never touch the registry on a malformed/missing
   file" and "skip an unresolvable Asset record gracefully" contracts).
5. **`src/Editor/SceneIO.cpp`** (new file):
   - `SaveScene()` builds a fresh, throwaway `AssetDatabase` scanning
     `ResolveProjectRootDirectory()`, calls PHASE4's
     `BuildSceneDocumentFromRegistry()`, serializes via PHASE3's
     `SerializeSceneDocument()`, creates the Project folder if missing
     (`std::filesystem::create_directories()`, mirroring `WriteGtaFile()`'s
     own tolerant convention), and writes the text to
     `<ProjectRoot>/TestScene.gtscene`, truncating any prior content.
   - `LoadScene()` reads `DefaultScenePath()`, parses it via
     `DeserializeSceneDocument()` (bailing out with `false` and touching
     nothing if that fails), scans a fresh `AssetDatabase`, calls PHASE4's
     `ClearSerializableSceneObjects()` to wipe exactly what this feature owns,
     then re-spawns every `SceneObjectRecord` via `Game::CreatePrimitiveEntity()`
     / `Game::CreateMeshEntityFromGtaFile()` (skipping an `Asset` record whose
     `Guid` no longer resolves), and writes back each spawned root's
     `Transform`/`Name` from the record.
6. **`CMakeLists.txt`** — added all four new files
   (`ProjectRootPath.h/.cpp`, `SceneIO.h/.cpp`) to the `GTE_ENABLE_EDITOR`
   block's `target_sources(gte_core PRIVATE ...)` list, right after
   `src/Editor/EditorContext.h` and before `src/Editor/Selection.h` —
   deliberately **outside** the nested `GTE_ENABLE_PROJECT_PANEL` block, so
   both compile whenever `GTE_ENABLE_EDITOR` is ON regardless of that
   separate switch.
7. **No new test file for `SceneIO.cpp` itself** — per the phase document's
   own step 3.7, this is a documented, accepted "Tier 2" gap
   (`SaveScene()`/`LoadScene()` need a live `Renderer` to actually exercise
   end-to-end via `Game::CreatePrimitiveEntity()`/
   `CreateMeshEntityFromGtaFile()`), the same bucket `RenderSystem::Draw()`/
   `PrimitiveGpuCatalog`/`Panels/ProjectPanel.cpp`'s own filesystem methods
   already fall into. Every PURE piece this file is built from
   (`SerializeSceneDocument()`/`DeserializeSceneDocument()` — PHASE3;
   `BuildSceneDocumentFromRegistry()`/`ClearSerializableSceneObjects()` —
   PHASE4) is already fully Tier-1-tested; `SceneIO.cpp` itself was reviewed
   by direct code inspection instead.

## Verification

- **Fast compile check** (per this task's workflow rules — no full build/
  regression test yet):
  - `cmake --build build --target gte_core` — **clean build**,
    `libgte_core.a` linked successfully (only the pre-existing, unrelated
    KTX-Software `git describe` version-fallback warning appeared, same as
    every prior phase's build log).
  - `cmake --build build --target GreatTamanaEngineTests` — **clean build**,
    `GreatTamanaEngineTests.exe` linked successfully (this phase adds no new
    test file, so this step only confirms the new/changed `src/Editor/`
    sources don't break anything the test binary already links against).
- Confirmed, before writing any code, that every API this phase's glue calls
  matches the live source tree exactly: `AssetDatabase::FindByGuid()`/
  `AssetRecord::gtaPath`, `Game::GetRegistry()`/`CreatePrimitiveEntity()`/
  `CreateMeshEntityFromGtaFile()`, `Registry::TryGetComponent<T>()`/
  `AddComponent<T>()`, `Entity`/`kInvalidEntity`'s `operator==`,
  `Scene/SceneBuilder.h`'s `BuildSceneDocumentFromRegistry()`/
  `ClearSerializableSceneObjects()` signatures, and `Scene/SceneDocument.h`'s
  `SceneObjectRecord`/`SceneObjectKind` shape — all confirmed to already
  exist from PHASE2/PHASE3/PHASE4, unchanged.
- Per this task's workflow rules, a full build/full regression `ctest` run
  was intentionally NOT performed — reserved for a later phase that
  explicitly calls for it (PHASE6, the final phase in this campaign, per
  the task instructions' own "if the Current task explicitly says to do a
  full build" carve-out).

## Notes / deviations from the phase document

None — every file was written byte-for-byte per the phase document's own
code listings (Steps 3.1–3.6), and the accepted test-coverage gap (Step 3.7)
was followed exactly as specified.

## Next phase

**PHASE6_FILE_MENU_SAVE_OPEN_CTRL_SHORTCUTS** — wires `File > Save Scene`
(Ctrl+S) and `File > Open Scene` (Ctrl+O) into `DockLayout.cpp`'s menu bar,
updates `ImGuiEditorLayer.cpp`'s call site, adds lightweight status feedback,
updates `CMakeLists.txt`/`tests/CMakeLists.txt`, closes out stale `TODO.md`
prose, and (per PHASE0's v2 audit) adds a fresh `README.md` "Status" bullet —
this is also the **last** phase of the `scene-serialization-1` campaign, and
per its own strategy document is the one expected to end with a full
build + regression `ctest` run.
