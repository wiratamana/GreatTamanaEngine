# PHASE5 — Multi-Render-Feature-Plugin Warning + Regression Lock — COMPLETION REPORT

**Status:** DONE. Verified with an incremental `gte_core` + full `build`
rebuild, a filtered `PluginRenderFeatureDiagnosticsTest.*` run, a live
`GreatTamanaEditor.exe` run via `GET /get_logs` + `GET /get_swapchain`, and a
rebuilt/re-run `tools/ci/gte_plugin_isolation_probe`.

## What changed

1. **`src/Core/Plugins/PluginRenderFeatureDiagnostics.h`/`.cpp`** (new) — a
   small, pure, Tier-1-testable `CountModulesImplementingRenderFeature(const
   std::vector<IPluginModule*>&)` free function, exactly per the phase file's
   Step 3.1 (word-for-word). Added to root `CMakeLists.txt`'s `gte_core`
   unconditional source list, right next to the existing
   `PluginRenderPassBuilderAdapter.h/.cpp` entries.

2. **`src/Core/Core.cpp`** — added `#include "Plugins/PluginRenderFeatureDiagnostics.h"`
   and `#include "Logging.h"` (the latter was missing before this phase —
   `Core.cpp` never logged anything until now). `Core::LoadPlugins()` now
   calls `CountModulesImplementingRenderFeature(m_pluginHost.AllLoadedModules())`
   right after `m_pluginHost.LoadPlugins(pluginsDirectory)`, and logs exactly
   one `GTE_LOG_WARNING("PluginHost", ...)` when the count is `> 1`, wording
   matching the phase file's Step 3.2 text exactly.

3. **`plugins/demo_render_feature_second/`** (new throwaway demo plugin) —
   `RenderFeaturePlugin.cpp` mirrors `plugins/demo_render_feature/
   RenderFeaturePlugin.cpp`'s exact shape, with: class names
   `DemoRenderFeatureSecond`/`DemoRenderFeatureSecondPluginModule`,
   `GetModuleInfo()`'s `name` = `"DemoRenderFeaturePluginSecond"` and a
   distinguishing `description`, pass name
   `"DemoRenderFeatureSecondPlugin_Clear"` (confirmed unique — read
   `PluginRenderPassBuilderAdapter.cpp` and `IPluginRenderPassBuilder.h`
   first: `debugName` is just forwarded straight into
   `RenderGraphBuilder::AddRenderPass()`'s own pass name, no hidden global
   registry beyond the per-frame graph itself, so a distinct literal per
   plugin is exactly what's required — no ambiguity, no `ask_questions`
   needed). The clear color stays the exact SAME solid magenta
   `(1.0f, 0.0f, 1.0f, 1.0f)` as the first demo, on purpose, so the visual
   baseline stays unchanged regardless of which plugin "wins" per frame.
   `CMakeLists.txt` mirrors `demo_render_feature/CMakeLists.txt`'s shape and
   calls `gte_apply_plugin_dll_shared_crt_linkage(demo_render_feature_second)`
   — PHASE4's helper, confirmed already landed via its own completion report
   before writing this file.

4. **Root `CMakeLists.txt`** — `add_subdirectory(plugins/demo_render_feature_second)`
   added right after the existing `add_subdirectory(plugins/demo_render_feature)`
   line, inside the same `if(GTE_ENABLE_PLUGINS)` block PHASE2 introduced
   (confirmed present, unchanged in shape). The
   `add_dependencies(gte_plugin_isolation_probe ...)` line (root
   `CMakeLists.txt`, NOT the probe's own small wrapper `CMakeLists.txt`) now
   also depends on `demo_render_feature_second`.

5. **`tools/ci/gte_plugin_isolation_probe/main.cpp`** — `LoadedModuleCount()`
   assertion changed `3` → `4`, `renderFeatureCount` assertion changed `1` →
   `2`, both the `FAIL:` message strings and the final `PASS:` message string
   updated to match, and the top-of-file/inline doc comments mentioning
   "three demo plugins"/"exactly one" updated to "four"/"two" so prose and
   code stay in sync. Also lightly touched (not required by the phase file,
   but left factually correct rather than stale) the probe's own
   `CMakeLists.txt` comments and `README.md`, which describe the same
   3-plugin/1-render-feature numbers in prose — updated to 4/2 for the same
   reason.

6. **`tests/Core/PluginRenderFeatureDiagnosticsTests.cpp`** (new) — the exact
   3 `TEST()` cases from the phase file's Step 3.6, verbatim (empty vector →
   0, mixed vector → counts only the modules that implement the capability,
   all-without → 0). Added to `tests/CMakeLists.txt`'s source list, in the
   `Core/` grouping right after `Core/EditorPanelRegistryTests.cpp`.

7. **`docs/conventions/plugin-architecture.md`** — added the one short
   paragraph from Step 3.7, right after the PHASE1 "Honest correction"
   paragraph, documenting the real, current "2+ plugins silently overwrite
   each other, `Core::LoadPlugins()` now warns" behavior.

No other files were touched. `IPluginModule`, `IRenderFeatureModule_v1`,
`IPluginRenderPassBuilder`, `PluginHost`'s public interface, and the
`"PluginRenderFeatures"` render-graph provider's own logic are all unchanged
— this phase adds a diagnostic (a pure counting function + one log line) and
a second demo `.dll`, it does not touch the actual rendering/compositing
behavior in any way. The 10 Locked Design Decisions from
`editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md` are unaffected.

## Verification evidence

