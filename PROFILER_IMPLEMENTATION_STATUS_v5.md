# Profiler Implementation Status (v5)

Status: LIVING DOCUMENT — reflects exactly what exists in the codebase as
of the Phase 5 implementation session on `feature/profiler-impl` (see
"Changelog: v4 -> v5" below for exactly what changed and why). Update this
file (or fold it into `TODO.md`/`README.md` and delete it) the next time a
Profiler phase is implemented, rather than letting it silently drift out of
sync with reality — see `PROFILER_STRATEGY_v2.md`'s own closing section for
why this codebase treats planning/status documents this way.

This file exists purely to answer, for anyone (human or AI agent) picking
this up next: **"what already works, what doesn't exist yet, and why was it
deliberately left for later rather than done now?"** It does not repeat
`PROFILER_STRATEGY_v2.md`'s own design reasoning in full, nor
`PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md`'s own Phase 5 reasoning in
full — read those documents for the *why behind the plan itself*; this
document is only about *how much of that plan is actually built*.

--------------------------------------------------------------------------
## Changelog: v4 -> v5 (read this first)
--------------------------------------------------------------------------

`PROFILER_IMPLEMENTATION_STATUS_v4.md` (v4) correctly described Phase 0/1/2/3
as implemented and Phases 4/5/6/7 as not implemented. **This session
implemented Phase 5** (`PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md`'s "GPU
memory usage over time") — see "What was implemented this session" below.
Phase 4/6/7 remain exactly as v4 described them; that reasoning is not
repeated here in full, only re-listed under "What was NOT implemented" with
an updated priority ordering.

Nothing in Phase 0/1/2/3's own implementation was touched this session,
beyond the one small, deliberate addition Phase 5's own strategy document
called for: a new, standalone header
(`src/Application/MemorySnapshotBuilder.h`) plus its own dedicated test file
(`tests/Application/MemorySnapshotBuilderTests.cpp`) — no existing file's
*behavior* changed, only `Application.cpp` (one new `#include`, one new
call site) and `AGENTS.md`/`TESTING.md` (documentation).

--------------------------------------------------------------------------
## What was implemented this session
--------------------------------------------------------------------------

### Phase 5 — GPU memory usage over time

A new, always-compiled, header-only file, **`src/Application/MemorySnapshotBuilder.h`**
(added to `gte_core`'s unconditional `add_library()` file list, right
alongside `Application.h`/`EventTranslator.h`):

- **`BuildMemorySnapshot(const GpuMemoryTracker::Totals&)`** — a pure,
  `noexcept`, arithmetic-free/branch-free field-for-field mapping from
  `Renderer::GetMemoryTotals()`'s result (a Vulkan-tied
  `GpuMemoryTracker::Totals`) into a `Profiling::MemorySnapshot` (a plain,
  Vulkan-free type `ProfilingTypes.h` had already reserved, unused, since
  Phase 0 specifically for this moment). Always sets
  `status = GpuSampleStatus::Present` — unlike a `GpuPass`'s draw-call/
  triangle count, `Renderer::GetMemoryTotals()` has no "didn't run this
  frame" concept at all; it is always a valid, meaningful O(1) read for as
  long as a live `Renderer` exists.
