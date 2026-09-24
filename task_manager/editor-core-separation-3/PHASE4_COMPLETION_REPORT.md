# PHASE4 COMPLETION REPORT — `IEditorPanelModule_v1`: A Plugin Gets a Real Dockable Panel

**Phase file**: `PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md`. **Parent**:
`PHASE0_MASTER_STRATEGY.md`. **Predecessors**:
`PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md`,
`PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md`,
`PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md` (all three read in full, along
with their own `PHASEn_COMPLETION_REPORT.md`, before starting). **Branch**:
`feature/editor-core-separation` (confirmed via `git_status` before starting —
clean tree, correct branch — and unchanged throughout).

## Summary — what was actually built

1. **`plugins/gte_plugin_abi/IEditorPanelModule.h`** (new) —
   `IEditorPanelModule_v1` + `kIEditorPanelModule_v1_Name`, exactly per
   Step 3.1.
2. **`plugins/gte_plugin_abi/IPluginPanelDrawContext.h`** (new) —
   `IPluginPanelDrawContext::Text()`/`Button()`/`Separator()`, exactly per
   Step 3.2 — the ONLY way a plugin ever draws ImGui content, closing the
   ImGui-shared-global-context hazard identified in
   `PHASE0_MASTER_STRATEGY.md`, Step 2.4.
3. **`src/Editor/Plugins/PluginPanelDrawContextAdapter.h/.cpp`** (new,
   `gte_editor`) — the host-side implementation, each method a one-line
   forward into the real `ImGui::*` API (`ImGui::TextUnformatted()`,
   `ImGui::Button()`, `ImGui::Separator()`), exactly per Step 3.3.
4. **`src/Core/EditorPanelRegistry.h/.cpp`** (new, `gte_core`) — a
   Meyers-singleton registry replacing `EditorPanelCatalog.h`'s fixed
   `kKnownEditorPanelNames[]` array: `RegisterBuiltinPanelName()`,
   `RegisterPluginPanel()`, `IsKnownName()`, `AllNames()`, `PluginPanels()`,
   exactly per Step 3.4.
