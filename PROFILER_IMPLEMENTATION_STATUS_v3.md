# Profiler Implementation Status (v3)

Status: LIVING DOCUMENT — reflects exactly what exists in the codebase as
of the Phase 2 implementation session on `feature/profiler-impl` (see
"Changelog: v2 -> v3" below for exactly what changed and why). Update this
file (or fold it into `TODO.md`/`README.md` and delete it) the next time a
Profiler phase is implemented, rather than letting it silently drift out of
sync with reality — see `PROFILER_STRATEGY_v2.md`'s own closing section for
why this codebase treats planning/status documents this way.

This file exists purely to answer, for anyone (human or AI agent) picking
this up next: **"what already works, what doesn't exist yet, and why was it
deliberately left for later rather than done now?"** It does not repeat
`PROFILER_STRATEGY_v2.md`'s own design reasoning in full, nor
`PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s own Phase 2 reasoning in full —
read those documents for the *why behind the plan itself*; this document is
only about *how much of that plan is actually built*.

--------------------------------------------------------------------------
## Changelog: v2 -> v3 (read this first)
--------------------------------------------------------------------------

`PROFILER_IMPLEMENTATION_STATUS_v2.md` (v2) correctly described Phase 0/1 as
implemented and Phases 2-7 as not implemented. **This session implemented
Phase 2** (`PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s "Frame-time graph
data" reshape) — see "What was implemented this session" below. Phases
3-7 remain exactly as v2 described them; that reasoning is not repeated
here in full, only re-listed under "What was NOT implemented" with an
updated priority ordering.

Nothing in Phase 0/1's own implementation was touched this session —
`FrameProfiler`/`ScopeTimer`/`ProfilingTypes.h` are byte-for-byte unchanged
from v2's description. Phase 2 is purely additive (two new files, one new
test file, two CMake list insertions, no existing call site touched — see
`PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s own Step 4 refusal list).

--------------------------------------------------------------------------
## What was implemented this session
--------------------------------------------------------------------------

### Phase 2 — Frame-time graph data (the pure "history → plottable points" reshape)

A new, always-compiled pair of files, **`src/Profiling/FrameGraphData.h/.cpp`**
(inside `namespace gte::Profiling`, zero ImGui/SDL/Vulkan-beyond-what-
`FrameProfiler.h` itself already needs — no `GTE_ENABLE_EDITOR`/
`GTE_ENABLE_PROFILER` dependency at all, same tier as `FrameProfiler.h/.cpp`
itself), added to `gte_core`'s unconditional `add_library()` file list
right alongside the four existing `src/Profiling/` entries:

- **`FrameGraphPoint`** — one retained frame's plottable data:
  `frameIndex` (`std::uint64_t`, copied verbatim from
  `FrameSample::frameIndex`), `cpuMilliseconds` (`double`, copied verbatim
  from `FrameSample::cpuFrameMilliseconds` — no tri-state needed, every
  retained frame already has a real CPU time), and `gpuPasses`
  (`std::array<GpuPassSample, kGpuPassCount>`, copied verbatim/index-for-
  index — the exact same type `FrameSample` itself already uses, never
  reshaped into a new tri-state representation).
- **`FrameGraphRange`** — a Y-axis auto-scale result: `hasData` (`bool`),
  `minMilliseconds`/`maxMilliseconds` (only meaningful when `hasData` is
  `true`).
- **`std::vector<FrameGraphPoint> BuildFrameGraphPoints(const FrameProfiler&)`**
  — reserves exactly `profiler.HistoryCount()` up front, then copies
  `HistoryAt(0..HistoryCount()-1)` into `FrameGraphPoint`s, oldest-first, a
  direct 1:1 order-preserving transcription. Returns an empty vector for an
  empty history. Safe to call at any point in the frame lifecycle,
  including strictly between a `BeginFrame()`/`EndFrame()` pair, since
  `HistoryAt()`/`HistoryCount()` only ever read the completed-and-pushed
  ring buffer, never the in-progress scratch `FrameSample`. No windowing/
  "last N frames" parameter, by design — a caller slices the returned
  `std::vector` itself.
- **`FrameGraphRange ComputeCpuMillisecondsRange(std::span<const FrameGraphPoint>)`**
  — min/max scan over every point's `cpuMilliseconds`; `hasData == false`
  only when `points` is empty.
- **`FrameGraphRange ComputeGpuMillisecondsRange(std::span<const FrameGraphPoint>, GpuPass)`**
  — min/max scan over `gpuPasses[pass].milliseconds`, including **only**
  entries whose `status == GpuSampleStatus::Present` (branching exclusively
  on `status`, never on whether the stored `milliseconds` value happens to
  look like zero — verified by a dedicated test seeding a non-zero,
  "stale-looking" value alongside `Absent`/`Unsupported`). Bounds-checks
  `pass` **unconditionally** (active in both Debug and Release, never a
  debug-only `assert()`), mirroring
  `FrameProfiler::SetGpuPassSample()`'s own already-shipped handling of an
  out-of-range `GpuPass` — an out-of-range value simply reports
  `hasData == false`, same as "a valid pass with zero `Present` entries."
- Both range functions accept `std::span<const FrameGraphPoint>` rather
  than `const std::vector<FrameGraphPoint>&` (this codebase's first use of
  `std::span`, noted honestly per
  `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s own Step 3.1/3.2 reasoning) —
  a `std::vector` argument still converts implicitly, so
  `BuildFrameGraphPoints()`'s output can be passed straight through with no
  change, while a future windowed caller can pass a zero-copy sub-range.

