# Job System — Phase 7: Editor "Jobs" Panel

Parent document: `JOBSYSTEM_PHASE0_MASTER_STRATEGY.md` (read first).
Previous phase: `JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING.md`
(must be done — there is now real, genuine per-worker job data being
produced every frame a rigged model animates).
Next phase: `JOBSYSTEM_PHASE8_TESTING_HARDENING_BENCHMARKING.md`.

**Definition of Done for this phase:** the Editor has a new, dockable
**"Jobs"** panel, following the exact same construction pattern as
`ProfilerPanel`/`RenderGraphPanel` (a small, explicitly-named, stateful
class — never a generic `IEditorPanel` — called explicitly from
`ImGuiEditorLayer::BuildUI()`), rendering a live, per-worker horizontal
timeline (Idle vs. named, colored job spans) for the last-captured frame,
reading exclusively from Phase 5's `WorkerTimelineData.h` reshape with zero
new engine-level tracking added, gated behind the existing `GTE_ENABLE_EDITOR`
switch (and implicitly `GTE_ENABLE_JOB_SYSTEM`/`GTE_ENABLE_PROFILER`, see
3.5), and visually recognizable as the same kind of view the brief's own
attached Unity Profiler Timeline screenshots show.

---

## Step 1: The Goal (Where are we going?)

Produce the actual, on-screen deliverable this whole campaign has been
building toward — a panel that looks, in spirit, exactly like the attached
screenshots:

```
Timeline                                    CPU: 2.14ms   GPU: --ms
Worker 0  | -- Idle (0.31ms) --> [SkinVertices:Furina.Part0] --> Idle -->
Worker 1  | -- Idle (0.28ms) --> [SkinVertices:Furina.Part1] --> Idle -->
Worker 2  | ------------------------- Idle --------------------------->
Worker 3  | ------------------------- Idle --------------------------->
```

— a horizontal bar per worker, spanning the captured frame's own duration,
with named/colored segments for each recorded `WorkerJobSample` and
everything else rendered as visually distinct "Idle" gaps, plus a hover
tooltip on any segment showing its exact job name and duration (mirroring
the brief's own attached screenshot's own hover tooltip: "job name /
duration / instances" style).

---

## Step 2: The Situation / The Problem (Where are we now?)

After Phase 6, real `WorkerJobSample` data exists in `FrameProfiler`'s
history whenever a rigged model is actively animating — but there is
**still no way for a human being to actually see it**. This matters more
than it might first appear:

1. **The whole master strategy's own Step 1, point 4 explicitly identifies
   this as a launch-blocking requirement, not a nice-to-have**: "a job
   system nobody can see working is a job system nobody can trust or
   tune" — without this panel, the only way to know whether Phase 6's
   migration is actually balancing work across workers (as opposed to,
   say, a subtle bug that routes everything back onto Worker 0 due to a
   queue-contention pattern nobody noticed) is to add ad hoc printf-style
   debugging, which is exactly the kind of throwaway, non-reusable
   diagnostic effort this engine's own Editor already exists to replace
   (see every other `Panels/*Panel.cpp` in `src/Editor/` — "Memory",
   "Profiler", "Render Graph" all exist for precisely this reason: turning
   an internal, otherwise-invisible engine data structure into a live,
   trustworthy, always-available visualization).
2. **This engine already has THREE precedents for exactly this kind of
   panel** (`ProfilerPanel`, `RenderGraphPanel`, and — for its own
   Pause-snapshot-holding stateful-class pattern — `BoneViewerWindow`), so
   this phase is deliberately NOT inventing a new Editor pattern; it is
   applying an already-proven one to new data.

---

## Step 3: The Plan (How will we get there?)

### 3.1 — File layout

```
src/Editor/Panels/
    JobsPanel.h/.cpp   - new, mirrors ProfilerPanel.h/.cpp's own shape exactly
```

Added to `CMakeLists.txt`'s `GTE_ENABLE_EDITOR` `target_sources()` block,
alongside `ProfilerPanel.h/.cpp`/`RenderGraphPanel.h/.cpp` — same file,
same gating, no new CMake option needed for the PANEL itself (see 3.5 for
how it degrades gracefully if the underlying `GTE_ENABLE_JOB_SYSTEM`/
`GTE_ENABLE_PROFILER` switches are off).

### 3.2 — `JobsPanel` as a small stateful class (matching `ProfilerPanel`'s
precedent, not a stateless free function)

Per `AGENTS.md`'s "Editor Module Structure" section, most panels are
plain free functions (`BuildXPanel(...)`) — but a panel that needs to hold
onto state ACROSS frames gets to be a small class instead, and this is
explicitly one of them, for the exact same reason `ProfilerPanel` already
is: a **Pause** control. Pausing this panel must freeze the displayed
timeline on the exact frame it was paused at, while `FrameProfiler`
continues collecting normally underneath (workers keep running, keep
recording — see `AGENTS.md`'s own description of `ProfilerPanel`'s
identical Pause semantics) — so `JobsPanel` needs a member field holding
its own frozen snapshot, exactly mirroring `ProfilerPanel`'s own design:

