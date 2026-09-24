# editor-core-separation-2 — CAMPAIGN COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. This report is the single, top-level summary of the whole
5-phase campaign, mirroring `task_manager/editor-core-separation-1/CAMPAIGN_COMPLETION_REPORT.md`'s
own shape and tone. See each `PHASEn_COMPLETION_REPORT.md` in this same folder for full per-phase
detail.

## What this campaign set out to do

`editor-core-separation-1`'s own closeout report (`CAMPAIGN_COMPLETION_REPORT.md`) was brutally
honest that 3 of its own 4 "Hard Rules" were still false: `gte_core.a` had zero SDL/ImGui, but it
still carried real, live `gte_core -> gte_editor`-only-symbol dependencies (`RecordFrameDebuggerDraws()`,
`Logger::Query()`/`Clear()`/etc.) and three real `#include` violations of "`gte_core` never includes
anything under `src/Editor/` except `EditorLayer.h`" — and its own verification tool
(`tools/ci/gte_core_standalone_probe`) was structurally incapable of ever catching the symbol-level
class of bug, since it only ever builds `gte_core` as a static archive, never links a real executable.

This campaign's goal was narrow and concrete: fix the three real, named code defects that made those
Hard Rules false, and add a second, permanent, real-executable-link probe that can actually catch this
class of bug, forever — replacing hope with mechanical proof.

## What shipped, phase by phase

**PHASE1 — `EditorPanelCatalog.h` Relocation.** A pure, byte-for-byte `git mv` of
`src/Editor/EditorPanelCatalog.h` → `src/Core/EditorPanelCatalog.h` (it is `gte_core`-owned compile-time
data with no `.cpp`, so it caused no link-time hazard, but its physical address under `src/Editor/` was
itself the architectural violation). Every real `#include` and doc-comment mention fixed
(`NetworkRoutes.h`, `DockLayout.cpp`, two test files, five doc-comment-only files). Zero deviations,
zero ambiguity.

**PHASE2 — `IFrameDebuggerCaptureRecorder` Interface (closes Defects A and B — the core of this
campaign).** A new, `gte_core`-owned, pure abstract interface (`src/Core/FrameDebuggerCaptureRecorder.h`)
with two methods (`RecordFrameDebuggerDraw()`, `AddReplayPasses()`) replacing the two free functions
`gte::RecordFrameDebuggerDraws()`/`gte::AddFrameDebuggerReplayPasses()` that only `gte_editor.a` defined.
`FrameDebuggerCaptureContext` (`gte_editor`-only) now implements this interface; every `gte_core`-tier
pass-through site (`Core.h/.cpp`, `RenderPasses.h/.cpp`, `Game.h/.cpp`, `RenderSystem.h/.cpp`,
`EditorLayer.h`) changed its pointer's static type from a bare `FrameDebuggerCaptureContext*` forward
declaration to `IFrameDebuggerCaptureRecorder*`. **One real, self-introduced bug found and fixed by this
phase's own mandatory compile check**: five new `#include` lines were accidentally placed inside each
file's own `namespace gte { ... }` block instead of above it, creating a bogus nested `gte::gte`
namespace — caught immediately by the first `gte_editor` build attempt, fixed, re-verified clean.
Frame Debugger smoke test confirmed real per-draw recording and replay-step preview generation both
still worked end-to-end through the new interface pointer.

**PHASE3 — `ILogQueryCapability` (closes Defect C).** A new Bucket-B-style capability interface
(`src/Core/EditorCapabilities.h`), mirroring `ISceneIOCapability` exactly, backed by a new
`EditorLogQueryCapability` (`gte_editor`) that delegates to the real `Logger` class.
`NetworkServer` gained a sixth, defaulted, nullable constructor pointer; `GET /get_logs`/
`POST /clear_logs` now call through the capability with a null-check degrading to `503`, instead of
calling `Logger::` directly. `Logger::kCapacity`'s literal value relocated into
`src/Core/Logging.h` as `kLogCapacity`, closing `NetworkRoutes.cpp`'s own compile-time-only violation.
**One real, necessary deviation**: the capability's static instance had to be a namespace-scope static
in `EditorHost.cpp` rather than function-local-inside-the-constructor-body (`NetworkServer`'s
member-initializer list needs its address before the constructor body runs) — documented in-place,
resolved without needing `ask_questions`. `tests/Network/LogEndpointsEndToEndTests.cpp`'s fixture
updated to wire a real capability in, plus one brand-new null-capability-degrades-to-503 test.

