# PHASE5 — Tests Review, Documentation Sweep, Full Build/Regression, Live Verification

## Parent -> `PHASE0_MASTER_STRATEGY.md`, read it first. Depends on PHASE1-4 already landed.

Branch: `feature/frame-debugger-impl`
Risk level: LOW (no new production behavior), but this is the phase that PROVES the whole campaign actually
works end to end — do not treat it as a rubber stamp.

## Step 1: The Goal (Where are we going?)

Close out the `frame-debugger-5` campaign exactly like `frame-debugger-4`'s own closing phase did: a full
Tier-1 test review/widening pass, every stale doc claim corrected, a full clean build of BOTH
`GTE_ENABLE_EDITOR` configurations, a full `ctest` regression pass, and — the single most important
deliverable of this whole campaign — a genuine, fully-automated, HTTP-driven, screenshot-verified live smoke
test PROVING that:

1. Every atmosphere LUT compute pass (Transmittance, Multi-Scattering, Sky-View, Aerial Perspective Volume,
   Aerial Perspective Composite) — and GPU Skinning, whenever a skinned model is present — now appears as
   its own real, distinct, selectable leaf under the Frame Debugger's split `"Compute Dispatches (Pre-GameView)"`/
   `"Compute Dispatches (Post-GameView)"` groups (v2 review finding — PHASE0's Locked Design Decision #8), with
   correct real read/write rows and GPU timing.
2. Selecting EACH ONE of those leaves shows a DIFFERENT, correct, that-pass's-own real output image — not
   all showing the same whole-frame Game View image, and not all showing each other's images by accident.
   Specifically: the Transmittance LUT leaf shows a 256x256-ish grey/blue gradient texture; the
   Multi-Scattering LUT leaf shows its own distinct 64x64 texture; the Sky-View LUT leaf shows its own
   200x100 sky-dome-shaped texture; the Aerial Perspective Volume leaf shows a real ray-marched thumbnail
   (PHASE4); the Aerial Perspective Composite leaf shows the same final image `GET /get_game_view` shows
   (unchanged from `frame-debugger-4`'s own already-proven behavior).

## Step 2: The Situation (Where are we now?)

PHASE1-4 have each individually fast-compile-checked and unit-tested their own slice, but no phase before
this one has: (a) run a FULL clean build of both Editor configurations, (b) run the FULL `ctest` suite (not
just `-R FrameDebugger`/`-R RenderGraph`), or (c) actually launched the real engine and looked at real
pixels for every newly-visible compute pass. This phase is where those three things finally happen, exactly
mirroring `frame-debugger-4/PHASE3_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md`'s own precedent and rigor
bar — read that file for the exact shape of a prior, successful instance of this same closing-phase pattern.

Known stale doc claims to correct (confirm each still says what's described here at implementation time —
docs may have drifted further since this was written):

- `AGENTS.md`'s "Frame Debugger" section still describes the OLD `"GPU Skinning"`/single hardcoded
  `"AtmosphereAerialPerspectiveCompositePass"` tree shape (as left by `frame-debugger-4`) — describe the NEW
  split `"Compute Dispatches (Pre-GameView)"`/`"Compute Dispatches (Post-GameView)"` groups instead (v2 review
  finding — PHASE0's Locked Design Decision #8), not a single unconditional `"Compute Dispatches"` group.
- `docs/conventions/frame-debugger.md`'s own tree-shape description and "Known limitation" sections are now
  stale — the limitation `frame-debugger-4` documented as "now fixed" was about composite-AWARENESS, not
  about compute-dispatch COVERAGE; this campaign's own limitation this doc must now describe as fixed is
  the "only 3 of 8+ real compute passes were ever visible" gap (`PHASE0_MASTER_STRATEGY.md`'s Step 2.2).
- `TODO.md`'s "Frame Debugger" section needs a new `~~struck-through~~"now fixed"` entry for this exact bug,
  plus (per `PHASE0_MASTER_STRATEGY.md`'s Non-Goal on true per-pass breakpointing) a new, clearly-scoped
  forward-looking item for a possible future "pause/step a specific compute dispatch" capability — do not
  silently drop this idea, but do not implement it either; if genuinely unsure how to phrase this TODO entry
  faithfully, `ask_questions`.
- `README.md`'s "Status" section needs a new top-of-list bug-fix bullet, mirroring `frame-debugger-4`'s own
  precedent exactly.

## Step 3: The Plan

### 3.1 Tier-1 test review (across all of PHASE1-4)

Re-read every test file touched by PHASE1-4 (`RenderGraphTypesTests.cpp`, `RenderGraphBuilderTests.cpp`,
`RenderGraphSnapshotTests.cpp`, `FrameDebuggerSnapshotBuilderTests.cpp`, `FrameDebuggerDataTests.cpp`,
`FrameDebuggerHistoryTests.cpp`, and any new file introduced along the way) end to end. Look specifically
for: (a) any test still asserting the OLD, now-replaced tree shape that PHASE2 was supposed to update but
may have missed (grep for `"GPU Skinning"`/`"AtmosphereAerialPerspectiveCompositePass"` one more time across
the WHOLE `tests/` tree, not just the one file PHASE2 already touched); (b) any genuinely extractable pure
function left un-tested inside a Tier-2 file (`FrameDebuggerHistory.cpp`'s new discovery/collection logic,
PHASE3/PHASE4) that should be pulled out and given real Tier-1 coverage now if it wasn't already; (c) whether
the new `ChooseFrameDebuggerPreviewSource()` 5-boolean input space (PHASE3) has full, exhaustive coverage of
every meaningful combination, matching the rigor `frame-debugger-4` PHASE3's own
`ChooseFrameDebuggerPreviewSourceTest` set as the bar.

### 3.2 Documentation sweep

Update `AGENTS.md`, `docs/conventions/frame-debugger.md`, `TODO.md`, `README.md` per Step 2 above. Keep each
edit factual and specific — describe the REAL new tree shape (`"Compute Dispatches"` group, generic
discovery via `isComputePass`), not a vague "compute passes are now better supported" hand-wave.

### 3.3 Full clean build — both Editor configurations

```
cmake --build build --clean-first
```

Then a fresh, `GTE_ENABLE_EDITOR=OFF` configure + build (zero network access needed, mirrors
`frame-debugger-4` PHASE3 exactly):

```
cmake -S . -B build-editor-off -DGTE_ENABLE_EDITOR=OFF
cmake --build build-editor-off --clean-first
```

Confirm zero errors in both, and confirm every Frame-Debugger-specific file this WHOLE `frame-debugger-5`
campaign touched is correctly absent from the `=OFF` compile log (mirrors `frame-debugger-4`'s own
precedent — `RenderGraphTypes.h`/`RenderGraphBuilder.h`/`RenderGraphSnapshot.h`/`.cpp` from PHASE1 are
NOT Editor-gated and MUST still compile in the `=OFF` configuration too, since core `RenderGraph` code is
never Editor-only — confirm this explicitly, it is a real, checkable fact, not an assumption).

### 3.4 Full `ctest` regression pass

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Expect 100% pass (matching every prior campaign's own bar — `frame-debugger-4` landed at 100% of 1406
tests, one pre-existing machine-gated skip). Any new failure must be root-caused and fixed before this
phase is considered done — never marked "pre-existing" without independently confirming it also fails on
the base commit before this campaign started.

### 3.5 Live, HTTP-automation-driven, screenshot-verified smoke test (REQUIRED, not optional)

Mirror `frame-debugger-4/PHASE3_COMPLETION_REPORT.md`'s own exact working method (real HTTP calls against a
real, running `GreatTamanaEngine.exe`, real screenshots via `gte_send_request`/`load_image`, zero
mouse/keyboard):

1. `run_app_background` the engine.
2. Spawn whatever real scene content is needed to make EVERY targeted compute pass actually run this frame
   — at minimum a real Sun light (drives the atmosphere passes, which the engine already runs
   unconditionally once a Sun exists, per `atmosphere-scattering-4`'s own precedent) AND a real
   GPU-skinned model instance if one is easily spawnable via the existing HTTP API (check
   `docs/`/`Network/NetworkRoutes.h` for an existing "spawn a skinned model" endpoint; if none exists cheaply,
   it is acceptable to verify GPU Skinning's own leaf presence via a scene that already has one loaded by
   default, or to explicitly note in the completion report that GPU-Skinning-leaf verification was skipped
   for a documented, specific reason — do not silently skip without recording why).
3. Enable the Frame Debugger and trigger a real capture over HTTP (existing `frame-debugger-3` PHASE7 HTTP
   automation entry points).
4. `GET /frame_debugger/state` to confirm the predicted tree shape (v2 review finding — PHASE0's Locked
   Design Decision #8, PHASE2: the tree is now SPLIT into `"Compute Dispatches (Pre-GameView)"` — expected to
   contain GPU Skinning/every atmosphere LUT pass/the Aerial Perspective Volume pass — and
   `"Compute Dispatches (Post-GameView)"` — expected to contain the Aerial Perspective Composite pass, the
   Aerial Perspective Volume Debug-Slice pass, and (ONLY if the Editor's own "Show Compute Blur (debug)"
   toggle was explicitly turned on for this smoke test AND "Scene" is visible — it is NOT part of this
   phase's own REQUIRED leaf list below, since it's Scene-View-related and off by default)
   `"ComputeBlurValidation"` — confirm BOTH groups are present, each with its own expected number of
   children).
5. For EACH expected compute-dispatch leaf (Transmittance LUT, Multi-Scattering LUT, Sky-View LUT, Aerial
   Perspective Volume, Aerial Perspective Composite, and GPU Skinning if verified): select it over HTTP,
   screenshot the Inspector's preview box, and visually confirm it shows a REAL, DISTINCT image — not a
   copy of the whole Game View, not a copy of a sibling leaf's own image, not a blank/black rectangle.
   Cross-check at least the Transmittance LUT and Aerial Perspective Composite leaves against known-good
   reference shapes (a transmittance LUT is a smooth, mostly-blue/white gradient; the composite leaf must
   pixel-match `GET /get_game_view`, exactly like `frame-debugger-4` already proved for that one leaf).
6. `stop_app_background` when done.

If anything in this live pass reveals a real behavioral gap PHASE1-4 missed (e.g. a pass that should show a
distinct image but doesn't), fix it as part of this closing phase — do not just document the gap and call
the campaign done. If fixing it would require a genuinely large design decision this document doesn't
already cover, `ask_questions` before proceeding.

### 3.6 Report

Write `task_manager/frame-debugger-5/PHASE5_COMPLETION_REPORT.md` with the full step-by-step live-smoke-test
transcript (mirroring `frame-debugger-4/PHASE3_COMPLETION_REPORT.md`'s own level of detail) plus the full
build/regression numbers. Then write
`task_manager/frame-debugger-5/CAMPAIGN_COMPLETION_REPORT.md` (the campaign-level summary, mirroring
`frame-debugger-4/CAMPAIGN_COMPLETION_REPORT.md`'s own shape: goal recap, phase-by-phase summary, final
architecture diagram, full file-change inventory, final verification evidence, outstanding/deferred items —
explicitly carry forward the "true per-pass stop/breakpoint" idea as a named, still-deferred future item,
per `PHASE0_MASTER_STRATEGY.md`'s own Non-Goals). Commit via `git_add`/`git_commit`.