### Testing

- **`tests/Profiling/FrameGraphDataTests.cpp` (12 tests)** — added to
  `tests/CMakeLists.txt`'s unconditional `GTE_TEST_SOURCES` list, right
  alongside `Profiling/FrameProfilerTests.cpp`/`ScopeTimerTests.cpp`:
  empty-history round-trip, a single frame's exact field-for-field
  round-trip (`EXPECT_DOUBLE_EQ` for the CPU value), multiple frames'
  EXACT `frameIndex` ordering (`0, 1, 2, ...`, not merely "increasing"),
  all three named `GpuPass` values with all three `GpuSampleStatus`
  tri-states round-tripping simultaneously in one frame with no
  cross-contamination (including a deliberate note that leaving
  `GpuPass::Present` at its default `GpuSampleStatus::Absent` proves the
  two same-named-but-unrelated `Present` identifiers are never confused),
  ring-buffer wraparound with EXACT boundary `frameIndex` values (mirroring
  `FrameProfilerTests.cpp`'s own `RingBufferWrapsAndKeepsMostRecentFrames`
  pattern), `ComputeCpuMillisecondsRange()` finding the real min/max even
  when neither sits at a sequence boundary, `ComputeGpuMillisecondsRange()`
  correctly ignoring `Absent` frames for one pass while a sibling pass with
  real data still resolves correctly, an all-`Unsupported` series reporting
  no data, an `Absent`/`Unsupported` sample carrying a stale non-zero value
  still being excluded (two dedicated tests, one per status), an
  out-of-range `GpuPass` reporting no data rather than reading out of
  bounds, and `BuildFrameGraphPoints()` never observing an in-progress
  (`BeginFrame()`'d but not yet `EndFrame()`'d) frame.
