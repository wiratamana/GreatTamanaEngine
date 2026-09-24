# PHASE8 — `PluginHost` Failure-Path Regression Tests + Campaign Closeout — COMPLETION REPORT

**Status:** DONE. Verified with a narrow filtered test run first (Step 3), then this
campaign's own one-time full clean rebuild + full `ctest` + all 4 probes + a live HTTP
smoke test + a fresh `-DGTE_ENABLE_PLUGINS=OFF` side build (Step 4). No regression was
found anywhere, so `delegate_task` was correctly never invoked.

## What changed (Issue #8 implementation)

1. **`tests/Fixtures/FakePlugins/`** (new folder) — its own `CMakeLists.txt` defining:
   - `set(GTE_FAKE_PLUGIN_FIXTURES_DIR "${CMAKE_BINARY_DIR}/test_fixtures/fake_plugins")`
     and a `configure_file()` producing
     `${CMAKE_CURRENT_BINARY_DIR}/generated/FakePluginFixturesDirGenerated.h` from
     `FakePluginFixturesDirGenerated.h.in` (two `constexpr` path strings:
     `kFakePluginUnhappyPathFixturesDir` / `kFakePluginDestroyOrderFixturesDir`).
   - `gte_fake_plugin_fixtures_dirs` — a small `INTERFACE` library exposing that
     generated header's directory to `GreatTamanaEngineTests`.
   - `add_fake_plugin_fixture(target_name source_file output_subdir)` — a helper
     function used by all 5 fixture targets: each is `add_library(... SHARED ...)`,
     `PREFIX ""`, links `PRIVATE gte_plugin_abi` only, gets its own
     `RUNTIME_OUTPUT_DIRECTORY` under `${GTE_FAKE_PLUGIN_FIXTURES_DIR}/unhappy_path` or
     `.../destroy_order`, and calls PHASE4's
     `gte_apply_plugin_dll_shared_crt_linkage()`.
   - The 5 fixtures: `fake_plugin_missing_export`, `fake_plugin_bad_fingerprint`,
     `fake_plugin_declines_to_load` (all in `unhappy_path/`),
     `fake_plugin_destroy_order_a`, `fake_plugin_destroy_order_b` (both in
     `destroy_order/`).
2. **5 new fixture `.cpp` files** under `tests/Fixtures/FakePlugins/`, each matching
   the phase file's own sketch exactly:
   - `FakePluginMissingExport.cpp` — omits `GTE_CreatePluginModule`.
   - `FakePluginBadFingerprint.cpp` — correct exports, `abiContractGeneration =
     999999` in its returned fingerprint.
   - `FakePluginDeclinesToLoad.cpp` — correct exports/fingerprint,
     `GTE_CreatePluginModule()` always returns `nullptr`.
   - `FakePluginDestroyOrderA.cpp` / `FakePluginDestroyOrderB.cpp` — correct,
     successfully-loading fixtures that append `"CREATE:A"`/`"DESTROY:A"` (or `B`)
     lines to a marker file named by the `GTE_PLUGIN_DESTROY_ORDER_MARKER_FILE`
     environment variable.
3. **`tests/Core/Plugins/PluginHostFailurePathTests.cpp`** (new) — the 3 `TEST()`
   cases from the phase file's Step 3.3, verbatim:
   - `LoadPlugins_UnhappyPathFolder_LoadsExactlyZeroModulesAndEachFixtureFailsForItsOwnDistinctReason`
   - `LoadPlugins_DestroyOrderFolder_LoadsExactlyTwoModules`
   - `Destructor_DestroysLoadedModulesInExactReverseOfTheirCreateOrder`
4. **`tests/CMakeLists.txt`** — `add_subdirectory(Fixtures/FakePlugins)` added near
   the top (before `GTE_TEST_SOURCES`/`add_executable`); the new test file added to
   `GTE_TEST_SOURCES`; and, right after the existing
   `target_link_libraries(GreatTamanaEngineTests PRIVATE imgui)` call:
   `target_link_libraries(GreatTamanaEngineTests PRIVATE gte_fake_plugin_fixtures_dirs)`
   and `add_dependencies(GreatTamanaEngineTests fake_plugin_missing_export
   fake_plugin_bad_fingerprint fake_plugin_declines_to_load
   fake_plugin_destroy_order_a fake_plugin_destroy_order_b)`.
5. **`docs/conventions/plugin-architecture.md`** — added the one short paragraph
   from Step 3.4, right after PHASE5's own paragraph.

## Real, discovered deviation from the phase file's own literal sketch

