# PHASE2 COMPLETION REPORT — `PluginHost`, Wired Into `Core`, Plus a Real Hello-World Handshake Probe

**Phase file**: `PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md`.
**Parent**: `PHASE0_MASTER_STRATEGY.md`. **Predecessor**:
`PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md` (read first, in full, along with its
own `PHASE1_COMPLETION_REPORT.md`). **Branch**: `feature/editor-core-
separation` (confirmed via `git_status` before starting and unchanged
throughout — never switched).

## Summary — what was actually built

1. **`gte::PluginHost`** (`src/Core/Plugins/PluginHost.h/.cpp`, new, compiled
   into `gte_core`) — implements the source design doc's Section 4.1 loader
   procedure exactly: `LoadPlugins()` enumerates `*.dll` directly inside a
   folder (non-existent folder → one `GTE_LOG_INFO` line, no throw);
   `TryLoadOnePlugin()` does `LoadLibraryW()` → resolve the 3 fixed exports by
   exact name → fingerprint check (byte-for-byte, every differing field
   logged by name on mismatch) → `GTE_CreatePluginModule()` (`nullptr` =
   legal decline, `GTE_LOG_INFO`, not a warning) → success (`GTE_LOG_INFO`
   naming the plugin) — every branch is a clean, logged skip, never a crash.
   `~PluginHost()` destroys/unloads in reverse load order. Always compiled
   (unconditional `target_sources()` entry, unconditional
   `target_link_libraries(gte_core PUBLIC gte_plugin_abi)`) — only the real
   runtime call site (`EditorHost.cpp`'s `m_core.LoadPlugins(...)`) is gated
   behind `#if GTE_ENABLE_PLUGINS`, mirroring `GTE_ENABLE_NETWORK`'s existing
   "gate the call site, not the class" precedent, per the phase file's own
   Step 3.3 instruction.
2. **`Core`** (`src/Core/Core.h/.cpp`) — new `#include "Plugins/PluginHost.h"`
   (file-scope, mirroring `FrameDebuggerCaptureRecorder.h`'s own placement
   discipline), new public `void LoadPlugins(const std::filesystem::path&)`
   (thin pass-through, always compiled), new public
   `const PluginHost& GetPluginHost() const noexcept` accessor (for PHASE3/4),
   new private `PluginHost m_pluginHost;` member (declared immediately before
   `m_gameTargetThisFrame`/`m_sceneTargetThisFrame`, matching the phase file's
   own placement instruction).
3. **`src/Editor/ProjectRootPath.h/.cpp`** — confirmed (via `read_file`,
   before writing anything) that this file did NOT already expose a plain
   "executable's own directory" accessor distinct from
   `ResolveProjectRootDirectory()`'s own `.../Project` subfolder — added one,
   small and additive: `ExecutableDirectory()`, sharing the exact same
   `SDL_GetBasePath()`-based resolution logic (refactored into one small,
   shared, anonymous-namespace helper, `ResolveExecutableDirectoryBasePath()`,
   rather than duplicating the SDL call a second time).
4. **`src/Editor/EditorHost.cpp`** — new `#include "ProjectRootPath.h"` and
   `#include <filesystem>`; the constructor now calls, immediately after
   `m_core.SetPresentImGuiRecorder(...)` and before the
   `static EditorSceneIOCapability s_editorSceneIOCapability;` block (exactly
   the placement the phase file specifies):
   ```cpp
   #if GTE_ENABLE_PLUGINS
       m_core.LoadPlugins(gte::ExecutableDirectory() / "plugins");
   #endif
   ```
5. **`plugins/demo_hello_world/`** (new) — `CMakeLists.txt` +
   `HelloWorldPlugin.cpp`: a real, trivial demo plugin `.dll` implementing
   `IPluginModule::GetModuleInfo()` and `QueryCapability()` (returning
   `nullptr` unconditionally — zero capabilities), links ONLY `gte_plugin_abi`
   (Locked Design Decision #2), staged into `GTE_PLUGIN_RUNTIME_OUTPUT_DIR`.
6. **`tools/ci/gte_plugin_abi_handshake_probe/`** (new) — `CMakeLists.txt`
   (outer nested-cmake wrapper, mirroring
   `tools/ci/gte_core_player_link_probe/CMakeLists.txt`'s exact shape) +
   `main.cpp` (a genuinely standalone program, zero `gte_core`/`gte_editor`
   dependency, `#include`ing only `plugins/gte_plugin_abi/`'s own headers) +
   `README.md`. Re-proves the whole handshake end-to-end outside
   `GreatTamanaEditor.exe` entirely. The inner, real root
   `CMakeLists.txt` gained the matching
   `add_executable(gte_plugin_abi_handshake_probe ...)` +
   `add_dependencies(gte_plugin_abi_handshake_probe demo_hello_world)` block,
   inside the existing `if(GTE_CORE_STANDALONE_PROBE_ONLY)` guard, right after
   `gte_core_player_link_probe`'s own block.
7. **Root `CMakeLists.txt`**:
   - `GTE_PLUGIN_RUNTIME_OUTPUT_DIR` variable + `add_subdirectory(plugins/
     gte_plugin_abi)` / `add_subdirectory(plugins/demo_hello_world)` (both
     gated by `if(GTE_ENABLE_PLUGINS)`, per the phase file's own literal Step
     3.5/3.6 code), placed immediately after `gte_core`'s own `add_library()`
     block's closing parenthesis (confirmed, via `read_file`, that this sits
     OUTSIDE/BEFORE the `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard
     wrapping the `gte_editor` block, exactly as required).
   - `src/Core/Plugins/PluginHost.h/.cpp` added to `gte_core`'s
     `target_sources()`, immediately after `src/Core/FrameDebuggerCaptureRecorder.h`.
   - New `target_compile_definitions(gte_core PUBLIC GTE_ENABLE_PLUGINS=$<BOOL:${GTE_ENABLE_PLUGINS}>)`
     (mirroring the `GTE_ENABLE_NETWORK`/etc. precedent immediately above it).
   - New `target_link_libraries(gte_core PUBLIC gte_plugin_abi)`.
   - New `gte_plugin_abi_handshake_probe` executable target (see point 6).
8. **`.gitignore`** — new `/build-plugin-abi-handshake-probe/` entry,
   mirroring the pre-existing `/build-player-link-probe/` precedent.

## Step 4 verification (mandated by the phase file)

1. **Incremental compile, in order**: `demo_hello_world` → `gte_core` →
   `gte_editor` → `GreatTamanaEditor`. All four built cleanly (see raw
   Ninja output captured during this phase — `demo_hello_world`: 2 steps;
   `gte_core`: 146 steps, including the new `PluginHost.cpp.obj`; `gte_editor`:
   59 steps; `GreatTamanaEditor`: linked + every shader staged + `SDL3.dll`
   copied). `gte_plugin_abi` itself is an `INTERFACE` library — configure
   succeeding (confirmed via a fresh `cmake -S . -B build`) is its own
   "compile check."
2. **`demo_hello_world.dll` genuinely exists in `<build-dir>/plugins/`** —
   confirmed via `browse_dir`. **Real, discovered deviation** (see below):
   the produced file is initially named `libdemo_hello_world.dll` (MinGW's
   default `SHARED`-library "lib" prefix), not `demo_hello_world.dll` as the
   phase file's own sketch assumes — fixed with `PREFIX ""`.
3. **Live runtime smoke test**: `run_app_background` the rebuilt
   `GreatTamanaEditor.exe` → `gte_send_request("/get_logs?limit=20")` — a
   real `"Loaded plugin 'HelloWorldPlugin' v1.0.0 from
   ...\build\plugins\demo_hello_world.dll"` `Info`-level `PluginHost` line
   confirmed present → `gte_send_request("/get_swapchain")` — screenshot
   confirmed the Editor renders identically to every prior campaign's own
   baseline (Scene/Hierarchy/Inspector/Project panels, sky gradient, dock
   layout — nothing regressed) → `stop_app_background`.
4. **Standalone handshake probe, built + run fresh** (nested-cmake pattern,
   `cmake -S tools/ci/gte_plugin_abi_handshake_probe -B build-plugin-abi-
   handshake-probe -G Ninja` then `cmake --build
   build-plugin-abi-handshake-probe`, which itself runs the inner
   `GTE_CORE_STANDALONE_PROBE_ONLY=ON`/`GTE_ENABLE_PLUGINS=ON`/
   `GTE_BUILD_TESTS=OFF` configure + build, transitively building
   `demo_hello_world.dll` via the `add_dependencies()` — confirmed this
   transitive dependency actually triggered a build of
   `demo_hello_world.dll`, no explicit second `--target` step was needed):
   running the resulting `.exe` printed
   `OK: loaded plugin 'HelloWorldPlugin' v1.0.0 - Milestone 0 handshake
   proof - implements zero capabilities.` and exited `0`.
5. **Negative test (mandatory, Step 4 point 5)** — temporarily renamed
   `demo_hello_world`'s `GTE_CreatePluginModule` export (source-level rename,
   not a fingerprint-generation-number bump — a simpler, equally valid way to
   force a real "missing export" failure path), rebuilt `demo_hello_world`
   only, and re-ran BOTH consumers:
   - `GreatTamanaEditor.exe`: `GET /get_logs` showed a real `Warning`-level
     `PluginHost` line — `"...demo_hello_world.dll is missing export
     GTE_CreatePluginModule, not a valid plugin, skipping"` — and the engine
     kept running completely normally (`GET /get_swapchain` still returned a
     correct, unchanged screenshot — no crash, no partial load).
   - `gte_plugin_abi_handshake_probe.exe`: printed
     `FAIL: demo_hello_world.dll is missing at least one of the 3 fixed
     exports` and returned a non-zero exit code.
   Reverted the deliberate breakage immediately after, rebuilt both
   `demo_hello_world` targets again, and re-confirmed BOTH consumers are back
   to their normal, successful "OK"/"Loaded plugin" behavior before
   committing anything.

## Deviations from the strategy doc (real, discovered, not silently smoothed over)

### Deviation #1 — the two Minor-Note-flagged include-path corrections (PHASE1's own flagged risk, hit exactly as predicted)

`PHASE1_COMPLETION_REPORT.md`'s own "Minor note" flagged that
`GtePluginAbiFingerprintGenerated.h` cannot be `#include`d via a literal
`../../../plugins/gte_plugin_abi/...` relative-source-tree path (it is a
CMake `configure_file()` OUTPUT, not a committed source file) — the correct
spelling relies on `gte_plugin_abi`'s own `INTERFACE` include directory
instead. This phase's own `PluginHost.cpp` sketch (Step 3.2) and
`HelloWorldPlugin.cpp` sketch (Step 3.5) BOTH still write the literal,
broken relative-path spelling — confirmed, applied the same fix PHASE1
already documented (`#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"`)
to both files, and to the new standalone probe's own `main.cpp`, which needed
the identical treatment (not previously flagged, since the probe didn't exist
yet during PHASE1). This is exactly the kind of "real source may have
drifted from the strategy doc's own necessarily-approximate sketch" case
Universal Rule 9 anticipates, and PHASE1 explicitly pre-warned about it —
applied directly rather than re-discovering it the hard way.

### Deviation #2 — MinGW's default "lib" prefix on `demo_hello_world.dll`'s own filename (real, newly discovered this phase)

The phase file's own Step 3.6 `main.cpp` sketch, and this phase's own first
draft of the standalone probe, assume the built plugin's own file is
literally named `demo_hello_world.dll`. Mechanically confirmed, on the real
build: MinGW/GCC's default `SHARED`-library naming convention prepends `lib`
to the CMake target name, producing `libdemo_hello_world.dll` instead — first
surfaced as a real `LoadLibraryW` failure (`GetLastError=126`,
`ERROR_MOD_NOT_FOUND`) when the standalone probe tried to load the hardcoded
`demo_hello_world.dll` path and found nothing there. `PluginHost` itself was
never affected by this (it scans the whole directory for any `*.dll`
regardless of name — confirmed working correctly with the `lib`-prefixed name
during the FIRST runtime smoke test, before this fix), but a stable,
predictable name is needed for the standalone probe (and any future
tooling/documentation) to hardcode a path against. Fixed by adding
`PREFIX ""` to `plugins/demo_hello_world/CMakeLists.txt`'s own
`set_target_properties()` call — the produced file is now plainly
`demo_hello_world.dll`, confirmed via a clean rebuild and a fresh
`browse_dir`/runtime smoke test of both consumers (the Editor and the
standalone probe) after the fix.

### Deviation #3 — a real, honestly-flagged tension between this phase's own literal CMake gating and `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision #6's own wording (not fully resolved, but not blocking)

`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision #6 states
`GTE_ENABLE_PLUGINS=OFF` means "the engine never scans/loads any `.dll` at
all, at zero cost, **while `PluginHost`/every capability interface still
compiles and is still testable**." This phase's own Step 3.6 code (and
`PHASE1_COMPLETION_REPORT.md`'s own restatement of it) both explicitly gate
`add_subdirectory(plugins/gte_plugin_abi)` — and therefore the
`GtePluginAbiFingerprintGenerated.h` CMake target `PluginHost.cpp` needs to
compile at all — behind `if(GTE_ENABLE_PLUGINS)`. Taken completely literally,
these two statements are in tension: with `GTE_ENABLE_PLUGINS=OFF`,
`gte_plugin_abi`'s own target would never exist, and `gte_core`'s own
unconditional `target_link_libraries(gte_core PUBLIC gte_plugin_abi)` /
`PluginHost.cpp`'s own unconditional `#include
"gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"` would very likely fail
that specific configuration's own build — i.e. `PluginHost` would NOT "still
compile" with the switch OFF, contradicting Locked Design Decision #6's own
plain-English claim.

This phase implemented the two `add_subdirectory()` calls and the
`target_link_libraries(gte_core PUBLIC gte_plugin_abi)` call EXACTLY as this
phase file's own Step 3.3/3.6 literally specify (both of which agree with
each other, and with `PHASE1_COMPLETION_REPORT.md`'s own prior restatement),
rather than silently reinterpreting either — since `GTE_ENABLE_PLUGINS`
defaults to `ON` (Locked Design Decision #6) and this phase's own Step 4
verification never asks for a `GTE_ENABLE_PLUGINS=OFF` build to be exercised,
this tension does not block or affect anything this phase actually needed to
prove; every real build/run performed this phase used the default `ON`
value and succeeded cleanly. This is flagged here explicitly, honestly, and
left UNRESOLVED (not guessed at) rather than silently picking one reading —
it is a genuine, pre-existing documentation inconsistency between two prior
phase artifacts (`PHASE0`'s prose vs. `PHASE1`+`PHASE2`'s own literal,
mutually-consistent code), not something this phase introduced, and a future
phase/task that actually needs to build and exercise
`GTE_ENABLE_PLUGINS=OFF` should resolve it deliberately (most likely by
making `add_subdirectory(plugins/gte_plugin_abi)` itself unconditional, while
keeping `demo_hello_world`'s own `add_subdirectory()` gated) rather than
inheriting this note as a surprise.

## Files added/changed

- `src/Core/Plugins/PluginHost.h` (new)
- `src/Core/Plugins/PluginHost.cpp` (new)
- `src/Core/Core.h` (modified — `#include "Plugins/PluginHost.h"`,
  `LoadPlugins()`/`GetPluginHost()`, `m_pluginHost` member)
- `src/Core/Core.cpp` (modified — `Core::LoadPlugins()` implementation)
- `src/Editor/ProjectRootPath.h` (modified — new `ExecutableDirectory()`
  declaration)
- `src/Editor/ProjectRootPath.cpp` (modified — shared
  `ResolveExecutableDirectoryBasePath()` helper, `ExecutableDirectory()`
  implementation)
- `src/Editor/EditorHost.cpp` (modified — new includes, constructor wiring)
- `plugins/demo_hello_world/CMakeLists.txt` (new)
- `plugins/demo_hello_world/HelloWorldPlugin.cpp` (new)
- `tools/ci/gte_plugin_abi_handshake_probe/CMakeLists.txt` (new)
- `tools/ci/gte_plugin_abi_handshake_probe/main.cpp` (new)
- `tools/ci/gte_plugin_abi_handshake_probe/README.md` (new)
- `CMakeLists.txt` (modified — `GTE_PLUGIN_RUNTIME_OUTPUT_DIR` +
  `add_subdirectory()` calls, `gte_core` `target_sources()`/
  `target_compile_definitions()`/`target_link_libraries()` additions, new
  `gte_plugin_abi_handshake_probe` executable target)
- `.gitignore` (modified — new `/build-plugin-abi-handshake-probe/` entry)

## Non-Goals honored (per the phase file's own list)

`demo_hello_world` implements zero capabilities (`QueryCapability()` always
returns `nullptr`) — no `IRenderFeatureModule_v1`/`IEditorPanelModule_v1` were
touched. No `gte_editor`-side plugin consumption exists yet — `DockLayout`/
the Editor's panel system were not touched at all this phase (PHASE4's job).

## Compile-check summary (for the record)

- `demo_hello_world`: built cleanly (2 Ninja steps).
- `gte_core`: built cleanly (146 Ninja steps, including the new
  `PluginHost.cpp.obj`).
- `gte_editor`: built cleanly (59 Ninja steps, untouched by this phase's own
  source changes beyond the one `EditorHost.cpp` edit).
- `GreatTamanaEditor`: linked cleanly; every shader staged; `SDL3.dll`
  copied; live `run_app_background` + `GET /get_logs` + `GET /get_swapchain`
  smoke test passed (plugin loaded, rendering unchanged).
- `gte_plugin_abi_handshake_probe` (nested-cmake standalone probe): built and
  ran successfully, printing the expected module info and exiting `0`.
- Negative test: both consumers (the Editor and the standalone probe) logged
  a clear, correct failure reason and neither crashed; both were confirmed
  restored to normal, successful behavior after reverting the deliberate
  breakage.
