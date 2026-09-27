# PHASE3 — Folder Layout + CMake Auto-Discovery + `gte_add_project()` — COMPLETION REPORT

**Campaign:** `editor-core-separation-11` — "Project Assembly" System
**Phase:** PHASE3 of 8
**Status:** DONE
**Date:** 2026-09-28

---

## What was built

All anchors were re-verified via `search_in_dir`/`read_line` immediately before
editing (LDD6) — every line number cited in `PHASE0_MASTER_STRATEGY.md`/this
phase's own file was still correct, byte-for-byte, at the time of editing.

### Root `CMakeLists.txt`

1. **New option** (near the existing `GTE_ENABLE_PLUGINS` at line 116):
   `option(GTE_ENABLE_PROJECT_ASSEMBLIES ... ON)`.
2. **`include(GteProject)`** (bare module name, LDD10), added immediately
   after the existing `include(MingwRuntime)`.
3. **`GTE_PROJECT_ASSEMBLY_OUTPUT_DIR`**, a new variable distinct from
   `GTE_PLUGIN_RUNTIME_OUTPUT_DIR`, added right after that variable's own
   `if/else/endif` block (unconditional `set()`, per the phase file's own
   reasoning).
4. **`GreatTamanaEditor`'s own target edit**, immediately after
   `target_link_libraries(GreatTamanaEditor PRIVATE gte_editor)`:
   - `set_target_properties(... ENABLE_EXPORTS ON WINDOWS_EXPORT_ALL_SYMBOLS ON)`
   - **`target_link_options(GreatTamanaEditor PRIVATE "-Wl,--export-all-symbols")`**
     — this extra flag is **not** in the phase file's own literal Step 2
     snippet; it comes straight from `PHASE2_COMPLETION_REPORT.md`'s own
     "Deviation 2" finding (`WINDOWS_EXPORT_ALL_SYMBOLS` is a documented
     no-op on this repo's real MinGW/GNU-ld toolchain — confirmed again here,
     independently, per the "New gap found" section below). Applying this
     phase's Step 2 literally, without the flag, would have silently left
     the whole system broken.
   - Step 3.1's unconditional `mingw_copy_runtime_dll(GreatTamanaEditor)`
     call, gated only by `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`.
