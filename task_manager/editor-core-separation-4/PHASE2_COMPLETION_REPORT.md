# PHASE2 — `GTE_ENABLE_PLUGINS=OFF` Build Fix — COMPLETION REPORT

**Status:** DONE. Verified with a throwaway `-DGTE_ENABLE_PLUGINS=OFF` configure +
`gte_core` build, plus confirmation the main `build/` tree (ON) is unaffected.

## What changed

1. **Root `CMakeLists.txt`** (the ONLY file that needed a code change) — split
   the single `if(GTE_ENABLE_PLUGINS) ... endif()` block that used to be at
   lines 722-736 (0-based line index 722-736 as read by `read_line`/
   `search_in_dir` — confirmed to match the phase file's own "approximately
   722-736" estimate exactly before editing) into two pieces:
   - `add_subdirectory(plugins/gte_plugin_abi)` is now UNCONDITIONAL — moved
     above the `if(GTE_ENABLE_PLUGINS)` line, with a new explanatory comment
     block (matching the phase file's own Step 3.1 wording exactly) recording
     WHY (it's a header-only `INTERFACE` library, `src/Core/Plugins/
     PluginHost.h/.cpp` are themselves unconditional `gte_core` sources and
     always need its include directories, and this is the exact previously-
     broken configuration this phase fixes).
   - The three demo plugin subdirectories (`demo_hello_world`,
     `demo_render_feature`, `demo_editor_panel`) stay exactly where they were,
     inside their own `if(GTE_ENABLE_PLUGINS) ... endif()` block, immediately
     below the now-unconditional `gte_plugin_abi` line. Zero change to their
     own order, comments, or content.
   - The file grew from 1406 lines net (15 old lines replaced by 37 new lines,
     +22 net) purely from the added explanatory comment block — no other line
     in the file was touched.

2. **`plugins/gte_plugin_abi/CMakeLists.txt`** — left completely untouched, as
   planned. Its own `if(GTE_ENABLE_PLUGINS AND
   GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)` check (line 47) already reads
   the real `GTE_ENABLE_PLUGINS` value correctly.

3. **`target_link_libraries(gte_core PUBLIC gte_plugin_abi)`** (line ~1033,
   now shifted down by +22 to ~1055) — left completely untouched, as planned.
   It was already unconditional and already correct; it simply now references
   a target that unconditionally exists.

4. **`docs/conventions/plugin-architecture.md`** and **`AGENTS.md`** — checked
   both via `search_in_dir("gte_plugin_abi")` and a full read of the "Plugin
   Architecture" sections. Neither claims `gte_plugin_abi` itself is
   conditional (both already correctly describe `PluginHost` as "always
   compiled") — matching the phase file's own prediction exactly. **No edit
   needed to either file.**

## Verification evidence

1. `cmake -S . -B build-plugins-off -G Ninja -DGTE_ENABLE_PLUGINS=OFF
   -DGTE_BUILD_TESTS=OFF` (working directory: project root) — **succeeded,
   zero errors**, in particular zero errors about `gte_plugin_abi` (the exact
   failure this phase fixes). Full configure log ends with:
   ```
   -- Configuring done (10.8s)
   -- Generating done (1.7s)
   -- Build files have been written to: C:/Users/F5954/Documents/TAMANA/GreatTamanaEngine/build-plugins-off
   ```
   (The only stderr output was the pre-existing, unrelated KTX-Software
   `git describe` "No names found" warning, which also appears in the main
   `build/` tree's own configure log — not caused by this phase.)

2. `cmake --build build-plugins-off --target gte_core` — **succeeded, 227/227
   steps, zero errors.** Final step:
   ```
   [227/227] Linking CXX static library libgte_core.a
   ```
   This confirms `src/Core/Plugins/PluginHost.cpp` compiled cleanly (it found
   both `plugins/gte_plugin_abi/PluginExports.h` and the CMake-generated
   `GtePluginAbiFingerprintGenerated.h`), and `gte_core` linked successfully
   against the now-unconditionally-existing `gte_plugin_abi` `INTERFACE`
   target — the exact previously-broken link this phase fixes.

3. Step 3 (optionally building the full executable in the throwaway tree) was
   skipped — step 2 already gave zero-error, full confidence for this narrow
   CMake-only change, and the phase file marks step 3 as optional "if time
   allows."

4. `cmake --build build --target gte_core` (main tree, `GTE_ENABLE_PLUGINS=ON`
   by default, untouched otherwise) — **succeeded.** CMake auto-detected the
   `CMakeLists.txt` change, re-ran configure (re-printing the expected
   `gte_apply_plugin_shared_crt_linkage()` honest no-op warnings for
   `demo_hello_world`/`demo_render_feature`/`demo_editor_panel`/
   `GreatTamanaEditor` — pre-existing, unrelated to this phase), then reported:
   ```
   ninja: no work to do.
   ```
   confirming `gte_core`'s own object files did not need to be rebuilt at all
   — this phase's CMake restructuring is a pure `add_subdirectory()`/`if()`
   reshuffle with no effect on any already-built target's real inputs in the
   default `ON` configuration.

5. Cleanup: `build-plugins-off` was deleted (`rmdir /s /q`) immediately after
   verification. Confirmed via `browse_dir` on the project root afterward —
   it is gone. (Several OTHER throwaway `build-*-probe`/`build-editor-off`
   directories remain in the repo root — these are pre-existing artifacts from
   earlier campaigns' own CI probes, not created or touched by this phase, and
   out of this phase's scope to clean up.)

## Deviations from the plan

None. The exact split described in the phase file's Step 3.1 was applied
verbatim (comment wording included), `plugins/gte_plugin_abi/CMakeLists.txt`
and the `target_link_libraries` line were left untouched as instructed, and
Step 3.4's doc double-check found no stale claim in either
`docs/conventions/plugin-architecture.md` or `AGENTS.md`, exactly as the phase
file predicted. No design ambiguity was encountered — the phase file was
fully unambiguous about the intended CMake shape, so `ask_questions` was not
needed.

## Locked Design Decisions check

No architectural shape changed: `gte_plugin_abi` remains a plain `INTERFACE`
library; `gte_core.a`/`gte_editor.a` remain `STATIC`; no plugin `.dll` gained
any new link dependency; `PluginHost::LoadPlugins()`'s own runtime call site
(gated via `#if GTE_ENABLE_PLUGINS` in `EditorHost.cpp`) was not touched. This
phase only changed WHEN one `add_subdirectory()` call runs at CMake configure
time — it added no new behavior and removed none of the 10 Locked Design
Decisions from `task_manager/editor-core-separation-3/
CAMPAIGN_COMPLETION_REPORT.md`.
