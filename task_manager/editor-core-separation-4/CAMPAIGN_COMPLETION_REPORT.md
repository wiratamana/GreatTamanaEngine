# editor-core-separation-4 — CAMPAIGN COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. This report is the single, top-level
summary of the whole 8-phase campaign, mirroring
`task_manager/editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`'s own shape
and tone. See each `PHASEn_COMPLETION_REPORT.md` in this same folder for full
per-phase detail.

## What this campaign set out to do

`editor-core-separation-3` shipped a real, working, dynamic `.dll` plugin system
for this engine (`gte_plugin_abi`, `PluginHost`, `IRenderFeatureModule_v1`,
`IEditorPanelModule_v1`, three throwaway demo plugins) and called itself "ready to
merge." An independent re-analysis of that exact shipped code
(`editor-core-separation-3_REANALYSIS_bugs_and_gaps_2026-09-24.md`) found **8
concrete, file/line-backed problems** that campaign's own closeout narrative never
mentioned: 2 CRITICAL, 2 HIGH, 2 MEDIUM, 2 LOW severity. **This campaign's goal was
narrow and concrete: fix all 8 of those problems, for real, in the actual shipped
code** — not redesign the architecture, not migrate a real feature onto it, not
build hot reload. One phase per issue, in severity order, each independently
buildable and committable.

## What shipped, phase by phase

**PHASE1 — Shared-CRT Fingerprint Honesty + Startup Warning (Issue #1,
Critical).** Fixed the doc-vs-code lie in
`plugins/gte_plugin_abi/GtePluginAbiFingerprint.h`'s own comment (it falsely
claimed the host "REFUSES to load ANY plugin" when `sharedRuntimeLinkage == 0` —
no such check ever existed). Chosen resolution: do NOT add a hard refusal (this
dev machine's only usable toolchain cannot produce shared-CRT binaries at all, so
that would disable plugin loading entirely) — instead, `PluginHost::
LogSharedCrtRiskWarningOnce()` logs one loud, one-time `GTE_LOG_WARNING` the first
time `LoadPlugins()` runs whenever the risk is real. Verified live: this warning is
genuinely the very first log entry (`id:1`) on every subsequent phase's own smoke
test, confirming it fires before any plugin scan. No deviations.

