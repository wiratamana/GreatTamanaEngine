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
- No GPU timing feeds the profiler panel yet, so its GPU rows read "not measured".
- `Application/MemorySnapshotBuilder.h` turns `Renderer::GetMemoryTotals()` into a `Profiling::MemorySnapshot`.
  It is always `Present`.
- `Renderer/DrawStats.h` counts draw calls and triangles inside the `FrameRecorder` record loop, on the same path that
  issues the draw. Triangle counts assume triangle lists and one instance per draw.

## Reading the data

- The Editor Profiler and Jobs panels only format this data. See [Editor Module Structure](editor-module-structure.md).
