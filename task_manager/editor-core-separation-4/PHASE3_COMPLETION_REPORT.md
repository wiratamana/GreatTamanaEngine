# PHASE3 — Editor Panel Name Collision Protection — COMPLETION REPORT

**Status:** DONE. Verified with an incremental `GreatTamanaEngineTests` build,
a narrow `EditorPanelRegistryTest.*` filtered run, and a live
`GreatTamanaEditor.exe` `/list_tabs` + `/get_logs` check.

## What changed

1. **`src/Core/EditorPanelRegistry.cpp`** —
   - Added `#include "Logging.h"` (was missing; this file never logged
     anything before this phase).
   - `RegisterPluginPanel()` now calls `IsKnownName(name)` FIRST. If the name
     is already known (a built-in panel name registered via
     `RegisterBuiltinPanelName()`, or an earlier-loaded plugin's own panel
     name), it emits exactly one `GTE_LOG_WARNING("EditorPanelRegistry", ...)`
     naming the exact colliding name and explaining why it was refused, then
     `return`s WITHOUT pushing anything into `m_allNames`/`m_pluginPanels` —
     the colliding plugin's panel is silently (from the plugin's own
     perspective) dropped, never shown, never docked, matching this codebase's
     established "clean skip, never crash, never silently corrupt state"
     convention already used throughout `PluginHost.cpp`'s own failure paths.
   - `RegisterBuiltinPanelName()` was left completely unguarded, on purpose,
     exactly as the phase file's Step 3.2 specifies — it is first-party,
     hand-written, trusted code (`EditorHost.cpp`'s own fixed list of 10 real
     built-in panel names), never touched by a plugin. No duplicate was found
     in that list during this phase's own read of `EditorHost.cpp`.
   - Wording of the log message and the doc-comment above the new `if` block
     match the phase file's Step 3.1 text exactly.

2. **`tests/Core/EditorPanelRegistryTests.cpp`** — added the two `TEST()`
   cases from the phase file's Step 3.3, verbatim, inserted right after the
   existing `RegisterPluginPanel_ModulePointerIsUsableAndDrawsThroughTheCuratedContext`
   test and before `IsKnownName_UnregisteredNameReturnsFalse`:
   - `RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredBuiltinName`
     — registers a built-in name, then tries to register a plugin panel with
     the identical name, and asserts it never appears in `PluginPanels()`.
   - `RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredPluginName`
     — registers two DIFFERENT `FakeEditorPanelModule` instances under the
     SAME name, and asserts exactly one entry exists in `PluginPanels()` for
     that name, and it still points at the FIRST module (first-registered
     wins, second is refused).
   - Both use the file's own existing `FakeEditorPanelModule` test double, its
     own existing naming convention (`Test_<Kind>_For_Collision_<GreekLetter>`),
     and its own existing "never a real built-in name, never reused elsewhere"
     discipline — no new test infrastructure was introduced.

3. **`docs/conventions/plugin-architecture.md`** — checked first via
   `search_in_dir("panel")` and `search_in_dir("RegisterPluginPanel")`: the
   file does NOT describe `RegisterPluginPanel()`'s behavior at all (only two
   unrelated mentions of the word "panel" exist, both about the overall
   capability shape, not registration semantics). Per the phase file's own
   Step 3.4 instruction ("If the file doesn't mention this level of detail at
   all today, skip this"), **no edit was made** — adding net-new documentation
   depth beyond what this phase's code change needs is explicitly out of
   scope.

No other files were touched. `RegisterBuiltinPanelName()`'s signature,
`IsKnownName()`, `AllNames()`, `PluginPanels()` are all unchanged. The plugin
architecture's shape (the 10 Locked Design Decisions from
`editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`) is unaffected — this
phase only adds a defensive refusal + a diagnostic log line inside an existing
method, changing no interface and no call site.

## Verification evidence

1. `cmake --build build --target GreatTamanaEngineTests` — succeeded, 5/5
   steps, zero errors:
   ```
   [1/5] Building CXX object CMakeFiles/gte_core.dir/src/Core/EditorPanelRegistry.cpp.obj
   [2/5] Linking CXX static library libgte_core.a
   [3/5] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Core/EditorPanelRegistryTests.cpp.obj
   [4/5] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Core/CoreHeadlessConstructionTests.cpp.obj
   [5/5] Linking CXX executable tests\GreatTamanaEngineTests.exe; Copying SDL3.dll next to GreatTamanaEngineTests
   ```