- **A genuine test-authoring gap discovered while writing the above, and
  fixed properly rather than worked around**: `FrameProfiler` had no test
  hook to force `cpuFrameMilliseconds` to a literal value (it is always
  genuinely computed from `SDL_GetPerformanceCounter()` inside
  `EndFrame()`). An initial pass worked around this with an
  `SDL_Delay()`-based helper (reading the real, actually-measured value
  back via `HistoryAt()` right after recording it) so cases 2/6 could at
  least prove copy-fidelity/a genuine min/max scan — but real-wall-clock
  timing is flaky by nature (OS scheduling jitter, especially under CI
  load) and weaker than the bit-exact, literal-value assertions this
  codebase's own testing convention already uses everywhere else (e.g.
  `SetGpuPassSample(pass, status, 3.5, 10, 200)`'s hand-chosen literals).
  **Reconsidered and fixed properly**: a new, narrowly-scoped test-only
  method, `FrameProfiler::OverrideLastFrameCpuMillisecondsForTesting(double)`
  (`src/Profiling/FrameProfiler.h/.cpp`), was added in the exact same spirit
  as the already-existing `ResetForTesting()` — it overwrites only the most
  recently completed history entry's `cpuFrameMilliseconds` with an exact,
  caller-chosen literal, touching nothing else (not the in-progress frame,
  not any other retained entry, not any of `BeginFrame()`/`EndFrame()`/
  `RecordCpuScope()`/`SetGpuPassSample()`/`SetMemorySnapshot()`'s own
  real-time behavior). This is a deliberate, narrow addition to
  `FrameProfiler`'s TEST-ONLY surface, not a change to its production
  contract — `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s Step 4 "no change to
  `FrameProfiler`" refusal was written assuming Phase 2 needed nothing new
  from `FrameProfiler`'s *production* API, which remains true; it did not
  anticipate this specific test-support gap. Two new tests were added to
  `tests/Profiling/FrameProfilerTests.cpp` for this new method
  (`OverrideLastFrameCpuMillisecondsForTestingReplacesOnlyTheMostRecentEntry`,
  `OverrideLastFrameCpuMillisecondsForTestingOnEmptyHistoryIsNoOp`), per
  `AGENTS.md`'s "every change to Tier 1 code must come with a matching test
  change" rule, and `tests/Profiling/FrameGraphDataTests.cpp`'s cases 2/6
  were rewritten to use it with literal, hand-chosen values instead of
  `SDL_Delay()` — fully deterministic, zero added wall-clock test time, and
  a strictly stronger assertion (`EXPECT_DOUBLE_EQ` against a literal, not
  against "whatever the real timing happened to be").
- **Verified**: full clean build (`cmake -S . -B build -G Ninja` then
  `cmake --build build`) succeeds with no new warnings, and a fresh
  `ctest` run reports **464/464 passing** (450 pre-existing + 12 new
  `FrameGraphDataTests.cpp` + 2 new `FrameProfilerTests.cpp`, 1
  pre-existing, unrelated, machine-gated smoke test skipped — same single
  skip as v2, `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`).
  This "464" was independently re-counted fresh for this document, not
  copied forward from a prior session's number — see
  `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s own explicit warning about not
  reintroducing the "miscounted test totals" mistake `PROFILER_IMPLEMENTATION_STATUS_v2.md`'s
  own v1 -> v2 changelog had to correct once already.
- **`AGENTS.md`'s existing "Profiling" section** gained two new bullet
  additions: one pointing at `src/Profiling/FrameGraphData.h/.cpp`, and one
  documenting `FrameProfiler::OverrideLastFrameCpuMillisecondsForTesting()`
  right alongside its existing `ResetForTesting()` mention.

--------------------------------------------------------------------------
## What was NOT implemented, and why
--------------------------------------------------------------------------

Every phase below is a genuine gap, left for a future session — none of
these are "secretly done" or partially wired up anywhere. (Phase 2's own
reasoning is now resolved — see above. Phases 3/4/5/6/7's reasoning is
unchanged from `PROFILER_IMPLEMENTATION_STATUS_v2.md` and is only
summarized here, not re-derived.)

### Phase 3 — Draw-call and triangle counts

**Not implemented.** `FrameRecorder::RecordFrame()`'s per-draw loop still
has no pure counting step extracted from it, and `FrameSample::gpuPasses`'
`drawCallCount`/`triangleCount` fields are never written to by anything
real — `FrameGraphPoint::gpuPasses` (this session's own new type) carries
those same two fields straight through unexamined, exactly mirroring
`FrameSample` itself, per `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s own
explicit "no draw-call/triangle-count logic" refusal for Phase 2.

**Why deferred:** Self-contained, low-risk, cheap — a natural next phase
now that Phase 2 exists to eventually carry its output.

### Phase 4 — Vulkan GPU timestamp queries

**Not implemented.** No `VkQueryPool`, no `vkCmdWriteTimestamp2` call, no
device timestamp-support query anywhere in the codebase — every
`GpuPassSample`/`FrameGraphPoint::gpuPasses` entry produced by real engine
code today is still unconditionally `GpuSampleStatus::Absent` (Phase 2's
own tests construct `Present`/`Unsupported` samples synthetically via
`FrameProfiler::SetGpuPassSample()`, exactly as
`PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s Step 4 requires — Phase 2 adds
zero real callers of that function).

