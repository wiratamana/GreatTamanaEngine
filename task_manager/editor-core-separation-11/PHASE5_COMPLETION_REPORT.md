# PHASE5 — `ProjectAssemblyHost` Runtime Loader — COMPLETION REPORT

**Campaign:** `editor-core-separation-11` — "Project Assembly" System
**Phase:** PHASE5 of 8
**Status:** DONE
**Date:** 2026-09-28

---

## What was built

All anchors were re-verified via `search_in_dir`/`read_line` immediately
before editing (LDD6) — every line number cited in
`PHASE0_MASTER_STRATEGY.md`/`PHASE5_PROJECTASSEMBLYHOST_RUNTIME_LOADER.md`
was still correct, byte-for-byte, at the time of editing.

### New files: `src/Core/Plugins/ProjectAssemblyHost.h`/`.cpp`

Modeled directly on `src/Core/Plugins/PluginHost.h`/`.cpp`, exactly as this
phase file specifies, verbatim in shape:

- `LoadProjectAssemblies(outputDirectory, core, editorHost)` — enumerates
  every regular file directly inside `outputDirectory` (no recursion), safe
  on a non-existent directory (logs one `GTE_LOG_INFO` line, returns).
- `TryLoadOneAssembly()` — filters by filename suffix (`_Game.dll`/
  `_Editor.dll`, everything else silently ignored), `LoadLibraryW()`s,
  resolves the ONE fixed export `GTE_RegisterProject` through TWO different
  function-pointer signatures depending on which suffix matched (never by
  introspecting the export itself — this coupling is documented loudly in
  both files, per the phase file's own explicit instruction), calls it
  once, keeps the `HMODULE` alive forever (LDD4 — no hot reload, never
  `FreeLibrary()`'d except in the "declined/invalid" early-return paths
  where the export was never called).
- An `_Editor.dll` found while `editorHost == nullptr` is skipped with a
  loud `GTE_LOG_WARNING`, never crashed on.
- Zero `#include` from `plugins/gte_plugin_abi/` anywhere in either file —
  confirmed by direct inspection, matching this phase's own Definition of
  Done item 1.

### `Core.h`/`Core.cpp`

- `Core.h`: added `class EditorHost;` forward declaration (right after the
  existing `class IEditorLayer;` at the confirmed anchor), a new
  `#include "Plugins/ProjectAssemblyHost.h"` (right after the existing
  `#include "Plugins/PluginHost.h"`), a new public
  `void LoadProjectAssemblies(const std::filesystem::path&, EditorHost*)`
  (right after `LoadPlugins()`), and a new private
  `ProjectAssemblyHost m_projectAssemblyHost;` member (right after
  `m_pluginHost`).
- `Core.cpp`: added the thin pass-through body, right after
  `Core::LoadPlugins()`'s own closing brace, forwarding to
  `m_projectAssemblyHost.LoadProjectAssemblies(outputDirectory, *this, editorHost)` —
  matching `Core::LoadPlugins()`'s own shape exactly (always compiled, no
  `#if` guard in this file at all).

### `src/Editor/EditorHost.cpp`

Added the real call site immediately after the existing
`#if GTE_ENABLE_PLUGINS ... m_core.LoadPlugins(...) ... #endif` block's own
`#endif`, gated by the new, independent `GTE_ENABLE_PROJECT_ASSEMBLIES` flag:

```cpp
#if GTE_ENABLE_PROJECT_ASSEMBLIES
    m_core.LoadProjectAssemblies(gte::ExecutableDirectory() / "project_assemblies", this);
#endif
```

`this` (an `EditorHost&` cast to `EditorHost*` inside `EditorHost`'s own
constructor) is passed exactly as this phase file's own snippet specifies.

### Root `CMakeLists.txt`

- New `target_compile_definitions(gte_core PUBLIC GTE_ENABLE_PROJECT_ASSEMBLIES=$<BOOL:${GTE_ENABLE_PROJECT_ASSEMBLIES}>)`,
  added immediately after the existing, real, re-confirmed
  `GTE_ENABLE_PLUGINS` compile-definition call site (`gte_core` links
  `PUBLIC` into `gte_editor`, confirmed via `search_in_dir` for
  `target_link_libraries(gte_editor`, so this correctly propagates to
  `EditorHost.cpp`'s own `#if GTE_ENABLE_PROJECT_ASSEMBLIES` guard, exactly
  like `GTE_ENABLE_PLUGINS` already does).
- `src/Core/Plugins/ProjectAssemblyHost.h`/`.cpp` added to `gte_core`'s own
  explicit source-file list (see "Real deviation" below — this is new,
  not mentioned anywhere in the phase file).

### `Projects/ProjectAssemblyProbe/` (Step 4 — temporary proof, kept
permanently)

`HelloGame.cpp`'s `RegisterProbeGame()` and
`Assets/Editor/HelloEditorPanel.cpp`'s `RegisterProbeEditor()` each gained
one `GTE_LOG_INFO("ProjectAssembly", ...)` line, exactly as this phase
file's own Step 4 snippet specifies (plus the matching `_Editor` line for
the Editor assembly, which the phase file describes in prose but does not
spell out verbatim — written analogously, substituting "Editor"/"gte::Core&
and gte::EditorHost&" for the Game version's wording).

