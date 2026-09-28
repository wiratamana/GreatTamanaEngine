# PHASE1 — Tier Classification Model
## editor-core-separation-17 (On-Engine Project Workflow, BIG-STEP 3 "Open Project")

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.
Depends on: nothing (this is the first real-code phase).
Blocks: PHASE3 (the capability method needs this enum/function to exist).

---
## Step 1: The Goal

Give the rest of this campaign a single, pure, already-fully-testable
answer to "how valid/ready is this candidate project folder?" — the
5-tier model the source document
(`PROJECTWORKFLOW_BIGSTEP_03_OPEN_PROJECT_2026-09-28.txt`, STEP 2) already
fully specifies, backed by checks this repository's OWN build system
already performs today (never invented from nothing).

## Step 2: The Situation

`cmake/GteProject.cmake`'s `gte_add_project()` (its `IS_DIRECTORY "${ASSETS}"`
early-exit check and its `GAME_SOURCES`/`EDITOR_SOURCES` bucketing block) and
root `CMakeLists.txt`'s own project auto-discovery loop (`EXISTS
"${GTE_PROJECT_DIR}/Libraries/CMakeLists.txt"`) already encode exactly
which folder shapes are buildable at all — this phase turns that same
logic into one small, pure C++ function so the Editor/HTTP layer can
answer the identical question without shelling out to CMake. **Re-read
`gte_add_project()`'s real, current body before writing the classification
sketch below — it does exactly ONE recursive glob,
`file(GLOB_RECURSE ALL_CPP CONFIGURE_DEPENDS "${ASSETS}/*.cpp")`, over
`Assets/` (not two separate non-recursive globs), then buckets EACH
resulting `.cpp` file into `EDITOR_SOURCES` or `GAME_SOURCES` purely by
whether its own full path contains the literal substring `"/Editor/"`
anywhere at any depth (`if(SRC MATCHES "/Editor/")`) — a `.cpp` file
nested several folders deep under `Assets/` (not just directly inside it)
is still real, buildable Game source as far as CMake is concerned, and
`ClassifyProjectAssemblyFolder()` below must agree with that or it will
wrongly report `NotBuildable` for a project CMake would happily build.**
No existing function in this codebase does this yet — confirmed via
`search_in_dir` for `ClassifyProjectAssemblyFolder`/`ProjectValidityTier`
(zero hits).

`src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp` already holds every
sibling "small, pure, already-established helper" this plan's own PHASE0
Section 2.1 lists (`ResolveCMakeBuildDirectory`,
`ResolveProjectAssemblySourceRootDirectory`,
`ResolveProjectAssemblyOutputDirectory`) — this is the one, correct,
existing file/layer to extend (`gte_core`-tier, zero engine-state
dependency, already `#include <filesystem>`).

## Step 3: The Plan

### 3.1 — Header addition (`src/Core/Plugins/ProjectAssemblyBuildRunner.h`)

Add, immediately after `ResolveProjectAssemblyOutputDirectory()`'s own
declaration (keeps every "resolve a path" helper grouped together, then
this new classification helper right after, since it consumes several of
them):