The phase file's own CMake sketch set
`target_include_directories(gte_fake_plugin_fixtures_dirs INTERFACE
"${CMAKE_CURRENT_BINARY_DIR}/generated")` while ALSO having the test file
`#include "generated/FakePluginFixturesDirGenerated.h"` (i.e. re-including the
`generated/` path component). Since the generated header physically lives directly
at `.../generated/FakePluginFixturesDirGenerated.h` (no further module-named
subfolder, unlike `gte_plugin_abi`'s own `generated/gte_plugin_abi/...` precedent),
this combination requires the compiler to search for a nonexistent, doubled
`generated/generated/...` path — confirmed as a real, reproducible "No such file or
directory" compile failure the first time `GreatTamanaEngineTests` was built with
this exact sketch. **Fix applied**: changed the `INTERFACE` include directory to
plain `"${CMAKE_CURRENT_BINARY_DIR}"` (no `/generated` suffix), so the test file's
own `#include "generated/FakePluginFixturesDirGenerated.h"` resolves correctly. This
is documented in-line in `tests/Fixtures/FakePlugins/CMakeLists.txt`'s own comment.
No other deviation from the phase file's sketches was needed.

## Step 3 verification evidence (narrow compile check)

1. `cmake -S . -B build` — succeeded, configured all 5 new fixture targets (each with
   the expected honest shared-CRT no-op warning).
2. `cmake --build build --target GreatTamanaEngineTests` — succeeded (184 steps on the
   first attempt after the include-path fix), all 5 fixture `.dll`s built into
   `build/test_fixtures/fake_plugins/{unhappy_path,destroy_order}`.
3. `build\tests\GreatTamanaEngineTests.exe --gtest_filter=PluginHostFailurePathTest.*`
   — **3 tests from 1 test suite, 3 PASSED, 0 failed**.
4. `browse_dir` on `build/plugins/` — confirmed the 4 real demo plugin `.dll`s only,
   zero fixture `.dll`s; `browse_dir` on `build/test_fixtures/fake_plugins/` confirmed
   the 5 fixture `.dll`s living there instead, isolated from the real scan folder.

## Step 4 — Campaign closeout evidence

A genuine, real environment surprise was hit and resolved before this step could
proceed honestly — see "Real environment finding" below.

1. **Full clean rebuild**: `rd /s /q build` failed once with "the specified file was
   not found" (a real, reproducible failure this time, unlike the prior campaign's
   own harmless one-off `rd` retry precedent) — retried via `powershell -Command
   "Remove-Item -LiteralPath ... -Recurse -Force"`, which succeeded. Reconfigured via
   `cmake -S . -B build -G Ninja` **with the compiler pinned explicitly** (see below),
   then `cmake --build build`: **524/524 steps, zero errors.** `build/plugins/`
   confirmed to contain exactly the 4 real demo plugin `.dll`s
   (`demo_editor_panel.dll`, `demo_hello_world.dll`, `demo_render_feature.dll`,
   `demo_render_feature_second.dll`); `build/test_fixtures/fake_plugins/` confirmed
   to contain the 5 fixture `.dll`s in their own isolated `unhappy_path`/
   `destroy_order` subfolders.
2. **Full `ctest -C Debug --output-on-failure`**: **1787 total tests, 1785 passed
   (100% of executed), 2 legitimate, pre-existing, environment-gated skips
   (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
   `CoreHeadlessConstructionTest.ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`),
   zero failures.** Compared against `editor-core-separation-3`'s own final baseline
   (1776 total, 1774 passed, 2 skips): the delta is **exactly +11** for both total and
   passed, matching PHASE3's 2 new `RegisterPluginPanel_RefusesACollisionWith...`
   tests + PHASE5's 3 new `PluginRenderFeatureDiagnosticsTest.*` tests + PHASE6's 3
   new `FixedBufferReaderTest.*` tests + this phase's own 3 new
   `PluginHostFailurePathTest.*` tests = 2+3+3+3 = 11. **Zero unexplained count
   drift.**
3. **Every probe, re-run fresh**: `tools/ci/gte_core_standalone_probe` and
   `tools/ci/gte_core_player_link_probe` (both built via their existing, pre-existing
   `build-core-probe`/`build-player-link-probe` outer directories, whose own nested
   inner-build caches keep the same pinned GCC 15.2.0 toolchain every prior phase
   used); `tools/ci/gte_plugin_abi_handshake_probe`
   (`build-plugin-abi-handshake-probe`) and `tools/ci/gte_plugin_isolation_probe`
   (`build-plugin-isolation-probe`), both freshly reconfigured/rebuilt this phase.
   Results:
   - `gte_core_player_link_probe.exe` → bonus check cleanly `SKIPPED` (this machine's
     Vulkan driver lacks `VK_EXT_headless_surface`), matching
     `CoreHeadlessConstructionTest`'s own documented skip.
   - `gte_plugin_abi_handshake_probe.exe` → `"OK: loaded plugin 'HelloWorldPlugin'
     v1.0.0 - Milestone 0 handshake proof - implements zero capabilities."`
   - `gte_plugin_isolation_probe.exe` → `"PASS: 4 plugin(s) loaded, exactly 2
     implement IRenderFeatureModule_v1, and this file never once asked any plugin
     for IEditorPanelModule_v1."` — matching PHASE5's own updated expectation
     exactly.
