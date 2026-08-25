# Profiler Implementation Status (v4)

Status: LIVING DOCUMENT — reflects exactly what exists in the codebase as
of the Phase 3 implementation session on `feature/profiler-impl` (see
"Changelog: v3 -> v4" below for exactly what changed and why). Update this
file (or fold it into `TODO.md`/`README.md` and delete it) the next time a
Profiler phase is implemented, rather than letting it silently drift out of
sync with reality — see `PROFILER_STRATEGY_v2.md`'s own closing section for
why this codebase treats planning/status documents this way.

This file exists purely to answer, for anyone (human or AI agent) picking
this up next: **"what already works, what doesn't exist yet, and why was it
deliberately left for later rather than done now?"** It does not repeat
`PROFILER_STRATEGY_v2.md`'s own design reasoning in full, nor
`PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md`'s own Phase 3 reasoning in
full — read those documents for the *why behind the plan itself*; this
document is only about *how much of that plan is actually built*.

--------------------------------------------------------------------------
## Changelog: v3 -> v4 (read this first)
--------------------------------------------------------------------------

`PROFILER_IMPLEMENTATION_STATUS_v3.md` (v3) correctly described Phase 0/1/2
as implemented and Phases 3-7 as not implemented. **This session implemented
Phase 3** (`PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md`'s "Draw-call and
triangle counts") — see "What was implemented this session" below. Phases
4-7 remain exactly as v3 described them; that reasoning is not repeated here
in full, only re-listed under "What was NOT implemented" with an updated
priority ordering.

Nothing in Phase 0/1/2's own implementation was touched this session beyond
the one deliberate, pre-planned correction Phase 3's own strategy document
called for: `GpuPassSample`'s single combined `status` field was split into
two independent tri-states, `timingStatus` and `countStatus` (see below) —
`FrameGraphData.cpp`'s `ComputeGpuMillisecondsRange()` was updated to read
`timingStatus` instead of the old `status` as a pure rename, with zero
behavioral change to any already-passing test.

--------------------------------------------------------------------------
## What was implemented this session
--------------------------------------------------------------------------

### Phase 3 — Draw-call and triangle counts

A new, always-compiled, Vulkan-free pair of files,
**`src/Renderer/DrawStats.h/.cpp`** (added to `gte_core`'s unconditional
`add_library()` file list, same tier as `Vertex.h`/`ResourcePool.h` — no
`GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROFILER` dependency at all):

- **`DrawStats`** — a plain `{drawCallCount, triangleCount}` struct.
- **`AccumulateDrawStats(DrawStats&, bool hasIndexBuffer, vertexCount, indexCount)`**
  — the production entry point: a `noexcept`, allocation-free per-item
  accumulator, called **inline from inside**
  `FrameRecorder::RecordFrame()`'s existing per-item loop, at the exact
  branch that already decides `vkCmdDraw` vs. `vkCmdDrawIndexed` —
  **never** from a separate pass over `m_drawQueue`. This fused design
  (the main correction from Phase 3's own v1 -> v2 document review) makes
  divergence between "what was counted" and "what was actually drawn"
  structurally impossible, rather than something a future edit could
  silently break by adding a skip/validity branch to only one of two
  separate loops.
- **`CountDrawStats(std::span<const CountableDrawItem>)`** — a test-facing
  batch wrapper over `AccumulateDrawStats()`, used only by
  `tests/Renderer/DrawStatsTests.cpp` for table-driven cases; production
  code never calls this, only the inline accumulator.
- Triangle counting (`(indexed ? indexCount : vertexCount) / 3`) is exact
  today because every `Pipeline` is unconditionally
  `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` (`Pipeline.cpp`) and every draw has
  `instanceCount == 1` (no instancing anywhere in this engine yet) — both
  assumptions are documented directly in `DrawStats.h`.
- Deliberately **NOT** gated behind `#if GTE_ENABLE_PROFILER` — `m_drawQueue`
  is already iterated unconditionally every frame to issue the real draw
  calls, so this accumulation rides along on that same, already-necessary
  iteration at effectively no extra measurable cost (a considered decision,
  not an oversight — see `DrawStats.h`'s own comment).

**`ProfilingTypes.h`'s `GpuPassSample` was corrected, splitting its single
combined `status` field into two independent tri-states** — the one real
design gap Phase 3's own strategy document existed to catch and fix at the
cheapest possible moment (zero production callers of the old
`SetGpuPassSample()` existed before this session):

```cpp
struct GpuPassSample {
    GpuSampleStatus timingStatus = GpuSampleStatus::Absent; // Governs milliseconds only - Phase 4's future concern.
    double milliseconds = 0.0;
    GpuSampleStatus countStatus = GpuSampleStatus::Absent;  // Governs drawCallCount/triangleCount only - Phase 3's concern.
    std::uint32_t drawCallCount = 0;
    std::uint32_t triangleCount = 0;
};
```

`FrameProfiler::SetGpuPassSample()` was **replaced** by two focused setters,
`SetGpuPassTiming(pass, status, milliseconds)` and
`SetGpuPassDrawStats(pass, status, drawCallCount, triangleCount)` — each
governs only its own half of `GpuPassSample`, verified by a new regression
test, `FrameProfilerTests.cpp`'s `SetGpuPassTimingNeverTouchesCountStatusOrViceVersa`,
plus the exact defect-catching test the strategy document specified,
`DrawStatsAloneDoNotImplyRealTimingData`. `FrameGraphData.cpp`'s
`ComputeGpuMillisecondsRange()` now reads `timingStatus` (a pure rename,
zero behavioral change) — a new test,
`FrameGraphDataTests.cpp`'s `DrawStatsOnlyPassReportsNoTimingData`, proves a
pass whose only data this session is a Phase-3-style draw-stats call
correctly reports `hasData == false` for GPU *timing*.

**The fused accumulation was wired all the way from `FrameRecorder` up to
`Application::Run()`:**

- `FrameRecorder::RecordFrame()` now returns `DrawStats` (previously
  `void`) — a single `DrawStats drawStats;` declared before the existing
  per-item loop, with one `AccumulateDrawStats(...)` call added
  immediately after the existing `vkCmdDraw`/`vkCmdDrawIndexed` branch,
  inside the very same loop iteration.
- `FramePresenter::Present()` now returns `std::optional<DrawStats>` —
  `std::nullopt` on every one of its three existing early-return paths
  (minimized window, still-pending resize, just-recreated swapchain, i.e.
  `VK_ERROR_OUT_OF_DATE_KHR`), and a real `DrawStats` (captured from
  `frameRecorder.RecordFrame(...)`) on the success path. This distinction
  is deliberate: a minimized-window frame must report `Present`'s `GpuPass`
  as **absent**, never a fabricated "ran with zero draws."
  `FramePresenter::RenderOffscreen()` (which has no early-return path
  today — re-verified against the current source before writing this)
  returns a plain `DrawStats`, never `std::optional`.
- `Renderer::Present()`/`RenderOffscreen()` mirror the same two shapes,
  each just forwarding to `FramePresenter`'s equivalent call — confirmed
  non-breaking for `AssetPreviewMesh.cpp`/`BoneViewerWindow.cpp` (bare-
  statement callers that already discard the return value) via a fresh
  repository-wide search for `RenderOffscreen(`/`.Present(` before making
  the change.
- `Application::Run()` is the one new production caller of
  `SetGpuPassDrawStats()`: right after each `RenderOffscreen()`/`Present()`
  call, it reports the returned `DrawStats` (or, for `Present()`, skips
  reporting entirely when the result is `std::nullopt` — leaving
  `GpuPass::Present`'s `countStatus` at its default `Absent`). Deliberately
  **not** wrapped in `#if GTE_ENABLE_PROFILER`, matching the existing,
  already-verified precedent in the same file
  (`FrameProfiler::Instance().BeginFrame()`/`EndFrame()` aren't gated
  either — only `GTE_PROFILE_SCOPE(...)`'s own macro body is).

### Testing

- **`tests/Renderer/DrawStatsTests.cpp` (7 tests)** — added to
  `tests/CMakeLists.txt`'s unconditional `GTE_TEST_SOURCES` list: empty
  queue, one non-indexed draw, one indexed draw, multiple mixed draws, a
  count not evenly divisible by 3 (truncates, never crashes), a degenerate
  zero-vertex/zero-index draw (still counts the draw call, zero
  triangles), and a case proving repeated `AccumulateDrawStats()` calls
  (what production code actually uses) produce an identical result to
  `CountDrawStats()` (what most of this file's own cases use) over the
  equivalent list.
- **`tests/Profiling/FrameProfilerTests.cpp`** — the old
  `SetGpuPassSampleAndMemorySnapshotAreRecorded`/
  `GpuPassSampleOutsideFrameBracketIsNoOp` tests were split/migrated into
  `SetGpuPassTimingAndDrawStatsAndMemorySnapshotAreRecorded`,
  `SetGpuPassTimingOutsideFrameBracketIsNoOp`, and
  `SetGpuPassDrawStatsOutsideFrameBracketIsNoOp`; the default-value test
  (`GpuPassAndMemorySamplesDefaultToAbsent`) now asserts both
  `timingStatus` and `countStatus`; two new tests were added:
  `SetGpuPassTimingNeverTouchesCountStatusOrViceVersa` and
  `DrawStatsAloneDoNotImplyRealTimingData` (the exact regression test
  Phase 3's own strategy document called for).
- **`tests/Profiling/FrameGraphDataTests.cpp`** — every
  `SetGpuPassSample(...)` call site was rewritten to call
  `SetGpuPassTiming`/`SetGpuPassDrawStats` individually depending on which
  half that specific test actually cares about; one new test,
  `DrawStatsOnlyPassReportsNoTimingData`, proves
  `ComputeGpuMillisecondsRange()` genuinely branches on `timingStatus`
  only.
- A repository-wide search for `SetGpuPassSample(` and bare `.status`
  (scoped to `GpuPassSample` usages) confirms zero remaining references to
  the old, combined API/field name anywhere in `src/` or `tests/`.
- **Verified**: full clean build (`cmake -S . -B build -G Ninja` then
  `cmake --build build`) succeeds with no new warnings, and a fresh
  `ctest` run reports **474/475 passing, 1 skipped** (the same
  pre-existing, unrelated, machine-gated smoke test skipped as v3 —
  `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`).
  This "475 total" was independently re-counted fresh for this document
  (464 from v3, +7 `DrawStatsTests.cpp`, +3 net new
  `FrameProfilerTests.cpp` cases, +1 new `FrameGraphDataTests.cpp` case =
  475), not copied forward from a prior session's number.
- **`AGENTS.md`'s existing "Profiling" section** gained three new bullet
  entries: the `timingStatus`/`countStatus` split and the rule it
  enforces, `src/Renderer/DrawStats.h/.cpp` and why
  `AccumulateDrawStats()` is called inline rather than via a separate
  pass, and an update to the pre-existing "GPU-side measurement" bullet
  noting draw-call/triangle counts are now real, wired-up data.
- **`TESTING.md`** gained a bullet for `Renderer/DrawStatsTests.cpp`
  alongside the existing `Renderer/ResourcePoolTests.cpp` entry, **and**
  closed the pre-existing gap this phase's own strategy document called
  out: `tests/Profiling/*` (FrameProfilerTests.cpp/ScopeTimerTests.cpp/
  FrameGraphDataTests.cpp) now has its own bullets too, despite predating
  this phase.

--------------------------------------------------------------------------
## What was NOT implemented, and why
--------------------------------------------------------------------------

Every phase below is a genuine gap, left for a future session — none of
these are "secretly done" or partially wired up anywhere. (Phase 3's own
reasoning is now resolved — see above. Phases 4/5/6/7's reasoning is
unchanged from `PROFILER_IMPLEMENTATION_STATUS_v3.md` and is only
summarized here, not re-derived.)

### Phase 4 — Vulkan GPU timestamp queries

**Not implemented.** No `VkQueryPool`, no `vkCmdWriteTimestamp2` call, no
device timestamp-support query anywhere in the codebase. `GpuPassSample::
timingStatus` is still unconditionally `GpuSampleStatus::Absent` for every
real frame the engine renders — only `FrameProfiler::SetGpuPassTiming()`'s
own tests construct a `Present`/`Unsupported` timing sample synthetically.

**Why deferred:** Same reasoning as v2/v3 — explicitly the most
substantial/risky remaining phase, best tackled as its own dedicated
session.

### Phase 5 — GPU memory usage over time

**Not implemented.** Nothing calls `Renderer::GetMemoryTotals()` once per
frame to feed `FrameProfiler::SetMemorySnapshot()`.

**Why deferred:** Same reasoning as v2/v3 — cheap, but not yet a priority
pick this session.

### Phase 6 — Benchmark mode (headless-of-the-Editor CLI run)

**Not implemented.** No `--benchmark` CLI flag, no CSV/summary exporter, no
warm-up-frame handling.

**Why deferred:** Same reasoning as v2/v3 — a pure consumer of Phases 0-5's
data model; now that Phase 3 gives it real draw-call/triangle data, it
still needs Phase 5's memory data plus its own CLI/workload design.

### Phase 7 — The Editor "Profiler" panel

**Not implemented — this is the actual visible window a user would open.**
No `src/Editor/Panels/ProfilerPanel.h/.cpp`, no
`src/Editor/ProfilerPanelData.h/.cpp`, no "Profiler" entry in
`DockLayout.cpp`. Opening the Editor today still shows exactly the same
panel set as before (Hierarchy/Inspector/Scene/Game/Memory/Project).

**Why deferred:** Same reasoning as v2/v3 — deliberately sequenced last.
Phase 3's own draw-call/triangle-count data is now real and tested,
`FrameGraphPoint::gpuPasses[...].drawCallCount`/`triangleCount` is ready
for this panel to consume immediately — it can show real counts today and
"N/A" for GPU timing until Phase 4 lands, the same tri-state "absent"
convention already established everywhere else.

--------------------------------------------------------------------------
## Known rough edges in what WAS implemented
--------------------------------------------------------------------------

- Everything `PROFILER_IMPLEMENTATION_STATUS_v3.md` listed here remains
  true and unchanged, **except** the `GpuPassSample::status` field itself
  no longer exists (split into `timingStatus`/`countStatus` — see above).
- **Draw-call/triangle counts never see geometry drawn via a `recordExtra`
  callback** (Dear ImGui's own overlay, `AssetPreviewMesh`/
  `BoneViewerWindow`'s previews) — those draws never enter `m_drawQueue` at
  all, issued directly against the `VkCommandBuffer` handed to
  `recordExtra`. This is a deliberate, documented scope boundary (see
  `PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md`, Step 2.3), not a bug —
  this phase counts the engine's own scene geometry, not Editor-only debug
  chrome.
- **`FrameGraphData.h/.cpp` still has zero real callers** for its own
  public API (`BuildFrameGraphPoints()` et al.) — a future Phase 6/7 is
  what would actually call it from real, non-test engine code. Expected/
  by-design, not a bug.

--------------------------------------------------------------------------
## How to verify the current state yourself
--------------------------------------------------------------------------

- Build: `cmake -S . -B build -G Ninja` then `cmake --build build`.
- Test: `ctest --test-dir build --output-on-failure` (expect 474 passing,
  1 skipped).
- There is still no visual/CLI way to see the collected profiling data —
  the only way to inspect a real frame's `drawCallCount`/`triangleCount`
  today is a unit test, or a throwaway diagnostic reading
  `Profiling::FrameProfiler::Instance().LastCompletedFrame().gpuPasses[...]`.

--------------------------------------------------------------------------
## Suggested next steps, in priority order
--------------------------------------------------------------------------

1. ~~Phase 2 (frame-time graph data reshape)~~ — DONE (v3 session).
2. ~~Phase 3 (draw-call/triangle counts)~~ — DONE as part of this v4
   session.
3. Phase 5 (GPU memory history) — cheap, per the strategy document's own
   assessment, and Phase 3's own `SetGpuPassDrawStats()` precedent makes
   the shape of a focused `SetMemorySnapshot()`-only caller obvious to
   follow.
4. Phase 7 (the Editor "Profiler" panel) — now has BOTH Phase 2's
   frame-time graph data AND Phase 3's draw-call/triangle counts ready to
   consume; can ship showing "N/A" for GPU timing until Phase 4 lands and
   "N/A" for memory until Phase 5 lands, same tri-state convention
   throughout.
5. Phase 4 (GPU timestamp queries) and Phase 6 (benchmark mode) — the two
   most substantial remaining phases, best tackled as their own dedicated
   sessions per the strategy document's own risk framing.

See `PROFILER_STRATEGY_v2.md` for the full 8-phase design reasoning and
`PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md` for Phase 3's own detailed
reasoning.
