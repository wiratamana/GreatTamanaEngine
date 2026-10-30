# Profiling
> The CPU and GPU frame profiler: what it measures, how to read it, how to add a scope. For anyone chasing a slow frame.
> Verified against code: 2026-10-31

## TL;DR
- `src/Profiling/` holds the CPU event log, the frame history and plain data. It has no SDL, Vulkan or ImGui code.
- Every number is a metric with a status. A value that was not measured is `null`, never `0`.
- Read the `verdict` and the `checks` first. If a check fails, do not trust the numbers.
- Look at it in the Profiler and Jobs panels, or over HTTP with `/profiler*`.
- Compare milliseconds, not FPS. With vsync on, FPS snaps to refresh steps.

## Quick start
1. Start the editor. Add `--no-vsync` for benchmarks (see [Gotchas](#gotchas)).
2. Read the overview. Look at `verdict.text` and `checks`.
3. List the slow frames. Open one with its `frame_index`.
4. Draw the timeline of that frame as a PNG.

```cmd
build\GreatTamanaEditor.exe --no-vsync
curl.exe "http://127.0.0.1:8080/profiler"
curl.exe "http://127.0.0.1:8080/profiler/spikes"
curl.exe "http://127.0.0.1:8080/profiler/spike?index=1234"
curl.exe -o timeline.png "http://127.0.0.1:8080/profiler/timeline_image?frame=1234&width=1600"
curl.exe "http://127.0.0.1:8080/profiler/table?view=flat&limit=40"
curl.exe -o graph.png "http://127.0.0.1:8080/profiler/graph_image"
```

- The timeline PNG has no text. Lanes: CPU main (rows by depth), waits, worker jobs, GPU per view.
- All route details are in [HTTP routes (debug)](http-routes-debug.md). More recipes: [HTTP cheat sheet](http-cheat-sheet.md).

## Reading the data
| Field in `GET /profiler` | Meaning | Look for |
|---|---|---|
| `verdict.kind`, `verdict.text` | Which resource limits the frame | See the verdict table below |
| `checks[]` | Self-checks of the profiler (`ok`, `warn`, `fail`) | Any `fail`. Then the numbers are wrong |
| `cpu_frame`, `untracked` | Frame time and time outside every scope | `untracked` above 5% warns |
| `blocked.gpu / workers / other` | Time spent waiting | A big `other` means a stall. See `verdict.stall_site` |
| `gpu.views`, `gpu.span` | GPU time per view (`shared`, `primary`, `secondary`) | `status` must be `Present` |
| `gpu.coverage` | Passes `declared`, `executed`, `recorded`, `disabled_by_toggle` | `executed` lower than `declared` |
| `drops` | Lost scopes, lost worker events, dropped GPU frames | Anything above 0 |
| `device_state.present_policy` | `capped` (vsync) or `uncapped` | `capped` makes FPS useless |
| `live_block` | Waits that are open right now | Works even when the main thread hangs |
| `snapshot_age` | How old this copy is, in ms | Large value = main thread is stuck |
| `overhead.warn` | Profiler cost is above 3% of the frame | `true` means the data is skewed |

Every metric is an object `{value, unit, kind, status, age_frames, reason}`.

| Status | Meaning |
|---|---|
| `Present` | Measured and fresh |
| `Stale` / `Partial` | Old value, or only part of it was measured. Read `reason` |
| `Absent` | Capture is off, or the frame was not submitted |
| `Unsupported` | The device cannot measure it |
| `Inactive` | This graph does not take stamps (one-shot and Cluster Viewer graphs) |
| `Error` | A stamp went backwards or the delta is huge |

`kind` is `Measured`, `Derived` or `Estimated`.

## Verdict
The verdict is the median of the newest 30 frames. Rules run in this order. The first match wins.

| Order | Verdict | Rule |
|---|---|---|
| 1 | `Unreliable` | Untracked time above 10% of the frame, or a check fails |
| 2 | `Stalled` | Other waits (not GPU, not present) above 20% |
| 3 | `PresentLimited` | Present wait above 30% |
| 4 | `GpuLimited` | GPU wait from 40% |
| 5 | `CpuLimited` | CPU work from 60% |
| 6 | `Serial` | None of the above. CPU and GPU run one after the other |

`Unknown` means no data, or fewer than 3 frames. If both present and GPU rules hit, the larger share wins.

## Spikes
- A frame is a spike when it takes more than 2x the median of the previous 60 frames, or more than `spike_threshold_ms`.
- The profiler freezes the spike with all its scopes. The store has 16 slots. The oldest unpinned slot is evicted.
- You can pin 8 spikes at most. A pinned spike is never evicted.
- `classification` is `explained` when a marker sits on the frame, or one scope covers at least half of the excess. Else `unexplained`.
- Markers sit only where periodic or request-driven work runs. Examples: memory summary, hot reload, swapchain recreation, host captures.

## Add a CPU scope
| Macro | Use |
|---|---|
| `GTE_PROFILE_FUNCTION()` | Time the rest of a function. The name is taken from the compiler: `Namespace::Class::Method`. First statement of the function |
| `GTE_PROFILE_SCOPE("Name")` | Time a block. The name must be a string literal. |
| `GTE_PROFILE_SCOPE_ID(id)` | Same, with a name from `InternName()` |
| `GTE_PROFILE_BLOCKED("Name")` | Time a wait. It counts as blocked, not as work |
| `GTE_PROFILE_MARKER("Name")` | Record an instant event |
| `GTE_PROFILE_JOB_SCOPE("Name")` | Time a job body on a worker thread |

Rules:
1. Only the main thread records frame scopes. A scope that ends off the main thread counts as `foreign`. Job bodies use `GTE_PROFILE_JOB_SCOPE`, not `GTE_PROFILE_FUNCTION`.
2. A scope that ends after its frame closed counts as `late`.
3. Job scopes go through a lock-free ring. The main thread drains it in `EndFrame()`.
4. Nothing on the hot path allocates. Every other `FrameProfiler` method is main thread only.
5. `FrameProfiler::Instance()` is a global. A test must call `ResetForTesting()` first.
6. Use one profile macro per line. Two on one line collide, because the local variable is named from `__LINE__`.
7. Never put an unbounded value (entity id, frame number, file name per object) in a scope name. Names are never freed.
8. `GTE_PROFILE_FUNCTION()` uses a function-local `static`. Do not use it in `constexpr` functions. Each template instantiation is its own call site and gets its own row.
9. With `GTE_ENABLE_PROFILER=OFF` the macros compile to nothing and do not evaluate their arguments.
10. If `GTE_ENABLE_PROFILER` is not defined at all, `ScopeTimer.h` stops the build with `#error`. The build system must define it as `0` or `1`.

## Time your own code
Project code (a project DLL) can be timed with one line. No catalog entry is needed.

```cpp
#include "../../../src/Profiling/ScopeTimer.h"

namespace profile_probe {
void BuildTintPass()
{
    GTE_PROFILE_FUNCTION();
    // ... work ...
}
} // namespace profile_probe
```

- The row is named `profile_probe::BuildTintPass`. It shows in the panel as "Your project code" with calls and ms.
- A function in an anonymous namespace shows as `{anonymous}::Helper`. A lambda shows as `Outer::<lambda>`.
- Project DLLs get the engine's `GTE_ENABLE_*` switches from `cmake/GteProject.cmake`, so the macro really records. The ctest `ProjectDefineProbe` fails to build if that is lost.
- Project passes and features already show as `Declare.*`, `Feature.<name>` and `Pass.<name>` without any macro.
- Project code runs on the main thread inside pass setup and execute lambdas, feature callbacks and panel code. Use `GTE_PROFILE_JOB_SCOPE` inside job bodies.
- Check it: `curl.exe "http://127.0.0.1:8080/profiler/scope?name=profile_probe::BuildTintPass"` or `/profiler/table?view=flat`.
- Known limits: names of `operator new`, `operator delete` and conversion operators are not trimmed well. The MSVC signature format is not tested. The engine builds with MinGW.

## Profiler panel
Panel layout, top to bottom. The first screen is the summary and the graph.
1. Controls: Capture, Pause, Tree or Flat, Inclusive or Self time, Technical names.
2. A plain summary sentence (frame time, CPU busy, CPU waiting, GPU), coloured by the verdict, then the verdict advice and the profiler overhead.
3. "What to look at": the biggest CPU rows by self time and the biggest GPU rows.
4. Graph: Frame (grey), CPU busy (blue) and GPU (orange). Each has a tick box in the legend. A GPU frame without a measurement is a gap, never 0. Guide lines mark 16.7 ms and 33.3 ms. Hover shows every value of that frame.
5. Filter box (matches the plain title and the code name), a Categories popup, Expand all and Collapse all.
6. Table with columns Graph, Name, Calls, Total, Self, %, bar. Lanes are Main thread (CPU), Helper threads (CPU) and Graphics card (GPU). Tick the Graph box of a row to add its line to the graph (8 lines at most). Hover a row for the plain explanation, the hint, the code name, the origin (Engine or Your project) and the source file and line.
7. Footer: names used, history tracks.

- Tree view merges calls of the same name under the same parent into one row with a call count. Flat view lists every name once and is exact even when the event buffer was full.
- Pause freezes the graph and the table. Recording continues.
- Models are in `src/Editor` (`ProfilerTableModel`, `ProfilerGraphModel`), widgets in `src/Editor/Panels`, text in `src/Application/ProfilerScopeCatalog`.

## Plain-English catalog
- `DescribeScope(name, lane, origin)` in `src/Application/ProfilerScopeCatalog.cpp` gives every row a plain title, an explanation, a hint and a category.
- Lookup order: exact name, then prefix rule (`Pass.`, `Declare.`, `Feature.`, `Panel.`, `GpuWait(`), then the GPU lane rule, then a fallback by origin. Project code that is not in the catalog shows as "Your code" with its function name.
- To add a name: add one row to the table in that file. Keep title short, explanation 1 or 2 plain sentences. The table sorts itself at compile time. A duplicate name does not compile.
- The test `ProfilerScopeCatalogCoverageTests` reads every `GTE_PROFILE_SCOPE`, `GTE_PROFILE_BLOCKED` and `GTE_PROFILE_JOB_SCOPE` string under `src/` and fails for a hand-written name that has no entry.

## Origin tag
- Every scope name remembers the file and line where it was first written (`InternNameAt`, `InternFunctionName`). Only the file base name is stored.
- The engine root is the folder of this checkout. It is read from the path of `ScopeSource.cpp` at build time (`EngineRoot()`).
- A path under `<root>src/` is "Engine". A path under `<root>Projects/` is "Your project". Another path under the root is unknown.
- A path outside the root (or no root): `/Projects/` in the path is "Your project", else `/src/` is "Engine", else unknown. `\` and `/` are the same.
- The first site that wrote a name wins.

## GPU rows
- `Core::PublishGpuMetrics` sends one row per executed offscreen pass: pass name, owning feature, barrier ms, work ms and status.
- In the table they sit in the GPU lane: feature row, with its passes below. A pass that was not measured shows its status word (absent, unsupported, inactive, error), never `0.00`. A partial row has a `~` prefix.
- Present graph passes have no rows.

## HTTP for the panel
| Route | What |
|---|---|
| `GET /profiler/table?view=tree\|flat&lane=all\|main\|workers\|gpu&limit=1..256` | The table of the newest frame with plain text and metric objects |
| `GET /profiler/graph_image?width=&height=&frames=` | PNG of the Frame, CPU busy and GPU lines. No screenshot needed. Ticked rows are not drawn |
| `GET /profiler/frames` | Each row also has `cpu_busy` |

## Budgets
| Item | Limit | On overflow |
|---|---|---|
| Scope events per frame | 2048 | Counted in `dropped_scopes` |
| Worker events per frame | 512 | Counted in `lost_worker_events` |
| Worker ring | 1024 slots | Counted in `lost_worker_events` |
| Name table | 64 KB, 3072 names | The name shows as `<overflow>` |
| Frame history | 300 frames | Oldest is dropped |
| Aggregate rows per frame (one per name and lane) | 768 | Rows past the limit are not recorded. Rows already in stay exact. The panel footer shows "Aggregate rows dropped: N" |
| GPU pass rows per frame | 256 | Rows past the limit are not recorded. The renderer folds passes past its own stamp budget into one `(folded passes)` row |
| Per-name history | 256 tracks x 300 frames | A track is recycled after 300 unused frames. A name with no free track has no history line. The footer shows `rows without history this frame` |
| Table rows | Panel: 384 Main, 64 Workers, 128 GPU. Snapshot: 160 Main, 32 Workers, 64 GPU | A busy lane never hides another lane. The panel shows `N rows hidden`. `rows_dropped` counts them |
| `GET /profiler/frames` | 1 to 120 rows | `count` is clamped |

## Frame stages
- `Core::BuildFrame` and `Core::Present` bracket the stages: `Render.Prepare`, `Offscreen.*`, `Present`.
- `Offscreen` has `Begin`, `Declare`, `Compile`, `Record`, `Finish`, `FinalizeViews`, `Submit`, `Finalize`.
- Render waits show as `GpuWait(Site:detail)`. Worker waits show as `Wait.Workers`.
- Provider and pass scopes are `Declare.<provider>` and `Pass.<name>`.
- Every render feature callback runs inside `Feature.<feature name>`. It sits inside the `Declare.*` bucket of its stage. This includes features from a project DLL. Scope names come from `FeatureScopeName()` in `src/Core/RenderTimingAdapter.h`.

## GPU time
- Stamps are Vulkan timestamp queries. They live in `src/Renderer` (`GpuStampPlan.h`, `RenderGraphTimestampPool`).
- One pass record has barrier time and work time. `GET /render_graph` shows `gpu_barrier_milliseconds`, `gpu_work_milliseconds` and `gpu_timing_status`.
- Barrier time exists only in the fine stamp mode. The coarse mode reads `null`.
- A graph with timing disabled writes no stamps and reads `Inactive`. Capture off reads `Absent`.
- GPU bars are not aligned to CPU time. No calibrated timestamp extension is enabled, so `timeline.aligned` is `false`.
- A cluster count is never shown as triangles. Cluster counts come from `GET /feature_debug/state`.

## Switches
| Switch | Where | Effect |
|---|---|---|
| `GTE_ENABLE_PROFILER` | CMake, default `ON` | `OFF` removes the scope macros. Routes answer `state: "compiled_out"` |
| `capture` | `POST /profiler/set`, Capture checkbox | Stops recording and GPU stamps |
| `display_pause` | `POST /profiler/set`, Pause button | Freezes the panel only. Recording continues |
| `--no-vsync` | Exe flag | Uncapped present: MAILBOX, else IMMEDIATE, else FIFO |
| `spike_threshold_ms` | `POST /profiler/set` | Extra spike limit, 0 to 10000 ms |

```cmd
curl.exe -X POST "http://127.0.0.1:8080/profiler/set?capture=false"
curl.exe -X POST "http://127.0.0.1:8080/profiler/set?pin=1234"
curl.exe -X POST "http://127.0.0.1:8080/profiler/export?frames=120" -o profile.json
```

## Gotchas
- Reads use a published copy. They answer even when the main thread is blocked.
- The main thread builds a fresh copy only while someone asked in the last 2 s. It also builds one now and then when idle, and on a new spike.
- Before the first copy, `GET /profiler` answers `200` with `state: "no_snapshot"` and only `live_block`. `/profiler/export` answers `503`.
- `/profiler/set` and `/profiler/export` refuse browser requests. Use `curl.exe`, not a web page.
- `/profiler/spike` and `/profiler/timeline_image?frame=` answer `404` for a frame that was never frozen or was evicted.
- A 9th pin answers `409`. Unpin one first.
- `/profiler/scope?name=<exact name>` searches the newest frame only. It answers `404` when that scope did not run in it.
- More than 2048 scope events in one frame fills the event buffer. The check `scope buffer full` then fails and the verdict becomes `Unreliable`. The tree detail is incomplete, but calls and times in the flat table and in the graph stay exact. They come from per-frame totals, not from the events.
- The per-name history behind the graph lines lives on the main thread. Only the Profiler panel reads it. HTTP gets the frame series and the table of the newest frame.
- GPU rows per pass exist for the offscreen graph only. The present graph reads GPU time in a later frame and has no per-pass rows.
- The name table holds 3072 names. Never build a scope name from an entity id, a frame number or a file name per object. The panel footer shows `Names used: N / 3072`.
- No route switches the GPU stamps off, so their own cost is not measured.
- The Profiler panel and `/profiler` read the same data. The panel only formats it.

## Related
- [HTTP cheat sheet](http-cheat-sheet.md) - recipes for perf hitches and A/B runs.
- [HTTP routes (debug)](http-routes-debug.md) - every `/profiler*` parameter.
- [Frame debugger](frame-debugger.md) - per-draw inspection of one frame.
- [Editor](../architecture/editor.md) - the Profiler and Jobs panels.
- `tools/profiler_ab/README.md` - repeatable A/B table with `run_ab.ps1`.
- [Job system](../conventions/job-system.md) - worker threads and their timeline.