5. **`src/Core/EditorPanelCatalog.h` deleted outright** (Step 3.4's own
   instruction — "delete the old mechanism, don't leave a parallel one",
   mirroring `editor-core-separation-2` PHASE2's precedent) — every real call
   site converted to `EditorPanelRegistry::Instance()`.
6. **`src/Editor/DockLayout.cpp`** — both real loops over
   `kKnownEditorPanelNames` (confirmed via `read_file` — one inside
   `DefaultDockLayoutIsNeeded()`, a SEPARATE second one physically inside
   `BuildDockspaceAndMenuBar()` itself) converted to iterate
   `EditorPanelRegistry::Instance().AllNames()`; `BuildDefaultDockLayout()`
   gained a new generic loop over `PluginPanels()`, appended immediately
   before `ImGui::DockBuilderFinish(dockspaceId)`, docking every plugin panel
   into the same `bottom` node as Memory/Profiler/Render Graph/Jobs/
   Atmosphere/Log/Project — exactly per Step 3.5. Every existing literal
   `DockBuilderDockWindow("Hierarchy", left)`-style call for built-in panels
   stayed byte-for-byte unchanged.
7. **`src/Editor/EditorHost.cpp`** — immediately after the existing
   `#if GTE_ENABLE_PLUGINS m_core.LoadPlugins(...) #endif` block (PHASE2),
   added the built-in-panel registration (confirmed byte-for-byte against
   `EditorPanelCatalog.h`'s real, current `kKnownEditorPanelNames[]` content
   BEFORE deleting that file, per the phase file's own explicit warning that
   this is "the ONE place regression risk lives in this whole phase") plus the
   post-`LoadPlugins()` plugin-panel query loop — exactly per Step 3.6.
8. **`src/Editor/ImGuiEditorLayer.cpp`**'s `BuildUI()` — confirmed the real,
   current end-of-function shape via `read_file` first (the phase file's own
   sketch's description matched exactly: the last unconditional call is
   `m_frameDebuggerPanel.Build(...)`, followed by an
   `#if GTE_ENABLE_PROJECT_PANEL ... #endif` block, then the closing `}`) —
   appended the new generic `#if GTE_ENABLE_PLUGINS` plugin-panel loop
   immediately after that `#endif`, calling `ImGui::Begin()`/`ImGui::End()`
   host-side and only the panel's own content through a freshly-constructed
   `PluginPanelDrawContextAdapter` — exactly per Step 3.7.
9. **`src/Network/NetworkRoutes.h`/`.cpp`** — every
   `kKnownEditorPanelNames`/`IsKnownEditorPanelName(...)` call site (confirmed
   via `search_in_dir` first: `ParseActivateTabQuery()`'s
   `IsKnownEditorPanelName(nameParam)` call and `BuildListTabsResponseJson()`'s
   own loop) converted to `EditorPanelRegistry::Instance().AllNames()`/
   `.IsKnownName(...)` — exactly per Step 3.8. `GET /list_tabs`'s JSON array
   now genuinely includes plugin panel names too, the correct, intended,
   additive behavior change.
10. **`plugins/demo_editor_panel/`** (new) — `CMakeLists.txt` (mirrors
    `plugins/demo_render_feature/CMakeLists.txt`'s exact shape, including its
    `PREFIX ""` fix and `gte_apply_plugin_shared_crt_linkage()` call) +
    `EditorPanelPlugin.cpp` (implements `IEditorPanelModule_v1`:
    `GetPanelName()` returns `"Demo Plugin Panel"`; `BuildPanel(ctx)` calls
    `ctx.Text("Hello from a plugin!");`), implementing ONLY the editor-tier
    capability — proving a plugin need not implement both halves of a
    "feature" to be valid — exactly per Step 3.9.
11. **Root `CMakeLists.txt`**:
    - `src/Core/EditorPanelCatalog.h` removed from `gte_core`'s
      `target_sources()`, replaced with
      `src/Core/EditorPanelRegistry.h`/`.cpp`.
    - `src/Editor/Plugins/PluginPanelDrawContextAdapter.h`/`.cpp` added to
      `gte_editor`'s `target_sources()`, immediately before
      `src/Editor/ImGuiEditorLayer.cpp`.
    - `add_subdirectory(plugins/demo_editor_panel)`, added immediately after
      the existing `add_subdirectory(plugins/demo_render_feature)` line
      inside the same `if(GTE_ENABLE_PLUGINS)` block.

## Step 4 verification (mandated by the phase file) — final, passing state

1. **Incremental compile, in order**: `demo_editor_panel` → `gte_core` →
   `gte_editor` → `GreatTamanaEditor`. All built cleanly (after one real,
   necessary correction — see Deviation #1 below). `demo_editor_panel.dll`
   confirmed to genuinely exist in `build/plugins/` via `browse_dir`.
2. **Live runtime smoke test**: `run_app_background` → `GET /get_logs?limit=30`
   — confirmed all three plugins (`DemoEditorPanelPlugin`, `HelloWorldPlugin`,
   `DemoRenderFeaturePlugin`) logged as loaded (`PluginHost` category, `Info`
   level), zero warnings/errors (`GET /get_logs?limit=50&min_level=warning`
   returned `{"count":0,...}`).
3. **`GET /list_tabs`** returned
   `["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render
   Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]` — every
   pre-existing built-in name present, unchanged, plus exactly one new one
   (`"Project"` present because this build has `GTE_ENABLE_PROJECT_PANEL=ON`).
4. **`GET /activate_tab?name=Demo Plugin Panel`** → `{"success":true,
   "activated_tab":"Demo Plugin Panel"}` → **`GET /get_swapchain`** — visually
   confirmed the panel is genuinely visible, docked in the bottom strip
   alongside Memory/Profiler/Render Graph/Atmosphere/Jobs/Log/Project, active
   (highlighted) tab, showing "Hello from a plugin!" — see the screenshot
   captured during this phase's own session.
5. **Re-ran PHASE3's own Game View magenta-clear check**: `GET
   /activate_tab?name=Game` → `GET /get_game_view` — still solid magenta,
   confirming this phase's own `EditorPanelCatalog.h` deletion did not
   regress PHASE3's plugin render-feature mechanism.
6. `stop_app_background` — done after the live session above.
7. **Targeted test-suite verification** (beyond the phase file's own literal
   Step 4 list, but required by `AGENTS.md`'s "Testability & Regression
   Safety" section — deleting `EditorPanelCatalog.h` broke real, existing
   Tier-1 test coverage that had to be repaired in the same change, not left
   broken for PHASE6 to discover): built `GreatTamanaEngineTests` (171 Ninja
   steps, clean, zero errors) and ran a FILTERED subset (never the full
   suite, per Universal Rule 4) covering every test file this phase touched —
   `EditorPanelRegistryTest.*`, `ParseActivateTabQueryTests.*`,
   `BuildListTabsResponseJsonTests.*`, `BuildActivateTabResponseJsonTests.*`,
   `BuildUnknownTabNameResponseJsonTests.*`, `ActivateTabEndpointEndToEndTest.*`,
   `ActivateTabEndpointNoStandInTests.*`, `ActivateTabEndpointNullBridgeTests.*`,
   and `NetworkServerTests.ActivateTab*` — **22 + 2 = 24 tests, 100% passing**.

## Deviations from the strategy doc (real, discovered, not silently smoothed over)

### Deviation #1 — `ImGuiEditorLayer.cpp`'s new plugin-panel loop needs its own direct `#include` of `IEditorPanelModule.h`

The phase file's own Step 3.7 sketch assumes `EditorPanelRegistry::
PluginPanelEntry::module` (declared as `IEditorPanelModule_v1*` in
`EditorPanelRegistry.h`, which only forward-declares that class) is directly
usable from `ImGuiEditorLayer.cpp` once `EditorPanelRegistry.h` is included.
Mechanically confirmed via the real compiler: `entry.module->BuildPanel(...)`
fails with `error: invalid use of incomplete type 'class
gte::IEditorPanelModule_v1'`, since `EditorPanelRegistry.h` (correctly, by its
own design — it must not need a real `gte_plugin_abi` header at `gte_core`
compile time beyond the forward declaration) never pulls in the class's real
definition. Fixed by adding
`#include "../../plugins/gte_plugin_abi/IEditorPanelModule.h"` directly to
`ImGuiEditorLayer.cpp`, alongside its own new `PluginPanelDrawContextAdapter.h`
include — a one-line, mechanical, unsurprising fix, confirmed by a clean
rebuild immediately after.

### Deviation #2 — deleting `EditorPanelCatalog.h` broke three pre-existing test files that referenced its compile-time constants directly, requiring real (not merely cosmetic) test-code changes

The phase file's own Step 4 verification list does not mention tests at all.
However, `AGENTS.md`'s "Testability & Regression Safety" section is explicit:
"Every change to Tier 1 code must come with a matching test change" and "a
change can compile cleanly while still silently breaking" something — leaving
these three files broken for PHASE6 to discover would have been exactly that.
`search_in_dir` (per Universal Rule 9) found:

1. **`tests/Editor/EditorPanelCatalogTests.cpp`** — directly tested the
   now-deleted `IsKnownEditorPanelName()`/`kKnownEditorPanelNames[]`. Deleted
   outright (mirroring the phase file's own "delete the old mechanism, don't
   leave a parallel one" instruction for the production file) and replaced
   with a new **`tests/Core/EditorPanelRegistryTests.cpp`** (6 new tests
   covering `RegisterBuiltinPanelName()`/`RegisterPluginPanel()`/
   `IsKnownName()`/`AllNames()`/`PluginPanels()`, each using uniquely-named
   test-only panel names so they never collide with real built-in names or
   with each other regardless of gtest run order — `EditorPanelRegistry` is a
   process-wide Meyers singleton with no `Clear()`/reset method, by design, so
   tests must never assert an absolute list size, only that their OWN
   registered name(s) round-trip correctly). `tests/CMakeLists.txt`'s
   `GTE_TEST_SOURCES` list updated to match (old entry removed, new entry
   added next to `Core/LogSinkTests.cpp`, which this new file's own header
   comment explicitly mirrors the shape of).
2. **This new test file ALSO registers one global gtest `Environment`**
   (`SeedBuiltinEditorPanelNamesEnvironment`, mirroring
   `tests/Core/LogSinkTests.cpp`'s own `InstallRealLoggerSinkEnvironment`
   precedent exactly) that seeds `EditorPanelRegistry` with the SAME real
   built-in panel names `EditorHost.cpp`'s own constructor registers in
   production — necessary because this test binary never constructs a real
   `EditorHost`, so without this, `EditorPanelRegistry` starts genuinely empty
   and every pre-existing test elsewhere in the SAME binary that activates a
   real panel name like `"Profiler"` (expecting it to be recognized) would
   start failing for a completely different, confusing reason (a real
   regression, not a false alarm).
3. **`tests/Network/ActivateTabEndpointEndToEndTests.cpp`** — its own
   `#include "Core/EditorPanelCatalog.h"` updated to
   `#include "Core/EditorPanelRegistry.h"`; its
   `ListTabsReturnsEveryKnownPanelName` test (asserted against
   `gte::kKnownEditorPanelNameCount`/`gte::kKnownEditorPanelNames[i]`, both
   now-deleted) rewritten as `ListTabsReturnsEveryRegisteredPanelName`,
   registering one uniquely-named test-only panel first and then comparing
   the HTTP response's own `"tabs"` array against
   `EditorPanelRegistry::Instance().AllNames()`'s own live state directly —
   proving real, generic behavior rather than asserting a value that no
   longer exists. Its fixture's `SetUp()` and one standalone
   `ActivateTabEndpointNullBridgeTests.ActivateTabReturns503WhenBridgeIsNull`
   test both gained an explicit, defensive
   `EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Profiler")` call
   (harmless if the global Environment above already registered it — a
   duplicate `IsKnownName()` entry is inert) as belt-and-suspenders insurance,
   since these specific tests activate `"Profiler"` by name and must not
   depend on cross-file gtest execution ordering.
4. **`tests/Network/NetworkRoutesTests.cpp`**'s
   `BuildListTabsResponseJsonTests.ContainsEveryKnownPanelName` test (same
   now-deleted constants) rewritten identically to (3) above, as
   `ContainsEveryRegisteredPanelName`.

All four files, plus `NetworkServerTests.cpp` (compiled cleanly with zero
source changes needed — it depends only on `"Profiler"` being known, which
the new global Environment now guarantees for the whole binary), were
confirmed passing via a targeted, filtered `GreatTamanaEngineTests.exe
--gtest_filter=...` run (24 tests total, 100% passing) — never the full
suite, honoring Universal Rule 4.

### Minor, non-blocking documentation touch-ups (not required by the phase file, done for consistency)

Several doc-comment-only references to the now-deleted `Core/EditorPanelCatalog.h`
were updated to `Core/EditorPanelRegistry.h` for accuracy, mirroring
`editor-core-separation-2` PHASE1's own precedent of updating every comment
reference alongside a file rename/deletion: `src/Editor/DockLayout.h`,
`src/Editor/EditorLayer.h`, `src/Editor/Panels/FrameDebuggerPanel.h`,
`src/Application/EditorUiCommandBridge.h`, `src/Network/NetworkServer.cpp`,
`docs/conventions/networking.md`, `docs/conventions/logging.md`,
`docs/conventions/frame-debugger.md`. Historical `task_manager/*/PHASEn_*.md`
completion reports and `docs/CHANGELOG.md` were deliberately left untouched —
they correctly describe what was true at the time they were written, and
editing them would misrepresent history.

## Files added/changed

- `plugins/gte_plugin_abi/IEditorPanelModule.h` (new)
- `plugins/gte_plugin_abi/IPluginPanelDrawContext.h` (new)
- `src/Editor/Plugins/PluginPanelDrawContextAdapter.h` (new)
- `src/Editor/Plugins/PluginPanelDrawContextAdapter.cpp` (new)
- `src/Core/EditorPanelRegistry.h` (new)
- `src/Core/EditorPanelRegistry.cpp` (new)
- `src/Core/EditorPanelCatalog.h` (deleted)
- `src/Editor/DockLayout.cpp` (modified — registry conversion + plugin dock loop)
- `src/Editor/EditorHost.cpp` (modified — built-in registration + plugin query loop)
- `src/Editor/ImGuiEditorLayer.cpp` (modified — new generic plugin-panel loop + includes)
- `src/Network/NetworkRoutes.h` (modified — include + doc comments)
- `src/Network/NetworkRoutes.cpp` (modified — registry conversion)
- `src/Network/NetworkServer.cpp` (modified — doc comment)
- `src/Editor/DockLayout.h`, `src/Editor/EditorLayer.h`,
  `src/Editor/Panels/FrameDebuggerPanel.h`,
  `src/Application/EditorUiCommandBridge.h` (modified — doc comments only)
- `plugins/demo_editor_panel/CMakeLists.txt` (new)
- `plugins/demo_editor_panel/EditorPanelPlugin.cpp` (new)
- `CMakeLists.txt` (modified — `gte_core`/`gte_editor` `target_sources()`,
  `add_subdirectory(plugins/demo_editor_panel)`)
- `tests/Core/EditorPanelRegistryTests.cpp` (new)
- `tests/Editor/EditorPanelCatalogTests.cpp` (deleted)
- `tests/Network/ActivateTabEndpointEndToEndTests.cpp` (modified)
- `tests/Network/NetworkRoutesTests.cpp` (modified)
- `tests/CMakeLists.txt` (modified — test source list)
- `docs/conventions/networking.md`, `docs/conventions/logging.md`,
  `docs/conventions/frame-debugger.md` (modified — doc comments only)

## Non-Goals honored (per the phase file's own list)

`IPluginPanelDrawContext` stays deliberately narrow — exactly `Text()`/
`Button()`/`Separator()`, no sliders/tables/tree nodes. Every plugin panel is
tabbed into the same fixed `bottom` node — no per-plugin-panel custom dock
position was implemented.

## Compile-check summary (for the record)

- `demo_editor_panel`: built cleanly (2 Ninja steps).
- `gte_core`: built cleanly (7 Ninja steps, including the new
  `EditorPanelRegistry.cpp.obj`).
- `gte_editor`: built cleanly (one real fix needed mid-phase — Deviation #1 —
  then clean).
- `GreatTamanaEditor`: linked cleanly; every shader staged; `SDL3.dll` copied;
  live `run_app_background` + `GET /get_logs` + `GET /list_tabs` +
  `GET /activate_tab` + `GET /get_swapchain` + `GET /get_game_view` smoke test
  passed (all three plugins loaded, "Demo Plugin Panel" genuinely visible and
  docked, PHASE3's magenta clear unregressed, zero warnings/errors logged).
- `GreatTamanaEngineTests`: built cleanly (171 Ninja steps) after this phase's
  own required test-file repairs (Deviation #2); a targeted, filtered run of
  every test suite this phase touched (24 tests) passed 100%.
