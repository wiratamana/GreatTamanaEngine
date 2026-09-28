# PHASE5 — Probe Fixture Extension, Full Live Verification, Full Build/Test, Campaign Closeout

Parent: `PHASE0_MASTER_STRATEGY.md`.
Depends on: PHASE1-4 ALL fully done (not merely started) — this phase is
the ONLY one in this whole campaign permitted to run a full build/full
`ctest` regression pass (mirrors `editor-core-separation-13`'s own PHASE5
workflow rule exactly).
Blocks: nothing — this is the last phase.

---

## STEP 1 — The Goal

Prove, live, against a real running `GreatTamanaEditor.exe`, that the
complete hot-reload cycle built by PHASE1-4 actually works for BOTH the
success path and the deliberately-broken-compile rollback path, using
`Projects/ProjectAssemblyProbe/` (the same permanent, `.gitignore`-excluded
fixture `editor-core-separation-13`'s own PHASE5 already extended with
`ProbeHotReloadMarker`) — then run the one full build + full `ctest` pass
this whole campaign is allowed, fix anything that pass finds, and write the
final `CAMPAIGN_COMPLETION_REPORT.md` + `AGENTS.md` update.

## STEP 2 — The Situation

Confirmed: `Projects/ProjectAssemblyProbe/` already exists (built by
`editor-core-separation-11`, extended by `editor-core-separation-13`'s own
PHASE5 with a throwaway `ProbeHotReloadMarker` custom ECS component). It is
excluded from git entirely (`.gitignore` line 115, `/Projects/`) —
confirmed by `editor-core-separation-13`'s own campaign report; any edits
this phase makes to it are real, on-disk, and exercised live, but never
committed (consistent with every prior campaign that touched this
directory).

## STEP 3 — The Plan

### 3.1 — Extend the probe fixture for a REAL rollback test

Add a small, genuinely toggleable compile-error mechanism to
`ProjectAssemblyProbe`'s own source (e.g. `HelloGame.cpp` or its compute
shader) — a `#if 0`/`#if 1`-guarded deliberately-invalid line, OR (cleaner,
less likely to be left on by accident) a small script-free manual edit
this phase's own implementer toggles on, tests, then toggles back off,
confirmed via `git diff`-equivalent (a plain before/after file-content
compare, since this directory is untracked) showing the source is back to
its pre-test, valid state before the campaign ends. Document the EXACT
line changed and reverted in this phase's own completion report (mirrors
`editor-core-separation-13`'s own "Deliberate deviations" item 2 precedent
for how to document a temporary, fully-reverted test change).

Add a REAL, meaningful, visibly-different source change for the success
path test (e.g. change the probe's compute-shader fill color, or add a
second small render pass) — this one is left in place after the test if it
usefully expands the fixture, or reverted if it was purely a throwaway
color swap; implementer's judgement, documented either way.

### 3.2 — Live verification (background-launched `GreatTamanaEditor.exe`)

**Baseline** (before any reload):
1. `GET /project_assembly/debug/loaded_assemblies` — confirm both
   `ProjectAssemblyProbe_Game.dll`/`_Editor.dll` present.
2. `GET /list_tabs` — confirm `"Probe Panel"` present.
3. `GET /render_graph` — confirm `"ProjectAssemblyProbe.FillTexture"`
   present.
4. `GET /get_swapchain` — confirm normal rendering.

**Rollback path** (deliberately-broken compile, 3.1):
5. `POST /project_assembly/debug/compile_only?name=ProjectAssemblyProbe` —
   confirm, via `GET /get_logs?category=ProjectAssemblyBuild`, that the
   introduced error is REAL and reproducible (a genuine compiler error line
   appears) BEFORE ever attempting the full cycle — this de-risks the next
   step by isolating "is my test error real" from "does hot-reload's own
   rollback logic work".
6. `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` — poll
   `GET /project_assembly/hot_reload/status` from a SECOND, concurrent
   `gte_send_request` call while step 6 is still in flight (dispatch both
   in the same tool-call block, or immediately back-to-back) — confirm
   `phase` advances through `CapturingState -> BackingUpBinaries ->
   Unloading -> Compiling -> RollingBack -> RestoringState -> Idle`, ending
   `lastOutcome == "RolledBack"`.
7. Post-cycle: `GET /project_assembly/debug/loaded_assemblies` (both files
   present again — the RESTORED backup, not the broken new one), `GET
   /list_tabs` (`"Probe Panel"` present), `GET /render_graph`
   (`"ProjectAssemblyProbe.FillTexture"` present), `GET /get_swapchain`
   (normal rendering, no corruption).
8. Process survives several real seconds of continued normal operation
   afterward (repeated `GET /get_swapchain`/`GET /get_logs` across a
   multi-second span) — **the single most important check, per
   `editor-core-separation-13`'s own PHASE5 precedent**: a subtly-wrong
   teardown/reload can pass every check above and still crash moments
   later.