2. `build\tests\GreatTamanaEngineTests.exe --gtest_filter=EditorPanelRegistryTest.*`
   — **8 tests from 1 test suite, 8 PASSED, 0 failed** (the pre-existing 6
   tests plus this phase's 2 new ones, all green):
   ```
   [ RUN      ] EditorPanelRegistryTest.RegisterBuiltinPanelName_MakesIsKnownNameTrueAndAppearsInAllNames
   [       OK ] EditorPanelRegistryTest.RegisterBuiltinPanelName_MakesIsKnownNameTrueAndAppearsInAllNames (0 ms)
   [ RUN      ] EditorPanelRegistryTest.RegisterBuiltinPanelName_DoesNotAppearInPluginPanels
   [       OK ] EditorPanelRegistryTest.RegisterBuiltinPanelName_DoesNotAppearInPluginPanels (0 ms)
   [ RUN      ] EditorPanelRegistryTest.RegisterPluginPanel_MakesIsKnownNameTrueAndAppearsInAllNamesAndPluginPanels
   [       OK ] EditorPanelRegistryTest.RegisterPluginPanel_MakesIsKnownNameTrueAndAppearsInAllNamesAndPluginPanels (0 ms)
   [ RUN      ] EditorPanelRegistryTest.RegisterPluginPanel_ModulePointerIsUsableAndDrawsThroughTheCuratedContext
   [       OK ] EditorPanelRegistryTest.RegisterPluginPanel_ModulePointerIsUsableAndDrawsThroughTheCuratedContext (0 ms)
   [ RUN      ] EditorPanelRegistryTest.RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredBuiltinName
   [       OK ] EditorPanelRegistryTest.RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredBuiltinName (0 ms)
   [ RUN      ] EditorPanelRegistryTest.RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredPluginName
   [       OK ] EditorPanelRegistryTest.RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredPluginName (0 ms)
   [ RUN      ] EditorPanelRegistryTest.IsKnownName_UnregisteredNameReturnsFalse
   [       OK ] EditorPanelRegistryTest.IsKnownName_UnregisteredNameReturnsFalse (0 ms)
   [ RUN      ] EditorPanelRegistryTest.IsKnownName_IsCaseSensitive
   [       OK ] EditorPanelRegistryTest.IsKnownName_IsCaseSensitive (0 ms)
   [==========] 8 tests from 1 test suite ran. (4 ms total)
   [  PASSED  ] 8 tests.
   ```

3. `cmake --build build --target GreatTamanaEditor` — succeeded (1/1 link
   step, relinked with the updated `gte_core`).

4. Live check: launched `build\GreatTamanaEditor.exe` via `run_app_background`
   (PID 20488).
   - `GET /list_tabs` → `200`:
     ```
     {"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]}
     ```
     "Demo Plugin Panel" (from `demo_editor_panel`) is present **exactly
     once** — confirms this phase's new collision guard does NOT refuse the
     already-working, non-colliding demo plugin panel.
   - `GET /get_logs?limit=50` → `200`, `count:6` — all 3 demo plugins loaded
     successfully (`DemoEditorPanelPlugin`, `HelloWorldPlugin`,
     `DemoRenderFeaturePlugin`), the PHASE1 shared-CRT startup warning still
     fires as expected, and **zero `EditorPanelRegistry` log entries appear**
     — confirming no real collision occurred in production (as expected,
     since only one plugin registers a panel and its name does not collide
     with any built-in).
   - `stop_app_background(pid: 20488)` — stopped cleanly.

## Deviations from the plan

None. The exact code from the phase file's Step 3.1 (including the doc
comment) and Step 3.3 (both `TEST()` cases) was applied verbatim.
`RegisterBuiltinPanelName()` was correctly left unguarded per Step 3.2, and no
duplicate built-in name was found in `EditorHost.cpp`'s registration list.
`docs/conventions/plugin-architecture.md` was correctly left untouched per
Step 3.4's own guidance after a fresh `search_in_dir` check confirmed it does
not describe `RegisterPluginPanel()`'s behavior at all. No design ambiguity
was encountered that required `ask_questions`.

## Locked Design Decisions check

No architectural shape changed: this is a pure defensive addition inside an
existing method's body — no new interface, no new call site, no change to
`IEditorPanelModule_v1`, `EditorHost.cpp`'s registration order, or any of the
10 Locked Design Decisions from `task_manager/editor-core-separation-3/
CAMPAIGN_COMPLETION_REPORT.md`.