1. `cmake -S . -B build` — succeeded (plain reconfigure, new CMake
   subdirectory picked up: `demo_render_feature_second`'s own honest
   shared-CRT no-op warning now appears alongside the other 3 demo plugins').
2. `cmake --build build --target gte_core` — succeeded, 3/3 steps:
   ```
   [1/3] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/PluginRenderFeatureDiagnostics.cpp.obj
   [2/3] Building CXX object CMakeFiles/gte_core.dir/src/Core/Core.cpp.obj
   [3/3] Linking CXX static library libgte_core.a
   ```
3. `cmake --build build` — succeeded, 5/5 steps (built
   `demo_render_feature_second.dll`, the new test `.obj`, relinked
   `GreatTamanaEditor.exe` and `GreatTamanaEngineTests.exe`), zero errors.
4. `build\tests\GreatTamanaEngineTests.exe --gtest_filter=PluginRenderFeatureDiagnosticsTest.*`
   — **3 tests from 1 test suite, 3 PASSED, 0 failed**:
   ```
   [ RUN      ] PluginRenderFeatureDiagnosticsTest.CountModulesImplementingRenderFeature_EmptyVectorReturnsZero
   [       OK ] PluginRenderFeatureDiagnosticsTest.CountModulesImplementingRenderFeature_EmptyVectorReturnsZero (0 ms)
   [ RUN      ] PluginRenderFeatureDiagnosticsTest.CountModulesImplementingRenderFeature_CountsOnlyModulesThatImplementIt
   [       OK ] PluginRenderFeatureDiagnosticsTest.CountModulesImplementingRenderFeature_CountsOnlyModulesThatImplementIt (0 ms)
   [ RUN      ] PluginRenderFeatureDiagnosticsTest.CountModulesImplementingRenderFeature_ZeroWhenNoneImplementIt
   [       OK ] PluginRenderFeatureDiagnosticsTest.CountModulesImplementingRenderFeature_ZeroWhenNoneImplementIt (0 ms)
   [==========] 3 tests from 1 test suite ran. (0 ms total)
   [  PASSED  ] 3 tests.
   ```
5. Live check: `run_app_background` on the rebuilt `GreatTamanaEditor.exe`
   (PID 5932).
   - `GET /get_logs?limit=50` → `200`, `count:8`. All 4 demo plugins loaded
     (`DemoEditorPanelPlugin`, `HelloWorldPlugin`, `DemoRenderFeaturePlugin`,
     `DemoRenderFeaturePluginSecond`), and the exact expected new warning
     appears (`id:6`):
     ```
     category=PluginHost, level=Warning:
     "2 loaded plugins implement IRenderFeatureModule_v1 - only the
     LAST-registered one's render output will be visible this frame
     (render-graph compositing for multiple render-feature plugins is not
     implemented - see docs/conventions/plugin-architecture.md)."
     ```
   - `GET /get_swapchain` → `200`, `image/png` — **visually confirmed via
     `load_image`-equivalent direct image content**: the Scene View and Game
     View are both STILL solid magenta, completely unchanged from every
     prior campaign's own documented baseline. No visual regression.
   - `stop_app_background(pid: 5932)` — stopped cleanly.
6. Rebuilt+re-ran `tools/ci/gte_plugin_isolation_probe` per its own
   nested-configure convention:
   ```
   cmake -S tools/ci/gte_plugin_isolation_probe -B build-plugin-isolation-probe -G Ninja
   cmake --build build-plugin-isolation-probe
   build-plugin-isolation-probe\gte_plugin_isolation_inner_build\gte_plugin_isolation_probe.exe
   ```
   Output:
   ```
   Loaded 'DemoEditorPanelPlugin' - IRenderFeatureModule_v1: no
   Loaded 'HelloWorldPlugin' - IRenderFeatureModule_v1: no
   Loaded 'DemoRenderFeaturePlugin' - IRenderFeatureModule_v1: yes
   Loaded 'DemoRenderFeaturePluginSecond' - IRenderFeatureModule_v1: yes
   PASS: 4 plugin(s) loaded, exactly 2 implement IRenderFeatureModule_v1, and
   this file never once asked any plugin for IEditorPanelModule_v1.
   ```
   Exactly the expected 4 plugins loaded, 2 implementing
   `IRenderFeatureModule_v1`, and the isolation proof (never referencing
   `IEditorPanelModule_v1`) still holds structurally (unchanged source).

## Deviations from the plan

None material. One minor addition beyond the phase file's own literal scope:
`tools/ci/gte_plugin_isolation_probe/CMakeLists.txt`'s comments and its
`README.md` also mention the old "three demo plugins"/"exactly one" numbers
in prose — these were updated too (not strictly required by the phase file,
which only calls out `main.cpp`), to avoid leaving now-factually-wrong
documentation sitting right next to the file that was fixed. No design
ambiguity was encountered that required `ask_questions` — the pass-name
uniqueness question the phase file specifically flagged was resolved by
reading `PluginRenderPassBuilderAdapter.cpp`/`IPluginRenderPassBuilder.h`
directly: `debugName` is a plain pass name forwarded into
`RenderGraphBuilder::AddRenderPass()`, so a distinct literal string per
plugin (already the phase file's own plan) is correct and sufficient.

## Locked Design Decisions check

No architectural shape changed: `IPluginModule`, `IRenderFeatureModule_v1`,
`IPluginRenderPassBuilder`, and `PluginHost`'s public interface are all
unchanged. `demo_render_feature_second.dll` links ONLY `gte_plugin_abi`
(Locked Design Decision #2), mirroring every other demo plugin. This phase
adds a diagnostic counting function + one log line + a second throwaway demo
plugin — it does not change any of the 10 Locked Design Decisions from
`task_manager/editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`.