4. **Live, HTTP-driven end-to-end smoke test**: `run_app_background` on
   `build\GreatTamanaEditor.exe` (PID 8340).
   - `GET /get_logs?limit=100` → `200`, `count:8`. PHASE1's shared-CRT risk warning
     (`id:1`), all 4 `"Loaded plugin '...'"` lines (`id:2`-`id:5`), PHASE5's "2 loaded
     plugins implement IRenderFeatureModule_v1" warning (`id:6`), `Network`/
     `EditorHost` startup lines (`id:7`-`id:8`). **The 5 fixture `.dll`s never appear
     anywhere in this log**, confirmed by inspection of every message.
   - `GET /list_tabs` → `200` — `{"tabs":["Hierarchy","Inspector","Scene","Game",
     "Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo
     Plugin Panel"]}` — unchanged.
   - `GET /get_swapchain` → `200`, real PNG — every pre-existing panel present,
     Scene View still solid magenta, "Demo Plugin Panel" docked and showing "Hello
     from a plugin!" — visually confirmed via the returned image.
   - `stop_app_background(pid: 8340)` — stopped cleanly.
5. **`-DGTE_ENABLE_PLUGINS=OFF` re-verification** — `cmake -S . -B build-plugins-off
   -G Ninja -DGTE_ENABLE_PLUGINS=OFF` (compiler pinned, `GTE_BUILD_TESTS` left at its
   default `ON`, deliberately different from PHASE2's own original
   `-DGTE_BUILD_TESTS=OFF` check), then `cmake --build build-plugins-off --target
   GreatTamanaEngineTests`: **486/486 steps, zero errors**, `GreatTamanaEngineTests.exe`
   built successfully with all 5 new fixture `.dll`s built alongside it. Deleted
   `build-plugins-off` afterward; confirmed gone via `browse_dir`.
6. **No real, newly-broken, unexplained test failure was found anywhere in this
   pass — `delegate_task` was correctly never invoked**, per `PHASE0_MASTER_STRATEGY.md`'s
   own Workflow Rule 7.

## Real environment finding (not a design ambiguity, but flagged honestly)

A truly fresh `cmake -S . -B build -G Ninja` (no compiler pinned) auto-detected
`C:\Users\F5954\scoop\apps\mingw\current\bin\c++.exe` (GCC 16.2.0, the
shared-runtime-**capable** toolchain) as the active compiler, NOT the
`C:\Users\F5954\scoop\apps\gcc\current\bin\g++.exe` (GCC 15.2.0, static-only)
every PHASE1-7 build in this campaign used. Root cause, confirmed via `where g++`/
`where c++`: this machine's system `PATH` now lists `scoop\apps\mingw\current\bin`
**before** `scoop\apps\gcc\current\bin` — an external environment change, not
anything this campaign's own code did. Letting this closeout's full rebuild use the
freshly-resolved compiler would have been an unplanned, de facto switch of this
repository's own active `CMAKE_CXX_COMPILER` to the shared-runtime-capable
toolchain — exactly the kind of change `PHASE0_MASTER_STRATEGY.md`'s own Non-Goals
list states is "explicitly deferred" and out of scope for this campaign. Per
`ask_questions` (the user was unavailable and explicitly delegated the decision),
the conservative, campaign-consistent choice was made: **pin
`-DCMAKE_CXX_COMPILER=C:/Users/F5954/scoop/apps/gcc/current/bin/g++.exe`
`-DCMAKE_C_COMPILER=.../gcc.exe` explicitly** on every fresh configure this phase
ran (the main `build/` reconfigure and the `build-plugins-off` side build), so this
closeout's own evidence matches every PHASE1-7 completion report's documented
baseline exactly, and treated the PATH reordering as an unrelated environment fact
to route around, not something this campaign should silently absorb into a
toolchain switch. This is restated in `CAMPAIGN_COMPLETION_REPORT.md`'s own "what
remains genuinely open" section — the underlying PATH ordering itself was NOT
reverted (out of scope for a source-code campaign), so a FUTURE bare `cmake -S . -B
build` with no explicit compiler pin (e.g. by a different developer, or a CI script
that doesn't know to pin it) will keep picking the shared-capable toolchain unless
and until this is addressed as its own, separate, deliberate decision.

## Locked Design Decisions check

No architectural shape changed: `PluginHost`'s public interface (`LoadPlugins()`,
`AllLoadedModules()`, `LoadedModuleCount()`) is unchanged; no ABI contract changed;
the 5 new fixture `.dll`s each link `PRIVATE gte_plugin_abi` only (Locked Design
Decision #2), mirroring every real demo plugin. This phase adds only test
infrastructure (fixture `.dll` targets + one new test file) and campaign-closeout
documentation — it does not change any of the 10 Locked Design Decisions from
`task_manager/editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`.