**Why deferred:** Same reasoning as v2 — explicitly the most substantial/
risky remaining phase, best tackled as its own dedicated session.

### Phase 5 — GPU memory usage over time

**Not implemented.** Nothing calls `Renderer::GetMemoryTotals()` once per
frame to feed `FrameProfiler::SetMemorySnapshot()`. (Phase 2 does not touch
`FrameSample::memory`/`MemorySnapshot` at all — that remains this
non-existent phase's own job, per `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s
Step 4.)

**Why deferred:** Same reasoning as v2 — cheap, but not yet a priority pick
this session.

### Phase 6 — Benchmark mode (headless-of-the-Editor CLI run)

**Not implemented.** No `--benchmark` CLI flag, no CSV/summary exporter, no
warm-up-frame handling.

**Why deferred:** Same reasoning as v2 — a pure consumer of Phases 0-5's
data model; Phase 2's own output (`BuildFrameGraphPoints()` et al.) is
Editor-independent by design specifically so this phase can consume it
later with zero rework, but it still needs Phase 3/5 to have real
draw-call/memory data worth exporting, and its own CLI/workload design.

### Phase 7 — The Editor "Profiler" panel

**Not implemented — this is the actual visible window a user would open.**
No `src/Editor/Panels/ProfilerPanel.h/.cpp`, no
`src/Editor/ProfilerPanelData.h/.cpp`, no "Profiler" entry in
`DockLayout.cpp`. Opening the Editor today still shows exactly the same
panel set as before (Hierarchy/Inspector/Scene/Game/Memory/Project).

**Why deferred:** Same reasoning as v2 — deliberately sequenced last.
Phase 2's own output is now real and tested, but Phase 7's own strategy
document (not yet written) would still want Phase 3/5 to exist first so
the panel has more than just a CPU line to show.

--------------------------------------------------------------------------
## Known rough edges in what WAS implemented
--------------------------------------------------------------------------

- Everything `PROFILER_IMPLEMENTATION_STATUS_v2.md` listed here remains
  true and unchanged — Phase 2 touched none of it.
- **`FrameGraphData.h/.cpp` has zero real callers**, exactly like
  `SetGpuPassSample()`/`SetMemorySnapshot()` before it — a future Phase 6/7
  is what would actually call `BuildFrameGraphPoints()` from real,
  non-test engine code. This is expected/by-design, not a bug; stated
  plainly so nobody assumes a Profiler UI already exists because the data
  pipeline underneath it now does.

--------------------------------------------------------------------------
## How to verify the current state yourself
--------------------------------------------------------------------------

- Build: `cmake -S . -B build -G Ninja` then `cmake --build build`.
- Test: `ctest --test-dir build --output-on-failure` (expect 464 passing,
  1 skipped).
- There is still no visual/CLI way to see the collected profiling data —
  the only way to inspect `BuildFrameGraphPoints()`'s output today is a
  unit test or a throwaway diagnostic against `FrameProfiler::Instance()`.

--------------------------------------------------------------------------
## Suggested next steps, in priority order
--------------------------------------------------------------------------

1. ~~Phase 2 (frame-time graph data reshape)~~ — DONE as part of this v3
   session.
2. Phase 3 (draw-call/triangle counts) — cheap, mechanical, low-risk, same
   assessment as v2.
3. Phase 5 (GPU memory history) — also cheap, per the strategy document's
   own assessment.
4. Phase 7 (the Editor "Profiler" panel) — once Phase 3/5 give it more real
   data to show, even before Phase 4/6 exist (it can simply show "N/A" for
   GPU timing until Phase 4 lands, matching every other tri-state "absent"
   convention already established, and Phase 2's own output is already
   real and ready for it to consume today).
5. Phase 4 (GPU timestamp queries) and Phase 6 (benchmark mode) — the two
   most substantial remaining phases, best tackled as their own dedicated
   sessions per the strategy document's own risk framing.

See `PROFILER_STRATEGY_v2.md` for the full 8-phase design reasoning and
`PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md` for Phase 2's own detailed
reasoning.
