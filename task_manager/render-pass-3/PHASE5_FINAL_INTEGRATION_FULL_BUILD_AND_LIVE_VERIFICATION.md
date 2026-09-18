# PHASE5: Final Integration — Full Build, Full Regression, Live Verification

_Child of `PHASE0_MASTER_STRATEGY.md` — read that file first. Also read
every prior phase's own `PHASEn_COMPLETION_REPORT.md` in this same folder
before starting — each may carry a clue, a deviation, or an open question
for this final phase to pick up. Part of the `render-pass-3` campaign._

**This is the ONLY phase in this campaign permitted to run a full clean-
equivalent build, a full `ctest` regression pass, and full live/HTTP
verification** — every earlier phase deliberately only did an incremental
compile check plus a narrow, targeted live check of just the thing it
changed. Mirrors `render-pass-1`'s own `PHASE7_FINAL_INTEGRATION_...` and
`render-pass-2`'s own `PHASE4_FINAL_INTEGRATION_...` precedent exactly.

## Step 1: The Goal

Confirm the WHOLE campaign — new vocabulary (PHASE1), the blackboard proof
(PHASE2), the full production-pass migration + Game/Scene view unification
(PHASE3), and the Frame Debugger pivot fix (PHASE4) — works together, end
to end, with zero regressions anywhere in the existing test suite, and
with the real, running engine visibly behaving identically (or, where
this campaign deliberately changed something visible — Scene View now
genuinely splitting into separate Opaque/Sky/Transparent passes internally
— behaving CORRECTLY) to before this campaign started. Write the
campaign's own final `CAMPAIGN_COMPLETION_REPORT.md`, update `AGENTS.md`
and `README.md`'s "Status" section, and loudly document every deviation
from `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`'s own original defaults
(Locked Design Decisions 1, 2, 4, 5, 6 from `PHASE0_MASTER_STRATEGY.md`).

## Step 2: The Situation

By the start of this phase:

- `src/Renderer/RenderGraph/RenderPipeline.h`/`.cpp` exist, fully unit
  tested (PHASE1).
- `Application` owns two `RenderPipeline` instances —
  `m_offscreenRenderPipeline` (GPU Skinning, Atmosphere ×6, Opaque, Sky,
  Transparent — Game AND Scene views) and `m_presentRenderPipeline`
  (`"Present"` alone) — and `Application.cpp`'s own `build` lambdas are
  correspondingly shorter (PHASE2, PHASE3).
- `AddSceneViewPass()` is deleted (PHASE3), and Scene View's own draw is
  now structurally split into separate `"RenderOpaque"`/
  `"DrawSkyBackground"`/`"RenderTransparent"` passes, tagged
  `ViewScope::SceneView`, exactly mirroring Game View's own already-shipped
  shape.
- `FrameDebuggerData.cpp`'s view-region pivot search is now structural
  (`FindViewRegionPivot()`, PHASE4) instead of a literal `"RenderOpaque"`
  name match — everything else about the Frame Debugger is unchanged.
- `AddFrameDebuggerReplayPasses()` and `ComputeBlurValidation.cpp` are
  BYTE-FOR-BYTE unchanged from before this campaign started (Locked
  Design Decision 4) — still declared via the OLD, direct
  `AddRenderPass()` call style, still read by the Frame Debugger via the
  OLD `ViewScope`/`RenderPassCategory` fields, unchanged.
- `RenderGraphBuilder::AddRenderPass()`'s two overloads carry ONE new
  trailing, defaulted parameter (`RenderPassEvent renderPassEvent =
  RenderPassEvent::Opaques`, PHASE1) beyond what existed before this
  campaign — every other pre-existing parameter/overload is unchanged.

## Step 3: The Plan

### 3.1 — Full build

`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`). Zero errors, zero new
warnings (pay particular attention to any exhaustive-switch warning on
`RenderPassEvent`/`ResourceAccess`/`PassKind`/`RenderPassCategory`/
`RenderPassDrawKind` — this campaign should not have introduced a new,
unhandled enumerator anywhere an existing exhaustive switch already
covers all of them).

### 3.2 — Full regression test

