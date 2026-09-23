# PHASE6 — Editor Tooling + Live Validation

## Parent
`PHASE0_MASTER_STRATEGY.md` — read it first. Also re-read `PHASE1`–`PHASE5`'s
own completion reports, PHASE5's dedicated double-check findings in
particular — resolve anything it flagged before starting here.

## Step 1: The Goal (Where are we going?)

Make the new mechanism OBSERVABLE (matching this engine's established
"Memory"/"Profiler"/"Render Graph" panel philosophy) and prove it works
end-to-end against a REAL scene with a meaningful, GPU-driven-eligible
instance count:

1. A small "instances culled this frame" readout, per eligible batch.
2. A real, live scene (or a real, repeatable spawn mechanism) that actually
   produces at least one batch at or above `kMinInstancesForGpuDrivenBatch`
   — the only way the Definition of Done in `PHASE0` can be honestly
   confirmed rather than assumed.
3. A live, HTTP-driven, screenshot-verified comparison proving the new path
   is visually indistinguishable from the old one, and that culling actually
   reduces draw output as the camera moves.

## Step 2: The Situation (Where are we now?)

- `RenderGraphSnapshot`/`BuildRenderGraphSnapshot()` (Phase 8 of the original
  render-graph campaign) already generalizes over an arbitrary declared
  pass/resource set with NO changes needed — confirmed by the compute-shader
  campaign's own Phase 7 finding, restated here for this campaign: the
  Editor's "Render Graph" panel will already show PHASE5's new compute/
  graphics passes and their real buffer reads/writes correctly, the moment
  they exist, with zero code changes.
- `GpuTimingService`'s real, driver-measured per-pass GPU milliseconds
  already cover every `RenderGraph` pass unconditionally (`B1_REAL_GPU_TIMING_
  COMPLETION_REPORT.md`) — PHASE5's new passes already get real GPU timing
  for free.
- `DrawStats`'s new `indirectDrawCount` field (PHASE2) needs a real consumer
  somewhere visible — likely the same place `DrawStats`'s existing fields are
  already surfaced (the "Profiler" panel — inspect
  `src/Editor/Panels/ProfilerPanel.cpp`/`FrameGraphData.h` directly before
  deciding exactly where this new field's own display line belongs).
- **This campaign's own instance count needs a REAL, meaningful, repeatable
  source.** `PrimitiveMeshGenerator`/`Game::CreatePrimitiveEntity()` shapes
  are explicitly OUT OF SCOPE for GPU-driven batching (Locked Design
  Decision 6, PHASE0 — they are non-indexed). This phase therefore needs
  either (a) an existing imported (`.gta`), untextured, `PositionNormal`
  mesh asset the demo scene already spawns more than one copy of, or (b) a
  new, small, explicit way to spawn several entities that all point at the
  exact SAME already-loaded `MeshHandle`+`PipelineHandle` (only their
  `Transform` differs) purely for validation purposes. **`Game.cpp`'s actual
  current demo-scene content was NOT exhaustively inventoried while writing
  this document** — inspect it directly at the start of this phase, and use
  `ask_questions` to confirm the right approach (reuse existing content vs.
  add a small, clearly-labeled validation spawn helper, and whether that
  helper should be permanent/always-on or gated behind something) if the
  demo scene doesn't already obviously provide a qualifying batch.

## Step 3: The Plan

### 3.1 — "Instances culled this frame" readout

- A small, non-blocking readback of each eligible batch's own count-buffer
  final value — mirror `GpuTimingService::ReadPresentResultIfAvailable()`'s
  own "read back a PAST frame's result at the point synchronization already
  proves it's safe" pattern exactly (never a new blocking GPU wait
  introduced by this readout — see `AGENTS.md`'s "Profiling" section: never
  add a new GPU wait purely to fetch a number sooner).
- Surface it as a new line/section in whichever Editor panel already shows
  per-pass draw stats (inspect `ProfilerPanel.cpp`/`RenderGraphPanel`-
  equivalent directly to decide the cleanest home; `ask_questions` if
  unclear), e.g. "`<batch name>`: N / M instances visible" — reusing
  `DrawStats::indirectDrawCount` (PHASE2) for the "an indirect draw happened"
  half and this new readback for the "how many instances" half, kept clearly
  labeled as two DIFFERENT kinds of number (one CPU-known-exact, one a
  deliberately-delayed GPU readback) per PHASE2's own documented rule.

### 3.2 — Real, meaningful-instance-count validation content

Resolve per Step 2's own `ask_questions` checkpoint. Whichever approach is
chosen, the end state must be: a live, running engine has at least one
`(MeshHandle, PipelineHandle)` batch with a real instance count at or above
`kMinInstancesForGpuDrivenBatch`, spawnable/reachable without any manual,
one-off, undocumented setup step (so PHASE7's own final smoke test, and any
future regression check, can rely on it existing).

### 3.3 — Live validation checklist (run once, by hand, this phase)

- The Editor's "Render Graph" panel shows the new compute pass and the new
  indirect graphics pass, in the correct order, with the correct declared
  reads/writes, and NEITHER culled from the graph.
- Validation layers report zero new warnings/errors across a normal play
  session with the batch visible.
- The new "instances culled this frame" readout (3.1) visibly DROPS as the
  camera is rotated/moved so part of the batch leaves the frustum, and
  recovers when it's back in view — the actual, visible proof culling is
  happening, not just compiling.
- A screenshot comparison (`GET /get_game_view` before vs. after this
  campaign's own cutover, same camera/scene state) shows PIXEL-IDENTICAL
  output for the batch — confirming this is a pure performance/mechanism
  change, never a rendering-behavior change (mirrors the original render-
  graph campaign's own Phase 7 "zero observable behavior difference"
  requirement for its own cutover).
- Every entity/group NOT part of a batch (primitives, textured submeshes,
  GPU-skinned models) is confirmed still rendering exactly as before, through
  the unmodified per-entity path.

### What We Will NOT Do (this phase)

- No new `Profiling::GpuPass` fixed-enum entry (`GameView`/`SceneView`/
  `Present` stays exactly 3 values) — this campaign's new passes are surfaced
  through the "Render Graph" panel's own already-generic mechanism, not
  "Profiler"'s fixed-pass display, matching the compute-shader campaign's own
  identical, already-established precedent.
- No automated visual-diff/screenshot-comparison TEST (remains Tier 2/manual,
  per this project's own accepted testing bucket, `AGENTS.md`) — this phase's
  checklist (3.3) is a manual/HTTP-driven verification, not a new automated
  test suite.
- No occlusion-culling visualization, no debug frustum-wireframe overlay, no
  bounding-box gizmo rendering — purely a numeric readout, matching the
  minimal-tooling precedent every prior campaign in this repo used for its
  own first cut.

### Compile check

Fast, targeted incremental compile check only. Confirm 3.3's live checklist
passes in full. Write `PHASE6_COMPLETION_REPORT.md` (record the exact
validation-content mechanism decided in 3.2/Step 2, including its
`ask_questions` outcome if one was needed, and a plain description — or an
attached screenshot reference — of the before/after comparison from 3.3).
Commit.
