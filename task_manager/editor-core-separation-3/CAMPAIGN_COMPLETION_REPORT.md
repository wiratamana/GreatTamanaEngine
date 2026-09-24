# editor-core-separation-3 — CAMPAIGN COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. This report is the single, top-level summary of the whole
6-phase campaign, mirroring `task_manager/editor-core-separation-2/CAMPAIGN_COMPLETION_REPORT.md`'s
own shape and tone. See each `PHASEn_COMPLETION_REPORT.md` in this same folder for full per-phase
detail.

## What this campaign set out to do

Give this engine a **real, dynamic, runtime-loadable plugin system**: a feature can ship as one or
more `.dll`s, dropped into a `plugins/` folder next to the built executable, and the running engine
discovers and uses them with **zero recompilation of the engine itself** and **zero per-plugin code
inside `gte_core`/`gte_editor`**. This campaign implements Milestones 0-3 of the source design doc's
own Section 11 phased roadmap (Milestone 4, hot reload, is explicitly, permanently out of scope).
Every plugin this campaign ships is a deliberately tiny, throwaway-quality proof — a solid-color
clear pass, one "hello from a plugin" ImGui panel — never a real engine feature migrated onto the
mechanism.

## What shipped, phase by phase

**PHASE1 — `gte_plugin_abi` Foundation.** The new `plugins/gte_plugin_abi/` module: the
fixed-size `GtePluginAbiFingerprint` POD struct + `operator==`, `IPluginModule`, the 3 fixed
`extern "C"` exports (`PluginExports.h`), a `configure_file()`-generated fingerprint header, and
`docs/conventions/plugin-architecture.md`. Also added `cmake/MingwRuntime.cmake`'s
`gte_apply_plugin_shared_crt_linkage()` helper. **Two real, necessary corrections to the strategy
doc's own literal code sketch**: `-shared-libstdc++` is not a real GCC flag (only `-shared-libgcc`
is; shared libstdc++ is that toolchain's own default when eligible), and — more significantly —
this development machine's ONLY installed MinGW toolchain was itself built `--disable-shared` and
cannot produce a shared-CRT-linked binary under any flag combination, discovered via two rounds of
`ask_questions`. Resolved with a configure-time capability probe
(`GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`) that turns the helper into a clean, honest,
one-line-per-target warning instead of either doing nothing silently or falsely claiming shared
linkage — a real, environment-driven limitation that persists, unchanged and correctly documented,
through every later phase and this closeout phase's own final evidence.

**PHASE2 — `PluginHost` + Hello-World Handshake Probe.** `gte::PluginHost`
(`src/Core/Plugins/PluginHost.h/.cpp`, new, in `gte_core`) implements the loader procedure: scan a
folder for `*.dll`, `LoadLibraryW()`, resolve the 3 fixed exports by name, fingerprint check
(byte-for-byte, every differing field named), `GTE_CreatePluginModule()` — every failure mode is a
clean, logged skip, never a crash. `Core::LoadPlugins()` is a thin, explicitly-called pass-through
(mirroring `SetEditorLayerHook()`'s own "host wires this in after construction" convention), called
once from `EditorHost`'s constructor. The first real demo plugin, `demo_hello_world` (implements
zero capabilities), and the first standalone CI probe outside `GreatTamanaEditor.exe` entirely,
`tools/ci/gte_plugin_abi_handshake_probe`, both shipped. A mandatory negative test (renaming the
`GTE_CreatePluginModule` export) confirmed both consumers degrade cleanly with a named warning/
failure message and no crash, then was reverted before committing. **Real, discovered deviation**:
MinGW's default `SHARED`-library "lib" filename prefix meant the built `.dll` was
`libdemo_hello_world.dll`, not the assumed `demo_hello_world.dll` — fixed with `PREFIX ""`.

