# PHASE1 COMPLETION REPORT — Tier Classification Model
## editor-core-separation-17 (On-Engine Project Workflow, BIG-STEP 3 "Open Project")

Parent: `PHASE0_MASTER_STRATEGY.md`. Spec: `PHASE1_TIER_CLASSIFICATION_MODEL.md`.
No prior `PHASE1_COMPLETION_REPORT.md` existed before this work — this is a
fresh implementation, not a continuation.

---

## What was built

1. **`src/Core/Plugins/ProjectAssemblyBuildRunner.h`**
   - Added `#include <vector>`.
   - Added `enum class ProjectValidityTier { NotAProject, NotBuildable,
     NotCompiled, Compiled, AlreadyLoaded };`, placed immediately after
     `ResolveProjectAssemblyOutputDirectory()`'s own declaration (grouping
     every "resolve a path" helper together, then this classification
     helper right after, exactly as the spec's 3.1 instructed).
   - Added the `ClassifyProjectAssemblyFolder(candidateFolder,
     outputDirectory, loadedDllFileNames)` declaration, with the exact doc
     comment content from the spec (trimmed the one stale
     "re-confirm its current real line number" aside since that
     instruction was for the AUTHOR writing the spec, not something to
     literally copy into the final header comment).

2. **`src/Core/Plugins/ProjectAssemblyBuildRunner.cpp`**
   - Added `#include <algorithm>` (for `std::find`).
   - Added the `ClassifyProjectAssemblyFolder()` implementation, placed
     right after `ResolveProjectAssemblyOutputDirectory()`'s own
     implementation. Logic matches the spec's sketch (3.2) essentially
     verbatim:
     - `is_directory()` false → `NotAProject`.
     - `Libraries/CMakeLists.txt` missing → `NotAProject`.
     - Recursive scan of `Assets/` (via
       `std::filesystem::recursive_directory_iterator`, exactly mirroring
       `gte_add_project()`'s own `GLOB_RECURSE`) for any `.cpp` file whose
       `generic_string()` does NOT contain `"/Editor/"` → if none found,
       `NotBuildable`.
     - `<Name>_Game.dll` missing at `outputDirectory` → `NotCompiled`.
     - `<Name>_Game.dll` file name present in `loadedDllFileNames` →
       `AlreadyLoaded`, else `Compiled`.
   - Used `std::error_code` overloads of `is_directory()`/`exists()`
     throughout (never the throwing overloads) so a genuinely bogus/
     inaccessible path can never make this function throw, matching the
     header's own "never throws/crashes" contract.

3. **Re-read `cmake/GteProject.cmake`'s real, current `gte_add_project()`
   body before writing any of the above** (per the task's explicit
   instruction). Confirmed line-for-line:
   - Exactly ONE recursive glob:
     `file(GLOB_RECURSE ALL_CPP CONFIGURE_DEPENDS "${ASSETS}/*.cpp")`.
   - Bucketing is `if(SRC MATCHES "/Editor/")` → `EDITOR_SOURCES`, else →
     `GAME_SOURCES` — case-sensitive, substring match on the full absolute
     path, at ANY depth.
   - The C++ implementation agrees with this exactly: recursive iteration,
     `generic_string().find("/Editor/")` substring check on the full
     absolute path (never a relative path, which could disagree with
     CMake's own absolute-path match).

4. **New test file:
   `tests/Core/Plugins/ProjectAssemblyBuildRunnerTierClassificationTests.cpp`**
   — a genuinely new file (no existing sibling to extend), mirroring
   `ProjectAssemblyBuildRunnerSourceRootTests.cpp`'s exact
   `TempOutputDirectory`/`WriteFile()` helper style (copied, with
   `WriteFile()` extended to `create_directories(path.parent_path())`
   first, since this file's own tests need nested paths like
   `Assets/Sub/SomethingGame.cpp` that `ProjectAssemblyBuildRunnerSourceRootTests.cpp`'s
   original version never needed). All 10 cases from the spec's 3.3 are
   present, including both load-bearing recursive/`/Editor/`-aware cases
   (case 9: nested `Assets/Sub/SomethingGame.cpp` → `NotCompiled`, proving
   it counts as real Game source; case 10: only
   `Assets/Editor/SomethingEditorTool.cpp` present → `NotBuildable`,
   proving Editor-only source never counts).

5. **`tests/CMakeLists.txt`** — registered the new test file immediately
   after `Core/Plugins/ProjectAssemblyBuildRunnerSourceRootTests.cpp` (real
   line 2172 before this edit), in the same `Core/Plugins/...` block, same
   relative-path style, with its own short comment block matching this
   file's existing convention.

---

## Build/test verification actually performed

- **Fast incremental build**, `cmake --build build --target
  GreatTamanaEngineTests` (from `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`,
  working directory explicit) — this re-ran CMake's own configure step
  (to pick up the new test source file added to `tests/CMakeLists.txt`),
  then compiled/linked cleanly. No errors, no warnings from the new code.
- **Narrow test run** (never the full `ctest` suite, per this phase's own
  build/test discipline):
  `GreatTamanaEngineTests.exe --gtest_filter=ProjectAssemblyBuildRunnerTierClassificationTest.*`
  — **all 10 new tests passed** (156 ms total, 0 failures).
- **Fast incremental build**, `cmake --build build --target
  GreatTamanaEditor` — compiled and linked cleanly (no changes needed;
  this confirms the new header/enum addition does not break any other
  translation unit that includes `ProjectAssemblyBuildRunner.h`).
- No full clean rebuild was performed. No full `ctest` regression run was
  performed. No `GreatTamanaEditor.exe` instance was launched in the
  background — this phase's new code has no HTTP/UI surface yet (per the
  task's own instructions, live verification was optional and skipped
  since the pure classification function is already fully covered by the
  Tier-1 test file above).

---

## Deviations from `PHASE1_TIER_CLASSIFICATION_MODEL.md`

**None, mechanically.** The header enum, the function signature, the
classification logic, the test file's helper-class style, and the
`tests/CMakeLists.txt` registration point all match the spec's own text
exactly. The only non-functional change made was trimming one
meta-instructional aside from the header doc comment (the spec's own text
told the *implementer* to "re-confirm [the CMakeLists.txt] line number
before citing one in code/comments" — since no such line number ended up
being cited in the final comment at all, that aside was simply dropped
rather than copied verbatim into the shipped code comment). This is a
documentation-wording choice only; it has zero effect on behavior,
compilation, or test coverage.

---

## Definition-of-done checklist (mirrors PHASE1's own 3.4)

- [x] `ProjectValidityTier` + `ClassifyProjectAssemblyFolder()` compile
      inside `ProjectAssemblyBuildRunner.h/.cpp`.
- [x] All 10 new Tier-1 tests pass.
- [x] Fast incremental compile check of the whole `GreatTamanaEditor`
      target succeeds (no full clean build performed).
- [x] `git_add` + `git_commit` this phase's own files
      (`ProjectAssemblyBuildRunner.h/.cpp`, the new test file,
      `tests/CMakeLists.txt`) plus this `PHASE1_COMPLETION_REPORT.md`.

---

## Non-goals confirmed untouched (per spec's 3.5)

- `ActiveProjectAssemblyState` — not touched.
- The cross-thread bridge (PHASE2) — not added.
- Any capability method, HTTP route, or ImGui window — not added.

## Handoff to PHASE2

`ProjectValidityTier` and `ClassifyProjectAssemblyFolder()` are now real,
compiled, tested code in `src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`,
ready for PHASE3's capability layer to consume once PHASE2's bridge exists.
Nothing was found during this phase that blocks PHASE2 from proceeding
independently, exactly as PHASE0's own dependency graph describes.