```cpp
// src/Editor/Panels/JobsPanel.h
namespace gte {

class JobsPanel {
public:
    void Build(EditorContext& context);

private:
    bool m_paused = false;
    std::vector<Profiling::WorkerTimelinePoint> m_frozenSnapshot; // only meaningful while m_paused
    // Reusable scratch buffers for ImGui draw calls (per-worker row rects,
    // color lookup by job name) - avoids reallocating every single frame,
    // same "reusable scratch buffers" precedent ProfilerPanel already
    // established for its own ImGui::PlotLines() calls.
};

} // namespace gte
```

Called explicitly, by name, from `ImGuiEditorLayer::BuildUI()`
(`m_jobsPanel.Build(context);`), exactly like `m_profilerPanel.Build(...)`/
`m_boneViewer.Build(...)` already are — no `IEditorPanel` interface
introduced.

### 3.3 — Docking

Docked alongside "Memory"/"Profiler"/"Render Graph" along the bottom (see
`DockLayout.cpp`'s existing bottom-dock group) — a natural fit, since all
four are "live engine internals, glanced at occasionally, not a primary
always-visible view" panels, matching the existing rationale for why
those three are grouped together today.

### 3.4 — The actual rendering

`JobsPanel::Build()`'s core logic:

1. If not paused, call `Profiling::BuildWorkerTimelinePoints()` (Phase 5)
   against `FrameProfiler::Instance().LastCompletedFrame()` — same
   "read the last COMPLETED frame, never the in-progress one" convention
   `ProfilerPanel` already follows.
2. If paused, use `m_frozenSnapshot` instead (captured once, the FRAME the
   Pause checkbox was ticked — mirroring `ProfilerPanel`'s own freeze
   timing exactly).
3. Determine the frame's own total duration (from
   `FrameSample::cpuFrameMilliseconds` — the SAME field the CPU frame-time
   graph in "Profiler" already uses) as the timeline's horizontal scale.