5. **Auto-discovery block**, placed just before the `endif() #
   GTE_CORE_STANDALONE_PROBE_ONLY` that closes the same guard
   `GreatTamanaEditor`'s own definition lives inside — `file(GLOB
   ... CONFIGURE_DEPENDS "Projects/*")` + `add_subdirectory()` per
   sub-project's own `Libraries/CMakeLists.txt`, gated by
   `GTE_ENABLE_PROJECT_ASSEMBLIES`.

### `cmake/MingwRuntime.cmake`

New `gte_apply_project_assembly_shared_crt_linkage(target_name)` function,
appended at the end of the file, gated ONLY by
`GTE_ENABLE_PROJECT_ASSEMBLIES` + `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`
— never by `GTE_ENABLE_PLUGINS` (Finding D).

### New file: `cmake/GteProject.cmake`

Defines:
- `gte_add_project(NAME)` — globs `Assets/*.cpp`, splits Game vs. Editor
  sources by an `/Editor/` path-segment match, builds `${NAME}_Game`/
  `${NAME}_Editor` `SHARED` libraries, links each `PRIVATE` against
  `GreatTamanaEditor` only (LDD2), applies header-propagation +
  CRT-linkage, sets `RUNTIME_OUTPUT_DIRECTORY`/`PREFIX ""`.
- `gte_project_assembly_apply_header_paths(TARGET_NAME)` — the Finding C
  fix: 12 `$<TARGET_PROPERTY:...,INTERFACE_INCLUDE_DIRECTORIES>` entries.
- `gte_add_project_shaders(TARGET ASSETS_DIR)` — **PHASE4's own function,
  implemented here in full** (see "Real deviations" below for why).
- `gte_project_assembly_apply_editor_header_paths(TARGET_NAME)` —
  **PHASE7's own Step 1 function, implemented here in full** (see "Real
  deviations" below for why).

### New file: `cmake/templates/ProjectAssemblyExports.h`

Canonical copy of the `GTE_DEFINE_PROJECT_EXPORTS_GAME`/`_EDITOR` macros,
exactly as specified.

### `.gitignore`

New `/Projects/` entry, exact text from the phase file.

### New test project: `Projects/ProjectAssemblyProbe/`

```
Projects/ProjectAssemblyProbe/
  Assets/
    HelloGame.cpp
    ProbeCompute.comp          <- PHASE4 test shader, added here too (see below)
    Editor/
      HelloEditorPanel.cpp
  Libraries/
    CMakeLists.txt              (gte_add_project(ProjectAssemblyProbe))
    ProjectAssemblyExports.h    (copy of the canonical template)
```

Builds into `ProjectAssemblyProbe_Game.dll` / `ProjectAssemblyProbe_Editor.dll`
in `build/project_assemblies/` (distinct from `build/plugins/`), no `lib`
prefix.

## Real deviations from this phase file's own literal instructions (both
deliberate, both per the phase file's own explicit guidance for exactly
this situation)

### Deviation 1 — `gte_add_project_shaders()` implemented in full, not stubbed

This phase file's own text says: *"`gte_add_project_shaders()` is called
here but defined in PHASE4 — implement PHASE4 immediately after this
phase (they are tightly coupled and small); do not leave a permanent
stub."* Followed literally: `gte_add_project_shaders()` (PHASE4's Step 1,
verbatim) was implemented directly inside `cmake/GteProject.cmake`, and
PHASE4's own Step 3 test shader (`Projects/ProjectAssemblyProbe/Assets/ProbeCompute.comp`)
was added and built successfully. **This means PHASE4
(`PHASE4_SHADER_COMPILATION_WIRING.md`) is now ALSO fully done** — its own
Definition of Done is satisfied and verified live (see "PHASE4 verification"
below). A separate `PHASE4_COMPLETION_REPORT.md` is written alongside this
one for continuity/audit-trail purposes, even though the work happened as
part of this same session.

### Deviation 2 — `gte_project_assembly_apply_editor_header_paths()` implemented in full (PHASE7's Step 1 only), not stubbed

This phase file's own text offers a choice: *"either stub it as a no-op or
implement PHASE7's Step A1 alongside this phase (both are tiny)."* Chose
the "implement it" branch, since PHASE7's own Step 1 (the CMake function
itself) is genuinely tiny, self-contained, and depends on nothing PHASE5/
PHASE7's own remaining work (Step 1a's `ImGuiEditorLayer.cpp` gate
widening, Step 2's real interactive panel, Step 3's live HTTP
verification) has not yet built. **PHASE7 itself is NOT considered done**
— only its own Step 1 was pulled forward, exactly as the phase file's own
wording allowed ("PHASE7's Step A1", i.e. PHASE7's own STEP 1, as opposed
to STEP 1a/2/3). This is recorded explicitly so whoever runs PHASE7 next
does not redo Step 1, but still must do Steps 1a/2/3.

## New gap found (worth flagging, independently confirms PHASE2's own finding)

Re-confirmed, independently, exactly what `PHASE2_COMPLETION_REPORT.md`'s
own "Deviation 2" already found for the probe: `WINDOWS_EXPORT_ALL_SYMBOLS`
is a genuine, documented no-op on this repo's real MinGW/GNU-ld toolchain.
This phase's own literal Step 2 CMake snippet (in
`PHASE3_FOLDER_LAYOUT_AND_CMAKE_AUTODISCOVERY.md`) sets only
`ENABLE_EXPORTS`/`WINDOWS_EXPORT_ALL_SYMBOLS` — it does **not** itself spell
out the `target_link_options(... "-Wl,--export-all-symbols")` line (that
line only appears in this phase file's own prose reference to PHASE2's
finding). This report adds it explicitly to the real `GreatTamanaEditor`
target, per PHASE2's own explicit instruction ("PHASE3 must apply this
exact recipe to the real `GreatTamanaEditor` target"). Recorded here again
so this phase's own file can be corrected in a future edit pass to include
the flag directly in its Step 2 snippet, rather than relying on a reader to
cross-reference PHASE2's own completion report.

## Live/compile verification performed

1. **`cmake -S . -B build`** — clean reconfigure, zero new warnings beyond
   the pre-existing, unrelated KTX version-tag warning.
2. **`cmake --build build --target GreatTamanaEditor`** — builds
   successfully with the new `ENABLE_EXPORTS`/`WINDOWS_EXPORT_ALL_SYMBOLS`/
   `-Wl,--export-all-symbols` properties. Confirmed no regression:
   `run_app_background` → `GET /get_logs` (all 9 demo plugins load, `Network`
   listens on `127.0.0.1:8080`, `EditorHost` constructs cleanly, only the
   pre-existing, documented warnings present) → `GET /get_swapchain` (real
   217053-byte PNG, genuine rendered Editor UI) → `stop_app_background`.
   Repeated a second time after the full set of PHASE3+4 changes, same
   result.
3. **`cmake --build build --target ProjectAssemblyProbe_Game --target
   ProjectAssemblyProbe_Editor`** — both `.dll`s build and link
   successfully on the FIRST attempt (after the header-propagation fix was
   already in place — see the negative-control test below for proof it is
   load-bearing).
4. **Distinct output folders confirmed** via `browse_dir`:
   `build/project_assemblies/{ProjectAssemblyProbe_Game.dll,
   ProjectAssemblyProbe_Editor.dll, shaders/ProbeCompute.comp.spv}` vs.
   `build/plugins/{demo_*.dll, gte_plugin_abi/}` — two genuinely separate
   scan folders, no name/prefix collision, no `lib` prefix on either new
   `.dll`.
5. **Negative-control test for the header-propagation fix (Finding C),
   proving it is load-bearing, not decorative** (this phase file's own Step
   5 explicit instruction): temporarily commented out
   `gte_project_assembly_apply_header_paths(${NAME}_Game)` inside
   `cmake/GteProject.cmake`, reconfigured, deleted the stale `.obj`, rebuilt
   `ProjectAssemblyProbe_Game` — **build genuinely failed**:
   ```
   C:/.../src/Renderer/RenderGraph/RenderGraphTypes.h:38:10: fatal error: volk.h: No such file or directory
   ```
   (reached via `HelloGame.cpp` → `Core.h` → `RenderPassViewData.h` →
   `RenderGraphTypes.h` → `<volk.h>` — exactly the transitive chain PHASE0's
   own Finding C predicted). Restored the function call, reconfigured,
   deleted the stale `.obj` again, rebuilt — **succeeded** cleanly. This is
   a genuine, mechanically-reproduced positive/negative pair, not an
   assumption.
6. **`CONFIGURE_DEPENDS` auto-discovery, proven live, both directions**
   (this phase file's own Definition-of-Done explicit instruction): deleted
   `Projects/ProjectAssemblyProbe/` entirely, ran `cmake --build build
   --target GreatTamanaEditor` with **no** explicit `cmake -S . -B build` —
   Ninja printed `Re-checking globbed directories... Re-running CMake...`
   and CMake itself printed `-- GLOB mismatch! The following files were
   removed: -.../Projects/ProjectAssemblyProbe` — confirmed automatic.
   Recreated the exact same project folder from scratch, then ran `cmake
   --build build --target ProjectAssemblyProbe_Game --target
   ProjectAssemblyProbe_Editor`, again with **no** explicit reconfigure
   command — Ninja again auto-reconfigured (`GLOB mismatch! ... files were
   added`), and both `.dll`s built successfully in the same invocation, zero
   manual intervention. This is the exact scenario the phase file's own
   Definition of Done calls for.
7. **Incremental shader rebuild confirmed** (PHASE4's own Step 3): a
   no-change rebuild produced `ninja: no work to do`; touching
   `ProbeCompute.comp` (adding a comment line) triggered exactly one
   `glslc` recompile and re-staged `build/project_assemblies/shaders/ProbeCompute.comp.spv`
   (confirmed via file timestamps), while every other target's object files
   were untouched.
8. **Final full re-verification pass**, after all the above manipulation:
   rebuilt `GreatTamanaEditor` + both probe `.dll`s together (`ninja: no
   work to do` — fully up to date), launched, `GET /get_logs` (clean),
   `GET /get_swapchain` (clean render), `stop_app_background`.

## PHASE4 verification (folded in here per Deviation 1 above — see also the
separate `PHASE4_COMPLETION_REPORT.md`)

- `gte_add_project_shaders()` defined, correctly re-bases globbed absolute
  paths to `CMAKE_SOURCE_DIR`-relative before calling `gte_add_shader()`.
- `ProbeCompute.comp` compiles to
  `build/shaders/ProbeCompute.comp.spv` (engine's own shared intermediate
  folder) and stages to
  `build/project_assemblies/shaders/ProbeCompute.comp.spv` (distinct from
  `build/shaders/` next to `GreatTamanaEditor.exe` itself).
- **The exact staged relative path string PHASE8 must reuse verbatim:**
  `"project_assemblies/shaders/ProbeCompute.comp.spv"` (relative to
  `GreatTamanaEditor.exe`'s own directory, per Finding F's convention — no
  `gte::ExecutableDirectory()` prefix).
- Incremental rebuild confirmed both directions (no-op vs. real change).

## Definition of Done — checklist (this phase file's own list)

- [x] `GTE_ENABLE_PROJECT_ASSEMBLIES` option exists, default `ON`.
- [x] `GreatTamanaEditor` has `ENABLE_EXPORTS ON` + `WINDOWS_EXPORT_ALL_SYMBOLS ON`
      **and** the real, load-bearing `-Wl,--export-all-symbols` flag; no
      regression in the existing build/probe/launch checklist.
- [x] `cmake/MingwRuntime.cmake` has `gte_apply_project_assembly_shared_crt_linkage()`,
      gated correctly; `GreatTamanaEditor` unconditionally stages its 3
      runtime DLLs regardless of `GTE_ENABLE_PLUGINS`.
- [x] `cmake/GteProject.cmake` exists, `include(GteProject)`-ed once (bare
      module name), defines `gte_add_project()` +
      `gte_project_assembly_apply_header_paths()` (plus, per the deviations
      above, `gte_add_project_shaders()` and
      `gte_project_assembly_apply_editor_header_paths()` too).
- [x] `GTE_PROJECT_ASSEMBLY_OUTPUT_DIR` exists, distinct from
      `GTE_PLUGIN_RUNTIME_OUTPUT_DIR`, confirmed by listing both folders.
- [x] Auto-discovery block exists, correctly placed/gated; fresh-folder
      pickup confirmed live, both directions (delete → build → recreate →
      build, no manual reconfigure either time).
- [x] `cmake/templates/ProjectAssemblyExports.h` has both macros.
- [x] The manual header-propagation (Finding C) test concretely proves the
      fix is load-bearing (build fails without it, succeeds with it).
- [x] `Projects/ProjectAssemblyProbe/` builds both `.dll`s successfully into
      `project_assemblies/`, no `lib` prefix.

## What this phase did NOT do (as instructed)

- Did NOT actually load `ProjectAssemblyProbe_*.dll` into the running
  engine — PHASE5's job.
- Did NOT implement any real capability in `RegisterProbeGame`/
  `RegisterProbeEditor` — intentionally empty, per PHASE7/PHASE8.
- Did NOT build a scaffolding tool — the test project was created by hand.
- Did NOT do PHASE7's Step 1a (ImGuiEditorLayer.cpp gate widening), Step 2
  (real interactive panel), or Step 3 (live HTTP verification) — only
  PHASE7's own Step 1 (the tiny CMake function) was pulled forward, per
  Deviation 2 above.

## Result

**PHASE3 is DONE**, and as a direct, deliberate consequence of following
this phase file's own explicit "do not leave a permanent stub" instruction,
**PHASE4 is ALSO fully done** (own completion report written separately).
PHASE7's own Step 1 (only) was pulled forward as permitted; PHASE7's
remaining steps (1a/2/3) are untouched and still fully PHASE7's own
responsibility. PHASE5 may now proceed — the `_Game`/`_Editor` CMake
targets it needs already exist, build cleanly, and land in the correct,
dedicated output folder.
