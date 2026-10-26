# Profiling

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

`src/Profiling/` is the CPU scope-timer and frame-history module. It is always compiled (no editor dependency) and has
no SDL dependency. The frame loop is `EditorHost::Run()`: it calls `BeginFrame()` / `EndFrame()` and publishes the
memory snapshot just before `EndFrame()`. Follow these rules when you touch profiling or add a call site.

## CPU scopes

- `GTE_PROFILE_SCOPE("Name")` (`ScopeTimer.h`) is the only way to add a CPU call site. Never call
  `FrameProfiler::RecordCpuScope()` or build a `ScopeTimer` by hand.
- `name` must be a string literal (static storage). Names are compared by pointer and `strcmp`.
- The CPU model is a flat table: every scope with the same name adds to one entry of `FrameSample::cpuScopes`.
  There is no parent and child tracking. A scope that calls itself in one frame is counted twice.
- Worker threads use `GTE_PROFILE_JOB_SCOPE` (`JobScopeTimer.h`). `RecordWorkerJobSample()` is the only thread-safe write.
  Every other `FrameProfiler` method is main thread only.
- Nothing on the hot path may allocate. History and per-frame tables are fixed-size arrays.

## Clock

- `ProfilingClock.h` is the one clock. It is `std::chrono::steady_clock` in nanoseconds (the frequency is a constant).
  SDL is not used: `SdlLinkageRegressionTests` scans `gte_core` for SDL symbols.

## On/off switches

- `GTE_ENABLE_PROFILER=OFF` (CMake) turns `ScopeTimer` into an empty type: no clock read at all.
- `FrameProfiler::SetCaptureEnabled(false)` (runtime) skips the clock read and the history write.
- `FrameProfiler` itself always compiles, also with the profiler OFF. Do not wrap it in `#if`.
- `FrameProfiler::Instance()` is a process-wide singleton. A test that uses it calls `ResetForTesting()` first.

## GPU and memory samples

- A value that was not measured is tagged `GpuSampleStatus::Absent` or `Unsupported`. It is never a bare `0`.
- `GpuPassSample` has two independent fields: `timingStatus` (set by `SetGpuPassTiming()`) and `countStatus`
  (set by `SetGpuPassDrawStats()`). Never merge them. A draw count must not imply a measured time.
- GPU time comes from timestamp queries in `RenderGraphTimestampPool`. The read is done after a fence wait the engine
  does anyway. No wait is added to fetch a timing.
- `Core` fills the Game, Scene and Present rows. The two view rows are built from one pass-name list, so they are not
  measured per view.
- `Renderer/DrawStats.h` counts draw calls and triangles inside the `FrameRecorder` record loop, on the same path that
  issues the draw. Triangle counts assume triangle lists and one instance per draw.
- `Application/MemorySnapshotBuilder.h` turns `Renderer::GetMemoryTotals()` into a `Profiling::MemorySnapshot`.
  It is always `Present`.

## Reading the data

- `FrameGraphData.h/.cpp` reshapes the history into plot points and min/max ranges. Ranges branch on status, never on
  the stored number. Use these functions instead of walking the history again.
- `WorkerTimelineData.h/.cpp` does the same for the worker job log.
- The Editor Profiler panel only formats this data. See [Editor Module Structure](editor-module-structure.md).
