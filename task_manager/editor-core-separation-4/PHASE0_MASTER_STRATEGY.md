# editor-core-separation-4 — PHASE0 MASTER STRATEGY

**Branch:** `feature/editor-core-separation` (stay on it — do not create a new branch).

**Project root:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

**This folder:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\task_manager\editor-core-separation-4`

This document is the ORCHESTRATOR for this whole campaign. Every child phase file
(`PHASE1_*.md` .. `PHASE8_*.md`) in this same folder must be read together with this
file before starting work on that phase. Every phase produces its own
`PHASEn_COMPLETION_REPORT.md` in this same folder when done.

---

## Step 1: The Goal (Where are we going?)

`editor-core-separation-3` (the previous campaign, see
`task_manager/editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`) shipped a real,
working, dynamic `.dll` plugin system for this engine: `gte_plugin_abi`, `PluginHost`,
`IRenderFeatureModule_v1`, `IEditorPanelModule_v1`, and three throwaway demo plugins.
It called itself "ready to merge," and its own mechanical evidence (clean build, full
`ctest`, live HTTP smoke test) was real and not fabricated.

However, an independent re-analysis of that exact shipped code
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
editor-core-separation-3_REANALYSIS_bugs_and_gaps_2026-09-24.md` — read this file
in full before starting ANY phase of this campaign, it is the primary source of
truth for what is wrong) found **8 concrete, file/line-backed problems** that
campaign's own closeout narrative never mentions: 2 CRITICAL, 2 HIGH, 2 MEDIUM,
2 LOW severity.

**The goal of THIS campaign (`editor-core-separation-4`) is narrow and concrete:
fix all 8 of those problems, for real, in the actual shipped code — not to
redesign the plugin architecture, not to migrate a real engine feature onto it,
not to build hot-reload.** Every phase below maps 1:1 onto one of the 8 numbered
issues in the re-analysis document, in severity order (Critical → High → Medium →
Low), so each phase is small, independently verifiable, and independently
committable — mirroring every prior campaign's own "small phase, real deliverable"
discipline (see `AGENTS.md`'s "Plugin Architecture" section for the shape of what
already shipped).

## Step 2: The Situation (Where are we now?)

The plugin system genuinely works for its 3 throwaway demo plugins' happy path.
But re-reading the actual shipped code (not just the campaign's own closeout
narrative) found these 8 real gaps — summarized here, full detail (exact
file/line evidence) lives in the re-analysis document and is restated, per-issue,
in each phase file below:

| # | Severity | File(s) | What's wrong |
|---|----------|---------|---------------|
| 1 | Critical | `plugins/gte_plugin_abi/GtePluginAbiFingerprint.h`, `src/Core/Plugins/PluginHost.cpp` | The doc comment claims "the host REFUSES to load ANY plugin unless it was built with shared/DLL CRT linkage" — this refusal does not exist anywhere in the real code. |
| 2 | Critical | root `CMakeLists.txt` (lines ~722, ~1033) | `-DGTE_ENABLE_PLUGINS=OFF` breaks the build outright (`gte_core` links against a `gte_plugin_abi` target that was never created) — never tested, sitting broken since PHASE1 of the prior campaign. |
| 3 | High | `src/Core/EditorPanelRegistry.cpp` | `RegisterPluginPanel()` never checks `IsKnownName()` first — a plugin can silently claim a built-in panel's name (or another plugin's), corrupting ImGui docking state. |
| 4 | High | `plugins/demo_*/CMakeLists.txt`, `cmake/MingwRuntime.cmake`, `src/Core/Plugins/PluginHost.cpp` | The moment a shared-CRT-capable toolchain is switched on (already planned, already installed via `scoop install mingw`), `PluginHost` will try to `LoadLibraryW()` the engine's own copied runtime DLLs as if they were plugins, forever, every startup. |
| 5 | Medium | `src/Core/Core.cpp`'s `"PluginRenderFeatures"` provider | 2+ plugins implementing `IRenderFeatureModule_v1` silently overwrite each other's render output with zero warning — the "one or more `.dll`s" plural case was never actually exercised. |
| 6 | Medium | `src/Core/Plugins/PluginHost.cpp` | Trusts a plugin's `GtePluginModuleInfo` fixed-size `char[]` buffers to be null-terminated, with no defensive bounded read — an out-of-bounds read the moment a badly-behaved third-party plugin doesn't self-terminate. |
| 7 | Low | `src/Core/Plugins/PluginHost.cpp`, `LoadPlugins()` | `entry.path().extension() != ".dll"` is case-sensitive — a `MyPlugin.DLL` file is silently, undiagnosably skipped. |
| 8 | Low | `tests/` | Zero automated regression coverage of `PluginHost`'s own 4 documented failure/skip paths (missing export, fingerprint mismatch, decline-to-load, destroy order) — only the happy path is tested anywhere. |