**PHASE2 — `GTE_ENABLE_PLUGINS=OFF` Build Fix (Issue #2, Critical).** Fixed a real,
previously-broken, never-before-exercised build configuration:
`-DGTE_ENABLE_PLUGINS=OFF` used to fail outright because `gte_plugin_abi`'s own
`add_subdirectory()` call was gated behind the same switch `PluginHost.h/.cpp`
(unconditionally in `gte_core`) needed. Fix: `add_subdirectory(plugins/gte_plugin_abi)`
became unconditional; only the 3 demo plugin subdirectories stayed gated. Verified
via a real, separate `-DGTE_ENABLE_PLUGINS=OFF -DGTE_BUILD_TESTS=OFF` throwaway
build — succeeded, 227/227 steps. No deviations.

**PHASE3 — Editor Panel Name Collision Protection (Issue #3, High).**
`EditorPanelRegistry::RegisterPluginPanel()` now calls `IsKnownName()` first and
refuses (logs + skips, never crashes) any colliding name — a plugin could
previously silently claim a built-in panel's name or another plugin's, corrupting
ImGui docking state. Added 2 new `TEST()` cases
(`RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredBuiltinName`/
`...PluginName`) to the existing `tests/Core/EditorPanelRegistryTests.cpp`. No
deviations.

**PHASE4 — Plugin Runtime DLL Landmine — Defense in Depth (Issue #4, High).** Two
independent mitigations for the moment a shared-CRT toolchain is ever switched to:
(a) a NEW `gte_apply_plugin_dll_shared_crt_linkage()` CMake helper for plugin
`.dll` targets that deliberately does NOT stage runtime DLLs into the shared
`plugins/` scan folder (unlike the original `gte_apply_plugin_shared_crt_linkage()`,
still used by the host `.exe`/probes); (b) `PluginHost::LoadPlugins()` itself now
skips 3 known non-plugin runtime DLL filenames
(`libstdc++-6.dll`/`libgcc_s_seh-1.dll`/`libwinpthread-1.dll`) defensively, even if
one somehow ends up in `plugins/` anyway. Verified with a manual, stronger proof:
copying a real `libstdc++-6.dll` (from the already-installed-but-not-active
shared-capable toolchain) into `build/plugins/` and confirming
`PluginHost` correctly logged an ignore-list skip rather than treating it as a
plugin. No deviations.

**PHASE5 — Multi-Render-Feature-Plugin Warning + Regression Lock (Issue #5,
Medium).** A new, pure, Tier-1-tested `CountModulesImplementingRenderFeature()`
free function; `Core::LoadPlugins()` logs one `GTE_LOG_WARNING` when more than one
loaded module implements `IRenderFeatureModule_v1` (previously: silent,
last-registered-wins overwrite, never even exercised with 2+ real plugins). A
SECOND, genuinely independent throwaway demo plugin,
`demo_render_feature_second`, was added specifically so this path is exercised for
real; `tools/ci/gte_plugin_isolation_probe` was updated to expect 4 plugins
loaded / 2 implementing the capability. No deviations beyond a documentation-only
touch-up (the probe's own `README.md`/CMake comments' stale "three plugins"
prose).

**PHASE6 — Defensive `GtePluginModuleInfo` Buffer Reads (Issue #6, Medium).** A
new, header-only `ReadFixedBuffer()` (`src/Core/Plugins/FixedBufferReader.h`,
`std::string(buffer, strnlen(buffer, bufferSize))`) replaces trusting a plugin's
own `GetModuleInfo()` to have null-terminated its fixed-size `char[]` buffers
correctly — `PluginHost`'s one real log call site now uses it for `info.name`/
`info.version`. `DescribeFingerprintMismatch()` was re-confirmed already safe
(compiler/CMake-generated code is mechanically guaranteed null-terminated) and
left unchanged. **One real deviation, caught during implementation**: the phase
file's own sketch had a stray extra `'` character that would have silently changed
the visible log format — fixed to reproduce the original, pre-PHASE6 log line's
exact visible text byte-for-byte (confirmed via a live `/get_logs` check).

**PHASE7 — Case-Insensitive `.dll` Extension Matching (Issue #7, Low).** A new
`HasDllExtensionCaseInsensitive()` helper replaces the case-sensitive
`entry.path().extension() != ".dll"` check that used to silently, undiagnosably
skip a file like `MyPlugin.DLL`; a new `GTE_LOG_DEBUG` line makes any non-`.dll`
file encountered during the scan at least discoverable. Verified with a manual
proof: copying `demo_hello_world.dll` to `CapitalCase.DLL` and confirming
`PluginHost` loaded it successfully (`HelloWorldPlugin` v1.0.0, `id:2`). No
deviations.

**PHASE8 (this phase) — `PluginHost` Failure-Path Regression Tests (Issue #8,
Low) + Final Full Regression Verification and Campaign Closeout.** Added real,
automated GoogleTest coverage for all 4 of `PluginHost`'s own documented
failure/skip paths (missing export, fingerprint mismatch, decline-to-load,
reverse-order destroy) — previously exercised only transitively, never directly —
via 5 new, deliberately-broken/instrumented fixture `.dll` targets under
`tests/Fixtures/FakePlugins/`, isolated from the real `plugins/` scan folder via
their own dedicated `RUNTIME_OUTPUT_DIRECTORY`. **One real, mechanically-discovered
deviation**: the phase file's own CMake sketch for
`gte_fake_plugin_fixtures_dirs`'s include directory did not actually match its own
test file's `#include` spelling (a doubled `generated/generated/...` path) — fixed
by adjusting the `INTERFACE` include directory to the plain
`CMAKE_CURRENT_BINARY_DIR` (see `PHASE8_COMPLETION_REPORT.md` for the exact
reasoning). Then this phase ran the one, single, campaign-wide full clean build +
full `ctest` pass + all 4 probes + a live HTTP smoke test + a fresh
`-DGTE_ENABLE_PLUGINS=OFF` side build — see below for the full, fresh evidence.

## Full clean build + full `ctest` regression pass (this phase's own mandatory checkpoint)

- **Full clean rebuild** (`build/` deleted via `powershell Remove-Item` after one
  failed `rd /s /q` attempt, reconfigured fresh via `cmake -S . -B build -G Ninja`
  — **with `CMAKE_CXX_COMPILER`/`CMAKE_C_COMPILER` explicitly pinned to
  `scoop/apps/gcc`'s GCC 15.2.0**, see "A real environment finding" below for why
  this was necessary — built via `cmake --build build`): **524/524 steps, zero
  errors.** All 4 real demo plugin `.dll`s (`demo_editor_panel.dll`,
  `demo_hello_world.dll`, `demo_render_feature.dll`,
  `demo_render_feature_second.dll`) confirmed genuinely present in `build/plugins/`
  via `browse_dir`; the 5 new fixture `.dll`s confirmed genuinely ABSENT there and
  present instead under `build/test_fixtures/fake_plugins/{unhappy_path,
  destroy_order}/`.
- **Full `ctest -C Debug --output-on-failure`**: **1787 total tests, 1785 passed
  (100% of executed), 2 legitimate, documented, environment-gated skips, zero
  failures**:
  - `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`
    (pre-existing, gated on a real MMD model file not present on this machine).
  - `CoreHeadlessConstructionTest.ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`
    (pre-existing, this machine's Vulkan driver lacks `VK_EXT_headless_surface`).
- **Before/after comparison against `editor-core-separation-3`'s own final
  baseline** ("1776 total, 1774 passed, 2 legitimate skips, zero failures"): this
  run is **1787 total, 1785 passed, 2 legitimate skips, zero failures** — the
  total grew by **exactly +11**, and the passing count grew by exactly +11 too.
  **Every one of the 11 is named and accounted for**:
  - PHASE3: 2 — `EditorPanelRegistryTest.RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredBuiltinName`,
    `...RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredPluginName`.
  - PHASE5: 3 — `PluginRenderFeatureDiagnosticsTest.CountModulesImplementingRenderFeature_EmptyVectorReturnsZero`,
    `..._CountsOnlyModulesThatImplementIt`, `..._ZeroWhenNoneImplementIt`.
  - PHASE6: 3 — `FixedBufferReaderTest.ReadFixedBuffer_NormalNullTerminatedStringReadsCorrectly`,
    `..._NonTerminatedFullBufferReadsExactlyItsFixedSizeNeverMore`,
    `..._EmptyBufferReadsEmptyString`.
  - PHASE8: 3 — `PluginHostFailurePathTest.LoadPlugins_UnhappyPathFolder_LoadsExactlyZeroModulesAndEachFixtureFailsForItsOwnDistinctReason`,
    `..._DestroyOrderFolder_LoadsExactlyTwoModules`,
    `Destructor_DestroysLoadedModulesInExactReverseOfTheirCreateOrder`.
  - 2+3+3+3 = **11**, matching the observed delta exactly. **Zero unexplained count
    drift, zero new failures.**

## Every probe, re-run fresh

- **`tools/ci/gte_core_standalone_probe`** (pre-existing, untouched by this
  campaign) — reconfigured/rebuilt via its existing `build-core-probe` outer
  directory; the nested inner build produced `libgte_core.a` successfully (its own
  cached `CMAKE_CXX_COMPILER` had already been GCC 15.2.0 from an earlier session,
  unaffected by the PATH-reordering finding below).
- **`tools/ci/gte_core_player_link_probe`** (extended by `editor-core-separation-3`
  PHASE5) — rebuilt via `build-player-link-probe`; `gte_core_player_link_probe.exe`
  ran, printing `"Bonus check SKIPPED: this machine's Vulkan driver lacks
  VK_EXT_headless_surface..."` — matching `CoreHeadlessConstructionTest`'s own
  documented skip exactly.
- **`tools/ci/gte_plugin_abi_handshake_probe`** — freshly reconfigured/rebuilt via
  `build-plugin-abi-handshake-probe`; ran successfully, printed `"OK: loaded plugin
  'HelloWorldPlugin' v1.0.0 - Milestone 0 handshake proof - implements zero
  capabilities."`, exit `0`.
- **`tools/ci/gte_plugin_isolation_probe`** — freshly reconfigured/rebuilt via
  `build-plugin-isolation-probe`; ran successfully, printed `"PASS: 4 plugin(s)
  loaded, exactly 2 implement IRenderFeatureModule_v1, and this file never once
  asked any plugin for IEditorPanelModule_v1."`, exit `0` — confirming
  `LoadedModuleCount() == 4` exactly and `renderFeatureCount == 2` exactly, per
  PHASE5's own updated expectation.

## Live, HTTP-driven end-to-end smoke test

Booted `build/GreatTamanaEditor.exe` via `run_app_background` (PID 8340), drove it
via `gte_send_request`:

| Endpoint | Result |
|---|---|
| `GET /get_logs?limit=100` | `200`, `count:8` — PHASE1's shared-CRT warning (`id:1`), all 4 `"Loaded plugin '...'"` lines (`id:2`-`id:5`), PHASE5's "2 loaded plugins implement IRenderFeatureModule_v1" warning (`id:6`), `Network`/`EditorHost` startup lines (`id:7`-`id:8`) — the 5 fixture `.dll`s from PHASE8 **never appear anywhere in this log** |
| `GET /list_tabs` | `200` — `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]}` — every pre-existing name present, unchanged |
| `GET /get_swapchain` | `200`, real rendered PNG — every pre-existing panel present, Scene View still solid magenta, "Demo Plugin Panel" docked and showing "Hello from a plugin!" — visually confirmed |

`stop_app_background(pid: 8340)` — stopped cleanly. Every endpoint behaves
correctly, with zero regression from every prior phase's own documented baseline.

## `-DGTE_ENABLE_PLUGINS=OFF` re-verification (PHASE2's own fix, re-confirmed after every later phase's changes)

`cmake -S . -B build-plugins-off -G Ninja -DGTE_ENABLE_PLUGINS=OFF` (compiler
pinned, deliberately WITHOUT `-DGTE_BUILD_TESTS=OFF` this time, unlike PHASE2's own
original check, since this phase's new fixture targets live under `tests/` and the
whole point of this re-run is confirming they ALSO build under
`GTE_ENABLE_PLUGINS=OFF`), then `cmake --build build-plugins-off --target
GreatTamanaEngineTests`: **486/486 steps, zero errors** — `GreatTamanaEngineTests.exe`
built successfully with all 5 new PHASE8 fixture `.dll`s built alongside it,
confirming PHASE2's own fix (`gte_plugin_abi`'s unconditional `add_subdirectory()`)
still correctly keeps every plugin-ABI-boundary target buildable even with plugins
disabled. `build-plugins-off` deleted afterward; confirmed gone via `browse_dir`.

## A real environment finding, honestly flagged (not a design ambiguity in the code)

Mid-way through this phase's own mandatory full clean rebuild, a truly fresh `cmake
-S . -B build -G Ninja` (no compiler pinned) auto-detected
`C:\Users\F5954\scoop\apps\mingw\current\bin\c++.exe` (GCC 16.2.0, the
shared-runtime-**capable** toolchain "installed but deliberately not yet switched
to," per `editor-core-separation-3` PHASE1's own Deviation #2 and this campaign's
own PHASE1) as the active compiler, instead of the
`scoop\apps\gcc\current\bin\g++.exe` (GCC 15.2.0, static-only) every PHASE1-7
build/verification in this campaign used. Root cause, confirmed via `where g++`/
`where c++`: this machine's system `PATH` now lists `scoop\apps\mingw\current\bin`
**before** `scoop\apps\gcc\current\bin` — a change to this development machine's own
environment, external to this campaign's own source-code changes. Letting the
fresh default silently drive this closeout's own evidence would have been an
unplanned, de facto switch of this repository's active `CMAKE_CXX_COMPILER` — the
exact thing `PHASE0_MASTER_STRATEGY.md`'s own Non-Goals list states is "explicitly
deferred" for this whole campaign. `ask_questions` was used; the user was
unavailable and explicitly delegated the decision. The conservative,
campaign-consistent choice was made: **explicitly pin
`-DCMAKE_CXX_COMPILER=.../scoop/apps/gcc/current/bin/g++.exe`
`-DCMAKE_C_COMPILER=.../gcc.exe`** on every fresh configure this phase ran, so this
closeout's evidence matches every earlier phase's own documented baseline exactly,
and treated the PATH reordering as an environment fact to route around rather than
something this campaign's own code should silently absorb. **This is a genuinely
open item, restated below** — the PATH ordering itself was not reverted (out of
scope for a source-code campaign), so any FUTURE bare `cmake -S . -B build` with no
explicit compiler pin will keep resolving to the shared-capable toolchain unless a
human deliberately addresses this.

## The 8 original re-analysis issues — restated before/after status

| # | Severity | Issue | Status | Fixed by |
|---|---|---|---|---|
| 1 | Critical | Doc comment falsely claimed the host refuses to load any plugin when `sharedRuntimeLinkage == 0` — no such check exists | **Fixed — honestly, not absolutely.** The false claim is corrected; a real, loud, one-time `GTE_LOG_WARNING` now makes the real risk visible. The hard refusal was deliberately NOT added (would disable plugin loading entirely on this dev machine) — this is a permanent, deliberate choice, not a stronger guarantee than what actually shipped. | PHASE1 |
| 2 | Critical | `-DGTE_ENABLE_PLUGINS=OFF` broke the build outright, never tested | **Fixed.** `gte_plugin_abi` is now an unconditional `add_subdirectory()`; verified with a real, separate build, twice (PHASE2's own check, and this phase's own re-run after every later phase's changes). | PHASE2 |
| 3 | High | `RegisterPluginPanel()` never checked for name collisions | **Fixed.** Refuses (logs + skips) any colliding name; 2 new regression tests lock this in. | PHASE3 |
| 4 | High | `PluginHost` would try to `LoadLibraryW()` the engine's own runtime DLLs as plugins the moment shared-CRT linkage is switched on | **Fixed, defense in depth.** Both the CMake-level root cause (never staging those DLLs into `plugins/` for a plugin target) and a code-level ignore-list safety net are in place; verified with a real manual copy-and-observe proof. | PHASE4 |
| 5 | Medium | 2+ plugins implementing `IRenderFeatureModule_v1` silently overwrite each other, never exercised | **Fixed (diagnostic only, by design).** A real second demo plugin now exercises this path for real; `Core` logs a clear warning. Per-plugin compositing itself remains explicitly out of scope (unchanged Non-Goal). | PHASE5 |
| 6 | Medium | `PluginHost` trusted a plugin's fixed-size `char[]` buffers to be null-terminated, no bounded read | **Fixed.** `ReadFixedBuffer()` bounds every read via `strnlen()`; the one real log call site uses it. | PHASE6 |
| 7 | Low | `.dll` extension matching was case-sensitive, silently skipping e.g. `MyPlugin.DLL` | **Fixed.** Case-insensitive matching, plus a debug-level diagnostic for any skipped non-`.dll` file; verified with a real manual `CapitalCase.DLL` proof. | PHASE7 |
| 8 | Low | Zero automated regression coverage of `PluginHost`'s 4 documented failure/skip paths | **Fixed.** 5 new deliberately-broken fixture `.dll`s + 3 new `TEST()` cases directly and mechanically prove missing-export, fingerprint-mismatch, decline-to-load, and reverse-order-destroy all behave exactly as documented. | PHASE8 (this phase) |

**Honest summary: all 8 of 8 original re-analysis issues are confirmed fixed in
the real, final code, with fresh, mechanical evidence gathered this phase** — not
inherited, unverified assumptions carried over from any single prior phase's own
narrower checks.

## The 10 Locked Design Decisions (`editor-core-separation-3`'s own list) — unaffected

Every phase's own completion report in this folder independently re-confirmed its
own change did not alter any of the 10 Locked Design Decisions restated in
`task_manager/editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`: this whole
campaign only hardens/corrects the existing implementation (documentation
honesty, a real build-configuration fix, defensive checks, diagnostics, and test
coverage) — it never changed `PluginHost`'s public interface, the fingerprint ABI
contract, any curated capability interface, or the "always-all-in, dynamically
loaded `.dll`" shape itself.

## Deviations from the original plan (consolidated from every phase's own report)

1. **PHASE1-5, PHASE7** — no deviations (each phase's own report explicitly
   confirms this).
2. **PHASE6** — one real deviation, caught and corrected during implementation
   (not a design change): the phase file's own sketch had a stray extra `'`
   character in its log-message concatenation that would have silently changed the
   visible log format; fixed to reproduce the original text exactly, confirmed via
   a live `/get_logs` check.
3. **PHASE8 (this phase)** — one real, mechanically-discovered deviation: the
   phase file's own `gte_fake_plugin_fixtures_dirs` INTERFACE include directory
   did not actually match its own test file's `#include` spelling (a doubled
   `generated/generated/...` path) — fixed by pointing the include directory at
   plain `CMAKE_CURRENT_BINARY_DIR` instead. Also: one real, external environment
   finding (the system `PATH`'s toolchain search order changed since this
   campaign began) required pinning `CMAKE_CXX_COMPILER`/`CMAKE_C_COMPILER`
   explicitly for this phase's own full rebuild + side build, to avoid an
   unplanned toolchain switch — see "A real environment finding" above.

## What remains genuinely open (honest, not silently dropped)

- **Issue #1's fix is a warning, not a hard refusal — this is a deliberate,
  permanent choice on this machine, not a stronger guarantee than what actually
  shipped.** `PluginHost` will keep loading a plugin with `sharedRuntimeLinkage ==
  0` forever, on this exact toolchain, unless a human deliberately switches
  `CMAKE_CXX_COMPILER` to the already-installed shared-runtime-capable MinGW and
  re-verifies end-to-end. Do not read PHASE1/this campaign as having "fully
  solved" the underlying cross-boundary heap-ownership hazard in any stronger
  sense than "it is now visible in the logs."
- **The system `PATH`'s own toolchain search order now resolves to the
  shared-runtime-capable MinGW ahead of the static-only one this whole campaign
  built and tested against** (discovered this phase, see above). This campaign
  deliberately did NOT change that PATH ordering (out of its own stated scope),
  and instead pinned the compiler explicitly for its own verification runs. A
  future bare `cmake -S . -B build` with no explicit pin will silently pick the
  different, shared-capable compiler — this is a genuinely open, unaddressed
  fact about this development machine, not something this campaign's source-code
  changes can or should paper over.
- **No actual switch of this repository's own active `CMAKE_CXX_COMPILER`** to
  the shared-runtime-capable toolchain was made — still an explicitly deferred,
  separate decision (unchanged from every prior campaign's own stated Non-Goals).
  If that switch is ever made for real, PHASE1's warning should stop firing, and
  PHASE4's runtime-DLL-landmine mitigations become empirically observable
  end-to-end for the first time (currently their real effect is structural/
  code-level only, per PHASE4's own completion report).
- **Issue #5's fix is diagnostic-only** — 2+ `IRenderFeatureModule_v1` plugins
  still silently overwrite each other's render output; real per-plugin
  compositing was never designed or built (an explicit, confirmed Non-Goal).
- **No real, existing engine feature was migrated onto the plugin mechanism** —
  every plugin across both this campaign and `editor-core-separation-3` remains a
  deliberately tiny, throwaway demo. Picking a real future migration target is
  separate, later, out-of-scope work.
- **No real CI pipeline exists for this repo** (unchanged) — every probe added
  across both plugin campaigns remains a manually-invocable local CMake project.
- **Hot reload, cross-process sandboxing, per-project plugin manifest/UI,
  cross-compiler third-party plugin SDK** — all remain permanent, unchanged
  Non-Goals from the original design doc, restated (not silently dropped) here.

`editor-core-separation-4` is complete. All 8 concrete, file/line-backed problems
the independent re-analysis found in `editor-core-separation-3`'s shipped code
have been fixed, for real, in the actual code — verified with fresh, real,
mechanical evidence from a completely clean build, the full regression suite, all
four probes, a live HTTP smoke test, and a re-verified `GTE_ENABLE_PLUGINS=OFF`
build — not inherited assumptions from any prior phase's own narrower checks.
Ready to merge.
