# PHASE2 — Fix the `AtmosphereAerialPerspectiveCompositePass` Toggle Lie

Parent: `PHASE0_MASTER_STRATEGY.md` (MUST READ FIRST).
Previous phase report: `PHASE1_COMPLETION_REPORT.md` (MUST READ FIRST — this
phase's entire plan branches on PHASE1's confirmed root cause; do not start
coding before reading it).

---

## Step 1: The Goal (Where are we going?)

Make the specific, reported lie physically impossible: disabling
`AtmosphereAerialPerspectiveCompositePass` (via the panel checkbox or
`GET /render_graph/set_pass_enabled`) must, starting the very next frame,
remove it from BOTH the real `RenderGraphSnapshot` AND the Frame Debugger's
own displayed event tree — with the Game/Scene View still rendering a sane,
non-crashing, non-magenta image (falling back to the pre-aerial-perspective
color target, exactly like `editor-core-separation-20`'s own established
graceful-degradation precedent for this same pass — see
`Core.cpp`'s `composited.value_or(viewData->colorTarget)` fallback
referenced in that campaign's own doc comments).

## Step 2: The Situation (Where are we now?)

PHASE1 has produced a mechanically confirmed root cause in
`PHASE1_COMPLETION_REPORT.md`. This phase file cannot know in advance which
of PHASE0's five hypotheses turned out true, so Step 3 below is written as a
branching decision tree keyed to PHASE1's finding. Read PHASE1's report
FIRST, identify which branch below applies, and follow ONLY that branch —
do not implement more than one fix speculatively.

## Step 3: The Plan

### 3.1 — Branch A: the registry mutation itself never applied (PHASE1 Step 3.2 confirmed this)

Likely concrete causes and fixes:
- **Query-param parsing bug** in `NetworkRoutes.cpp`'s
  `/render_graph/set_pass_enabled` parser (e.g. it silently rejects a valid
  name/enabled pair, or defaults `enabled` to `true` on a malformed request
  it should instead reject/log). Fix the parser, add a Tier-1 test for it if
  one does not already exist covering this exact malformed-input shape.
- **Bridge race/threading bug**: `RenderGraphControlCommandBridge` (or
  equivalent) drops/loses the mutation before the main thread applies it.
  Fix the bridge's queuing/consumption logic; this is Tier-2 (needs the live
  engine), verify via the exact repro script from PHASE1 Step 3.1.