**PHASE3 — `IRenderFeatureModule_v1`: a Plugin Contributes a Real Render-Graph Pass (the core of
this campaign).** `IRenderFeatureModule_v1`/`IPluginRenderPassBuilder` (new, `gte_plugin_abi`);
`PluginRenderPassBuilderAdapter` (new, `gte_core`) forwards `AddFullscreenClearPass()` into a real
`rg::RenderGraphBuilder::AddRenderPass()` call; a new `"PluginRenderFeatures"` provider in
`Core::RegisterOffscreenRenderPipelineProviders()` generically discovers and invokes every loaded
module implementing the capability, with zero hardcoded plugin name anywhere in `Core`. The demo
plugin, `demo_render_feature`, draws a solid magenta clear. **Three real, live-testing-confirmed
logic bugs, found only by actually running the engine and looking at the resulting image** (not
mere line-number drift): (1) the pass needed an explicit `RenderPassEvent::AfterEverything` tag or
it was scheduled — and overwritten — too early; (2) a correctly-ordered but never-read write-only
pass is silently culled as dead code unless it pushes its own handle into
`finalTextureOutputs` itself; (3) the significant one, resolved via `ask_questions` — the plugin
must draw onto the POST-composite `"GameViewComposited"`/`"SceneViewComposited"` texture, not the
raw pre-atmosphere-composite handle the phase file's own sketch assumed, requiring two new
`RenderPassBlackboard` keys the `"AtmosphereComposite"` provider now publishes.

**PHASE4 — `IEditorPanelModule_v1`: a Plugin Gets a Real Dockable Panel.**
`IEditorPanelModule_v1`/`IPluginPanelDrawContext` (new, `gte_plugin_abi`) — the ONLY way a plugin
ever draws ImGui content, closing the shared-`GImGui`-context hazard `PHASE0_MASTER_STRATEGY.md`'s
own Step 2.4 identified up front. `PluginPanelDrawContextAdapter` (new, `gte_editor`) forwards each
method into the real `ImGui::*` API host-side. `src/Core/EditorPanelCatalog.h`'s old, fixed
`kKnownEditorPanelNames[]` array was **deleted outright** and replaced by a real
`EditorPanelRegistry` singleton (`RegisterBuiltinPanelName()`/`RegisterPluginPanel()`/
`IsKnownName()`/`AllNames()`/`PluginPanels()`); `DockLayout.cpp`/`NetworkRoutes.cpp`/`EditorHost.cpp`
all converted to use it. The demo plugin, `demo_editor_panel`, implements ONLY the editor-tier
capability (proving a plugin need not implement both halves of a "feature"). **Real, necessary
follow-up work beyond the phase file's own literal Step 4 list**: deleting `EditorPanelCatalog.h`
broke three pre-existing test files that referenced its compile-time constants directly — repaired
in the same change per `AGENTS.md`'s "Testability & Regression Safety" mandate, including a brand
new `tests/Core/EditorPanelRegistryTests.cpp` (6 tests) replacing the deleted
`tests/Editor/EditorPanelCatalogTests.cpp` (4 tests) — this net `+2` test delta is exactly what this
closeout phase's own full `ctest` pass observed and explained.

