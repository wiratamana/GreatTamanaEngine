# Profiling

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

`src/Profiling/` is the CPU event log, the frame history and the plain data the panels and HTTP routes read. It has no
SDL, Vulkan or ImGui dependency. The frame loop is `EditorHost::Run()`: it calls `BeginFrame()` and `EndFrame()`, and
publishes the memory snapshot just before `EndFrame()`.

Every number the profiler shows is measured and carries a status. A value that was not measured is never `0`.

## Status and metrics

- `SampleStatus`: `Present`, `Stale`, `Absent`, `Unsupported`, `Partial`, `Error`, `Inactive`.
- `Metric` is `{value, unit, kind, status, ageFrames, reason}` (`Metric.h`). `kind` is `Measured`, `Derived` or `Estimated`.
  `FormatMetric()` prints a value for `Present`, a value plus the reason for `Partial` and `Stale`, and only the status word
  otherwise. GPU numbers print through `FormatGpuMetric()`.

## CPU scopes

- `GTE_PROFILE_SCOPE("Name")` is the way to add a CPU call site. The name is a string literal, interned once per call site
  into a 64 KB name table. `GTE_PROFILE_SCOPE_ID(id)` takes a name interned with `InternName()`. `GTE_PROFILE_BLOCKED(name)`
  marks waiting time. `GTE_PROFILE_MARKER(name)` records an instant event.
- A finished scope is a 24 byte `ScopeEvent`. Events are appended in post order to a fixed array of 2048 per frame.
  Overflow is counted, never silent.
- `ComputeScopeTimes()` gives total and self time in one pass over the events.
- `FrameGate` decides whether a finished scope belongs to the open frame. A scope that ends after its frame closed is
  counted as late. A scope finished off the main thread is counted as foreign.
- Worker threads use `GTE_PROFILE_JOB_SCOPE` inside job bodies. It pushes a `WorkerEvent` into a lock-free ring of 1024
  slots. The main thread drains it in `EndFrame()` into 512 events per frame. Lost events are counted.
- Every other `FrameProfiler` method is main thread only. Nothing on the hot path allocates.
- The clock is `std::chrono::steady_clock` in nanoseconds (`ProfilingClock.h`).

## Frame tree and waits

- `Core::BuildFrame` and `Core::Present` bracket their stages: `Render.Prepare`, `Offscreen` (`Begin`, `Declare` with
  `Collect` inside, `Compile`, `Record`, `Finish`, `FinalizeViews`, `Submit`, `Finalize`) and `Present`.
- `IRenderTimingObserver` reports waits, graph phases, provider declarations, pass recording and markers from the renderer.
  `Core/RenderTimingAdapter` turns them into scopes: `GpuWait(Site:detail)` (Blocked), `Declare.<provider>`, `Pass.<name>`
  and `<label>.<phase>`. Only the main thread records frame scopes. `Wait.Workers` brackets waits on worker jobs.
- `FrameTotals` sums top-level, blocked (GPU, workers, other) and untracked time of one frame.
- `EvaluateChecks()` returns one check per `CheckId` (`Ok`, `Warn`, `Fail`): capture off, dropped scopes, late scopes,
  off-thread scopes, lost worker events, reconciliation (untracked above 5% warns, overrun above 2% fails) and nesting.

## GPU time

- Timestamp queries live in `src/Renderer` (`GpuStampPlan.h`, `RenderGraphTimestampPool`). One reset of the frame's query
  region, a stamp before and after each pass's barriers, and an end and final stamp. Passes past the record budget fold into
  one overflow record. A coarse mode uses fewer stamps.
- The region is read in one `vkGetQueryPoolResults` call with availability. A missing stamp reads `Absent` or `Partial`, a
  backwards or huge delta reads `Error`, a device without timestamps reads `Unsupported`.
- The offscreen regime reads after its own wait. The present regime reads the slot the presenter just waited on. A frame that
  was never submitted, or left the slot ring, counts in `dropped_gpu_frames`.
- A graph made with `TimingMode::Disabled` (one-shot and cluster viewer graphs) writes no stamps and reads `Inactive`.
  Capture off reads `Absent`.
