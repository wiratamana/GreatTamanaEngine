# PHASE8 — Integration, build wiring, docs, full regression, live automated end-to-end verification

## Parent -> `PHASE0_MASTER_STRATEGY.md` (READ THIS FIRST).
## Depends on: `PHASE1`..`PHASE7` (already landed).

## Step 1: The Goal

Close out the campaign exactly the way `frame-debugger-1`/`frame-debugger-2` both did: confirm
every build-system wiring point is correct, rewrite every piece of documentation this campaign
made stale, run the full validation suite (both Editor build configurations, full `ctest`
regression), and — the part neither prior campaign could ever do — run a genuine, fully-automated,
HTTP-driven, screenshot-verified end-to-end smoke test of the ENTIRE feature with no human
mouse/keyboard involved at all, using PHASE7's own new endpoints.

## Step 2: The Situation

- `frame-debugger-2`'s own PHASE7 is the exact template for this phase's own shape: re-confirm
  build-system wiring, full doc sweep (`docs/conventions/frame-debugger.md`, `AGENTS.md`,
  `docs/README.md`, `README.md`, `TODO.md`), full clean build in BOTH `GTE_ENABLE_EDITOR=ON` and
  `=OFF` configurations, full `ctest` regression, live smoke test, `CAMPAIGN_COMPLETION_REPORT.md`.
- `docs/conventions/frame-debugger.md` currently describes the `frame-debugger-2` GUI-only
  scaffolding as the CURRENT state and lists three "glue seams" for "a future real-capture
  campaign" — this phase must REWRITE that document to describe the REAL system this campaign
  built (real pass-level capture, the ring buffer, real property reflection, real Channels/Levels,
  real HTTP automation), retiring the "glue seams" framing entirely (it is now history, not a
  forward-looking TODO) while keeping the file's own useful "what NOT to do" warnings that are
  STILL true (e.g. "no per-draw-call granularity" is now a PERMANENT, locked design fact about this
  feature, not a temporary scaffolding gap — say so explicitly, referencing PHASE0's own Locked
  Design Decision #1, so a future reader doesn't mistake the coarser-than-Unity granularity for an
  unfinished feature).
- `AGENTS.md`'s existing "Frame Debugger (scaffolding)" section header/content must be updated to
  drop the word "scaffolding" and describe the real system (mirror how "Time and Playback Pause"'s
  own section already reads once that feature was real, as the tone/structure template).
- `TODO.md`'s existing "Frame Debugger (scaffolding)" / "Deferred from the `frame-debugger-2`
  campaign" section needs updating: items this campaign actually closed (real capture, real
  property introspection, a frame-history ring buffer, an HTTP endpoint, interactive
  click-driven verification via automation) move OUT of "deferred," and genuinely still-deferred
  items (per-draw-call granularity, Scene View/Present capture, a full generic shader-reflection
  system) get their own clearly-labeled, still-accurate entries — copy `atmosphere-scattering-4`'s
  own precedent of a completion report explicitly separating "what this closed" from "what
  remains" if that reads cleaner than editing `TODO.md`'s prose in place.

## Step 3: The Plan

### 3.1 Build-system re-confirmation

Re-check every source file PHASE1-7 added is correctly listed in `CMakeLists.txt`'s
`target_sources(gte_core PRIVATE ...)` (Editor-gated section) and every new Tier-1 test file is
listed in `tests/CMakeLists.txt`'s own Editor-gated `GTE_TEST_SOURCES` block, and that the new
`Shaders/FrameDebuggerPreview.comp` is picked up by `cmake/CompileShaders.cmake`'s existing
glob/list (confirm which mechanism that file actually uses before assuming either).

### 3.2 Documentation sweep

- Rewrite `docs/conventions/frame-debugger.md` per Step 2 above.
- Update `AGENTS.md`'s "Frame Debugger" section.
- Add a new top-of-"Status" bullet in `README.md` describing this campaign's real result (mirror
  the exact prose density/style of this repository's own most recent `README.md` "Status" bullets —
  read a couple of the most recent ones first for tone).
- Update `docs/README.md`'s index entry if the convention doc's own title/scope changed materially.
- Update `TODO.md` per Step 2 above.

### 3.3 Full validation