- Deliberately its OWN small header, not an anonymous-namespace helper
  inlined into `Application.cpp` (the shape the phase's own v1 strategy
  document originally proposed, corrected in v2 before implementation
  started — see `PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md`'s "Changes from
  v1"): this is what lets `BuildMemorySnapshot()` itself be called
  directly from a unit test, rather than leaving the one genuinely new
  piece of logic in this whole phase completely untested (every
  `FrameProfiler`-level test hand-constructs a `MemorySnapshot` directly
  and never calls this function — see "Testing" below).

**`Application::Run()` is the ONE production call site**, added right after
the existing `m_editorLayer->RenderPlatformWindows();` line and immediately
before the existing `Profiling::FrameProfiler::Instance().EndFrame();`
line — i.e. as late as possible in the frame while still inside the
`BeginFrame()`/`EndFrame()` bracket, so the snapshot reflects every GPU
resource created/destroyed anywhere that frame (including by
`IEditorLayer::BuildUI()`'s own Inspector/Project-panel asset loading
earlier in the same frame):

```cpp
Profiling::FrameProfiler::Instance().SetMemorySnapshot(BuildMemorySnapshot(m_renderer.GetMemoryTotals()));
```

Deliberately **not** wrapped in `#if GTE_ENABLE_PROFILER`/`GTE_ENABLE_EDITOR`,
matching this same function's own existing, already-verified precedent
(`BeginFrame()`/`EndFrame()`/`SetGpuPassDrawStats()` calls in the same
function aren't gated either — only `GTE_PROFILE_SCOPE(...)`'s own macro
body is).

No change was needed to `FrameProfiler.h/.cpp`, `ProfilingTypes.h`, or
`GpuMemoryTracker.h/.cpp` — every type/method this phase needed
(`Profiling::MemorySnapshot`, `FrameProfiler::SetMemorySnapshot()`,
`FrameSample::memory`, `Renderer::GetMemoryTotals()`) already existed,
already correct, reserved for exactly this moment since Phase 0.

### Testing

- **`tests/Application/MemorySnapshotBuilderTests.cpp` (2 tests)** — added
  to `tests/CMakeLists.txt`'s unconditional `GTE_TEST_SOURCES` list, right
  alongside `Application/EventTranslatorTests.cpp`:
  `MapsEveryFieldToItsMatchingSnapshotField` (every one of the eight
  fields given a DISTINCT value, so a transposed-field bug — e.g.
  `bufferBytes`/`textureBytes` swapped — is guaranteed to be caught) and
  `AllZeroTotalsStillReportsPresent` (a genuinely all-zero `Totals` must
  still map to `status == Present`, never `Absent` — that distinction is a
  `FrameProfiler`-level concept, never something this function decides).
- **`tests/Profiling/FrameProfilerTests.cpp` (6 new tests)** — added
  directly after the pre-existing `SetGpuPassDrawStatsOutsideFrameBracketIsNoOp`:
  `SetMemorySnapshotRecordsEveryFieldExactly` (all eight fields, at the
  `FrameProfiler` storage level — deliberately does NOT go through
  `BuildMemorySnapshot()`, that's the job of the two tests above),
  `SetMemorySnapshotOutsideFrameBracketIsNoOp` (closes a real,
  pre-existing gap: `SetGpuPassTiming()`/`SetGpuPassDrawStats()` each
  already had this exact test; `SetMemorySnapshot()` did not, until now),
  `MemorySnapshotStaysCorrectAcrossMultipleFrames`,
  `MemorySnapshotSurvivesRingBufferWraparound`,
  `AbsentMemorySnapshotIsDistinctFromRealZeroBytes` (the single most
  important test in this phase — a frame where `SetMemorySnapshot()` is
  never called reads back `status == Absent`, `totalBytes == 0`; a frame
  where it IS called with a genuinely empty total reads back
  `status == Present`, `totalBytes == 0` — same numeric value, provably
  distinguishable tri-state), and
  `SetMemorySnapshotDoesNotAffectCpuScopesGpuTimingOrDrawStats` (a direct
  isolation/regression check, mirroring the pre-existing
  `SetGpuPassTimingNeverTouchesCountStatusOrViceVersa`).
- **Verified**: full clean build (`cmake -S . -B build` then
  `cmake --build build --config Debug`) succeeds with no new warnings, and
  a fresh `ctest` run reports **482/483 passing, 1 skipped** (the same
  pre-existing, unrelated, machine-gated smoke test skipped as v4 —
  `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`).
  This "483 total" was independently re-counted fresh for this document
  (475 from v4, +2 `MemorySnapshotBuilderTests.cpp`, +6 new
  `FrameProfilerTests.cpp` cases = 483), not copied forward from a prior
  session's number.
- **`AGENTS.md`'s existing "Profiling" section** was corrected (not just
  added to): the pre-existing sentence claiming "GPU TIMING (Phase 4) and
  the memory snapshot (Phase 5) remain the only producers still unwired"
  was updated to say GPU timing (Phase 4) ALONE remains unwired, since the
  memory snapshot became real, wired production data this session — and a
  new bullet describing `src/Application/MemorySnapshotBuilder.h` was
  added right after the pre-existing `FrameGraphData.h/.cpp` bullet.
- **`TESTING.md`** gained a new bullet for
  `Application/MemorySnapshotBuilderTests.cpp` (right after the
  pre-existing `Application/EventTranslatorTests.cpp` entry — the first
  ever `tests/Application/` bullet besides that one), and the pre-existing
  `Profiling/FrameProfilerTests.cpp` bullet gained a clause describing the
  new, thorough `SetMemorySnapshot()` coverage (this bullet was NOT stale
  before this session, contrary to what an earlier draft of the Phase 5
  strategy document incorrectly claimed — see
  `PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md`'s own "Changes from v1" for
  that correction).

--------------------------------------------------------------------------
## What was NOT implemented, and why
--------------------------------------------------------------------------

Every phase below is a genuine gap, left for a future session — none of
these are "secretly done" or partially wired up anywhere. (Phase 5's own
reasoning is now resolved — see above. Phase 4/6/7's reasoning is
unchanged from `PROFILER_IMPLEMENTATION_STATUS_v4.md` and is only
summarized here, not re-derived.)

### Phase 4 — Vulkan GPU timestamp queries

**Not implemented.** No `VkQueryPool`, no `vkCmdWriteTimestamp2` call, no
device timestamp-support query anywhere in the codebase. `GpuPassSample::
timingStatus` is still unconditionally `GpuSampleStatus::Absent` for every
real frame the engine renders — only `FrameProfiler::SetGpuPassTiming()`'s
own tests construct a `Present`/`Unsupported` timing sample synthetically.

**Why deferred:** Same reasoning as v2/v3/v4 — explicitly the most
substantial/risky remaining phase, best tackled as its own dedicated
session. This remains true even now that Phase 5 is done — memory and GPU
timing are fully independent producers, and nothing about implementing
Phase 5 made Phase 4 any easier or harder.

### Phase 6 — Benchmark mode (headless-of-the-Editor CLI run)

**Not implemented.** No `--benchmark` CLI flag, no CSV/summary exporter, no
warm-up-frame handling.

**Why deferred:** Same reasoning as v2/v3/v4 — a pure consumer of Phases
0-5's data model; now that Phase 5 gives it real memory data (alongside
Phase 3's real draw-call/triangle data), it still needs its own CLI/
workload design. The data model itself is now fully ready for this phase
to consume, apart from Phase 4's still-synthetic GPU timing.

### Phase 7 — The Editor "Profiler" panel

**Not implemented — this is the actual visible window a user would open.**
No `src/Editor/Panels/ProfilerPanel.h/.cpp`, no
`src/Editor/ProfilerPanelData.h/.cpp`, no "Profiler" entry in
`DockLayout.cpp`. Opening the Editor today still shows exactly the same
panel set as before (Hierarchy/Inspector/Scene/Game/Memory/Project).

**Why deferred:** Same reasoning as v2/v3/v4 — deliberately sequenced
last. Phase 2's frame-time graph data, Phase 3's draw-call/triangle
counts, AND now Phase 5's real memory history are all ready for this
panel to consume immediately — it can show real frame time, real draw/
triangle counts, and real memory usage over time today, with "N/A" for
GPU timing alone until Phase 4 lands, the same tri-state "absent"
convention already established everywhere else.

--------------------------------------------------------------------------
## Known rough edges in what WAS implemented
--------------------------------------------------------------------------

- Everything `PROFILER_IMPLEMENTATION_STATUS_v4.md` listed here remains
  true and unchanged — nothing about Phase 3's own draw-call/triangle-count
  wiring, or the `timingStatus`/`countStatus` split, was touched this
  session.
- **The memory snapshot is a single, instantaneous per-frame read, not a
  frame-spanning aggregate** — `Renderer::GetMemoryTotals()` is called
  exactly once, as late as possible in the frame (see above), so a GPU
  resource created and destroyed entirely BEFORE that point in the same
  frame (or entirely AFTER it, next frame) is reflected in whichever
  frame's read actually observed it; this is inherent to what an
  "instantaneous snapshot" primitive like `GpuMemoryTracker` is, not a bug
  to fix here — see `PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md`, Step 2.4.
- **No Editor panel shows this data yet** — Phase 7 is what would actually
  display `FrameSample::memory` history to a user; today the only way to
  observe it is a unit test, or a throwaway diagnostic reading
  `Profiling::FrameProfiler::Instance().LastCompletedFrame().memory`.
- **`FrameGraphData.h/.cpp` still has zero real callers**, and gained no
  new functionality this session (deliberately out of scope — see
  `PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md`, Step 4's own "No
  `Profiling::FrameGraphData.h` extension" scope refusal) — a future
  consumer needing a memory-bytes-over-time plottable range would add a
  `ComputeMemoryBytesRange()` sibling to `ComputeCpuMillisecondsRange()`/
  `ComputeGpuMillisecondsRange()` then, not before.

--------------------------------------------------------------------------
## How to verify the current state yourself
--------------------------------------------------------------------------

- Build: `cmake -S . -B build` then `cmake --build build --config Debug`.
- Test: `ctest --test-dir build -C Debug --output-on-failure` (expect 482
  passing, 1 skipped).
- There is still no visual/CLI way to see the collected profiling data —
  the only way to inspect a real frame's memory snapshot today is a unit
  test, or a throwaway diagnostic reading
  `Profiling::FrameProfiler::Instance().LastCompletedFrame().memory`.

--------------------------------------------------------------------------
## Suggested next steps, in priority order
--------------------------------------------------------------------------

1. ~~Phase 2 (frame-time graph data reshape)~~ — DONE (v3 session).
2. ~~Phase 3 (draw-call/triangle counts)~~ — DONE (v4 session).
3. ~~Phase 5 (GPU memory history)~~ — DONE as part of this v5 session.
4. Phase 7 (the Editor "Profiler" panel) — now has Phase 2's frame-time
   graph data, Phase 3's draw-call/triangle counts, AND Phase 5's real
   memory history all ready to consume; can ship showing "N/A" for GPU
   timing alone until Phase 4 lands, same tri-state convention throughout.
5. Phase 4 (GPU timestamp queries) and Phase 6 (benchmark mode) — the two
   most substantial remaining phases, best tackled as their own dedicated
   sessions per the strategy document's own risk framing. Phase 6 is now
   additionally unblocked data-wise (Phase 5 was its last real data-model
   dependency besides Phase 4's still-synthetic GPU timing).

See `PROFILER_STRATEGY_v2.md` for the full 8-phase design reasoning and
`PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md` for Phase 5's own detailed
reasoning.