4. For each distinct `workerIndex` present (via
   `ComputeDistinctWorkerCount()`, Phase 5) up through
   `Jobs::JobSystem::Instance().WorkerCount()` (so a worker that recorded
   ZERO job spans this frame still gets its own "entirely Idle" row —
   showing an empty/idle worker is just as important a signal as showing a
   busy one, exactly matching the attached screenshot's own "Idle
   (0.23ms)" rows for workers that did nothing that frame), draw one
   horizontal row:
   - A background "Idle" bar spanning the full frame duration.
   - For every `WorkerTimelinePoint` matching that `workerIndex`, an
     `ImGui::GetWindowDrawList()`-drawn colored rectangle positioned at
     `(startMilliseconds / frameDuration)` through
     `((startMilliseconds + durationMilliseconds) / frameDuration)` of the
     row's own width — the same "draw list rectangles positioned by
     normalized fraction of a known total" technique
     `RenderGraphPanel`/`MemoryPanel` already use elsewhere for their own
     bars/sparklines, applied here to a horizontal timeline instead.
   - A stable color per DISTINCT `jobName` (a small hash-based color
     picker, or a fixed rotating palette indexed by a name→color lookup
     built once per frame) — so, e.g., every `"SkinVertices"` segment
     across every worker/row shares the same color, making it visually
     obvious at a glance which named job is dominating the timeline, the
     same visual language the attached screenshots use (each distinct job
     kind gets its own consistent color).
   - `ImGui::IsMouseHoveringRect()` + `ImGui::SetTooltip()` on each colored
     segment showing its exact job name + duration in milliseconds —
     mirroring the attached screenshot's own hover tooltip exactly.
5. A **Capture**/**Pause** control pair — but note: unlike `ProfilerPanel`,
   this panel does NOT need its own separate "Capture" toggle, since it
   reads the SAME `FrameProfiler::SetCaptureEnabled()` flag "Profiler"'s
   own Capture checkbox already controls (both panels are reading from the
   same underlying `FrameProfiler` — there is no reason for two
   independent capture toggles controlling the identical underlying data
   source). Only **Pause** is genuinely local to this panel's own display,
   exactly like `ProfilerPanel`'s own Pause.
6. A **worker-utilization summary line** above the timeline (e.g. "6 / 8
   workers had at least one job this frame — 2 idle the whole frame") —
   a small, high-value, at-a-glance answer to exactly the "is this
   actually balanced" question Step 2 identifies as the whole point of
   this panel, computed trivially from the same `WorkerTimelinePoint` list
   already being iterated for rendering.

### 3.5 — Graceful degradation when the Job System / Profiler are compiled
out

- `GTE_ENABLE_JOB_SYSTEM=OFF` (Phase 1): `Jobs::JobSystem::Instance().WorkerCount()`
  still resolves (to a well-defined, small value — see Phase 1's own
  "OFF" behavior), so the panel still builds/renders, just shows every
  row as entirely idle forever (there are no real workers, nothing is ever
  dispatched) — never a compile error, never a crash, matching this
  engine's blanket "a disabled module degrades to an honest, harmless
  no-op display, never a build failure" convention (`NullEditorLayer`,
  `ProfilerPanel`'s own honest "instrumentation compiled out" wording for
  `GTE_ENABLE_PROFILER=OFF` — see `AGENTS.md`'s regression test callout
  for exactly this distinction, `CpuScopeTableEmptyMessageMatchesCompileTimeFlag`).
- `GTE_ENABLE_PROFILER=OFF`: `FrameProfiler`'s underlying `workerJobs` data
  is never populated (per Phase 5's own gating) — the panel must show an
  HONEST distinct message ("Job timing instrumentation is compiled out of
  this build" — mirroring `ProfilerPanel`'s own CPU-scope-table precedent
  exactly, including its own dedicated regression test proving the message
  text and the compile-time flag never silently disagree) rather than a
  blank/empty-looking timeline that could be mistaken for "nothing is
  running" when actually "we can't tell you".

### 3.6 — Testing

Pure data-shaping logic this panel needs beyond Phase 5's own
`BuildWorkerTimelinePoints()`/`ComputeDistinctWorkerCount()` (e.g. the
per-jobName stable-color-assignment function, and the "N / M workers had
at least one job" summary computation) must be pulled OUT into a plain,
ImGui-free `src/Editor/JobsPanelData.h/.cpp` (mirroring
`ProfilerPanelData.h`/`MemoryPanelData.h`'s own precedent exactly) and
Tier-1-tested in `tests/Editor/JobsPanelDataTests.cpp` — the actual
`JobsPanel.cpp` ImGui-drawing code itself stays untested, same accepted
"Editor panel widget code is Tier 2, its data-shaping helpers are Tier 1"
split this whole `src/Editor/` module already applies everywhere else.

---

## Step 4: What We Will NOT Do (Focus)

- **We will NOT build a scrolling, multi-frame timeline history view (the
  Unity screenshot's own "CPU Usage" graph above its Timeline).** This
  phase's panel shows exactly ONE frame's timeline at a time (the last
  completed one, or a paused snapshot) — a multi-frame scrolling history
  view for job timelines specifically is a natural, separate future
  enhancement (the "Profiler" panel's own CPU frame-time graph already
  covers the "history over time" need at the aggregate level), not
  required for this campaign's own definition of done.
- **We will NOT add zoom/pan/scrub interaction to the timeline in this
  phase.** A fixed, whole-frame view is enough to prove the core
  visualization value; interactive zooming is a nice-to-have follow-up,
  not a blocker.
- **We will NOT add a second, independent Capture toggle.** As established
  in 3.4, point 5 — this panel shares `FrameProfiler`'s existing capture
  flag with "Profiler", never introduces a second, confusing, independently-
  stateful toggle over the same underlying data source.
- **We will NOT add any NEW engine-level tracking in this phase.** Every
  piece of data this panel shows already exists, in full, from Phase 5 —
  this is purely a presentation-layer addition, exactly like
  `PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md`'s own precedent explicitly
  states about ITS Profiler panel ("No new engine-level tracking was
  needed").
- **We will NOT try to visualize job DEPENDENCIES (Phase 3's continuation
  graph) in this panel.** This is a TIMELINE (when did work run, on which
  worker), not a DEPENDENCY GRAPH (what waited on what) — those are
  different visualizations answering different questions; a future
  "Jobs Graph" view is a distinct, separately-scoped idea, not folded into
  this phase.

---

## Step 5: Their Role (What does this mean for you?)

- Build `JobsPanelData.h/.cpp`'s pure helpers FIRST, test them, THEN build
  `JobsPanel.cpp`'s ImGui wrapper around them — same "pure logic first,
  thin ImGui shell second" sequencing this whole `src/Editor/` module
  already established for every other panel with nontrivial data-shaping
  needs.
- Verify the graceful-degradation paths (3.5) by ACTUALLY building and
  running with `GTE_ENABLE_JOB_SYSTEM=OFF` and separately with
  `GTE_ENABLE_PROFILER=OFF`, looking at the real panel each time — don't
  just reason about it on paper; this engine's own existing regression
  test (`CpuScopeTableEmptyMessageMatchesCompileTimeFlag`) exists precisely
  because a documented-but-unverified "should degrade gracefully" claim
  has already, historically, been worth guarding with a real test in this
  codebase.
- Once the panel is built, wired into `DockLayout.cpp`, and its own data
  helpers are Tier-1-tested, run the REAL Furina-model animation scenario
  from Phase 6 with the Editor open and LOOK at the panel — confirm, with
  your own eyes, that multiple worker rows show real, colored,
  non-overlapping `SkinVertices` segments, and that the "N / M workers had
  at least one job" summary reflects genuine parallelism, not everything
  quietly landing on Worker 0. This is the moment the whole campaign's
  promise becomes directly, visually verifiable — treat it as such,
  not as a formality.
- Move on to `JOBSYSTEM_PHASE8_TESTING_HARDENING_BENCHMARKING.md` once this
  is confirmed.
