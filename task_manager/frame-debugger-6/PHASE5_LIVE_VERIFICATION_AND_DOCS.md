# PHASE5 — Incremental Build, Light Live Verification, Docs & Completion Report

_Read `PHASE0_MASTER_STRATEGY.md` in full first. This is the FINAL phase —
PHASE1 through PHASE4 must all already be complete, committed, and each
individually incrementally-built-and-manually-checked per their own
Definition of Done. Per the user's own explicit instruction, this phase is
LIGHT: an incremental build plus a couple of targeted, HTTP-driven screenshots
— NOT a full `ctest` regression run. Only do a full build/full test run here
if this phase's own checklist below explicitly says so._

## Step 1: The Goal (Where are we going?)

Produce final, live, screenshot-backed proof that this whole campaign's two
workstreams both actually work together, end to end, against the real engine
— and leave the campaign's documentation/history in a clean, accurate,
reviewable state for the next engineer who touches this feature.

## Step 2: The Situation (Where are we now?)

- PHASE1+PHASE2 fixed the duplicate/mis-scoped Scene-View compute-pass leak.
- PHASE3+PHASE4 added real, selectable per-entity draw-attribution leaves
  under `"GameView"`.
- Each phase already did its own incremental build + a narrow manual
  HTTP/screenshot check as part of ITS OWN Definition of Done. This phase's
  job is a final, slightly broader pass confirming the FULL, combined
  end-state — both fixes together, in one capture — plus documentation
  cleanup and the campaign completion report, mirroring
  `frame-debugger-4`/`frame-debugger-5`'s own closing phases (see
  `task_manager/frame-debugger-4/PHASE3_COMPLETION_REPORT.md`/
  `CAMPAIGN_COMPLETION_REPORT.md` and
  `task_manager/frame-debugger-5/PHASE5_COMPLETION_REPORT.md`/
  `CAMPAIGN_COMPLETION_REPORT.md` for the level of detail/evidence expected —
  but note this campaign's own final phase is explicitly LIGHTER on the
  testing side per the user's own request; match their WRITE-UP quality/style,
  not their full-`ctest`-run scope).

## Step 3: The Plan

### 3.1 Incremental build only

```
cmake --build build
```

