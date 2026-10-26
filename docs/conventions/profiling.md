# Profiling

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

`src/Profiling/` is the CPU event log and frame history. It is always compiled (no editor dependency) and has no SDL
dependency. The frame loop is `EditorHost::Run()`: it calls `BeginFrame()` / `EndFrame()` and publishes the memory
snapshot just before `EndFrame()`. Follow these rules when you touch profiling or add a call site.

## CPU scopes

- `GTE_PROFILE_SCOPE("Name")` (`ScopeTimer.h`) is the only way to add a CPU call site. The name must be a string
  literal. It is interned once per call site into a 64 KB name table (`NameTable.h`). `GTE_PROFILE_SCOPE_ID(id)` takes a
  name interned earlier with `InternName()`. `GTE_PROFILE_BLOCKED(name)` marks time spent waiting. `GTE_PROFILE_MARKER(name)`
  records an instant event.
- A finished scope is a 24 byte `ScopeEvent` (start, end, name id, depth, kind, view). Events are appended in post order
  to a fixed array of 2048 per frame. Overflow is counted, never silent: the frame is `Partial`.
- Total and self time come from `ComputeScopeTimes()` (`ScopeTree.h`), one pass over the events. Depth is explicit, so a
  scope that calls itself is not double counted by the tree.
- `FrameGate.h` decides if a finished scope belongs to the open frame. A scope that ends after its frame closed is
  counted as late. A scope finished off the main thread is counted as foreign. Both counters are in `FrameSummary`.
- Worker threads use `GTE_PROFILE_JOB_SCOPE` (`JobScopeTimer.h`) only inside job bodies. It pushes a `WorkerEvent` into
  the lock-free `WorkerRing` (1024 slots). The main thread drains the ring in `EndFrame()` into 512 events per frame.
  Lost events are counted in `FrameSummary::lostWorkerEvents`.
- Every other `FrameProfiler` method is main thread only. The first `BeginFrame()` marks the calling thread as main.
- Nothing on the hot path allocates. All buffers are fixed arrays.

## Frame tree and waits

- `Core::BuildFrame` and `Core::Present` bracket their stages with scopes: `Render.Prepare`, `Offscreen` (children
  `Offscreen.Begin`, `Offscreen.Declare` with `Offscreen.Collect` inside, `Offscreen.Compile`, `Offscreen.Record`,
  `Offscreen.Finish`, `Offscreen.FinalizeViews`, `Offscreen.Submit`, `Offscreen.Finalize`) and `Present` with
  `Present.Declare`, `Present.Compile`, `Present.Record`, `Present.Finish`.
- `IRenderTimingObserver` (`Renderer/RenderTimingObserver.h`) reports waits, graph phases, provider declarations and
  pass recording. `Core/RenderTimingAdapter` turns them into scopes: `GpuWait(Site:detail)` (Blocked), `Declare.<provider>`,
  `Pass.<name>` and `<label>.<phase>`. Only the main thread records; other threads are ignored.
- `Wait.Workers` (Blocked) brackets every wait on worker jobs.
- `FrameTotals` (`FrameChecks.h`) sums top-level, blocked (GPU, workers, other) and untracked time of one frame.
  `FrameSummary` keeps `topLevelTicks`, `blockedTicks` and `blockedOtherTicks`.
- `EvaluateChecks()` returns one `FrameCheck` per `CheckId` with `Ok`, `Warn` or `Fail`: capture off, dropped scopes,
  late scopes, off-thread scopes, lost worker events, reconciliation (untracked above 5% warns, overrun above 2% fails)
  and scope nesting. The Profiler panel lists every non-Ok check and shows untracked time as its own tree row.

## Frame history

- `FrameProfiler` keeps a ring of 300 `FrameSummary` rows (index, ticks, CPU ms, memory, drop counters) and the full
  `FrameRecord` (scope and worker events) of the newest finished frame.
- `FrameGraphData.h/.cpp` reshapes the summary history into plot points and ranges. Ranges branch on status, never on
  the stored number.
- `WorkerTimelineData.h/.cpp` turns the worker events of one frame into timeline rows. A job that started before the
  frame starts at 0.

## Clock

- `ProfilingClock.h` is the one clock: `std::chrono::steady_clock` in nanoseconds. SDL is not used:
  `SdlLinkageRegressionTests` scans `gte_core` for SDL symbols.

## On/off switches

- `GTE_ENABLE_PROFILER=OFF` (CMake) removes the scope macros: no clock read, no event.
- `ProfilerControl` (`FrameProfiler::Control()`) holds two atomic runtime switches. `capture` stops recording (frames,
  scopes and worker events). `displayPaused` only freezes what the panels show; recording continues.
- `FrameProfiler` itself always compiles, also with the profiler OFF. Do not wrap it in `#if`.
- `FrameProfiler::Instance()` is a process-wide singleton. A test that uses it calls `ResetForTesting()` first.

## GPU and memory samples

- A value that was not measured carries a `SampleStatus` (`Present`, `Stale`, `Absent`, `Unsupported`, `Partial`,
  `Error`, `Inactive`). It is never a bare `0`. `FormatGpuTimingLine()` prints a number only for `Present`.