**PHASE5 — Proving "Always All-In" Stays Editor-Clean in a Player-Shaped Process.**
`tools/ci/gte_plugin_isolation_probe` (new) — links `gte_core.a` **alone** (no `gte_editor`/SDL/
ImGui), loads all three demo plugins via the real, production `PluginHost`, confirms
`LoadedModuleCount() == 3` exactly and exactly one implements `IRenderFeatureModule_v1`, and never
once references `IEditorPanelModule_v1` anywhere in its own source — the isolation proof itself.
`tools/ci/gte_core_player_link_probe` (from `editor-core-separation-2`) gained a bonus, headless-
surface-gated check that constructs a real headless `gte::Core`, loads plugins, and calls
`BuildFrame()` once — cleanly self-skips on this machine (no `VK_EXT_headless_surface`). **Real,
necessary deviations**: the probe's own outer `CMakeLists.txt` needed an explicit
`add_dependencies()` on all three demo plugin targets (otherwise a bare build produces zero `.dll`s
in its own inner build tree); the plugins-directory resolution was corrected from
`std::filesystem::current_path()` (never reliably the `.exe`'s own directory) to the same
`GetModuleFileNameW()`-based approach PHASE2's own probe already established as this repo's
precedent.

**PHASE6 (this phase) — Final Full Regression Verification and Campaign Closeout.** See below for
the full, fresh, mechanical re-check.

## Full clean build + full `ctest` regression pass (this phase's own mandatory checkpoint)

- **Full clean rebuild** (`build/` deleted, reconfigured fresh via `cmake -S . -B build -G Ninja`,
  built via `cmake --build build`): **508/508 steps, zero errors.** (Up from
  `editor-core-separation-2`'s own final count of 498 — the delta of 10 is this campaign's own new
  `gte_core`/`gte_editor` source files plus the three new demo plugin `.dll` targets.) All three
  demo plugin `.dll`s (`demo_hello_world.dll`, `demo_render_feature.dll`, `demo_editor_panel.dll`)
  confirmed genuinely present in `build/plugins/` via `browse_dir`. The shared-CRT runtime DLLs
  (`libstdc++-6.dll`/`libgcc_s_seh-1.dll`/`libwinpthread-1.dll`) are correctly, honestly ABSENT next
  to `build/GreatTamanaEditor.exe` — this development machine's toolchain cannot produce them at
  all (PHASE1's own Deviation #2, unchanged, restated here with fresh evidence, not a new gap).
- **Full `ctest -C Debug --output-on-failure`**: **1776 total tests, 1774 passed (100% of
  executed), 2 legitimate, documented, environment-gated skips, zero failures**:
  - `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` (pre-existing, gated on a
    real MMD model file not present on this machine).
  - `CoreHeadlessConstructionTest.ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`
    (pre-existing, this machine's Vulkan driver lacks `VK_EXT_headless_surface`).
- **Before/after comparison against `editor-core-separation-2`'s own final baseline** ("1774 total,
  1772 passed, 2 legitimate skips, zero failures"): this run is **1776 total, 1774 passed, 2
  legitimate skips, zero failures** — the total grew by **exactly 2**, and the passing count grew
  by exactly 2 too. **This campaign was not expected to add any new GoogleTest file for its own new
  classes** (`PluginHost`, the adapters, `EditorPanelRegistry` are exercised by the manual/HTTP-
  driven smoke checks and the dedicated standalone probes, per the phase file's own prediction) —
  **except PHASE4's own required, honest exception**: deleting `EditorPanelCatalog.h` broke
  pre-existing Tier-1 test coverage that had to be repaired in the same change, per `AGENTS.md`'s
  "Testability & Regression Safety" mandate. That repair replaced
  `tests/Editor/EditorPanelCatalogTests.cpp` (4 `TEST()` cases, confirmed via `git show` on the
  commit immediately before its deletion) with `tests/Core/EditorPanelRegistryTests.cpp` (6
  `TEST()` cases, confirmed via a fresh `findstr` on the real, current file) — a net **+2 tests**,
  matching the observed count drift exactly. **Zero unexplained count drift, zero new failures.**

## Every probe, re-run fresh from a clean build directory

- **`tools/ci/gte_core_standalone_probe`** (pre-existing, untouched by this campaign): **227/227
  steps, zero errors**, `[227/227] Linking CXX static library libgte_core.a`.
- **`tools/ci/gte_core_player_link_probe`** (extended by PHASE5): **235/235 steps, zero errors,
  zero `undefined reference`**, all three demo plugin `.dll`s confirmed present in its own inner
  build's `plugins/` folder; ran successfully, PHASE5's own bonus check cleanly `SKIPPED`
  (documented, expected — this machine's Vulkan driver lacks `VK_EXT_headless_surface`, matching
  `CoreHeadlessConstructionTest`'s own skip above).
- **`tools/ci/gte_plugin_abi_handshake_probe`** (new, PHASE2): 4/4 steps, zero errors (only needs
  `gte_plugin_abi` + `demo_hello_world`); `demo_hello_world.dll` confirmed present; ran
  successfully, printed `"OK: loaded plugin 'HelloWorldPlugin' v1.0.0 - Milestone 0 handshake proof
  - implements zero capabilities."`, exit `0`.
- **`tools/ci/gte_plugin_isolation_probe`** (new, PHASE5): **235/235 steps, zero errors**; all
  three demo plugin `.dll`s confirmed present; ran successfully, printed
  `"PASS: 3 plugin(s) loaded, exactly 1 implements IRenderFeatureModule_v1, and this file never
  once asked any plugin for IEditorPanelModule_v1."`, exit `0` — confirming `LoadedModuleCount() ==
  3` exactly and `renderFeatureCount == 1` exactly, per this phase's own explicit requirement.

## Live, HTTP-driven end-to-end smoke test

Booted `build/GreatTamanaEditor.exe` via `run_app_background`, drove it via `gte_send_request`:

| Endpoint | Result |
|---|---|
| `GET /get_swapchain` | `200`, real rendered PNG — every pre-existing panel present, plus the Scene panel's solid-magenta clear and the new "Demo Plugin Panel" tab docked in the bottom strip |
| `GET /list_tabs` | `200` — `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]}` — every pre-existing built-in name present, plus exactly one new one |
| `GET /get_logs?limit=50` | `200`, `count:5` — real `"Loaded plugin '...'"` lines for all 3 demo plugins; a follow-up `min_level=warning` query returned `count:0` — zero warnings/errors |
| `GET /activate_tab?name=Game` → `GET /get_game_view` | `200` each — visibly, genuinely solid magenta |
| `GET /activate_tab?name=Scene` → `GET /get_texture?texture_name=SceneViewComposited` | `200` each — Scene View ALSO shows the same solid magenta clear |
| `GET /activate_tab?name=Demo Plugin Panel` → `GET /get_swapchain` | `200` each — panel visible, docked, active, showing "Hello from a plugin!" |
| `POST /save_scene`, `GET /frame_debugger/state`, `POST /clear_logs` | `200` each — unchanged behavior, spot-checked per this phase's own "at least 3" instruction |

`stop_app_background`'d the process cleanly when done. Every endpoint behaves correctly.

## The 10 Locked Design Decisions (`PHASE0_MASTER_STRATEGY.md`, Step 3) — restated, with fresh evidence

1. **True dynamic, self-registering, always-all-in `.dll` plugins.** **YES** —
   `PluginHost::LoadPlugins()` unconditionally loads every `*.dll` in the folder; the live smoke
   test confirms all three demo plugins loaded with zero manifest/toggle.
2. **A plugin `.dll` NEVER links or calls a real `gte_core`/`gte_editor` symbol directly.**
   **YES** — `demo_hello_world`'s own `CMakeLists.txt` links only `gte_plugin_abi`.
3. **Every cross-boundary interface uses ONLY plain, built-in C++ types.** **YES** —
   `search_in_dir(plugins/gte_plugin_abi, "std::string|std::vector|std::filesystem")` finds only
   doc-comment prose explaining the rule, never a real declaration violating it.
4. **CRT/owned-handle sub-decisions.** **YES, unchanged** — the shared/DLL CRT requirement is a
   real, joint requirement, correctly applied (as a documented, honest no-op on this machine) to
   all seven named target types (host `.exe`, all 3 demo plugins, all 3 plugin-loading probes); the
   owned-handle mechanism remains a deliberate, honestly-flagged, unbuilt Non-Goal.
5. **The fingerprint gate is real, mandatory, checked FIRST, always.** **YES** —
   `PluginHost.cpp`'s own load order re-confirmed: export resolution, THEN the fingerprint check
   (its own comment: "the single most important check in this whole function"), THEN
   `GTE_CreatePluginModule()`.
6. **`GTE_ENABLE_PLUGINS` defaults `ON`.** **YES** — confirmed directly in the real, current root
   `CMakeLists.txt`.
7. **Physical location** (`plugins/gte_plugin_abi/` + `plugins/demo_*/` at the repo root;
   `<build-dir>/plugins/` at runtime). **YES** — confirmed via `browse_dir` on both locations.
8. **Exactly ONE `PluginHost` instance per process.** **YES** — `Core.h` declares exactly one
   `m_pluginHost` member; `gte_editor`'s `EditorHost.cpp` reads it via `GetPluginHost()` rather than
   re-scanning the folder itself.
9. **Plugins load once, at host-construction time, never re-scanned per frame.** **YES** —
   `EditorHost.cpp`'s constructor calls `LoadPlugins()` exactly once; no other call site exists.
10. **Every new interface/class follows this codebase's own conventions** (`namespace gte`, RAII,
    direct Windows API use with no abstraction layer). **YES** — `PluginHost.cpp` calls
    `LoadLibraryW`/`GetProcAddress` directly, matching this repo's own Windows-only precedent.

**`gte_core.a`/`gte_editor.a` never became `SHARED` libraries** (Non-Goal, restated with fresh
evidence): `search_in_dir` on the real, current root `CMakeLists.txt` confirms both
`add_library(gte_core STATIC ...)` and `add_library(gte_editor STATIC ...)` remain unchanged.

**Honest summary: all 10 of 10 Locked Design Decisions are confirmed true in the real, final
code, with fresh, mechanical evidence gathered this phase — not inherited, unverified assumptions
carried over from any prior phase's own narrower checks.**

## Deviations from the original plan (consolidated from every phase's own report)

1. **PHASE1** — two real, necessary corrections: `-shared-libstdc++` is not a real GCC flag (only
   `-shared-libgcc` is real; shared libstdc++ is the toolchain's own default when eligible); and
   this development machine's only installed MinGW toolchain cannot produce a shared-CRT binary at
   all (`--disable-shared`), discovered via two rounds of `ask_questions` and resolved with a
   configure-time capability probe rather than a silently-wrong claim.
2. **PHASE2** — MinGW's default `lib`-prefix on the built plugin `.dll`'s own filename needed a
   `PREFIX ""` fix; a flagged, honestly-unresolved tension between `PHASE0`'s prose ("PluginHost
   still compiles with `GTE_ENABLE_PLUGINS=OFF`") and the literal, mutually-consistent code every
   phase actually wrote (which gates `gte_plugin_abi`'s own target behind the same switch) — never
   exercised or resolved this campaign, since every real build used the default `ON` value.
3. **PHASE3** — three real, live-testing-confirmed logic bugs beyond the strategy doc's own literal
   code sketch, the most significant resolved via `ask_questions` (the plugin must draw onto the
   POST-composite texture, not the raw pre-atmosphere-composite handle the sketch assumed).
4. **PHASE4** — one mechanical include-completeness fix (`ImGuiEditorLayer.cpp` needed a direct
   `#include` of `IEditorPanelModule.h`); one real, necessary test-repair beyond the phase file's
   own literal Step 4 list, required by `AGENTS.md`'s own testability mandate (deleting
   `EditorPanelCatalog.h` broke three pre-existing test files, repaired in the same change).
5. **PHASE5** — the probe's own outer `CMakeLists.txt` needed an explicit `add_dependencies()` on
   all three demo plugin targets; the plugins-directory resolution was corrected from
   `std::filesystem::current_path()` to the already-established `GetModuleFileNameW()` approach.
6. **PHASE6 (this phase)** — one observed, non-reproducible tool quirk (`rd /s /q build` failed
   once, succeeded immediately on retry with the same intent) that did not meet the bar for a
   `bug_report`; **no genuine regression was found anywhere in this phase's own full `ctest` pass**
   (the one count drift observed was fully, concretely explained by name — PHASE4's own
   already-documented test-file swap, independently re-confirmed via `git show`), so
   `delegate_task` was correctly never invoked, mirroring `editor-core-separation-2` PHASE5's own
   identical precedent.

## What remains genuinely open (honest, not silently dropped)

- **Milestone 4 — hot reload** (swapping a plugin `.dll` for a rebuilt one while the engine keeps
  running) remains explicitly, permanently out of scope, confirmed via `ask_questions` during
  `PHASE0`'s own drafting. `OnBeforeUnload()`/`OnAfterReload()` lifecycle hooks were never designed
  or implemented.
- **The owned-handle-with-bundled-deleter mechanism, and debug-build allocation-tagging**, remain
  deliberately, honestly unbuilt — Locked Design Decision #4's own real, stated reason (zero
  cross-boundary calls in this campaign's own Milestones 0-3 ever transfer heap ownership) still
  holds true today, confirmed by this phase's own fresh review; the moment a future capability
  needs to hand over real ownership across the boundary, THAT is the point this mechanism must be
  designed for real, not before.
- **The shared/DLL CRT linkage requirement is designed and consistently applied, but still not
  proven to actually achieve real shared linkage on THIS development machine** — its toolchain
  cannot produce one at all (PHASE1's Deviation #2). A future, deliberate decision to switch this
  repository's own active `CMAKE_CXX_COMPILER` to a shared-runtime-capable toolchain (already
  installed via `scoop install mingw` per PHASE1, but never wired into the active build) remains a
  genuinely open, explicitly deferred item — not this campaign's own job to force through, since it
  would require a full rebuild of the entire repository outside any single phase's own narrower
  scope.
- **No real, existing engine feature (Atmosphere, GPU-Driven Batching, Frame Debugger, ...) was
  migrated onto this plugin mechanism** — every plugin this campaign ships remains a deliberately
  tiny, throwaway demo, exactly as designed. Picking a real future migration target is separate,
  later, out-of-scope work.
- **The Player Build Pipeline remains explicitly, permanently OUT OF SCOPE** — this campaign proves
  the mechanism works end-to-end in a Player-SHAPED process (`gte_plugin_isolation_probe`, linking
  `gte_core.a` alone), it does not build the actual Player tooling. No `<ProjectName>.exe`
  generation, no per-project build system.
- **No real CI pipeline exists for this repo** (unchanged) — every probe this campaign added
  remains a manually-invocable local CMake project, exactly like every prior campaign's own probes.
- **A pre-existing, unrelated tension flagged by PHASE2, left unresolved as documented there**: a
  literal reading of `PHASE0_MASTER_STRATEGY.md`'s own prose ("`GTE_ENABLE_PLUGINS=OFF` means
  `PluginHost` still compiles") is in tension with the literal, mutually-consistent CMake code every
  phase actually wrote (which gates `gte_plugin_abi`'s own target behind the same switch). This was
  never exercised or resolved this campaign (every real build/run used the default `ON` value) — a
  future task that actually needs to build and exercise `GTE_ENABLE_PLUGINS=OFF` should resolve it
  deliberately rather than inheriting it as a surprise.

`editor-core-separation-3` is complete. Its real, substantial deliverable — a working, end-to-end,
dynamic plugin system spanning Milestones 0-3 (the ABI foundation, the host + hello-world proof, a
runtime render-graph capability, an editor-panel capability, and a Player-shaped isolation proof) —
has shipped and is verified working end-to-end, with fresh, real, mechanical evidence from a
completely clean build, full regression suite, all four probes, and a live HTTP smoke test — not
inherited assumptions from any prior phase's own narrower checks. Ready to merge.
