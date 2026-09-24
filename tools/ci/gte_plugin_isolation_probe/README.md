# `gte_core` Plugin Isolation Probe

## What this is

A tiny, **manually-invocable** local CMake project, mirroring
`tools/ci/gte_plugin_abi_handshake_probe/`'s own nested-cmake-invocation shape
(see that project's own `README.md` for the full "why nested cmake, not
`add_subdirectory()`" reasoning, which applies here identically). It
configures the real repository root `CMakeLists.txt` with
`GTE_CORE_STANDALONE_PROBE_ONLY=ON` and `GTE_ENABLE_PLUGINS=ON`, then builds
ONE target that only exists in that configuration: `gte_plugin_isolation_probe`
- a tiny executable **linked against `gte_core.a` alone** (no `gte_editor`, no
SDL3, no ImGui at all - a genuinely Player-shaped process).

editor-core-separation-3 campaign, PHASE5
(`task_manager/editor-core-separation-3/PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md`)
- Milestone 3. Proves, mechanically, that "always all-in" (source design doc
Section 0/7) still keeps a Player-shaped process editor-clean: a process that
never links `gte_editor` at all, pointed at the exact same `plugins/` folder
`GreatTamanaEditor.exe` itself scans, still gets the runtime-tier plugin's
behavior (`demo_render_feature.dll`'s `IRenderFeatureModule_v1`), while the
editor-tier plugin (`demo_editor_panel.dll`, `IEditorPanelModule_v1`) sits
there, loaded, fully inert - never crashing, never producing any visible
effect, simply never asked for anything.

editor-core-separation-4 campaign, PHASE5
(`task_manager/editor-core-separation-4/PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md`)
added a SECOND `IRenderFeatureModule_v1` demo plugin (`demo_render_feature_second.dll`)
so this probe now genuinely exercises "2+ plugins implementing the same
capability" - it loads 4 demo plugins total (was 3), 2 of which implement
`IRenderFeatureModule_v1` (was 1).

## How it works

`main.cpp` uses `gte::PluginHost` directly (a plain `gte_core`-owned class
with zero GPU/window dependency of its own):

1. `PluginHost::LoadPlugins()` scans the shared `plugins/` folder (resolved
   relative to this exe's own directory via `GetModuleFileNameW`, mirroring
   `gte_plugin_abi_handshake_probe`'s own precedent) and loads every `.dll`
   found there - all four demo plugins (editor-core-separation-4 campaign
   PHASE5 added `demo_render_feature_second` - was three).
2. Confirms `LoadedModuleCount() == 4` exactly (not merely `> 0`) - ruling out
   a false-positive pass caused by a folder-path mistake.
3. Queries `IRenderFeatureModule_v1` on every loaded module, printing which
   ones implement it, and confirms exactly two do (`demo_render_feature` and
   `demo_render_feature_second`).
4. **This file never once references `IEditorPanelModule_v1` anywhere in its
   own source** - the isolation mechanism IS this absence of a call, made
   visible/checkable by a human reader of this file, not a runtime assertion.

Any failure prints a clear `FAIL: ...` message to `stderr` and returns a
non-zero exit code - success prints `PASS: ...` and returns `0`.

## Exact command to run this by hand

From the repository root:

```
cmake -S tools/ci/gte_plugin_isolation_probe -B build-plugin-isolation-probe -G Ninja
cmake --build build-plugin-isolation-probe
build-plugin-isolation-probe\gte_plugin_isolation_inner_build\gte_plugin_isolation_probe.exe
```

(`-G Ninja` is required on a machine whose default CMake generator isn't
already Ninja - this repository's own main build tree already uses Ninja, but
this OUTER wrapper project is configured independently and does not inherit
that choice automatically.)

## What a successful run proves

- `gte_core.a` links into a real, standalone EXECUTABLE (zero `gte_editor`
  involvement) that can still genuinely discover and load plugin `.dll`s
  through the real, production `PluginHost` class - not just a link-only
  proof (`gte_core_player_link_probe`'s own job).
- The runtime-tier capability (`IRenderFeatureModule_v1`) resolves correctly
  from a Player-shaped process, exactly as it does inside
  `GreatTamanaEditor.exe` (PHASE3), and now for TWO independent plugins at
  once (PHASE5, editor-core-separation-4 campaign).
- The editor-tier plugin (`demo_editor_panel.dll`) loads (its fingerprint/
  export handshake succeeds - `PluginHost` never distinguishes "runtime" from
  "editor" plugins at load time) but is never queried for
  `IEditorPanelModule_v1` anywhere in this probe's own source - "always
  all-in" genuinely stays editor-clean in a process with no `gte_editor` at
  all.

## What this probe deliberately does NOT do

- It does not exercise any real rendering (no `gte::Renderer`/Vulkan
  involvement at all - `PluginHost` itself has zero GPU dependency). Proving
  the runtime-tier plugin's render-graph pass actually renders correctly
  inside a headless `gte::Core` is `tools/ci/gte_core_player_link_probe`'s
  own separate, GPU/headless-surface-gated bonus check.
- It is not wired into any GitHub Actions workflow or other real CI system -
  none exists in this repository. Run it by hand whenever you want to
  re-confirm plugin isolation still holds.
- It never appears as a user-facing option inside the main build.