- Per-view numbers live in `FrameSummary::gpu` (`GpuFrameMetrics`), indexed by `ViewSlot`. A view is `Present` only when
  every record of it was measured and none was folded; a mix is `Partial`. A view without a render target is `Inactive`.
  `indirectDraws` is its own count and is never shown as triangles.
- Coverage reads "executed E of declared D, recorded R, disabled by toggle K". A pass toggled off is never declared.
- Calibration (`Renderer/GpuClockCalibration.h`) fits CPU time against GPU ticks from paired samples and reports its
  uncertainty. The device enables no calibrated timestamp extension, so `timeline.aligned` is false and `timeline.reason`
  says so. The timeline image then draws GPU bars on their own lane from the axis origin.
- Cluster counts come from `GET /feature_debug/state`, not from the profiler: `testedClusters` is the current frame,
  `visibleClusters` is two frames old (age is `frame - visibleFrame`), `statsCollected` says whether they are valid, and
  `hidden` with `hiddenValid` is derived from them. A cluster count is never shown as triangles.

## Verdict, spikes, markers, overhead

- `ComputeVerdict()` is one pure function with one constants block. It takes the median of the newest 30 frames and applies
  the rules in order: untracked above 10% is `Unreliable`, other waits above 20% is `Stalled`, present above 30% or GPU from
  40% is `PresentLimited` or `GpuLimited`, CPU from 60% is `CpuLimited`, else `Serial`. It always prints its numbers.
- A frame is a spike when it takes more than twice the median of the previous 60 frames, or more than the user threshold.
  `SpikeStore` freezes it: 16 preallocated slots, the oldest unpinned slot is evicted, at most 8 are pinned. A spike is
  `Explained` when a marker sits on the frame or one scope covers at least half of the excess, else `Unexplained`.
- Markers sit only where periodic or request-driven work runs: memory summary rebuild, release sweeps, cluster table
  synthesis, swapchain recreation, host captures, hot reload and code unload.
- `MeasureScopeEventCostNs()` times a private scope log once at startup. `EstimateOverhead()` turns it into an `Estimated`
  metric and warns above 3% of the frame.

## Switches

- `GTE_ENABLE_PROFILER=OFF` removes the scope macros. `FrameProfiler` itself always compiles.
- `ProfilerControl` holds the runtime switches: `capture` stops recording, `displayPaused` only freezes the panels.
- `FrameProfiler::Instance()` is a process-wide singleton. A test that uses it calls `ResetForTesting()` first.

## HTTP

Reads use a published copy, so they answer while the main thread is blocked (`snapshot_age` tells how old it is). The main
thread builds the copy only while a reader asked in the last 2 s. Every number is inside a metric object, except
`schema_version`, `frame_index`, `thread_slot` and `depth`. Details and keys are in [Networking](networking.md).

| Route | Use |
| --- | --- |
| `GET /profiler` | Overview: CPU, blocked, per-view GPU, coverage, checks, drops, device state, live waits, verdict, overhead, timeline |
| `GET /profiler/frames?count=N` | The newest N frame rows (1 to 120) |
| `GET /profiler/spikes`, `/profiler/spike?index=F` | Frozen spikes and one frozen frame with its scopes |
| `GET /profiler/timeline_image?frame=F&width=W` | PNG drawn on the CPU from frame data; never reads GPU pixels |
| `POST /profiler/set` | Capture, display pause, pin, unpin, spike threshold |
| `POST /profiler/export?frames=N` | One JSON document: overview, newest N frames, pinned spikes |

Debug flow: `GET /profiler` for the verdict and checks, `GET /profiler/spikes` for slow frames and their causes,
`GET /profiler/timeline_image` to see where one frame spent its time, `GET /render_graph` for per-pass GPU records (each
pass has `view`, `instance`, `kind` and a `gpu_timing_status`). `tools/profiler_ab/run_ab.ps1` runs a repeatable A/B table.

## Known limits

- GPU bars are not aligned to CPU time until a calibrated timestamp extension is enabled.
- There is no route that switches the GPU stamps off, so their cost is not measured.
- The timeline image has no text; its lanes are CPU main (rows by depth), waits, worker jobs and GPU per view.

## Reading the data

- The Editor Profiler and Jobs panels only format this data. See [Editor Module Structure](editor-module-structure.md).