- **Wrong registry instance**: something (e.g. a headless test fixture, or a
  second `Core` accidentally constructed somewhere) applies `SetEnabled()` to
  a registry instance the real render loop never reads. Fix by ensuring
  `EditorHost`'s bridge and `Core::RegisterOffscreenRenderPipelineProviders()`
  share the exact same `Core::m_renderPassToggleRegistry` reference (they are
  supposed to already — if they don't, that IS the bug).

### 3.2 — Branch B: a genuinely stale Frame Debugger capture (PHASE1 Step 3.1 confirmed this)

If PHASE1 proved the registry DOES correctly flip and the declare-time guard
DOES correctly skip the pass, but the Frame Debugger's own displayed state
was simply never refreshed after the toggle (a UX/workflow gap, not a
render-graph correctness bug): this is NOT a silent engine lie, it is a
"the tool needs an explicit re-capture" limitation. Do not just close this as
"not a bug" — that contradicts the user's own "iron rule". Instead:
- Add an automatic re-capture trigger: when `RenderPassToggleRegistry::SetEnabled()`
  is called for ANY pass (via the bridge, main-thread side), and the Frame
  Debugger is currently `Enabled`, automatically request one fresh capture on
  the next frame (mirroring the existing "Enable-edge / Step / Capture button"
  trigger list already documented in `AGENTS.md`'s "Frame Debugger" section —
  this becomes a fourth trigger). This closes the UX gap so a human (or an
  HTTP client) can never again be shown stale, contradicting data after a
  toggle, without needing to remember to click "Capture" again.
- Add a Tier-1 test for whatever pure trigger-decision logic this introduces
  (mirroring the existing "Enable-edge" detection code's own shape — locate
  it in `src/Editor/FrameDebuggerCommandBridge.h/.cpp` or
  `FrameDebuggerHistory.h/.cpp` first, and extend the SAME mechanism rather
  than inventing a second, parallel one).

### 3.3 — Branch C: a second, undiscovered declare site or duplicate pass (PHASE1 Step 3.3 confirmed this)

If PHASE1 found a second place that declares a pass named (or effectively
aliasing) `"AtmosphereAerialPerspectiveCompositePass"` without consulting
`RenderPassToggleRegistry` (e.g. inside `FrameDebuggerReplayPasses.cpp`, or a
newer campaign's own render-feature code):
- If it is `FrameDebuggerReplayPasses.cpp`'s own deliberate replay
  mechanism: confirm with `ask_questions` whether this file's replay passes
  should now ALSO honor the toggle registry (a real, in-scope behavior
  change to a file `AGENTS.md` currently documents as "DELIBERATELY,
  PERMANENTLY left on the OLD... call style" for an unrelated reason —
  migration cost, not toggle-honesty) — do not silently decide this alone,
  since it contradicts a previously "permanent" decision.
- If it is any other production call site: consult the toggle registry there
  too, using the exact same `NoteDeclaredAndCheckEnabled(name)` pattern
  already established, with the SAME literal pass name string (never a
  second, slightly different name — that would just create a third, subtler
  lie).

### 3.4 — Branch D: `FrameDebuggerData.cpp`'s tree-builder fabricates/reuses a stale leaf (PHASE1 Step 3.4 confirmed this)

Fix the caching/staleness bug directly in `FrameDebuggerData.cpp`'s tree
construction so it is rebuilt PURELY from the current capture's own
`RenderGraphSnapshot::passesInExecutionOrder` every single time, with no
leftover state surviving from a previous capture. Add a Tier-1 test
constructing two different synthetic `RenderGraphSnapshot` values in
sequence (one WITH the pass, one WITHOUT) through whatever pure tree-builder
function this logic lives in (extract one first, if it is not already pure
and dependency-free, mirroring `AtmospherePassToggleLogic.h`'s own precedent)
and asserting the second call's output has zero trace of the first's pass.

### 3.5 — Branch E: something genuinely new PHASE1 flagged via `ask_questions`

Follow whatever direction the user (or standing-in orchestrator) actually
decided in response to PHASE1's `ask_questions` call. Document that decision
explicitly at the top of this phase's own completion report before doing
anything else.

### 3.6 — Every branch: mandatory shared verification (do this regardless of which branch applied)

1. Rebuild incrementally.
2. Run the EXACT SAME live HTTP repro script from
   `PHASE1_LIVE_REPRODUCTION_AND_ROOT_CAUSE_DIAGNOSIS.md` Step 3.1, start to
   finish, and confirm:
   - `GET /render_graph/passes` reports `enabled: false` after the mutation.
   - A fresh `GET /frame_debugger/capture` + `GET /frame_debugger/state`
     shows `AtmosphereAerialPerspectiveCompositePass` **completely absent**
     from the event tree (not merely greyed out — physically absent, exactly
     matching `RenderGraphPanel.cpp`'s own documented contract: "a disabled
     pass leaves ZERO trace in `rg::RenderGraphMetadata`").
   - `GET /get_game_view` still shows a sane rendered image (no magenta, sky
     still visible, aerial perspective simply no longer applied).
   - Re-enabling it (`enabled=true`) restores the exact prior behavior with
     no crash and no leaked/stale GPU state (watch `GET /get_logs` for any
     new warning/error around this moment).
3. Remove every remaining `"RenderPassHonestyDiag"`-category temporary log
   line from PHASE1 that did not turn out to carry permanent diagnostic
   value. If PHASE5's planned permanent detector (see
   `PHASE5_IRON_RULE_PERMANENT_MISMATCH_DETECTOR.md`) would directly benefit
   from keeping a trimmed version of any specific log line, leave a clear
   `// TODO(editor-core-separation-21 PHASE5)` comment there instead of
   deleting it outright, but do not leave noisy per-frame debug logging
   enabled by default.
4. Add/update the Tier-1 test(s) called for by whichever branch applied.
5. Incremental compile check must succeed.
6. Write `PHASE2_COMPLETION_REPORT.md`: which branch applied, exact diff
   summary, and the full live verification transcript from step 2 above.
7. `git_add` + `git_commit`.
8. `stop_app_background` the running engine instance before finishing.
9. Do NOT call `delegate_task`. Use `ask_questions` for any new ambiguity.
