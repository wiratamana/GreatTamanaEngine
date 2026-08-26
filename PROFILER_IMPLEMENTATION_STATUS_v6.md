# Profiler Implementation Status (v6)

Status: LIVING DOCUMENT — reflects exactly what exists in the codebase as
of the Phase 7 implementation session on `feature/profiler-impl` (see
"Changelog: v5 -> v6" below for exactly what changed and why). Update this
file (or fold it into `TODO.md`/`README.md` and delete it) the next time a
Profiler phase is implemented, rather than letting it silently drift out of
sync with reality — see `PROFILER_STRATEGY_v2.md`'s own closing section for
why this codebase treats planning/status documents this way.

This file exists purely to answer, for anyone (human or AI agent) picking
this up next: **"what already works, what doesn't exist yet, and why was it
deliberately left for later rather than done now?"** It does not repeat
`PROFILER_STRATEGY_v2.md`'s own design reasoning in full, nor
`PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md`'s own Phase 7 reasoning in
full — read those documents for the *why behind the plan itself*; this
document is only about *how much of that plan is actually built*.

--------------------------------------------------------------------------
## Changelog: v5 -> v6 (read this first)
--------------------------------------------------------------------------

`PROFILER_IMPLEMENTATION_STATUS_v5.md` (v5) correctly described Phase
0/1/2/3/5 as implemented and Phase 4/6/7 as not implemented. **This session
implemented Phase 7** (`PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md`'s "The
Editor 'Profiler' panel") — see "What was implemented this session" below.
Phase 4/6 remain exactly as v5 described them; that reasoning is not
repeated here in full, only re-listed under "What was NOT implemented" with
an updated priority ordering.

Nothing in Phase 0/1/2/3/5's own implementation was touched this session,
beyond the one small, deliberate extension Phase 7's own strategy document
called for: `Profiling::FrameGraphPoint` gained a `memory` field and
`Profiling::FrameGraphData.h/.cpp` gained `ComputeMemoryBytesRange()` — a
predicted, already-anticipated addition (see v5's own "Known rough edges"
section, which named this exact function by name as "a future consumer
needing memory-over-time... then, not before" — this phase is "then").

--------------------------------------------------------------------------
## What was implemented this session
--------------------------------------------------------------------------

### Phase 7 — The Editor "Profiler" panel

A new, sixth dockable Editor panel, **"Profiler"**, docked alongside
"Memory" (and "Project", if enabled) along the bottom of the default
layout:

- **`src/Profiling/FrameGraphData.h/.cpp` extended** — `FrameGraphPoint`
  gained a `MemorySnapshot memory` field (copied verbatim from
  `FrameSample::memory` inside `BuildFrameGraphPoints()`), and a new sibling
  function `ComputeMemoryBytesRange()` (a `MemoryBytesRange` result -
  `hasData`/`minBytes`/`maxBytes`, `std::uint64_t`-based rather than
  `double`-based since a byte count has no meaningful fractional part) scans
  a span of points and reports the min/max TOTAL GPU memory across every
  entry whose `memory.status == GpuSampleStatus::Present`, excluding
  `Absent` entries regardless of whatever stale numeric value they carry -
  mirroring `ComputeGpuMillisecondsRange()`'s own "branch on status, never
  on the value" rule exactly. 5 new tests added to the existing
  `tests/Profiling/FrameGraphDataTests.cpp`.
- **New file: `src/Editor/ProfilerPanelData.h/.cpp`** - pure, ImGui-free
  reshape/formatting functions, following `MemoryPanelData.h`'s template:
  `BuildSortedCpuScopeRows()` (biggest-total-first, respecting
  `cpuScopeCount` not the fixed array's capacity), `FormatDuration()`/
  `FormatFrameTimeSummary()`/`FormatCount()`, `ResolveGpuPassCounts()` (the
  one place the draw-call/triangle tri-state collapses to a simple
  available/not-available bool for THIS panel specifically),
  `ToString(GpuPass)`, `FormatGpuTimingLine()` (an honest "N/A" for
  `Absent`/`Unsupported`, never a fabricated "0.00 ms"), and
  `kCpuScopeInstrumentationCompiledIn`/`CpuScopeTableEmptyMessage()` - a
  compile-time constant (mirroring `GTE_ENABLE_PROFILER`) plus its
  corresponding user-facing message, so the CPU Scopes table can honestly
  distinguish "this build compiles CPU scope instrumentation out entirely"
  from "no scopes recorded yet this particular frame" instead of just
  looking identically empty either way. 15 new tests in the new
  `tests/Editor/ProfilerPanelDataTests.cpp`.
- **New file: `src/Editor/Panels/ProfilerPanel.h/.cpp`** - a small,
  STATEFUL class (the second real-world instance of this pre-approved
  `AGENTS.md` exception, alongside `BoneViewerWindow`), holding: `m_paused`
  (the Pause control's own state, fully independent of
  `FrameProfiler::SetCaptureEnabled()`), `m_frozenPoints`/
  `m_frozenLatestFrame` (the snapshot captured the instant Pause is
  switched on), and `m_cpuGraphScratch`/`m_memoryGraphScratch` (reusable
  `std::vector<float>` scratch buffers for the two
  `ImGui::PlotLines()` calls, cleared and refilled every visible frame but
  only reallocating when the window genuinely grows past its previous
  largest size). `Build()` renders, in order: a Capture/Pause controls row;
  a CPU frame-time graph (last 240 frames, auto-scaled, "Waiting for
  profiler data..." when empty); a sorted CPU-scope table (or the
  compiled-out-vs-empty-aware message above); Draw Calls/Triangles for all
  three named passes (Game View primary, Scene View/Present de-emphasized,
  "N/A" for any `Absent` pass); current GPU memory totals plus a sparkline
  (a gap frame repeats the last known value rather than dropping to zero,
  so the line reads as a flat segment rather than a misleading spike); a
  GPU Timing section for all three named passes (always "N/A" today, since
  Phase 4 isn't implemented, but wired to flip to a real value with zero
  code changes here the moment it is); and a disabled, tooltipped
  "Export CSV" stub button ("Planned for the benchmark/export phase
  (Phase 6)").
- **Wired in**: `ImGuiEditorLayer` gained one new member (`m_profilerPanel`,
  NOT gated behind `GTE_ENABLE_PROJECT_PANEL` - it depends only on
  `Profiling::FrameProfiler`, always compiled regardless of that switch) and
  one new call in `BuildUI()` right after `BuildMemoryPanel(...)`.
  `DockLayout.cpp`'s `kAllPanelNames` and `BuildDefaultDockLayout()` were
  both updated in the same change, docking "Profiler" unconditionally
  alongside "Memory" in the `bottom` node.
- **Capture vs. Pause, genuinely independent**: re-confirmed directly
  against `FrameProfiler::BeginFrame()`/`EndFrame()`'s own bodies before
  writing the Capture checkbox's descriptive text - with capture disabled,
  BOTH early-return without advancing the ring buffer OR the frame index at
  all (and every `RecordCpuScope()`/`SetGpuPassTiming()`/
  `SetGpuPassDrawStats()`/`SetMemorySnapshot()` call no-ops the same way) -
  i.e. NO new `FrameSample` is recorded at all while Capture is off, exactly
  as the panel's own text now says. Pause instead only ever swaps which
  points/frame the panel's OWN sections read from (frozen vs. live),
  touching nothing in `FrameProfiler` itself.

### Testing

- **`tests/Profiling/FrameGraphDataTests.cpp` (+5 tests)**:
  `BuildFrameGraphPointsCopiesMemorySnapshotVerbatim`,
  `ComputeMemoryBytesRangeIgnoresAbsentEntries`,
  `ComputeMemoryBytesRangeOnAllAbsentReportsNoData`,
  `ComputeMemoryBytesRangeOnEmptyPointsReportsNoData`,
  `AbsentMemorySampleWithStaleNonZeroValueIsStillExcluded`.
- **`tests/Editor/ProfilerPanelDataTests.cpp` (new, 15 tests)** - added to
  `tests/CMakeLists.txt`'s existing `if(GTE_ENABLE_EDITOR)` test block,
  alongside `Editor/MemoryPanelDataTests.cpp`.
- **Verified**: full clean build (`cmake -S . -B build` then
  `cmake --build build --config Debug`) succeeds with no new warnings, and
  a fresh `ctest` run reports **501/502 passing, 1 skipped** (the same
  pre-existing, unrelated, machine-gated smoke test skipped as v5 -
  `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`). This
  "502 total" was independently re-counted fresh for this document (483
  from v5, +5 `FrameGraphDataTests.cpp` memory-range cases, +14 new
  `Editor/ProfilerPanelDataTests.cpp` cases = 502).
  Additionally verified: a clean configure+build with
  `-DGTE_ENABLE_EDITOR=OFF` still succeeds with zero ImGui linked in, and a
  clean configure+build with `-DGTE_ENABLE_PROFILER=OFF` (Editor still ON)
  succeeds and its own `ctest` run passes 100% (500 tests, 1 skipped) with
  `ProfilerPanelDataTest.CpuScopeTableEmptyMessageMatchesCompileTimeFlag`
  specifically confirming the "compiled out" wording in that configuration.
- **`AGENTS.md`'s "Profiling" section** gained a new bullet describing the
  `FrameGraphPoint::memory`/`ComputeMemoryBytesRange()` extension, and its
  "Editor Module Structure" section gained `ProfilerPanel` as the second
  documented "stateful panel" precedent.
- **`README.md`** gained a "Profiler panel:" bullet under "Editor / Debug
  UI" and a matching "Status" entry.
- **`TESTING.md`** gained bullets for `Profiling/FrameGraphDataTests.cpp`'s
  new memory-range cases and the new `Editor/ProfilerPanelDataTests.cpp`
  file.

--------------------------------------------------------------------------
## What was NOT implemented, and why
--------------------------------------------------------------------------

Every phase below is a genuine gap, left for a future session — none of
these are "secretly done" or partially wired up anywhere. (Phase 7's own
reasoning is now resolved — see above. Phase 4/6's reasoning is unchanged
from `PROFILER_IMPLEMENTATION_STATUS_v5.md` and is only summarized here,
not re-derived.)

### Phase 4 — Vulkan GPU timestamp queries

**Not implemented.** No `VkQueryPool`, no `vkCmdWriteTimestamp2` call, no
device timestamp-support query anywhere in the codebase. `GpuPassSample::
timingStatus` is still unconditionally `GpuSampleStatus::Absent` for every
real frame the engine renders - the new "Profiler" panel's own "GPU Timing"
section reflects this honestly ("N/A" for all three passes), and is wired
to start showing real numbers, per pass, the moment this phase lands, with
zero further Editor-side changes needed.

**Why deferred:** Same reasoning as v2/v3/v4/v5 — explicitly the most
substantial/risky remaining phase, best tackled as its own dedicated
session. This remains true even now that Phase 7 is done — the Editor
panel and GPU timing are fully independent producers/consumers, and nothing
about implementing Phase 7 made Phase 4 any easier or harder.

### Phase 6 — Benchmark mode (headless-of-the-Editor CLI run)

**Not implemented.** No `--benchmark` CLI flag, no CSV/summary exporter, no
warm-up-frame handling. The new "Profiler" panel's own "Export CSV" button
is a deliberately disabled stub with a tooltip pointing at this phase -
Phase 7's strategy document was explicit that building a second, Editor-only
CSV exporter now (only to replace it with Phase 6's shared one later) would
be wasted, divergent work.

**Why deferred:** Same reasoning as v2/v3/v4/v5 — a pure consumer of Phases
0-5's data model (now ALSO true of Phase 7's own reshape additions,
`FrameGraphPoint::memory`/`ComputeMemoryBytesRange()`); it still needs its
own CLI/workload design, and now has a concrete, disabled "Export CSV"
button in the Editor already waiting for it to wire up to.

--------------------------------------------------------------------------
## Known rough edges in what WAS implemented
--------------------------------------------------------------------------

- Everything `PROFILER_IMPLEMENTATION_STATUS_v5.md` listed here remains
  true and unchanged — nothing about Phase 5's own memory-snapshot wiring
  was touched this session, beyond the additive `FrameGraphPoint::memory`/
  `ComputeMemoryBytesRange()` reshape described above.
- **The Profiler panel's CPU/memory graphs show at most the last ~240
  retained frames**, a deliberately smaller window than
  `FrameProfiler`'s own 300-frame (`kMaxFrameHistory`) ring buffer - the
  graph itself is only a few hundred pixels wide, so plotting the full
  history buys nothing a shorter window doesn't already show just as well.
- **GPU Timing remains the one section with no real data to show at all**
  (see Phase 4 above) - every other section (CPU frame time, CPU scopes,
  draw calls/triangles, GPU memory) shows genuinely live, real production
  data today.
- **No CSV export yet** - the "Export CSV" button is a disabled stub
  pointing at Phase 6, by design (see above).

--------------------------------------------------------------------------
## How to verify the current state yourself
--------------------------------------------------------------------------

- Build: `cmake -S . -B build` then `cmake --build build --config Debug`.
- Test: `ctest --test-dir build -C Debug --output-on-failure` (expect 501
  passing, 1 skipped).
- Open the Editor, spawn a few primitive entities via "Hierarchy" -> "Create
  3D Object", and switch to the "Profiler" tab (tabbed alongside "Memory")
  to see the CPU frame-time graph, CPU scope table, draw-call/triangle
  counts, and GPU memory sparkline all update live.

--------------------------------------------------------------------------
## Suggested next steps, in priority order
--------------------------------------------------------------------------

1. ~~Phase 2 (frame-time graph data reshape)~~ — DONE (v3 session).
2. ~~Phase 3 (draw-call/triangle counts)~~ — DONE (v4 session).
3. ~~Phase 5 (GPU memory history)~~ — DONE (v5 session).
4. ~~Phase 7 (the Editor "Profiler" panel)~~ — DONE as part of this v6
   session.
5. Phase 4 (GPU timestamp queries) and Phase 6 (benchmark mode) — the two
   remaining phases, best tackled as their own dedicated sessions per the
   strategy document's own risk framing. Phase 6 is now further unblocked:
   the Editor panel it will eventually share a CSV exporter with already
   exists, with a disabled button waiting for it.

See `PROFILER_STRATEGY_v2.md` for the full 8-phase design reasoning and
`PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md` for Phase 7's own detailed
reasoning.