**Decision — diagnostic line disposition:** kept permanently (the phase
file's own explicitly recommended option: *"a cheap, always-on 'is this
working at all' smoke signal"*). This is a decision the phase file already
gives clear, sufficient guidance for (a recommendation, not an open
question), so no `ask_questions` call was needed — resolved directly per
this campaign's own "use best engineering judgment when the phase file
already gives enough guidance" rule. PHASE7/8 may still choose to remove or
supersede it once the test project has real capability code; this is
recorded here so whoever runs those phases next knows the current state
and the reasoning, not just the code.

## Real deviation from this phase file's own literal instructions (found
and fixed, not silently skipped)

**The phase file never mentions that a new gte_core `.cpp` file must be
added to `gte_core`'s own explicit CMake source-file list.** `gte_core`'s
sources in root `CMakeLists.txt` are an explicit, hand-maintained list (NOT
a `file(GLOB ...)`) — confirmed directly: the first incremental compile
attempt (`cmake --build build --target GreatTamanaEditor`) configured and
compiled cleanly with zero errors, but FAILED at the final link step with:

```
undefined reference to `gte::ProjectAssemblyHost::~ProjectAssemblyHost()'
undefined reference to `gte::ProjectAssemblyHost::LoadProjectAssemblies(...)'
```

Root cause, confirmed by reading the build log: `ProjectAssemblyHost.cpp`
was never compiled into any `.obj` at all — Ninja's object list for
`gte_core` did not include it, because `PluginHost.cpp` (the file this
class is modeled on) is itself listed explicitly, by exact relative path,
in root `CMakeLists.txt`'s `gte_core` target source list (confirmed via
`search_in_dir` for `PluginHost.cpp` inside `CMakeLists.txt`, 3 hits, one of
which is the actual `add_library(gte_core ... src/Core/Plugins/PluginHost.cpp ...)`
entry). **Fix**: added
`src/Core/Plugins/ProjectAssemblyHost.h`/`ProjectAssemblyHost.cpp` to that
same explicit list, immediately after the `PluginHost.h`/`.cpp`/
`FixedBufferReader.h` block, with a header comment explaining why (mirrors
`PluginHost`'s own "mechanism class is unconditional, only the call site is
gated" precedent). Re-built afterward — succeeded on the first attempt.
This is flagged here explicitly because it is a genuine gap in the phase
file's own Step-by-step instructions (it describes creating the files and
editing `Core.h`/`.cpp`/`EditorHost.cpp`, but never the CMake source-list
registration step a brand-new `gte_core`-owned `.cpp` file actually needs
on this repo's real, non-GLOB build setup) — a future phase file author
should add this as an explicit step.

## Live/compile verification performed

1. **Incremental compile check**: `cmake --build build --target GreatTamanaEditor` —
   failed once (the deviation above), fixed, rebuilt — succeeded cleanly,
   zero new warnings.
2. **Incremental compile check**: `cmake --build build --target ProjectAssemblyProbe_Game --target ProjectAssemblyProbe_Editor` —
   both `.dll`s rebuilt successfully with the new diagnostic log lines.
3. **Live verification — both diagnostic log lines present, exactly once
   each** (`run_app_background` → `GET /get_logs`):
   - `"ProjectAssembly"` / `"ProjectAssemblyProbe_Editor.dll: GTE_RegisterProject called with a real, live gte::Core& and gte::EditorHost&."`
   - `"ProjectAssembly"` / `"ProjectAssemblyProbe_Game.dll: GTE_RegisterProject called with a real, live gte::Core&."`
   - Followed immediately by two `"Loaded Project Assembly '...'"` info
     lines, one per `.dll`. No new warnings/errors versus the pre-existing,
     already-documented demo-plugin priority-tie-break warnings.
4. **Live verification — clean render**: `GET /get_swapchain` → real
   217053-byte PNG, genuine rendered Editor UI, identical to the
   pre-PHASE5 baseline (no visual regression — this phase adds no UI of its
   own, per its own "What this phase does NOT do").
5. **Explicit "no Project Assembly at all" test, done TWICE, the second
   time correcting an incomplete first attempt** (this phase file's own
   Definition of Done explicitly requires NOT assuming this from
   `PluginHost`'s own analogous guarantee):
   - First attempt: deleted `Projects/ProjectAssemblyProbe/` (the SOURCE
     folder — confirmed `.gitignore`d, so this really does delete it with
     no git safety net) and rebuilt. `Ninja`/CMake correctly auto-reran
     configure (`GLOB mismatch! ... files were removed`) and reported
     `ninja: no work to do` for `GreatTamanaEditor` itself (nothing to
     relink). Launched, `GET /get_logs` still showed BOTH diagnostic lines
     and `GET /get_swapchain` still rendered cleanly — **but this was not
     actually testing "neither .dll exists"**: the OLD, already-built
     `build/project_assemblies/ProjectAssemblyProbe_{Game,Editor}.dll`
     files were untouched by deleting the SOURCE folder (CMake has no
     "delete stale build output when a subdirectory is removed" behavior),
     so `ProjectAssemblyHost` was loading stale binaries, not testing the
     "folder doesn't exist" code path at all. Caught this via `browse_dir`
     on `build/project_assemblies/` before declaring success.
   - Second, corrected attempt: additionally deleted
     `build/project_assemblies/` itself (the actual runtime scan folder),
     relaunched. `GET /get_logs` now correctly showed
     `"no project_assemblies directory found at ... build\\project_assemblies, skipping"`
     (the exact log line `ProjectAssemblyHost::LoadProjectAssemblies()`
     emits on a missing directory) and zero `ProjectAssembly`-category
     "Loaded"/`GTE_RegisterProject` lines at all. `GET /get_swapchain` still
     returned a real, clean 217053-byte PNG — **no crash, confirmed for
     real, mechanically, not assumed from `PluginHost`'s own precedent.**
6. **Restoration**: recreated `Projects/ProjectAssemblyProbe/` byte-for-byte
   (all 5 files: `Libraries/CMakeLists.txt`, `Libraries/ProjectAssemblyExports.h`,
   `Assets/ProbeCompute.comp`, `Assets/HelloGame.cpp`,
   `Assets/Editor/HelloEditorPanel.cpp` — the last two now carrying the
   permanent PHASE5 diagnostic lines, everything else byte-identical to the
   PHASE3/4 committed state), rebuilt both `.dll`s (auto-reconfigure
   correctly picked up the re-created folder, `GLOB mismatch! ... files
   were added`), and ran one final full live pass: `GET /get_logs` (both
   diagnostic lines present again, clean), `GET /get_swapchain` (clean
   render), `stop_app_background`. `git status` confirms only the intended,
   tracked files (`CMakeLists.txt`, `src/Core/Core.h`, `src/Core/Core.cpp`,
   `src/Editor/EditorHost.cpp`, plus the two new untracked
   `ProjectAssemblyHost.h`/`.cpp` files) show as changed — `Projects/` itself
   is `.gitignore`d and never appears in `git status` at all, confirming
   LDD3 is still respected.

## Definition of Done — checklist (this phase file's own list)

- [x] `src/Core/Plugins/ProjectAssemblyHost.h`/`.cpp` exist, compile, and
      never `#include` anything from `plugins/gte_plugin_abi/`.
- [x] `Core::LoadProjectAssemblies()` thin pass-through exists, mirroring
      `Core::LoadPlugins()` exactly; `Core.h` gained the `class EditorHost;`
      forward declaration.
- [x] `EditorHost.cpp` calls it once, at construction time, gated by
      `GTE_ENABLE_PROJECT_ASSEMBLIES` (its own, independent flag), scanning
      a SEPARATE folder (`project_assemblies/`) from the existing plugin
      scan (`plugins/`).
- [x] A fresh launch of `GreatTamanaEditor.exe`, checked via `GET /get_logs`,
      shows the diagnostic log line from BOTH `ProjectAssemblyProbe_Game.dll`
      and `ProjectAssemblyProbe_Editor.dll`, exactly once each, at startup.
- [x] Deleting `Projects/ProjectAssemblyProbe/` entirely (AND its stale
      build output, `build/project_assemblies/` — the genuine, complete
      test) and rebuilding still launches `GreatTamanaEditor.exe` cleanly
      (`GET /get_swapchain` still returns a real image), with no crash —
      tested explicitly, twice (once incompletely, once correctly), not
      assumed from `PluginHost`'s own analogous guarantee.

## What this phase does NOT do (as instructed, re-confirmed)

- Does NOT implement the "Compile" button (PHASE6) — unchanged.
- Does NOT implement any real Editor panel or render pass registration —
  `RegisterProbeGame`/`RegisterProbeEditor` still do nothing but log.
  PHASE7/8's job, unchanged.

## New gap found, worth flagging for future phases/maintenance

See "Real deviation" above — `gte_core`'s explicit, hand-maintained source
list (not a GLOB) means every future `gte_core`-owned `.cpp` file (PHASE6's
`ProjectAssemblyBuildRunner.cpp`, per the file manifest in
`PHASE0_MASTER_STRATEGY.md` §3.4) will hit this exact same "compiles fine,
fails at final link with undefined references" symptom unless it is also
added to that list explicitly. Flagging this now so PHASE6 does not have to
rediscover it independently.

## Result

**PHASE5 is DONE** — every item on its own Definition of Done is
mechanically verified, including the one item this phase file explicitly
warned not to take for granted (the "no Project Assembly at all" no-crash
guarantee), which was tested twice: once incompletely (stale build output
still present, caught and corrected), and once correctly (both the source
folder and its build output genuinely absent). One real gap in the phase
file's own instructions was found and fixed (the missing `gte_core` source-
list registration step) and is recorded here for PHASE6's benefit. PHASE6,
7, and 8 may now proceed — a real, working Project Assembly runtime loader
exists, compiled, and live-verified.