```cpp
#include <vector>

// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE1. The 5-tier validity model a candidate
// Projects/<Name>/ folder can be in, from the "Open Project" feature's own
// point of view - see PROJECTWORKFLOW_BIGSTEP_03_OPEN_PROJECT_2026-09-28.txt,
// STEP 2, for the full narrative. Ordered from "least ready" to "most
// ready" on purpose - a future caller may reasonably compare tiers with
// `<`/`>=` (e.g. "selectable" == `tier != ProjectValidityTier::NotAProject`).
enum class ProjectValidityTier {
    NotAProject,    // missing Libraries/CMakeLists.txt - invisible to CMake entirely.
    NotBuildable,    // has Libraries/CMakeLists.txt, but no real Game .cpp source yet.
    NotCompiled,     // has real Game source, but no matching _Game.dll exists yet.
    Compiled,        // a matching _Game.dll exists at the output directory.
    AlreadyLoaded,   // that _Game.dll's own file name is already in loadedDllFileNames.
};

// Classifies `candidateFolder` (expected to be one direct child of the
// resolved Project Assembly source root, e.g.
// "<repo root>/Projects/MyProject") into exactly one ProjectValidityTier.
// Pure, synchronous, filesystem-only (plus a plain string-vector scan) -
// touches NO live engine state, safe to call from ANY thread. Mirrors
// gte_add_project()'s own real early-exit checks (cmake/GteProject.cmake -
// its `IS_DIRECTORY "${ASSETS}"` guard and its `GAME_SOURCES`/
// `EDITOR_SOURCES` bucketing `if`/`else`) and root CMakeLists.txt's own
// project auto-discovery loop's `EXISTS ".../Libraries/CMakeLists.txt"`
// gate (re-confirm its current real line number by searching for that
// exact string before citing one in code/comments - it has already moved
// at least once as this file grew) - never invents
// a new rule that contradicts what the build system itself already
// decides. `outputDirectory` is the resolved Project Assembly OUTPUT
// directory (ResolveProjectAssemblyOutputDirectory()'s own return value) -
// an EXPLICIT, REQUIRED parameter, mirroring every sibling resolver in this
// file's own "never resolve a path internally" convention.
// `loadedDllFileNames` is a plain snapshot (e.g. from
// ProjectAssemblyHost::GetLoadedAssemblyFileNames(), taken by the CALLER
// under GetHotReloadEngineStateMutex() if a live race is possible - this
// function itself takes no lock, since it only reads a plain,
// already-captured std::vector<std::string> the caller handed it).
// `candidateFolder` not existing at all, or not being a directory, returns
// NotAProject (never throws/crashes).
ProjectValidityTier ClassifyProjectAssemblyFolder(
    const std::filesystem::path& candidateFolder,
    const std::filesystem::path& outputDirectory,
    const std::vector<std::string>& loadedDllFileNames);
```

### 3.2 — Implementation (`ProjectAssemblyBuildRunner.cpp`)

The "does `Assets/` have any real, non-`Editor/`-path `.cpp` file" check
must mirror `gte_add_project()`'s own real glob (`cmake/GteProject.cmake`
— re-read that function's current, real body before writing this: it does
ONE recursive glob, `file(GLOB_RECURSE ALL_CPP CONFIGURE_DEPENDS
"${ASSETS}/*.cpp")`, then buckets each resulting file into
`EDITOR_SOURCES`/`GAME_SOURCES` purely by whether its own full path
contains the literal substring `"/Editor/"` anywhere at any depth
(`if(SRC MATCHES "/Editor/")`) — confirm this from the real file, do not
guess). Sketch:

```cpp
ProjectValidityTier ClassifyProjectAssemblyFolder(
    const std::filesystem::path& candidateFolder,
    const std::filesystem::path& outputDirectory,
    const std::vector<std::string>& loadedDllFileNames)
{
    std::error_code existsError;
    if (!std::filesystem::is_directory(candidateFolder, existsError)) {
        return ProjectValidityTier::NotAProject;
    }
    if (!std::filesystem::exists(candidateFolder / "Libraries" / "CMakeLists.txt")) {
        return ProjectValidityTier::NotAProject;
    }

    // RECURSIVE, mirroring gte_add_project()'s own GLOB_RECURSE - a .cpp
    // file nested several folders deep under Assets/ is still real,
    // buildable Game source as far as CMake is concerned, so a plain
    // one-level directory_iterator here would silently disagree with the
    // build system and misclassify a valid project as NotBuildable.
    bool hasRealGameSource = false;
    std::error_code iterationError;
    const std::filesystem::path assetsDirectory = candidateFolder / "Assets";
    if (std::filesystem::is_directory(assetsDirectory, iterationError)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsDirectory, iterationError)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
                continue;
            }
            // generic_string() always uses forward slashes regardless of
            // platform, so this substring check behaves identically to
            // CMake's own `if(SRC MATCHES "/Editor/")` on this same
            // absolute path - never re-derive this from a relative path,
            // which could disagree with CMake's own ABSOLUTE-path match.
            if (entry.path().generic_string().find("/Editor/") != std::string::npos) {
                continue; // Editor-only source - never counts as Game source.
            }
            hasRealGameSource = true;
            break;
        }
    }
    if (!hasRealGameSource) {
        return ProjectValidityTier::NotBuildable;
    }

    const std::string name = candidateFolder.filename().string();
    const std::string dllFileName = name + "_Game.dll";
    if (!std::filesystem::exists(outputDirectory / dllFileName)) {
        return ProjectValidityTier::NotCompiled;
    }

    const bool alreadyLoaded = std::find(loadedDllFileNames.begin(), loadedDllFileNames.end(), dllFileName)
        != loadedDllFileNames.end();
    return alreadyLoaded ? ProjectValidityTier::AlreadyLoaded : ProjectValidityTier::Compiled;
}
```