**PHASE4 — `$<LINK_GROUP:RESCAN,...>` Removal + Permanent Player-Link Probe (the phase that PROVES the
fix).** Replaced the `$<LINK_GROUP:RESCAN,gte_editor,gte_core>` generator expression in both
`CMakeLists.txt` (`GreatTamanaEditor`) and `tests/CMakeLists.txt` (`GreatTamanaEngineTests`) with a
plain `target_link_libraries(... gte_editor)` — and it linked successfully on the **first attempt, with
zero `undefined reference` errors**, the single strongest mechanical confirmation that PHASE2/PHASE3
had genuinely closed every real symbol dependency. Added a brand-new, permanent, checked-in probe
project, `tools/ci/gte_core_player_link_probe/` — a tiny `main.cpp` that forces `RenderSystem.cpp.obj`,
`Core.cpp.obj`, and `Network/NetworkServer.cpp.obj` to be pulled from `libgte_core.a` (via
member-function-pointer address-taking plus a real, safe `NetworkServer` construction) and links a real
executable against `gte_core.a` **alone** — no `gte_editor`, no SDL, no ImGui. Succeeded on the first
attempt too. `tools/ci/gte_core_standalone_probe/` (the pre-existing, PHASE18-of-`editor-core-separation-1`
probe) was left completely untouched, as required.

**PHASE5 (this phase) — Final Full Regression Verification and Campaign Closeout.** See below for the
full, fresh, mechanical re-check.

## Full clean build + full `ctest` regression pass (this phase's own mandatory checkpoint)

- **Full clean rebuild** (`build/` deleted, reconfigured fresh via `cmake -S . -B build -G Ninja`,
  built via `cmake --build build`): **498/498 steps, zero errors.** (Up from
  `editor-core-separation-1`'s own final count of 497 — the delta of 1 is `src/Core/FrameDebuggerCaptureRecorder.h`,
  the only new header this campaign added to a `target_sources()` list; every other new file this
  campaign added either replaced an existing file's own content in place, or was a `.cpp` counted
  elsewhere.) The link for `GreatTamanaEditor`/`GreatTamanaEngineTests` used the plain, single-pass
  `target_link_libraries(... gte_editor)` line PHASE4 put in place — no `RESCAN`, confirmed directly
  in the build log (`[496/498] Linking CXX executable GreatTamanaEditor.exe`, no rescan-related output).