1. **Full clean build, `GTE_ENABLE_EDITOR=ON`**: `cmake --build build --clean-first`, zero errors.
2. **Full clean build, `GTE_ENABLE_EDITOR=OFF`**: fresh configure with `-DGTE_ENABLE_EDITOR=OFF`
   into `build-editor-off/`, `cmake --build build-editor-off --clean-first`, zero errors — confirm
   every new file from this whole campaign (PHASE1-7's Renderer-layer changes included — e.g.
   `Pipeline`'s new debug-name parameter must compile fine in BOTH configurations, since `Pipeline`
   itself is not Editor-gated even though the CAPTURE CONTEXT that reads it is) compiles correctly
   in both regimes, and that nothing Editor-only leaks into the non-Editor build.
3. **Full `ctest` regression**: `ctest -C Debug --output-on-failure` from `build/` — 100% pass
   rate expected (matching every prior campaign's own bar), covering every new
   `FrameDebuggerCapture`/`FrameDebuggerHistory`/`FrameDebuggerPreviewProcessing`/snapshot-builder
   test added across PHASE1-6.

### 3.4 Live, HTTP-automation-driven end-to-end smoke test (the closing achievement of this
whole campaign)

Using `run_app_background`/`gte_send_request` (or this repository's own equivalent tooling),
drive the ENTIRE feature with no mouse/keyboard, confirming visually via `GET /get_swapchain` at
each numbered step:

1. `GET /frame_debugger/open` -> `GET /get_swapchain` shows the "Frame Debugger" window, pinned
   inside the main viewport, titlebar visible.
2. `GET /frame_debugger/enable?value=true` -> `GET /get_swapchain` shows the event tree now
   containing real rows (not "No frame captured yet.") — the Enable edge's own auto-capture (PHASE3)
   fired.
3. `GET /frame_debugger/state` -> confirm the JSON reports `enabled: true`, a real
   `totalEventCount >= 1`, `historyCount >= 1`.
4. `GET /frame_debugger/select_event?index=0` (or whichever real index the tree actually produced)
   -> `GET /get_swapchain` shows the event-details section now populated with real Shader/Pass/
   Blend/Z-state/Stencil/Textures/Vectors/Matrices content, not "No event selected."
5. `GET /frame_debugger/capture` -> confirm a new history entry was added (`GET
   /frame_debugger/state`'s `historyCount` increments, or wraps at `kCapacity` — whichever is
   correct given how many captures happened so far).
6. `GET /frame_debugger/step_history?direction=prev` -> confirm (via `state` and/or a screenshot)
   the viewed frame's content genuinely changed to an earlier captured frame.
7. `GET /frame_debugger/set_channel?value=r` -> `GET /get_swapchain` shows the preview image
   visibly changed to an isolated-red-channel rendering.
8. `GET /frame_debugger/set_levels?black=0.2&white=0.8` -> `GET /get_swapchain` shows the preview
   image's contrast visibly changed.
9. Clean up: disable/close as appropriate, stop the background process.

Every step's actual observed result (not just "it worked") goes into
`PHASE8_COMPLETION_REPORT.md`/`CAMPAIGN_COMPLETION_REPORT.md` — this is the concrete evidence that
finally closes the manual-verification gap both `frame-debugger-1` and `frame-debugger-2` had to
accept as a documented limitation.

### 3.5 Completion reporting

Write `PHASE8_COMPLETION_REPORT.md`, then a full `CAMPAIGN_COMPLETION_REPORT.md` (mirroring
`frame-debugger-2/CAMPAIGN_COMPLETION_REPORT.md`'s own structure: goal recap, phase-by-phase
summary, final architecture diagram, file-change inventory across the whole campaign, final
verification evidence, and — for the first time in this feature's history — a "manual-verification
limitation" section that can finally say this gap is CLOSED rather than merely documented as
accepted).

### 3.6 File-change inventory

Modified only: `CMakeLists.txt`, `tests/CMakeLists.txt` (if any gap found in 3.1),
`docs/conventions/frame-debugger.md`, `AGENTS.md`, `README.md`, `TODO.md`, `docs/README.md`, plus
the two new report files. No production source code should need to change in this phase unless
3.1/3.3's re-confirmation pass finds a genuine, small integration bug — if so, fix it minimally and
say so explicitly in the completion report, the same way `frame-debugger-2`'s own PHASE6 openly
documented its one unrelated environment issue (a near-full disk) rather than glossing over it.