(Needs `#include <algorithm>` for `std::find` if not already present in
the `.cpp` - `<filesystem>` already provides `recursive_directory_iterator`
alongside the plain `directory_iterator` this file's sibling functions
already use.)

### 3.3 — New Tier-1 test file

`tests/Core/Plugins/ProjectAssemblyBuildRunnerTierClassificationTests.cpp`
(a genuinely new file — no existing sibling to extend, mirroring
`ProjectAssemblyBuildRunnerSourceRootTests.cpp`'s own exact
`TempOutputDirectory`/`WriteFile()` helper style — copy that file's own
two small helper classes/functions rather than re-inventing them). Cover,
at minimum, one test per tier, PLUS the two extra cases below that
specifically exercise the recursive/`/Editor/`-aware scan (3.2) — do not
skip these two, they are the real regression test proving this function
agrees with what `gte_add_project()` itself would actually build:

1. `NotAProject` — an empty temp directory (no `Libraries/` at all).
2. `NotAProject` — `Libraries/` exists but has no `CMakeLists.txt`.
3. `NotBuildable` — `Libraries/CMakeLists.txt` exists, `Assets/` missing
   entirely.
4. `NotBuildable` — `Assets/` exists but is empty.
5. `NotCompiled` — `Assets/SomethingGame.cpp` exists, but no matching
   `.dll` at the (fake, temp) output directory.
6. `Compiled` — the matching `<Name>_Game.dll` file exists at the fake
   output directory (a plain empty file is enough — the function only
   calls `std::filesystem::exists()`, never opens/parses it),
   `loadedDllFileNames` does NOT contain it.
7. `AlreadyLoaded` — same as 6, but `loadedDllFileNames` DOES contain
   `"<Name>_Game.dll"`.
8. A sanity check that a completely non-existent `candidateFolder` (never
   created at all) also returns `NotAProject`, never throws.
9. `NotCompiled` (recursive case) — `Assets/` has ONLY a NESTED file,
   e.g. `Assets/Sub/SomethingGame.cpp` (nothing directly inside `Assets/`
   itself) — must still classify as real Game source (NOT `NotBuildable`),
   mirroring `gte_add_project()`'s own `GLOB_RECURSE` behavior.
10. `NotBuildable` (Editor-only case) — `Assets/` has ONLY
    `Assets/Editor/SomethingEditorTool.cpp` and nothing else — must
    classify as `NotBuildable` (no real Game source), since that one file
    is Editor-only source, mirroring `gte_add_project()`'s own
    `"/Editor/"` bucketing rule.

Register the new file in `tests/CMakeLists.txt`, in the same
`Core/Plugins/...` block as `ProjectAssemblyBuildRunnerSourceRootTests.cpp`
(confirmed real line: `tests/CMakeLists.txt` line 2172 lists that sibling
file — add the new one immediately after it, same relative-path style,
`Core/Plugins/ProjectAssemblyBuildRunnerTierClassificationTests.cpp`).

### 3.4 — Definition of done

- [ ] `ProjectValidityTier` + `ClassifyProjectAssemblyFolder()` compile
      inside `ProjectAssemblyBuildRunner.h/.cpp`.
- [ ] All 10 new Tier-1 tests pass
      (`ctest -C Debug -R ProjectAssemblyBuildRunnerTierClassification
      --output-on-failure`, or the project's own equivalent narrow-target
      incremental build+run).
- [ ] Fast incremental compile check of the whole `GreatTamanaEditor`
      target succeeds (no full clean build this phase).
- [ ] `git_add` + `git_commit` this phase's own files
      (`ProjectAssemblyBuildRunner.h/.cpp`, the new test file,
      `tests/CMakeLists.txt`) plus a `PHASE1_COMPLETION_REPORT.md` in this
      same folder.

### 3.5 — Non-goals for this phase specifically

- Does NOT touch `ActiveProjectAssemblyState` (already fully built — see
  `PHASE0_MASTER_STRATEGY.md`, Section 2.1).
- Does NOT add the cross-thread bridge (PHASE2).
- Does NOT add any capability method, HTTP route, or ImGui window yet.