(Working directory: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`.) Fix
any compile error before proceeding — do not defer it. Do **NOT** run `ctest`
in this phase unless the incremental build itself reveals a regression that
genuinely requires the full suite to safely diagnose (an exceptional case,
not the default path) — if you find yourself needing to do this, note clearly
in the completion report WHY the lighter path was insufficient.

### 3.2 Light, targeted live verification (HTTP + screenshots)

Use `run_app_background` to launch `build/GreatTamanaEngine.exe`, then
`gte_send_request`/`run_shell` (`curl`) for the rest, and
`stop_app_background` at the end. Suggested minimal sequence (adjust as
needed, but keep it to "a couple of targeted screenshots", not a full
re-run of every HTTP route this feature has):

1. `POST /load_scene` with body `{}` (loads `Project/TestScene.gtscene`, the
   default path).
2. `GET /frame_debugger/open`, then `GET /frame_debugger/enable?value=true`
   (this also fires the first auto-capture).
3. `GET /get_swapchain` — screenshot #1. Visually confirm:
   - `"Compute Dispatches (Pre-GameView)"` has exactly 5 children, no
     duplicate names (PHASE1+PHASE2 proof).
   - `"Compute Dispatches (Post-GameView)"` has exactly 1 child
     (`AtmosphereAerialPerspectiveCompositePass`), not 4 (PHASE1+PHASE2 proof).
   - `"GameView"` is now expandable (has a visible tree-arrow) and, once
     expanded (it should default-open), shows a `"terrain (Entity N)"` leaf
     and a `"SmokeTestCube (Entity M)"` leaf (PHASE3+PHASE4 proof).
4. Determine the terrain leaf's real `eventIndex` (read it off the tree in the
   screenshot, or via `GET /frame_debugger/state`'s `totalEventCount` plus
   counting, or by temporarily selecting a few candidate indices and checking
   the returned `passName`/name in the detail pane) and call
   `GET /frame_debugger/select_event?index=<that index>`.
5. `GET /get_swapchain` — screenshot #2. Visually confirm the detail pane
   shows the `terrain` entity's own shader name and a triangle count close to
   ~1,045,458 (its own real per-draw count, not the pass-level aggregate
   ~1,045,470). ALSO confirm the preview texture box itself is unchanged from
   screenshot #1 (still the real, final, atmosphere-composited image) —
   PHASE4's own document flags a real risk it guards against (a per-entity
   leaf's `details.passName` must not literally collide with the string
   `"GameView"`, or the preview would wrongly snap to the raw
   pre-atmosphere-composite image whenever a per-entity leaf is selected) —
   this is the cheap, final, end-to-end confirmation that guard actually
   holds against the real running engine.
6. `stop_app_background` the process.

If either check fails, diagnose it directly (this is still "writing code to
solve the problem", per this campaign's own operating rules) — fix the
regression in the relevant phase's own files, re-run this phase's build +
checks, and only then proceed. Do not silently ship a failing check.

### 3.3 Documentation final pass

- Re-read `docs/conventions/frame-debugger.md` and `AGENTS.md`'s "Frame
  Debugger" section TOP TO BOTTOM one more time now that all four
  implementation phases are done — confirm there is no leftover stale prose
  anywhere (e.g. an old sentence still claiming compute-pass duplication is
  possible, or still claiming `"GameView"` can never have children) and that
  every new behavior this campaign shipped is described exactly once, in the
  right file (short summary in `AGENTS.md`, full detail in
  `docs/conventions/frame-debugger.md`), matching every prior campaign's own
  established split.
- If `TODO.md` has a "Frame Debugger" section referencing the now-fixed
  duplicate-pass limitation or the now-added per-entity attribution as a
  future item, update/remove that entry so it does not contradict the shipped
  state.

### 3.4 `CAMPAIGN_COMPLETION_REPORT.md`

Write `task_manager/frame-debugger-6/CAMPAIGN_COMPLETION_REPORT.md`,
mirroring `frame-debugger-5/CAMPAIGN_COMPLETION_REPORT.md`'s own shape:

- A short recap of the original user report and the two confirmed root
  causes/workstreams (Section 0 of `PHASE0_MASTER_STRATEGY.md` is the source
  of truth for this — summarize, don't just copy-paste verbatim).
- Per-phase summary (1-2 sentences each) of what actually shipped, with any
  deviations from the original phase documents called out explicitly and
  explained (this is normal and expected — a strategy document is a plan, not
  a contract; if reality forced a different approach, say so plainly here).
  If PHASE1's own dedicated extra double-check (see this campaign's own
  delegation history) found anything worth noting, summarize it here too.
- The final live-verification evidence from Step 3.2 above (what was checked,
  what was seen).
- Any explicitly-deferred/out-of-scope items (e.g. isolated per-mesh preview
  images — PHASE4's own "What We Will NOT Do").

## Step 4: Definition of Done for PHASE5 (and the whole campaign)

- [ ] Incremental build succeeds with zero errors/warnings introduced by this
      campaign.
- [ ] Both targeted live checks in Step 3.2 pass, with screenshots described
      in the completion report (paths/descriptions are enough — this
      environment does not require embedding actual image files into the
      report).
- [ ] `docs/conventions/frame-debugger.md`/`AGENTS.md`/`TODO.md` (if
      applicable) are accurate and internally consistent with the shipped
      behavior.
- [ ] `CAMPAIGN_COMPLETION_REPORT.md` written.
- [ ] Final `git_add`/`git_commit` for this phase's own changes (docs +
      completion report + anything Step 3.2 required fixing).
- [ ] Confirm every phase's own individual completion note/report (if written
      separately per phase) is present and committed.
