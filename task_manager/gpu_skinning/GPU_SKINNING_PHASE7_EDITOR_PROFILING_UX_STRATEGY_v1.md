# GPU Vertex Skinning — Phase 7: Editor Toggle & Profiling UX

Part of the GPU Vertex Skinning campaign — see
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v1.md` for the campaign map. Depends
on Phase 5 (the real runtime switch) and Phase 6 (a validated, trustworthy
GPU path) both being complete. This is the last phase — it closes the loop
back to the original request: *being able to tell the difference in
performance between the two tech stacks*.

## Step 1: The Goal (Where are we going?)

A human, sitting in the Editor, can:

1. Flip a single, obvious control between "CPU (Job System)" and "GPU
   (Compute)" skinning, live, while animated models are on screen.
2. Immediately see the performance consequence of that choice, using
   surfaces that **already exist** in this engine — the CPU worker
   timeline (Job System Phase 5's `GTE_PROFILE_JOB_SCOPE("SkinVertices")`
   entries, visible in the Editor's "Jobs" panel) and the GPU pass timing
   (`RenderGraph`'s per-pass real GPU timestamp queries, visible in the
   Editor's "Render Graph" panel and/or "Profiler" panel) — with **no new
   profiling subsystem invented for this campaign**.
3. Trust that what they're looking at is real (not a fabricated `0.00 ms`,
   not a stale number from before the switch), following this engine's own
   long-standing "never fabricate a status/value, always report an honest
   Absent/Unsupported/N/A" discipline (`AGENTS.md`'s "Profiling" section).

## Step 2: The Situation / The Problem (Where are we now?)

This engine already has essentially every profiling surface this phase
needs — the job here is almost entirely *wiring and UX*, not new
instrumentation:

- **CPU mode is already fully instrumented.** `RunSkinningBatch()`
  (`AnimationSystem.cpp`) already wraps its call to `SkinVertexRange()` in
  `GTE_PROFILE_JOB_SCOPE("SkinVertices")` — every batch, on every worker,
  every frame. `Profiling::WorkerTimelineData.h`'s `BuildWorkerTimelinePoints()`
  already reshapes this into a per-worker timeline, and the Editor's
  existing "Jobs" panel (`Panels/JobsPanel.cpp`, `JobsPanelData.h/.cpp`)
  already displays it. **Nothing here needs to change for CPU mode at
  all** — it already works, today, for the currently-shipped CPU path.
- **GPU mode's pass timing is automatic, for free, the moment the pass has
  a name.** `RenderGraph::LastKnownStatsFor(passName)` and the whole B.1
  real-GPU-timestamp-query infrastructure (`RenderGraphTimestampPool`,
  wired into `RenderGraph::ExecuteCompiledGraph()`) already time **every**
  named pass automatically — Phase 3's `AddComputePass("SkinModel:<name>", ...)`
  call already gets real, driver-measured GPU milliseconds with zero
  additional instrumentation code, the exact same way every other named
  pass (Game View, Scene View, Present) already does. The Editor's
  existing "Render Graph" panel (`Panels/RenderGraphPanel.cpp`,
  `BuildRenderGraphSnapshot()`) already shows every pass's GPU time in a
  table.
- **What genuinely does not exist yet**: (a) the actual UI control to flip
  `AnimationSystem::SetSkinningMode()`, and (b) a small amount of care
  around *where* that control lives and *what it's next to*, so the
  comparison is actually easy for a human to make (today, "Jobs" and
  "Render Graph" are two separate, independently-dockable panels — a user
  has to have both open simultaneously to compare, which is a real, if
  small, UX gap worth explicitly closing).

## Step 3: The Plan (How will we get there?)

### 3.1 — The toggle itself

A single, small control — a two-option radio/dropdown, "Skinning: CPU
(Job System) | GPU (Compute)" — added to whichever existing panel is the
most natural home. Two reasonable candidates, evaluated explicitly rather
than picked arbitrarily:

- **The "Jobs" panel** (`Panels/JobsPanel.cpp`) — already exists, already
  shows worker-timeline data directly relevant to CPU mode. Argument for:
  a user already looking at "why is my CPU busy" naturally finds the
  switch that controls it right there.
- **The "Render Graph" panel** (`Panels/RenderGraphPanel.cpp`) — already
  exists, already shows the new GPU skinning pass (once Phase 3 lands)
  with its real timing. Argument for: same reasoning, mirrored for GPU
  mode.

**Decision: put it in the "Jobs" panel**, with a short, one-line note next
to it: *"When set to GPU, see the 'Render Graph' panel's 'SkinModel:...'
pass(es) for GPU timing instead."* Rationale: the "Jobs" panel is where a
user already goes to reason about the Job System's own worker activity —
putting the switch there makes the *disappearance* of `SkinVertices`
worker-timeline entries when GPU mode is selected self-explanatory in
context (a user watching the Jobs panel who flips the switch and sees the
skinning entries vanish, with the pointer note right there telling them
where to look next, is a much better experience than the entries just
silently vanishing with no explanation). Revisit this placement decision
if implementation reveals a better option — this is a UX judgment call,
not a load-bearing architectural one, and the exact right answer is best
confirmed by actually looking at both panels once Phase 3's real pass
exists.

### 3.2 — Wiring

`Panels/JobsPanel.cpp` gains a reference to `AnimationSystem&` (via
whatever existing plumbing already gets `Game&`/its systems to Editor
panels — check `EditorContext`/`ImGuiEditorLayer::BuildUI()`'s existing
call signature for `JobsPanel`'s build function, extend it the same way
other panels already receive `Registry&`/`Game&` where needed) and calls
`animationSystem.SetSkinningMode(...)` directly on a UI interaction — no
new indirection layer, mirroring how `ProfilerPanel`'s own
Capture/Pause controls call straight into `FrameProfiler`/`Renderer`
methods with no intermediate abstraction.

### 3.3 — Making the comparison trustworthy, not just visible

Per this campaign's Must-Have #6/#7 and this engine's own "never fabricate
a status" discipline:

- When CPU mode is active, the "Render Graph" panel's `SkinModel:...`
  pass(es) simply **do not exist in the graph at all** for that frame
  (Phase 3's skinning pass is only declared when
  `CollectModelsNeedingGpuSkinningThisFrame()` returns something — which
  is empty in CPU mode) — so there is nothing misleading to show; the
  pass is just absent from the table, exactly like any other genuinely-not-run
  pass already is (see `RenderGraphSnapshot`'s existing "culled vs. never
  declared" distinction — confirm a never-declared pass and a culled
  pass are visually distinguishable in the existing panel, and that
  neither is confusable with "ran but cost nothing").
- When GPU mode is active, the "Jobs" panel's worker timeline simply has
  no `SkinVertices` entries for that frame — same reasoning, mirrored.
- Neither panel needs a NEW "N/A" state invented for this feature — the
  absence of a row **is** the honest signal, in both directions, given how
  both panels already work today. This is worth explicitly confirming
  (not assuming) once Phase 3/5 are both live: watch both panels while
  flipping the switch and confirm the entries genuinely appear/disappear
  as expected, with no leftover stale row from a previous frame's mode.

### 3.4 — A note on comparing like-for-like

Document prominently, in the Editor (a tooltip on the toggle) and in this
phase's own completion report: a fair CPU-vs-GPU comparison needs the
**same model, same frame, same vertex count** in both modes — flipping the
switch mid-playback and comparing the CPU worker-timeline number you saw
two seconds ago against the GPU pass-timing number you see now is still a
meaningful, useful comparison for this campaign's purpose (that's exactly
what a user asked for), but comparing across two *different* models/vertex
counts is not. No engineering is needed to enforce this — it is a
documentation/tooltip-level responsibility, not a code-level one, since
enforcing "the exact same conditions" automatically would require freezing
animation playback/model selection, which is out of scope and unnecessary
for the comparison to be useful in practice.

### 3.5 — Closing the loop: `AGENTS.md`/`README.md`/`TODO.md`

Once Phase 7 lands (and only once every phase does), update the shared
project docs the same way every other completed campaign already has its
own section:

- `README.md`'s "Status" section gains a new entry describing GPU vertex
  skinning as a shipped, switchable feature — modeled on the existing "A
  spawned MMD model can now actually be ANIMATED" and Job System Phase 6
  entries' own level of detail (what was built, what was verified, what
  numbers were observed).
- `AGENTS.md` gains a new "GPU Vertex Skinning" section, written in the
  same convention-documenting style as its existing "Job System"/
  "Profiling"/"Render Target Format Matching" sections — the load-bearing
  rules a future engineer must not violate (the CPU path stays the
  oracle and is never modified to "agree" with the GPU path; the phantom
  `VertexBufferRead` declaration in the graphics pass must never be
  "cleaned up"; `ComputeDescriptorSet::Rewrite()` is deliberately called
  only once per model, not every frame, for this specific feature; the
  doubled-GPU-memory trade-off from Phase 5 is deliberate, not a bug to
  "fix").
- `TODO.md` gains an entry for whatever this campaign explicitly deferred
  (per each phase's own "What We Will NOT Do" — GPU-side pose evaluation,
  per-model mode override, halving the doubled memory footprint, any
  kernel-performance optimization identified but not pursued during
  Phase 6/7's own measurement).

## Step 4: What We Will NOT Do (Focus)

- **No new charting/graphing UI.** Every visualization this phase needs
  (worker timeline, per-pass GPU timing table) already exists — this
  phase adds a toggle and, at most, a one-line cross-reference tooltip,
  never a new graph/plot.
- **No automated regression test asserting "GPU mode is faster than CPU
  mode" (or vice versa).** Performance is workload/hardware-dependent by
  nature (this campaign's own explicit reason for existing — to let *you*
  observe the difference on *your* hardware) — asserting a specific
  performance relationship in an automated test would be flaky, dishonest
  in intent, and contrary to why this feature was built as an
  observable comparison rather than a hidden auto-optimization.
- **No CSV/benchmark-export feature specific to this campaign.** The
  Profiler's own already-planned (elsewhere, stubbed) "Export CSV" feature
  is a separate, general capability — this campaign does not build its
  own bespoke exporter, and should not duplicate that effort.
- **No removal of the CPU path**, ever, as part of this campaign or
  implied by it — CPU mode remains the default and remains fully
  supported indefinitely; this campaign is additive only.

## Step 5: Their Role (What does this mean for you?)

- Land the toggle in the "Jobs" panel (or wherever 3.1's judgment call
  ultimately lands, if implementation reveals a better home), wired
  straight to `AnimationSystem::SetSkinningMode()`.
- Manually verify, with a real animated model on screen: flipping the
  toggle produces exactly the expected appear/disappear behavior in both
  the "Jobs" and "Render Graph" panels, with no stale/leftover rows, no
  visual glitch in the rendered model at the transition frame, and no
  crash under repeated rapid toggling (a deliberately adversarial "flip it
  as fast as you can click for ten seconds" manual stress check is cheap
  insurance here, given Phase 5's own mid-session-switch correctness
  claims).
- Perform, and **write down**, at least one real side-by-side comparison:
  the same real model, same animation, same frame range, CPU worker-timeline
  total vs. GPU pass-timing milliseconds, on whatever hardware you have
  available (explicitly including, if possible, the "weak hardware" target
  mentioned as this whole campaign's original motivation — the CPU path's
  own `README.md` history already notes it "work[s] quite well on weak
  hardware," making it the natural baseline to compare a GPU path against
  on that same class of machine). This number is the actual deliverable
  the entire eight-phase campaign exists to produce — do not consider
  Phase 7, or the campaign, done without it.
- Finish by updating `AGENTS.md`/`README.md`/`TODO.md` per 3.5, and write
  a final, campaign-level completion report (mirroring
  `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`'s own "tie together every
  phase's own completion report" shape) summarizing what was built, what
  was verified, what was deferred, and — most importantly — what the
  actual measured CPU-vs-GPU performance difference was.