- GPU time is measured by timestamp queries in `src/Renderer` (`GpuStampPlan.h`, `RenderGraphTimestampPool`):
  - One reset of the frame's query region, then a start stamp before each pass's barriers, a stamp after them, and an
    end and a final stamp. Passes past the record budget fold into one overflow record.
  - The region is read back in one `vkGetQueryPoolResults` call with availability. A missing stamp reads `Absent` or
    `Partial`, a backwards or huge delta reads `Error`, and a device without timestamps reads `Unsupported`.
  - The offscreen regime reads after its own wait. The present regime reads the slot the presenter just waited on.
    A frame that was never submitted, or left the slot ring, is counted in `dropped_gpu_frames`.
  - A graph made with `TimingMode::Disabled` (one-shot and cluster viewer graphs) writes no stamps; its records read
    `Inactive`. Capture off reads `Absent`.
- Per-view GPU numbers live in `FrameSummary::gpu` (`GpuFrameMetrics`), indexed by `ViewSlot`:
  - `Core::PublishGpuMetrics()` sums barrier and work time of the newest synchronous snapshot's records per view
    (`BuildGpuFrameMetrics()` in `Core/GpuViewMetrics.h`, the one mapper from `GpuReadState` and `rg::ViewScope`).
  - A view is `Present` only when every record of it was measured and none was folded; a mix is `Partial`. A view
    without a render target is `Inactive`. Capture off reads `Absent`.
  - `indirectDraws` is its own count per view and is never shown as triangles. `declaredPasses`, `executedPasses`,
    `recordedPasses` and `disabledByToggle` feed the coverage text "executed E of declared D, recorded R, disabled by
    toggle K". A pass toggled off is never declared, so it is absent from the tables.
  - The panels print every GPU number through `FormatGpuMetric()`. The Frame Debugger reads the selected pass's own
    record, never a pass found by name.
- `Application/MemorySnapshotBuilder.h` turns `Renderer::GetMemoryTotals()` into a `Profiling::MemorySnapshot`.
  It is always `Present`.
- `Renderer/DrawStats.h` counts draw calls and triangles inside the `FrameRecorder` record loop, on the same path that
  issues the draw. Triangle counts assume triangle lists and one instance per draw.

## Metrics and HTTP

- A `Profiling::Metric` is `{value, unit, kind, status, ageFrames, reason}`. `FormatMetric()` prints it: a value for
  `Present`, a value plus the reason for `Partial` and `Stale`, only the status word otherwise. A missing value is never
  printed as a number.
- `BuildProfilerSnapshot()` copies the newest finished frame into a plain `ProfilerSnapshot` (CPU numbers, scope rows,
  per-view GPU metrics, coverage counts, checks, drops, device state, 120 frame rows). The main thread builds it only
  while a reader asked in the last 2 s, and publishes it through `ProfilerSnapshotBridge`.
- Waits in progress are not part of the published copy. `LiveBlockTable` keeps one record per thread (8 threads, one
  seqlock slot each, microseconds); readers fetch it at request time. Waits report from any thread through the timing
  observer.
- `GET /profiler`, `/profiler/frames` and `/profiler/spikes` read only the snapshot and the live table, so they answer while
  the main thread is blocked. `snapshot_age` tells how old the copy is. `POST /profiler/set` switches capture and the display
  pause. See [Networking](networking.md).

## Verdict, spikes, markers and overhead

- `ComputeVerdict()` (`Verdict.h`) is one pure function with one constants block (`VerdictConstants`). It takes the median
  of the newest 30 frames (CPU work, GPU waits, present waits, other waits, untracked) and applies the rules in order:
  untracked above 10% is `Unreliable`, other waits above 20% is `Stalled`, present above 30% or GPU from 40% is
  `PresentLimited` or `GpuLimited` (larger share wins), CPU from 60% is `CpuLimited`, else `Serial`. A failing self-check on
  most frames of the window makes it `Unreliable`. `FormatVerdict()` always prints the numbers it used.
- A frame is a spike when it takes more than twice the median of the previous 60 frames, or more than
  `ProfilerControl::spikeThresholdMs` when that is set. `FrameProfiler::EndFrame()` freezes it into `SpikeStore`: 16 slots of
  full frames, the oldest unpinned slot is evicted, at most 8 are pinned (the ninth pin is refused with `LimitReached`). Slots
  are preallocated. The store locks only to copy a frame, so network readers can pin and read while the main thread runs.
- `ClassifySpike()` finds the scope or wait with the largest self time. The spike is `Explained` when a marker sits on the
  frame or that scope covers at least half of the excess, otherwise `Unexplained`.
- `FrameProfiler::AttachLateGpuReport()` puts a GPU report that arrives up to 2 frames late into the frozen copy and the
  summary history.
- `GTE_PROFILE_MARKER` writes an instant event where periodic or request-driven work happens: the GPU memory summary rebuild,
  mesh and material release sweeps, cluster table synthesis, swapchain recreation (reported by the renderer through
  `IRenderTimingObserver::OnMarker`), host captures, project hot reload and code unload. Every GPU wait is a Blocked scope
  already. A marker is written only where the work really runs, never every frame.
- `MeasureScopeEventCostNs()` times a private scope log once at startup. `EstimateOverhead()` multiplies it by the events of the
  frame and warns above 3% of the frame. The result is an `Estimated` metric, never a reading.

## Reading the data

- The Editor Profiler and Jobs panels only format this data. See [Editor Module Structure](editor-module-structure.md).
