# PHASE5 COMPLETION REPORT — Proving "Always All-In" Stays Editor-Clean in a Player-Shaped Process

**Phase file**: `PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md`. **Parent**:
`PHASE0_MASTER_STRATEGY.md`. **Predecessors**:
`PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md`,
`PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md`,
`PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md`,
`PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md` (all four read in full,
along with their own `PHASEn_COMPLETION_REPORT.md`, before starting).
**Branch**: `feature/editor-core-separation` (confirmed via `git_status`
before starting — clean tree, correct branch — and unchanged throughout).

## Summary — what was actually built

1. **`tools/ci/gte_plugin_isolation_probe/`** (new) — the primary,
   no-GPU-needed deliverable (Step 3.1):
   - `main.cpp` — links `gte_core.a` **alone** (no `gte_editor`/SDL/ImGui —
     confirmed via this probe's own `CMakeLists.txt` using
     `GTE_CORE_STANDALONE_PROBE_ONLY=ON`, mirroring
     `tools/ci/gte_core_player_link_probe`'s own precedent), uses
     `gte::PluginHost` directly, loads every `.dll` in the shared `plugins/`
     folder, confirms `LoadedModuleCount() == 3` exactly (a stricter check
     than the phase file's own literal `== 0` failure-sketch — see Deviation
     #2 below), queries `IRenderFeatureModule_v1` on every module and
     confirms exactly one implements it, and **never once references
     `IEditorPanelModule_v1` anywhere in its own source** — the isolation
     proof itself.
   - `CMakeLists.txt` — the outer nested-cmake-invocation wrapper, mirroring
     `tools/ci/gte_plugin_abi_handshake_probe/`'s (PHASE2) own shape exactly.
   - `README.md` (new, not explicitly required by the phase file, added for
     consistency with every other `tools/ci/*` probe, all of which have one).
2. **`tools/ci/gte_core_player_link_probe/main.cpp`** (modified) — the bonus,
   GPU/headless-surface-gated deliverable (Step 3.2): a new block appended
   after the three pre-existing forced-link expressions, wrapped in
   `try`/`catch` mirroring `CoreHeadlessConstructionTests.cpp`'s own
   established "construction attempt IS the skip-detection signal" shape
   exactly (there is no standalone `SupportsHeadlessSurface()` predicate
   anywhere in this codebase, confirmed by `search_in_dir` before writing
   this) — constructs a real, headless `gte::Core` via
   `tests/Fakes/HeadlessSurfaceProvider.h` + a locally-defined
   `NoopHostServices`, calls `Core::LoadPlugins()` against this probe's own
   real `plugins/` folder, and calls `Core::BuildFrame()` once.
3. **`tools/ci/gte_core_player_link_probe/README.md`** (modified) — updated
   to document the new bonus check, and corrected the prior, now-inaccurate
   "It never actually EXECUTES the resulting `.exe`" claim (see Deviation #3
   below).
4. **Root `CMakeLists.txt`** — inside the existing
   `if(GTE_CORE_STANDALONE_PROBE_ONLY)` block (confirmed real, current
   location via `read_line`/`search_in_dir` first):
   - `gte_apply_plugin_shared_crt_linkage(gte_core_player_link_probe)` added
     (this probe now genuinely participates in the plugin ABI boundary, per
     Step 3.2's own explicit instruction and PHASE0's Locked Design Decision
     #4's "JOINT requirement" restatement).
   - `add_dependencies(gte_core_player_link_probe demo_hello_world
     demo_render_feature demo_editor_panel)` added — a real, necessary
     addition beyond the phase file's own literal Step 3.2 text (see
     Deviation #1 below).
   - New `gte_plugin_isolation_probe` executable target, linking
     `gte_core`/`gte_plugin_abi`, `gte_apply_plugin_shared_crt_linkage()`'d,
     depending on all three demo plugin targets — exactly per Step 3.1.
5. **`.gitignore`** — new `/build-plugin-isolation-probe/` entry, mirroring
   the pre-existing `/build-plugin-abi-handshake-probe/` precedent.

## Step 2's own required re-confirmation (mandatory before starting Step 3)

Re-confirmed, via `search_in_dir`/`read_line` on the real, current root
`CMakeLists.txt`, that `add_subdirectory(plugins/gte_plugin_abi)` /
`add_subdirectory(plugins/demo_hello_world)` /
`add_subdirectory(plugins/demo_render_feature)` /
`add_subdirectory(plugins/demo_editor_panel)` (all four, gated by
`if(GTE_ENABLE_PLUGINS)`) genuinely still sit OUTSIDE/BEFORE the
`if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard that wraps `gte_editor` itself
— confirmed true, unchanged since PHASE2. No fix-forward was needed here.

## Step 4 verification (mandated by the phase file) — final, passing state

1. **Built `tools/ci/gte_plugin_isolation_probe` fresh** (nested-cmake
   pattern, its own inner build directory,
   `build-plugin-isolation-probe/gte_plugin_isolation_inner_build/`) — 235
   Ninja steps, clean (only the expected, pre-existing
   `gte_apply_plugin_shared_crt_linkage()` no-op warnings from this
   development machine's static-only toolchain — see PHASE1's own Deviation
   #2, unchanged). Ran the resulting `.exe` directly (its own stdout is only
   visible when redirected to a file — `run_shell`'s console capture of a
   Windows console-subsystem `.exe`'s direct stdout was unreliable in this
   session, worked around with `> out.txt 2>&1` + `type`/`read_file` —
   nothing wrong with the probe itself, confirmed by inspecting the captured
   file):
   ```
   Loaded 'DemoEditorPanelPlugin' - IRenderFeatureModule_v1: no
   Loaded 'HelloWorldPlugin' - IRenderFeatureModule_v1: no
   Loaded 'DemoRenderFeaturePlugin' - IRenderFeatureModule_v1: yes
   PASS: 3 plugin(s) loaded, exactly 1 implements IRenderFeatureModule_v1, and this file never once asked any plugin for IEditorPanelModule_v1.
   ```
   Exit code `0`. **Confirms `LoadedModuleCount() == 3` exactly** (not merely
   `> 0`) — ruling out a folder-path-mistake false positive.
2. **Rebuilt `tools/ci/gte_core_player_link_probe` fresh** — 235 Ninja steps,
   clean (same expected warnings). Ran the resulting `.exe`:
   ```
   Bonus check SKIPPED: this machine's Vulkan driver lacks VK_EXT_headless_surface (matches the existing, documented CoreHeadlessConstructionTest skip). Real reason: vkCreateInstance failed (VkResult=-7)
   ```
   (stderr also showed the pre-existing, unrelated `[Vulkan] Validation was
   requested but VK_LAYER_KHRONOS_validation is not available` line every
   Vulkan-instance-creation attempt on this machine already prints). This is
   an **acceptable PASS** per the phase file's own Step 4 point 2 — "either
   outcome is an acceptable PASS for this phase's own purposes; only a
   genuine crash/hang is a failure" — and matches
   `CoreHeadlessConstructionTest`'s own pre-existing, documented skip on this
   exact development machine (never independently re-verified against that
   test in this phase, since re-running it would require the full test
   binary, out of scope per Universal Rule 4 — the skip MESSAGE explicitly
   names and cross-references that test for a future reader). No crash, no
   hang.
3. **Re-confirmed, via `browse_dir`, that all three demo plugin `.dll`s
   genuinely exist** in both probes' own inner build `plugins/` folders
   before each ran:
   - `build-plugin-isolation-probe/gte_plugin_isolation_inner_build/plugins/`:
     `demo_editor_panel.dll`, `demo_hello_world.dll`,
     `demo_render_feature.dll` — all three present.
   - `build-player-link-probe/gte_core_inner_build/plugins/`:
     `demo_editor_panel.dll`, `demo_hello_world.dll`,
     `demo_render_feature.dll` — all three present.

## Additional verification beyond the phase file's own literal Step 4 (Universal Rules 4/10)

- **Targeted incremental compile check of the main build tree** (Universal
  Rule 4) — `cmake --build build --target gte_core` /
  `--target gte_editor` / `--target GreatTamanaEditor`: all three reported
  `"ninja: no work to do."` — mechanically confirming this phase's own
  changes (entirely inside the `if(GTE_CORE_STANDALONE_PROBE_ONLY)` block,
  never reached by a normal `GTE_CORE_STANDALONE_PROBE_ONLY=OFF` configure)
  touch **zero** files the main, non-probe build tree actually compiles.
- **Re-ran `gte_plugin_abi_handshake_probe`** (PHASE2's own probe, untouched
  by this phase) fresh — still printed
  `OK: loaded plugin 'HelloWorldPlugin' v1.0.0 - Milestone 0 handshake proof - implements zero capabilities.`
  and exited `0`, confirming this phase introduced no regression to it.
- **Live runtime smoke test** (Universal Rule 10) —
  `run_app_background(GreatTamanaEditor.exe)` →
  `GET /get_logs?limit=30` (all three plugins logged loaded, `PluginHost`
  category, `Info` level) → `GET /get_logs?limit=30&min_level=warning`
  (`{"count":0,...}`, zero warnings/errors) →
  `GET /activate_tab?name=Game` + `GET /get_swapchain` (screenshot confirmed
  the Scene panel still shows PHASE3's solid-magenta plugin clear, the
  "Demo Plugin Panel" tab from PHASE4 is still docked in the bottom strip and
  still shows "Hello from a plugin!", and the rest of the Editor UI —
  Hierarchy/Inspector panels, dock layout — is visually unchanged) →
  `GET /list_tabs` (unchanged:
  `["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render
  Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]`) →
  `stop_app_background`. Confirms this phase — which added zero new code
  inside `gte_core`/`gte_editor`/`GreatTamanaEditor` itself, only new
  standalone probes plus small root-`CMakeLists.txt`/`.gitignore` additions —
  genuinely regressed nothing.

## Deviations from the strategy doc (real, discovered, not silently smoothed over)

### Deviation #1 — `gte_core_player_link_probe` needed its own `add_dependencies()` on all three demo plugins, not mentioned by the phase file's own literal Step 3.2 text

The phase file's Step 3.2 only says to add
`gte_apply_plugin_shared_crt_linkage(gte_core_player_link_probe)` "if PHASE2
did not already add it there for a different reason" — it does not mention
`add_dependencies()`. However, this probe's own outer
`CMakeLists.txt` (`tools/ci/gte_core_player_link_probe/CMakeLists.txt`,
unmodified by this phase, written by `editor-core-separation-2` PHASE4)
builds only `--target gte_core_player_link_probe` from its inner configure —
never the inner build's own `ALL` target. Since `add_subdirectory(plugins/...)`
targets are only built if something depends on them or `ALL` is requested,
without an explicit `add_dependencies()`, a bare `cmake --build
build-player-link-probe` would produce a linked `.exe` with **zero** `.dll`s
in its own `<inner-build-dir>/plugins/` folder — this phase's own bonus check
would then report "loaded 0 plugins" (not a crash, so technically still a
"PASS" per Step 4 point 2's own wording, but a materially weaker, less
meaningful proof than intended, and directly contradicting Step 4 point 3's
own explicit requirement to confirm all three demo plugin `.dll`s genuinely
exist in EACH probe's own `plugins/` folder before it runs). Fixed by adding
`add_dependencies(gte_core_player_link_probe demo_hello_world
demo_render_feature demo_editor_panel)`, mirroring
`gte_plugin_abi_handshake_probe`'s own existing, adjacent
`add_dependencies(... demo_hello_world)` precedent exactly — confirmed, by a
fresh rebuild, that a plain `cmake --build build-player-link-probe --target
gte_core_player_link_probe` now genuinely produces all three `.dll`s in that
inner build's own `plugins/` folder with no separate `--target` step needed.

### Deviation #2 — `gte_plugin_isolation_probe/main.cpp`'s own plugins-directory resolution corrected from `std::filesystem::current_path()` to `GetModuleFileNameW()`-based resolution (both new probe and the extended `gte_core_player_link_probe`)

The phase file's own Step 3.1 code sketch resolves the shared `plugins/`
folder via `std::filesystem::current_path() / "plugins"`. A process's current
working directory at execution time is whatever its CALLER happened to set
when launching it — never guaranteed to be the `.exe`'s own directory (e.g.
this session ran the built `.exe` via `run_shell` with the repository root as
the working directory, which would have resolved to a nonexistent
`<repo-root>/plugins` folder rather than the real
`<inner-build-dir>/plugins`). This was corrected to the same
`GetModuleFileNameW()`-based approach `tools/ci/gte_plugin_abi_handshake_probe/main.cpp`'s
own `ResolveDemoHelloWorldDllPath()` (PHASE2) already established as this
repository's own precedent for exactly this problem — confirmed working by
this phase's own Step 4 evidence above (both probes genuinely found and
loaded all three plugins/demo `.dll`s regardless of the working directory
the probe happened to be launched from). Also strengthened
`gte_plugin_isolation_probe/main.cpp`'s own failure check from the phase
file's literal `LoadedModuleCount() == 0` (a "FAIL: expected at least the 3
demo plugins to load, loaded 0" message) to `!= 3` — directly satisfying Step
4 point 3's own explicit instruction to confirm exactly 3, not merely more
than 0, in code rather than only in this report's own prose.

### Deviation #3 — `gte_core_player_link_probe/README.md` needed correction beyond what the phase file asked for

Not explicitly required by the phase file, but a real, discovered
documentation-accuracy gap: that probe's own pre-existing `README.md` (from
`editor-core-separation-2` PHASE4) stated, twice, "It never actually EXECUTES
the resulting `gte_core_player_link_probe.exe` - only compiles and links it.
Running it would do nothing observable anyway." This became actively
inaccurate the moment this phase's own bonus check was added — running the
`.exe` now does something observable (the bonus check's own PASS/SKIPPED
message). Corrected both the "How it works" section (added a new "PHASE5 - a
fourth, genuinely EXECUTED bonus check" subsection) and the "What this probe
deliberately does NOT do" list (removed the now-false "never actually
EXECUTES" bullet), and updated the "Exact command to run this by hand"
section to include the now-meaningful third line that actually runs the
`.exe`. Left every other pre-existing claim in that file (the "sibling of
`gte_core_standalone_probe`", "not wired into any CI system",
"never a user-facing option" bullets) untouched, since all three remain
true.

## What this phase deliberately did NOT do (per its own Non-Goals / the phase file's own scope)

No real Player Build Pipeline/`<ProjectName>.exe` generation. No true
multi-process IPC/sandboxing. No new capability interface, no new demo
plugin, no change to `PluginHost`/`Core`/any `gte_core` or `gte_editor`
production source file — this phase's entire footprint is two standalone
`tools/ci/` probes plus small, additive root-`CMakeLists.txt`/`.gitignore`
entries, exactly matching this phase's own stated scope (a verification/proof
phase, not a new-capability phase like PHASE3/PHASE4).

## Files added/changed

- `tools/ci/gte_plugin_isolation_probe/main.cpp` (new)
- `tools/ci/gte_plugin_isolation_probe/CMakeLists.txt` (new)
- `tools/ci/gte_plugin_isolation_probe/README.md` (new)
- `tools/ci/gte_core_player_link_probe/main.cpp` (modified — new bonus check
  block, new includes, new local `NoopHostServices`/`ResolvePluginsDirectory()`
  helpers)
- `tools/ci/gte_core_player_link_probe/README.md` (modified — documents the
  new bonus check, corrects the now-inaccurate "never executed" claim)
- `CMakeLists.txt` (modified — `gte_apply_plugin_shared_crt_linkage(gte_core_player_link_probe)`
  + `add_dependencies(gte_core_player_link_probe ...)` additions, new
  `gte_plugin_isolation_probe` executable target)
- `.gitignore` (modified — new `/build-plugin-isolation-probe/` entry)

## Non-Goals honored (per the phase file's own list)

No real Player Build Pipeline/`<ProjectName>.exe` generation. No true
multi-process IPC/sandboxing (unchanged, confirmed).

## Compile-check summary (for the record)

- `gte_plugin_isolation_probe` (nested-cmake standalone probe, fresh
  configure + build): 235 Ninja steps, clean; ran successfully, `PASS`, exit
  `0`.
- `gte_core_player_link_probe` (nested-cmake standalone probe, fresh
  configure + build): 235 Ninja steps, clean; ran successfully, bonus check
  cleanly `SKIPPED` (documented, expected on this machine), exit `0`.
- `gte_plugin_abi_handshake_probe` (PHASE2's own probe, re-run unmodified):
  "no work to do" on rebuild, still passes when run (`OK: ...`).
- Main build tree (`build/`): `gte_core`/`gte_editor`/`GreatTamanaEditor` all
  reported "no work to do" — this phase's changes are 100% confined to the
  `GTE_CORE_STANDALONE_PROBE_ONLY=ON` probe configuration, confirmed to touch
  zero files the main build tree compiles.
- Live `run_app_background` + `GET /get_logs` + `GET /get_swapchain` +
  `GET /list_tabs` smoke test of `GreatTamanaEditor.exe`: all three plugins
  loaded, zero warnings/errors, PHASE3's magenta clear and PHASE4's "Demo
  Plugin Panel" both visually unregressed.