## Step 3: The Plan (How do we get there?)

One phase per issue, ordered by severity, each independently buildable and
committable. See each phase's own file for full implementation detail — this
section only lists the phase map and the campaign-wide rules every phase must
follow.

### Phase map

- **`PHASE1_SHARED_CRT_FINGERPRINT_HONESTY_AND_STARTUP_WARNING.md`** — Issue #1
  (Critical). Fixes the doc-vs-code lie. Chosen resolution (confirmed via
  `ask_questions` during this campaign's own planning): do **not** hard-refuse
  loading (that would disable plugin loading entirely on this dev machine, since
  its toolchain cannot produce shared-CRT binaries at all). Instead: rewrite the
  misleading doc comment to state the truth, and add a real, loud, one-time
  startup `GTE_LOG_WARNING` whenever the host's own fingerprint has
  `sharedRuntimeLinkage == 0`, so the real risk is visible instead of silently
  implied-but-unenforced.
- **`PHASE2_GTE_ENABLE_PLUGINS_OFF_BUILD_FIX.md`** — Issue #2 (Critical). Fixes
  the real build break. Chosen resolution (confirmed via `ask_questions`):
  `gte_plugin_abi` becomes an unconditional `add_subdirectory()` (it's
  header-only and cheap) — only the 3 demo plugins and the real
  `PluginHost::LoadPlugins()` runtime call site stay gated behind
  `GTE_ENABLE_PLUGINS`. Verified by an actual `-DGTE_ENABLE_PLUGINS=OFF`
  configure + build in a separate build directory (never attempted before).
- **`PHASE3_EDITOR_PANEL_NAME_COLLISION_PROTECTION.md`** — Issue #3 (High).
  `EditorPanelRegistry::RegisterPluginPanel()` refuses (logs + skips) a
  colliding name instead of silently accepting it.
- **`PHASE4_PLUGIN_RUNTIME_DLL_LANDMINE_DEFENSE_IN_DEPTH.md`** — Issue #4
  (High). Chosen resolution (confirmed via `ask_questions`): BOTH (a) stop
  staging runtime DLLs into the shared `plugins/` scan folder for plugin `.dll`
  targets (rely on the host `.exe`'s own directory + Windows DLL search order
  instead), AND (b) make `PluginHost` itself skip known non-plugin runtime DLL
  filenames defensively.
- **`PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md`** —
  Issue #5 (Medium). Chosen resolution (confirmed via `ask_questions`): minimal
  fix only — no new compositing work. A real, Tier-1-tested pure function
  counts how many loaded modules implement `IRenderFeatureModule_v1`; `Core`
  logs one clear warning when the count is `> 1`. A second, genuinely
  independent throwaway demo plugin is added so this path is exercised for
  real (not just theorized), and `gte_plugin_isolation_probe` is updated to
  expect it.
- **`PHASE6_DEFENSIVE_MODULE_INFO_BUFFER_READS.md`** — Issue #6 (Medium).
  `PluginHost` reads `GtePluginModuleInfo`'s fixed buffers via
  `strnlen(buf, sizeof(buf))`-bounded reads everywhere they're consumed,
  never trusting a plugin to self-terminate.
- **`PHASE7_CASE_INSENSITIVE_DLL_EXTENSION_MATCHING.md`** — Issue #7 (Low).
  Case-insensitive `.dll`/`.DLL`/`.Dll` extension matching, plus a debug-level
  log line for any non-`.dll` file encountered while scanning, so a
  capitalization mismatch is at least discoverable.
- **`PHASE8_PLUGINHOST_FAILURE_PATH_REGRESSION_TESTS_AND_CAMPAIGN_CLOSEOUT.md`**
  — Issue #8 (Low) **plus this campaign's own mandatory final full regression
  verification and closeout** (mirroring every prior campaign's own last-phase
  shape). Adds real, deliberately-broken fixture plugin `.dll`s and GoogleTest
  coverage for all 4 of `PluginHost`'s documented failure/skip paths, then runs
  the one, single, campaign-wide full clean build + full `ctest` pass + live
  HTTP smoke test this whole campaign is allowed to run (per this campaign's
  own Workflow Rule 1 below), and writes `CAMPAIGN_COMPLETION_REPORT.md`.

### Campaign-wide rules every phase must follow ("Workflow Rule 1" – "Workflow Rule 10" below — later phase files, including PHASE8, cite these exact numbers, e.g. "Workflow Rule 7")

1. **No full build, no full regression test, for Phases 1–7.** Only a fast,
   targeted incremental compile check (see each phase's own "Verification"
   section for the exact narrow command) — this machine's full clean build and
   full `ctest` pass are slow and are reserved for Phase 8 alone.
2. **Fast, incremental, narrow verification only, for Phases 1–7**: rebuild
   only the specific target(s) touched (e.g. `cmake --build build --target
   gte_core`, or a dedicated small nested configure for a CMake-only change —
   see Phase 2's own file for exactly how to verify a `GTE_ENABLE_PLUGINS=OFF`
   configure without a full rebuild of the `ON` tree). Use
   `run_app_background` + `gte_send_request` for any visual/behavioral check
   that needs a running `GreatTamanaEditor.exe` (e.g. confirming a log line
   appears via `GET /get_logs`, confirming a panel is still listed via
   `GET /list_tabs`) — always `stop_app_background` it when done.
3. **Use the engine's own internal logging + `GET /get_logs`, never
   `printf`/`std::cout`/`OutputDebugString`,** for anything inside
   `src/` (this repo's own `AGENTS.md` "Logging" convention — `GTE_LOG_DEBUG/
   INFO/WARNING/ERROR`, `Core/Logging.h`). Standalone `tools/ci/*` probe
   `main.cpp` files are the one pre-existing exception (they use `std::printf`/
   `std::fprintf` to stdout/stderr, matching `gte_plugin_isolation_probe`'s own
   established precedent) — keep matching that precedent exactly if a phase
   touches one of those files, do not introduce engine logging there instead.
4. **Every phase writes its own `PHASEn_COMPLETION_REPORT.md`** in this same
   folder once its own compile check passes, documenting exactly what changed,
   any real deviation from this phase's own plan, and the exact verification
   evidence gathered (command run + its real output/exit code).
5. **`git_add` + `git_commit` at the end of every phase** — one commit per
   phase, message referencing the phase number and its one-line summary,
   mirroring every prior campaign's own commit-per-phase discipline.
6. **If a phase hits a genuine design ambiguity not already resolved by this
   master strategy or its own phase file, use `ask_questions` before guessing.**
   This applies transitively: if this phase itself ever delegates a further
   sub-task, that sub-task must also be instructed to use `ask_questions` for
   its own genuine ambiguities.
7. **Implementation phases (1–8) must NOT call `delegate_task` themselves —
   with exactly ONE narrow, explicit exception.** Only the double-check/
   orchestration step of this campaign (run separately, after all 8 phases
   are scaffolded and reviewed) is allowed to use `delegate_task` to hand off
   each phase's real implementation. **The one exception**: PHASE8's own
   mandatory final full regression pass (its own Step 4) may invoke
   `delegate_task` ONLY if that regression pass surfaces a real,
   newly-broken, unexplained test failure that needs a dedicated fix — never
   for any other reason, and never by any phase other than PHASE8.
8. **Every phase's code must follow `AGENTS.md`'s existing coding guidelines**
   (Clean Architecture, RAII, `namespace gte`) and this repo's own established
   plugin-architecture conventions (`docs/conventions/plugin-architecture.md`,
   `plugins/gte_plugin_abi/PublicSurface.md`) — never invent a new pattern
   where an existing one already covers the same shape (e.g. Phase 1's warning
   uses the exact same `GTE_LOG_WARNING` mechanism every other subsystem
   already uses, never a new logging path).
9. **Never break any of the 10 Locked Design Decisions** the prior campaign
   confirmed (see `task_manager/editor-core-separation-3/
   CAMPAIGN_COMPLETION_REPORT.md`'s own restated list) — this campaign only
   hardens/corrects the existing implementation, it does not change the
   architecture's own shape.
10. **Update `docs/conventions/plugin-architecture.md` and
    `plugins/gte_plugin_abi/PublicSurface.md`** whenever a phase changes a
    behavior either document currently describes incorrectly or incompletely
    (Phase 1 in particular — the CRT-linkage section there currently repeats
    the same false "the host REFUSES to load" claim the fingerprint header
    itself makes).

### What this campaign explicitly does NOT do (Non-Goals, restated)

- No new compositing/render-target work for multiple `IRenderFeatureModule_v1`
  plugins (Issue #5's minimal-fix scope, confirmed via `ask_questions`).
- No actual switch of this repository's active `CMAKE_CXX_COMPILER` to the
  already-installed shared-runtime-capable MinGW toolchain — that remains its
  own, separate, deliberately deferred decision (unchanged from the prior
  campaign's own stated Non-Goals).
- No hot reload, no cross-process sandboxing, no per-project plugin
  manifest/UI, no cross-compiler third-party plugin SDK — all unchanged,
  permanent Non-Goals from the source design doc and the prior campaign.
- No real engine feature migrated onto the plugin mechanism.

### Reference commands

- Configure + incremental build: `cmake --build build` (working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`, build folder `build`,
  Ninja/MinGW).
- Full regression test (Phase 8 ONLY): `cd /d
  C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug
  --output-on-failure`.
- Run the live Editor for HTTP-driven smoke checks:
  `run_app_background` on `build\GreatTamanaEditor.exe`, then
  `gte_send_request` against `http://127.0.0.1:8080` (default port) —
  `GET /get_logs?limit=50`, `GET /list_tabs`, `GET /get_swapchain`,
  `GET /get_game_view`, `POST /clear_logs`, etc. Always
  `stop_app_background` the PID when finished.

### Every child phase file in this folder

1. `PHASE1_SHARED_CRT_FINGERPRINT_HONESTY_AND_STARTUP_WARNING.md`
2. `PHASE2_GTE_ENABLE_PLUGINS_OFF_BUILD_FIX.md`
3. `PHASE3_EDITOR_PANEL_NAME_COLLISION_PROTECTION.md`
4. `PHASE4_PLUGIN_RUNTIME_DLL_LANDMINE_DEFENSE_IN_DEPTH.md`
5. `PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md`
6. `PHASE6_DEFENSIVE_MODULE_INFO_BUFFER_READS.md`
7. `PHASE7_CASE_INSENSITIVE_DLL_EXTENSION_MATCHING.md`
8. `PHASE8_PLUGINHOST_FAILURE_PATH_REGRESSION_TESTS_AND_CAMPAIGN_CLOSEOUT.md`

Read this master file FIRST, then the one phase file you are working on, then
(if it exists yet) the previous phase's own `PHASEn_COMPLETION_REPORT.md` for
continuity clues, before writing any code.
