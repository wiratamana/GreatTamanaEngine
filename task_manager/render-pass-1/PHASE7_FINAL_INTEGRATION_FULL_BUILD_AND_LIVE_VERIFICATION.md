# PHASE7: Final Integration — Full Build, Full Regression Test, Live Frame Debugger Verification

_Child of `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1` through
`PHASE6` already being merged. Part of the `render-pass-1` campaign. This
is the ONLY phase in this campaign allowed to run a full build/full
regression test, per `PHASE0_MASTER_STRATEGY.md`'s own cross-cutting
rules._

## Step 1: The Goal

Prove, end-to-end, on the real, running engine, that this whole campaign
achieved what the user actually asked for: every render/compute/blit
operation goes through a real Render Pass declared via the new
`AddRenderPass()` chokepoint, the test scene's Frame Debugger tree is
clean and matches the target shape from `PHASE0_MASTER_STRATEGY.md`'s own
Step 1 diagram, and nothing else in the engine regressed. Write the
campaign's own final `CAMPAIGN_COMPLETION_REPORT.md`.

## Step 2: The Situation

- Every earlier phase in this campaign only ran an INCREMENTAL compile
  check — this is the first point in the whole campaign a full,
  from-scratch build has been attempted, so this is also the first
  opportunity to discover any cross-phase integration issue (a stale
  include, a forward-declaration that should have become a full include,
  a test file registered in `tests/CMakeLists.txt` but never actually
  exercised incrementally).
- The regression test command is `cd /d
  C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C
  Debug --output-on-failure`. The build command is `cmake --build build`
  (working directory `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`).
- Live verification needs a running engine instance
  (`run_app_background`) and the `gte_send_request` tool to drive its
  embedded HTTP server (`GET /frame_debugger/open`, `/enable`,
  `/capture`, `/select_event?index=N`, and `GET /get_swapchain`/
  `/get_game_view` for screenshots) — mirrors every prior
  `frame-debugger-N` campaign's own closing "live, HTTP-driven,
  screenshot-verified proof" methodology (see
  `docs/conventions/frame-debugger.md`'s own repeated references to this
  exact verification style).
- The test scene referenced by the original task (screenshot: a mountain
  landscape under a sky, with `SmokeTestCube`, `Entity 2`, and the
  Atmosphere sky background all present) should already exist in this
  engine's current default/save state, or can be assembled at runtime via
  the `/instantiate_primitive`/scene-load HTTP routes if not — check
  `TESTING.md`/`README.md` first for how this project's own existing
  manual test scene is normally reached before inventing a new one.

## Step 3: The Plan

### 3.1 — Full build

Run `cmake --build build` (or the equivalent Ninja/MinGW invocation this
repo's own `BUILDING.md` documents — read it first) from the repo root.
Fix any compile/link error surfaced here that no earlier phase's
incremental check caught — this is expected to be rare (each phase
already compiled incrementally) but is exactly what this phase exists to
catch if it happens. If a fix is needed, keep it minimal and scoped
exactly to the failure; do not use this as an opportunity to make
unrelated design changes.

### 3.2 — Full regression test

Run `ctest -C Debug --output-on-failure` from the `build` directory.
Every test must pass. If a genuine regression is found (a previously-
passing test now fails because of this campaign's changes, not because
the test itself needed updating for a deliberate, documented behavior
change already called out in an earlier phase's own "Definition of Done"),
diagnose it and fix the root cause — do not loosen a test's assertion
just to make it pass without understanding why it failed (this is an
explicit `AGENTS.md` rule, "Testability & Regression Safety").

### 3.3 — Live launch + Frame Debugger verification

1. `run_app_background` the built engine executable.
2. `gte_send_request` `GET /frame_debugger/open`.
3. `gte_send_request` `GET /frame_debugger/enable?value=true`.
4. `gte_send_request` `GET /frame_debugger/capture`.
5. `gte_send_request` `GET /get_swapchain` — visually confirm (via
   `load_image` on the returned screenshot, or directly via the image
   content `gte_send_request` already returns) the event tree shows, in
   this order: a `"Compute LUT"` group (if the scene has a Sun/atmosphere
   active — it should, per the reference screenshot), `"RenderOpaque"`
   with its own per-entity children (`SmokeTestCube`, etc.),
   `"DrawSkyBackground"` as its own separate, selectable row, and no
   visible `"RenderTransparent"` row (since it's still a no-op — see
   PHASE2's own Definition of Done).
6. `gte_send_request` `GET /frame_debugger/select_event?index=N` for a
   couple of different N values (pick the `"RenderOpaque"` row and the
   `"DrawSkyBackground"` row specifically) and take a follow-up
   `/get_swapchain` screenshot each time, confirming the Inspector pane's
   Event Details section shows sensible, real data for each (correct
   pass name, correct Blend/Z/Stencil rows — `"DrawSkyBackground"` should
   show `Depth Test = Equal`/`Depth Write = Off`, matching
   `DescribeSkyBackgroundPipelineState()`'s own values).
7. `stop_app_background` the engine once done.

### 3.4 — Campaign completion report

Write `task_manager/render-pass-1/CAMPAIGN_COMPLETION_REPORT.md`,
mirroring the shape of `task_manager/render_graphs/RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`
(read that file first for the expected structure/tone): a short recap of
the original problem, what shipped in each phase, what was explicitly
NOT done (transparency itself, async compute, breakpoint-level
stepping — all still out of scope), and the live verification evidence
from 3.3 (screenshot descriptions + what they proved). This report is the
definitive record of this whole campaign for any future engineer/agent.

## Definition of Done

- `cmake --build build` succeeds with zero errors.
- `ctest -C Debug --output-on-failure` reports 100% passing tests.
- The live HTTP-driven verification in 3.3 confirms the Frame Debugger
  tree matches this campaign's target shape (PHASE0's own Step 1
  diagram) on the real, running engine — not just in unit tests.
- `CAMPAIGN_COMPLETION_REPORT.md` exists, is accurate, and is committed.
- Final `git_status` on the `feature/render-pass-impl` branch shows a
  clean tree (everything committed).

## What We Will NOT Do

- Do NOT attempt to fix anything outside this campaign's own stated
  scope, even if noticed during verification (e.g. an unrelated,
  pre-existing bug in a completely different subsystem) — note it in the
  completion report as a follow-up item instead, exactly like every
  prior campaign's own "Section C" gap-analysis convention
  (`RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md` is the
  precedent to follow for how to record such a finding without acting on
  it here).
- Do NOT merge `feature/render-pass-impl` into any other branch — that is
  outside this campaign's own scope/authority.