`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C
Debug --output-on-failure`. Compare the resulting pass count against
`render-pass-2`'s own final baseline (1589 tests, 100% passing, one
expected environment-gated skip — see
`task_manager/render-pass-2/CAMPAIGN_COMPLETION_REPORT.md`'s own "Final
state" section) — this campaign should show a HIGHER total count (every
new PHASE1 `RenderPipelineTests.cpp` test, plus PHASE4's one new
regression test, plus whatever else each phase's own completion report
already added, all net positive) with the SAME "1 expected skip, 0 real
failures" shape. Any newly-failing pre-existing test is a REAL regression
to diagnose and fix — never loosen a test's own expectation to make it
pass without first understanding exactly why it started failing (per
`AGENTS.md`'s own Testability/Regression-Safety rule).

### 3.3 — Live launch + verification

1. Launch the built `GreatTamanaEngine.exe` in the background
   (`run_app_background`).
2. `GET /frame_debugger/open` → `200`. `GET /frame_debugger/enable?value=true`
   → `200`. `GET /frame_debugger/capture` → confirm `hasCapturedFrame:
   true` and a `totalEventCount` consistent with the SAME tree shape
   `render-pass-2`'s own final verification already documented (this
   campaign must not have changed the Game-View Frame Debugger tree shape
   at all — PHASE4's own fix is internal-mechanism-only).
3. `GET /get_swapchain` — visually confirm the Frame Debugger tree still
   shows `"Compute LUT"` (5 sub-passes), `"RenderOpaque"` (with its
   per-entity children, if any test primitives are spawned — mirror
   `render-pass-1`/`render-pass-2`'s own `POST /instantiate_primitive`
   methodology), `"DrawSkyBackground"` (still expandable with its `"Draw
   Quad"` child, from `render-pass-2`), and the Post-GameView composite
   group — all IDENTICAL to `render-pass-2`'s own final screenshot.
4. Activate the "Scene" tab (`GET /activate_tab?name=Scene`) and confirm,
   via `GET /get_swapchain`, that the Scene View panel now shows correctly
   rendered geometry + sky + the Editor's own ground-grid overlay, with
   correct ordering (grid composites over the sky where they intersect) —
   this is the one genuinely NEW, visible thing this campaign's Scene View
   split (PHASE3) needs a real, live confirmation of, since no automated
   test captures a real Vulkan-rendered image.
5. If a GPU-skinned/animated model asset is available in this environment,
   spawn/load it and confirm it still animates and renders correctly in
   BOTH Game View and Scene View (proving PHASE2's blackboard hand-off,
   and PHASE3's extension of it to Scene View/Present, both work for real,
   non-empty GPU Skinning output) — if no such asset is available in this
   environment, note that explicitly as a limitation of this specific
   verification pass, exactly as `render-pass-1`'s own final phase already
   had to do for comparable environment-gated limitations.
6. Stop the engine (`stop_app_background`).

### 3.4 — Documentation

- `AGENTS.md`'s existing "Render Pass System" section gets a new
  paragraph (append, do not rewrite the existing `render-pass-1`/
  `render-pass-2` history) describing: the new `RenderPipeline`/
  `RenderPassDesc`/`RenderPassProvider`/`RenderPassBlackboard` declaration
  layer, that it sits ABOVE the unchanged `AddRenderPass()` chokepoint
  (never replacing it), that `ViewScope`/`RenderPassCategory`/
  `RenderPassDrawKind` remain permanently in place (loudly note this is a
  deliberate deviation from a generic design brief, by explicit user
  decision, not an oversight), that Game View and Scene View now share one
  generic per-view provider loop instead of hand-duplicated code, and that
  Editor-only/debug passes (Frame Debugger replay steps, Compute Blur
  Validation) deliberately remain on the old mechanism forever.
- `README.md`'s "## Status" section gets one new bullet, in the same style
  and level of detail as the existing `render-pass-1`/`render-pass-2`
  entries immediately above it (see those two for the exact tone/format
  expected — confirmed facts, concrete file names, a one-line "why this
  existed" hook, and the final verification numbers).
- Write `task_manager/render-pass-3/CAMPAIGN_COMPLETION_REPORT.md`,
  mirroring `render-pass-1`'s and `render-pass-2`'s own campaign
  completion reports EXACTLY in structure ("Why this campaign existed",
  a one-line-per-phase table, final shipped tree/architecture shape,
  verification performed, "what shipped", "explicit breaking changes",
  "what was explicitly NOT done", "recommendation for whoever picks up the
  next session", "final state"). This report MUST include a dedicated,
  clearly-labeled section enumerating every deviation from
  `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`'s own original text (the five
  Locked Design Decisions from `PHASE0_MASTER_STRATEGY.md` that reversed or
  amended the design doc's own stated defaults), so a future reader of the
  ORIGINAL design doc is not misled into thinking it was implemented
  exactly as originally written.

## Definition of Done

- `cmake --build build` succeeds with zero errors and no new warnings.
- `ctest -C Debug --output-on-failure` shows 100% passing (same single,
  pre-existing, environment-gated skip as every prior campaign; zero new
  failures), with a total test count strictly higher than
  `render-pass-2`'s own 1589 baseline.
- Every live/HTTP verification step in Step 3.3 above is performed and its
  result recorded (screenshots described in text, exact HTTP
  status/response values quoted) in the campaign completion report.
- `AGENTS.md`, `README.md`, and
  `task_manager/render-pass-3/CAMPAIGN_COMPLETION_REPORT.md` are all
  updated/written, with the deviations from the original design doc called
  out explicitly and unambiguously.
- `git status` on `feature/render-pass-impl` is clean after this phase's
  own final commit (aside from any genuinely pre-existing, unrelated
  untracked files the way `render-pass-2`'s own final phase already
  documented as acceptable).

## What We Will NOT Do

- Do NOT merge `feature/render-pass-impl` into any other branch — outside
  this campaign's own authority entirely, exactly like every prior
  campaign on this same branch.
- Do NOT use this phase to sneak in any NEW scope beyond verifying and
  documenting what PHASE1-4 already built — if this phase's own build/test
  pass surfaces a real regression, fix ONLY that regression (and say so
  loudly in the completion report), never expand scope opportunistically.
- Do NOT revisit or re-litigate any Locked Design Decision from
  `PHASE0_MASTER_STRATEGY.md` — this phase documents and verifies them, it
  does not reconsider them.