- **Full `ctest -C Debug --output-on-failure`**: **1774 total tests, 1772 passed (100% of executed),
  2 legitimate, documented, environment-gated skips, zero failures**:
  - `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` (pre-existing, gated on a real
    MMD model file not present on this machine).
  - `CoreHeadlessConstructionTest.ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`
    (pre-existing, this machine's Vulkan driver lacks `VK_EXT_headless_surface`).
- **Before/after comparison against `editor-core-separation-1`'s own final baseline** (Phase 19: "1773
  total, 1771 passed, 2 legitimate skips, zero failures"): this run is **1774 total, 1772 passed, 2
  legitimate skips, zero failures** — the total grew by **exactly 1**, and the passing count grew by
  exactly 1 too, matching PHASE0_MASTER_STRATEGY.md's own prediction precisely (PHASE3's one new
  `LogEndpointsNullCapabilityTests.GetLogsAndClearLogsReturn503WhenCapabilityIsNull` test, confirmed
  running and passing as test #95 in this run). **Zero new failures, zero unexplained count drift.**

## Both CI probes, re-run fresh from a clean build directory

- **`tools/ci/gte_core_standalone_probe`** (`cmake -S ... -B build-core-probe -G Ninja && cmake --build
  build-core-probe`, no `--target` passed, per that project's own documented default-`ALL`-target
  shape): **224/224 steps, zero errors**, `[224/224] Linking CXX static library libgte_core.a`.
- **`tools/ci/gte_core_player_link_probe`** (`cmake -S ... -B build-player-link-probe -G Ninja && cmake
  --build build-player-link-probe`): **226/226 steps, zero errors, zero `undefined reference`**,
  `[226/226] Linking CXX executable gte_core_player_link_probe.exe`. Confirmed via `browse_dir` that
  the real executable genuinely exists on disk (32.6 MB, alongside `libgte_core.a` at 63.8 MB, and
  critically, **no `libgte_editor.a`, no SDL3, no ImGui anywhere in that build tree**) — a real,
  successfully-linked, standalone executable, not just a "build succeeded" message.

This is this campaign's own Definition of Done, satisfied mechanically, for real, from a completely
fresh build directory: a real `cmake --build` linked a real, standalone executable against `gte_core.a`
alone (no `gte_editor` anywhere in that configuration's link line) that references
`RenderSystem::Draw()` and constructs a real `gte::Network::NetworkServer`, and it succeeded.

## Live, HTTP-driven end-to-end smoke test

Booted `build/GreatTamanaEditor.exe` via `run_app_background`, drove it via `gte_send_request`:

| Endpoint | Result |
|---|---|
| `GET /get_swapchain` | `200`, real rendered PNG — Editor renders correctly end-to-end (Hierarchy/Scene/Game/Inspector/Memory/Profiler/Render Graph/Atmosphere/Jobs/Log/Project all present) |
| `GET /list_tabs` | `200` — `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project"]}` |
| `GET /activate_tab?name=Hierarchy` | `200` — `{"activated_tab":"Hierarchy","success":true}` — PHASE1's `EditorPanelCatalog.h` relocation didn't break tab routing |
| `GET /frame_debugger/open` → `.../enable?value=true` → `.../capture` → `GET /frame_debugger/state` | See "Investigation finding" below — once the Game tab was activated (a real dock-layout precondition, not code), this sequence reproduced PHASE2's own exact result: real tree (`Compute LUT` → `RenderOpaque` → `DrawSkyBackground` → `Compute Dispatches (Post-GameView)` → `AtmosphereAerialPerspectiveCompositePass`), `hasCapturedFrame:true`, `totalEventCount:15` — PHASE2's `IFrameDebuggerCaptureRecorder` conversion genuinely still records real per-draw/replay-step data through the new interface pointer |
| `GET /get_logs?limit=20` | `200`, `count:2` real startup entries — PHASE3's `ILogQueryCapability` wiring genuinely reaches the real `Logger` singleton |
| `POST /clear_logs` then `GET /get_logs?limit=20` again | `200`, `{"cleared_count":2,"success":true}`, then `count:0` — confirms the capability's `Clear()` path genuinely empties the real ring buffer (`latest_id` stayed `2`, never reset, exactly as documented), not a stub |
| `POST /save_scene` (`{"path":""}`) / `POST /load_scene` (`{"path":""}`) | `200`, `success:true`, same resolved path both times (`build/Project/TestScene.gtscene`) — the `ISceneIOCapability`/`EngineCommandBridge` path is completely untouched by this campaign's own Frame Debugger/Logger changes, confirmed |

`stop_app_background`'d the process cleanly when done. Every endpoint behaves correctly.

### Investigation finding (not a regression — recorded honestly, not swept aside)

The first attempt at the Frame Debugger sequence (`open` → `enable=true` → `capture` → `state`) returned
`hasCapturedFrame:false`, `totalEventCount:0` repeatedly, across many real HTTP round-trips — a result
that, taken at face value, would look exactly like a genuine PHASE2 regression. Direct source
investigation (`ImGuiEditorLayer.cpp`'s `GameViewTarget()`) found the real cause: `GameViewTarget()`
returns `nullptr` — and the entire Game-view render/capture pipeline is skipped that frame — whenever
`!m_ctx.gameViewVisible`, i.e. whenever the "Game" tab is not the currently-active dock tab. This
session's freshly-launched Editor had "Scene" as the active tab (a persisted ImGui docking-layout state
left over from a previous run, unrelated to any code this campaign touched). Once `GET
/activate_tab?name=Game` was called first, the exact same Frame Debugger sequence reproduced PHASE2's
own documented result byte-for-byte (`totalEventCount:15`, the same named pass tree). This is a real,
pre-existing, environment/session-state precondition of the Frame Debugger feature itself (from the
much older, unrelated `frame-debugger-*` campaigns) — not a defect introduced or left behind by
`editor-core-separation-2`. Flagged here in full, exactly as this phase's own "brutal honesty" mandate
requires, rather than silently omitted after being resolved.

## The Four Hard Rules (`PHASE0_MASTER_STRATEGY.md`, restated from `editor-core-separation-1`, Section 1.3) — restated, with fresh evidence

1. **"`gte_core.a` compiles and links standalone with literally zero editor code, zero ImGui, zero
   debug-only feature code, zero SDL headers in its own translation units — checked mechanically."**
   **YES — now the FULL bar, not `editor-core-separation-1`'s own "partially true" verdict.**
   `search_in_dir(src, "#include\s*"[./]*Editor/")` (regex) across the ENTIRE `src/` tree returns
   exactly **two** matches: `src/Core/Core.cpp` → `../Editor/EditorLayer.h` (the one documented,
   permanently-sanctioned exception) and `src/main.cpp` → `Editor/EditorHost.h` (this file is the
   executable's own entry point, compiled directly into `GreatTamanaEditor`, never into `gte_core.a`
   itself — confirmed it is not part of `gte_core`'s own `target_sources()` list). **Zero** real
   violations remain anywhere in `gte_core`'s own compiled sources — the three that
   `editor-core-separation-1` left open (`NetworkRoutes.h`/`.cpp`, `NetworkServer.cpp`) are gone,
   confirmed directly: `search_in_dir(src/Network, "Editor/")` now finds only prose comments describing
   the fix, zero real `#include`s. The standalone-core probe re-confirms this mechanically (224/224
   steps, zero errors, zero `gte_editor`/SDL/ImGui in the produced tree).
2. **"`gte_editor.a` may depend on `gte_core.a`. Never the reverse... AND true in the weaker sense of
   'no unresolved external symbol only gte_editor.a defines'."** **YES, fully, for the first time in
   either campaign.** The `#include`/`target_link_libraries()` direction was already correct
   (confirmed again by reading the final `CMakeLists.txt`: `gte_core` links only third-party/engine
   deps, `gte_editor` links `gte_core` PUBLIC plus ImGui/ImGuizmo/SDL3). The stronger "no unresolved
   symbol" half — the actual, real gap `editor-core-separation-1` left open — is now closed: the plain,
   single-pass `target_link_libraries(GreatTamanaEditor PRIVATE gte_editor)` (no `RESCAN`) linked
   successfully on the first attempt, confirmed fresh in this phase's own full clean build
   (`search_in_dir` for `LINK_GROUP:RESCAN` across the real `CMakeLists.txt`/`tests/CMakeLists.txt`
   returns zero matches — every remaining mention is historical prose in `task_manager/**` reports/git
   log, not live build-system code).
3. **"A thin Player-build host... can link `gte_core.a` alone and get a running, renderable engine."**
   **YES, for the first time in either campaign's history, with permanent, re-runnable, mechanical
   proof.** `tools/ci/gte_core_player_link_probe` — re-run this phase from a completely fresh build
   directory — linked a real executable (`gte_core_player_link_probe.exe`, 32.6 MB, confirmed present
   on disk) against `libgte_core.a` alone (226/226 steps, zero errors, zero `undefined reference`), with
   zero `gte_editor.a`/SDL3/ImGui anywhere in that build tree. This is no longer "a throwaway experiment
   that happened to work once, then was deleted" (`editor-core-separation-1`'s own Phase 19 evidence) —
   it is a permanent, checked-into-this-repo probe project any future phase/campaign can re-run.
4. **"`gte_editor.a` is unconditionally configured every single time this repository is configured —
   there is no CMake option... anywhere that skips building it (except the pre-authorized
   `GTE_CORE_STANDALONE_PROBE_ONLY` escape hatch)."** **YES, unchanged from `editor-core-separation-1`'s
   own already-correct verdict.** Re-confirmed directly in the final `CMakeLists.txt`: `gte_editor`'s
   `add_library()` block is gated by exactly one `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard, nothing
   else. This campaign added no new way to skip `gte_editor` in a normal configure.

**Honest summary: 4 of 4 Hard Rules are now fully, mechanically satisfied — this is the first time
either campaign has been able to say that.** `editor-core-separation-1` closed with "1 of 4 fully
satisfied, 3 of 4 tracing to the same two pre-existing symbol dependencies." This campaign's own real,
mechanical checks (a full clean build, a full `ctest` pass, both probes fresh, a live smoke test) all
agree: every one of the three named defects (Defect A, B, C) is genuinely closed, and the fourth Rule
was already true and remains untouched.

## Deviations from the original plan (consolidated from every phase's own report)

1. **PHASE1** — none. Every real `#include`/doc-comment location matched the plan's own prediction
   exactly; one small addition (four extra bare-name comment mentions in `NetworkRoutes.h`) already
   anticipated by the plan's own "final sweep" instruction.
2. **PHASE2** — one real, self-introduced implementation bug (a `namespace gte { #include ... }`
   nesting mistake affecting five files), found and fixed by this phase's own mandatory compile check,
   documented honestly rather than silently fixed. No architectural ambiguity.
3. **PHASE3** — one real, necessary deviation (the new capability's static instance had to be a
   namespace-scope static, not function-local, due to `NetworkServer`'s member-initializer-list timing
   requirement) — documented in-place, resolved without needing `ask_questions`.
4. **PHASE4** — none of substance on the code-changes side; both `RESCAN` removals and the new probe
   linked on the first attempt with zero errors — the direct, honest confirmation that PHASE2/PHASE3
   closed every real call site completely. One small addition beyond the plan's own file list
   (`.gitignore` entry for the new probe's build tree, mirroring the existing precedent).
5. **PHASE5 (this phase)** — one real investigation finding (the Frame Debugger's `hasCapturedFrame`
   staying `false` until the Game tab was made the active dock tab) was chased down to its real root
   cause and confirmed to be a pre-existing, unrelated, environment/session-state precondition of the
   much older `frame-debugger-*` feature — not a regression introduced by this campaign. No genuine
   regression was found anywhere in Step 3's mechanical checks, so `delegate_task` was never invoked
   (per PHASE0's own Universal Rule 8 and this phase's own Step 4 instructions — a fix task is only
   spun off when Step 3 actually surfaces a real regression, and it did not).

## What remains genuinely open (honest, not silently dropped)

- **Nothing from this campaign's own, narrow scope remains open.** All three named defects (A, B, C)
  are closed, confirmed by fresh, real, mechanical evidence (a full clean build, a full `ctest` pass,
  both probes, a live smoke test) — not merely "confirmed by code reading," the exact failure mode this
  campaign exists to fix.
- **The Player Build Pipeline remains explicitly, permanently OUT OF SCOPE** — restated here one final
  time, exactly as `editor-core-separation-1`'s own report insisted, and exactly as this campaign's own
  `PHASE0_MASTER_STRATEGY.md` Non-Goals section states: this campaign proves `gte_core.a` COULD support
  a real Player host (via the new player-link probe), it does not build the actual Player tooling. No
  `<ProjectName>.exe` generation, no per-project build system.
- **No real CI pipeline exists for this repo** (unchanged) — both probes remain manually-invocable local
  CMake projects, exactly as designed.
- **A pre-existing, unrelated doc/code drift, flagged by PHASE4, left untouched as instructed**:
  `tools/ci/gte_core_standalone_probe/README.md`'s own "How it works" section still describes an
  `add_subdirectory()`-based mechanism, but the real, current `CMakeLists.txt` in that same folder
  actually uses the nested-`cmake`-invocation mechanism (the README appears to describe an earlier,
  abandoned design that was never updated after the pivot). PHASE4 was explicitly instructed not to
  touch that probe project at all, so this was correctly left alone — flagged again here for a future
  maintainer, not silently dropped.
- **A separate, pre-existing, unrelated documentation drift, noticed but out of this campaign's own
  scope to fix**: `BUILDING.md`, `AGENTS.md`, and numerous `docs/conventions/*.md` files still describe
  a `GTE_ENABLE_EDITOR` CMake option/macro as if it still exists — it does not; `editor-core-separation-1`
  Phase 8 deleted it outright, over a year before this campaign started. This is stale documentation
  left over from before `editor-core-separation-1`, not something `editor-core-separation-2` introduced
  or was ever asked to fix — a documentation-refactor concern for a future, separate campaign, not this
  one's job.

`editor-core-separation-2` is complete. Its real, substantial deliverable — all four of the design
doc's own Hard Rules now mechanically, provably true, with a new permanent probe proving the one that
matters most (Rule 3) forever — has shipped and is verified working end-to-end, with fresh, real,
mechanical evidence from a completely clean build, not inherited assumptions from any prior phase's own
narrower checks. Ready to merge.