**Revert the deliberate compile error (3.1)** before continuing.

**Success path** (real, meaningful source change, 3.1):
9. `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` — confirm
   `phase` advances through `CapturingState -> BackingUpBinaries ->
   Unloading -> Compiling -> ReloadingNewCode -> RestoringState -> Idle`,
   ending `lastOutcome == "Success"`.
10. `GET /render_graph`/`GET /get_swapchain` — confirm the NEW behavior is
    genuinely visible (a real, observable difference from the baseline
    screenshot/metadata — use `load_image`-equivalent comparison of two
    `GET /get_swapchain` captures if the change is visual).
11. No relaunch of `GreatTamanaEditor.exe` at any point across steps 5-10 —
    confirm via a single, continuously-running background process id
    throughout (never stopped/restarted between checks).

**Slow-build / message-pump check** (PHASE4's own deferred Definition of
Done item):
12. Temporarily insert a `Sleep(5000)` (or similar) into
    `ProjectAssemblyProbe`'s own build step (or, less invasively, into
    `RunOneBuildTarget()`'s own poll loop FOR TESTING ONLY, reverted
    immediately after) long enough to matter, trigger a hot reload, and
    confirm via continuous `GET /project_assembly/hot_reload/status`
    polling that `phase` stays `"Compiling"` (not stuck/hung, elapsed time
    advancing) for the whole artificial delay, and that the main window is
    not force-closed by Windows in the meantime (no visible crash/"Not
    Responding" kill). Revert the temporary `Sleep()` before finishing this
    phase.

**In-flight guard cross-check**:
13. Dispatch `POST /project_assembly/debug/compile_only?name=ProjectAssemblyProbe`
    and, within the same tool-call block (to beat the round-trip),
    `POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` — confirm
    the SECOND of the two observes rejection (whichever arrives second per
    the shared in-flight guard) rather than two overlapping builds racing
    each other's output.

### 3.3 — Full build + full `ctest` regression pass

```
cmake --build build
```
Then:
```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
Compare against `editor-core-separation-13`'s own documented baseline
(1925 tests, 100% passing, 5 legitimate environment-gated skips). Any NEW
failure must be diagnosed and fixed via `delegate_task` before this phase
can close — mirrors `editor-core-separation-13`'s own PHASE5, which found
and fixed two genuine, previously-latent defects this exact way (the
`ComponentTypeRegistry` bootstrap-attribution bug and the
`RenderGraphNameSlotTable` dangling-`const char*` bug) — **this campaign's
own new code (PHASE1-4) is squarely in the same "first time this code path
is ever really exercised end-to-end" position those two bugs came from; do
not be surprised if this pass finds something new, and do not skip it.**

### 3.4 — Completion report + `AGENTS.md`

Write `CAMPAIGN_COMPLETION_REPORT.md` in this same folder, mirroring
`editor-core-separation-13/CAMPAIGN_COMPLETION_REPORT.md`'s own shape
exactly: what was built per phase, every deliberate deviation from this
strategy's own files (and why — including the two corrections PHASE4's own
file already flagged in advance), the full live-verification checklist
results, the full build/`ctest` numbers before/after, and an explicit,
honest restatement that **BIG-STEP 4 (state snapshot/restore) remains
fully unimplemented** (HOOK POINT A/B are still no-op stubs) — mirroring
`editor-core-separation-12`/`-13`'s own identical "restate what remains
unimplemented" convention.

Update `AGENTS.md`'s existing "Project Assembly" section (search for
`editor-core-separation-13` or `IHotReloadDebugCapability` inside
`AGENTS.md` to find the right place) documenting: the real, working
`POST /project_assembly/hot_reload` cycle; the new files
(`ProjectAssemblyHotReload.h/.cpp`,
`ProjectAssemblyHotReloadCommandBridge.h/.cpp`); the
`GetHotReloadEngineStateMutex()` closure from PHASE1; and the explicit
statement that BIG-STEP 4 is the next, not-yet-started campaign.

## Definition of Done — this phase, and this whole campaign

- [ ] All 13 live checks in 3.2 pass.
- [ ] The deliberate compile-error toggle (3.1) is confirmed fully reverted
      before this phase ends.
- [ ] Full build succeeds; full `ctest` shows zero new regressions versus
      the `editor-core-separation-13` baseline (or every new failure found
      is diagnosed and fixed, via `delegate_task`, before closing).
- [ ] `CAMPAIGN_COMPLETION_REPORT.md` exists in this folder.
- [ ] `AGENTS.md` has an accurate, updated entry for this campaign's
      shipped surface, explicitly stating BIG-STEP 4 remains unimplemented.
- [ ] Every checkbox in every one of PHASE1-4's own Definition of Done is
      re-confirmed still true.

**This whole campaign (`editor-core-separation-14`, BIG-STEP 3 of 4) is
DONE only once every box above is checked.**
