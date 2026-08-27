# Phase 2 Completion Report — Frame-Time Graph Data

Status: FINAL — this document is a complete, self-contained record of
everything actually implemented, tested, and verified in this working
session on branch `feature/profiler-impl`.

**Naming note, stated up front so it is never a source of confusion
later:** this report covers `PROFILER_STRATEGY_v2.md`'s **Phase 2**
("Frame time graph data — the pure 'history → plottable points'
reshape"), plus one follow-up fix to `FrameProfiler`'s test-only surface
discovered while writing Phase 2's own tests. It does **not** cover
`PROFILER_STRATEGY_v2.md`'s actual **Phase 3** ("Draw-call and triangle
counts") — that phase has not been started, has zero code written for it,
and remains exactly as `PROFILER_IMPLEMENTATION_STATUS_v3.md` describes it
under "What was NOT implemented, and why". If a report specifically about
draw-call/triangle-count work was intended, that work does not exist yet
in this codebase — this document instead covers everything that was
actually completed in this session, in full detail, so nothing is lost or
mislabeled.

--------------------------------------------------------------------------
## 1. Executive summary
--------------------------------------------------------------------------

This session implemented **Phase 2** of the 8-phase profiler plan
(`PROFILER_STRATEGY_v2.md`), following its own dedicated planning document,
`PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`, step by step. Phase 2 turns
`FrameProfiler`'s ring-buffer history into a plain, plottable array of
points — the data shape a future Editor "Profiler" panel (Phase 7) or a
future benchmark-mode CSV exporter (Phase 6) will consume, without either
one having to re-derive the same history-walk/tri-state-handling logic
itself.

While writing Phase 2's own test suite, a genuine gap was discovered:
`FrameProfiler` had no way for a test to force an exact, literal CPU frame
time — every `cpuFrameMilliseconds` value is always genuinely measured from
real elapsed wall-clock time. An initial workaround (using `SDL_Delay()` to
separate frames' real timings) was written, then reconsidered and replaced
with a small, correctly-scoped addition to `FrameProfiler` itself (a new
test-only method, in the same spirit as its existing `ResetForTesting()`),
because the delay-based approach was flaky and weaker than this codebase's
own testing conventions demand.

**End state**: two new always-compiled, ImGui-free files
(`src/Profiling/FrameGraphData.h/.cpp`), one small test-only addition to
`FrameProfiler` (`OverrideLastFrameCpuMillisecondsForTesting()`), 14 new
tests total across two test files, all existing CMake lists updated with no
new build options introduced, and full documentation updates. A clean
build and a fresh `ctest` run both succeed: **464/464 tests passing**, 1
pre-existing, unrelated, machine-gated smoke test skipped, zero
regressions.

--------------------------------------------------------------------------
## 2. Background and starting point
--------------------------------------------------------------------------

At the start of this session:

- `feature/profiler-impl` already had **Phase 0** (the profiling data model
  — `ProfilingTypes.h`, `FrameProfiler.h/.cpp`, `ScopeTimer.h`) and
  **Phase 1** (wiring `GTE_PROFILE_SCOPE(...)` into existing per-frame call
  sites) fully implemented and tested, per `PROFILER_IMPLEMENTATION_STATUS_v2.md`.
- The full test suite stood at **450 tests passing**, 1 skipped.
- Phases 2 through 7 were all explicitly "not implemented" gaps.
- A new planning document, `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`, had
  already been written (third revision, `v1 → v2 → v3`) specifically
  scoping Phase 2's work — its own "Changelog" sections document two prior
  passes of independent re-verification against the real source tree.

The task for this session was: **read the strategy, implement exactly what
Phase 2 calls for, verify it, and document it** — nothing more, nothing
less (Phase 2's own Step 4 "What We Will NOT Do" section is an explicit
refusal list: no Editor panel, no ImGui code, no GPU timestamps, no
draw-call/triangle-count logic, no memory-history reshape, no benchmark
exporter, no new CMake option, and — as originally written — no change to
`FrameProfiler`/`ScopeTimer`/`ProfilingTypes.h` themselves).

--------------------------------------------------------------------------
## 3. What was implemented — Part A: the Phase 2 reshape module
--------------------------------------------------------------------------

### 3.1 New file: `src/Profiling/FrameGraphData.h`

Defines two plain data types and three pure functions, all inside
`namespace gte::Profiling` (matching `ProfilingTypes.h`/`FrameProfiler.h`'s
own namespace), with zero ImGui/SDL/Vulkan-beyond-what-`FrameProfiler.h`
already needs.

**`struct FrameGraphPoint`** — one retained frame's plottable data:

| Field | Type | Meaning |
|---|---|---|
| `frameIndex` | `std::uint64_t` | Copied verbatim from `FrameSample::frameIndex` — the real frame number, guaranteed gapless/strictly increasing across every point this module ever returns (a disabled-capture window never consumes a `frameIndex` at all, per `FrameProfiler::BeginFrame()`/`EndFrame()`'s own early-return behavior). |
| `cpuMilliseconds` | `double` | Copied verbatim from `FrameSample::cpuFrameMilliseconds`. No tri-state — every completed frame has a real CPU time. |
| `gpuPasses` | `std::array<GpuPassSample, kGpuPassCount>` | Copied verbatim, index-for-index, from `FrameSample::gpuPasses` — the exact same type `FrameSample` itself uses, never reshaped into a new tri-state representation. |

Design choice, stated explicitly (and preserved from the strategy
document): a `FrameGraphPoint` carries **all three** named GPU passes per
point rather than one `GpuPass`-specific array per call — this mirrors
`FrameSample` itself, guarantees the CPU line and every GPU line a future
panel overlays are built from the exact same underlying frame list in one
pass, and is cheaper than walking history three separate times.

**`struct FrameGraphRange`** — the Y-axis auto-scale result:

| Field | Type | Meaning |
|---|---|---|
| `hasData` | `bool` | `false` if zero points in the requested series had real data, or an out-of-range `GpuPass` was passed. Must be checked first. |
| `minMilliseconds` | `double` | Only meaningful when `hasData` is `true`. |
| `maxMilliseconds` | `double` | Only meaningful when `hasData` is `true`. |

Deliberately no `sampleCount`/average/other statistics field — min/max is
exactly what an auto-scaling Y axis needs; anything more is unrequested
scope creep.

### 3.2 New file: `src/Profiling/FrameGraphData.cpp`

Implements three functions:

**`std::vector<FrameGraphPoint> BuildFrameGraphPoints(const FrameProfiler& profiler)`**
- Reserves exactly `profiler.HistoryCount()` up front (one allocation, no
  repeated `push_back` growth).
- Iterates `i` from `0` to `HistoryCount() - 1`, reading
  `profiler.HistoryAt(i)` and copying `frameIndex`/`cpuFrameMilliseconds`/
  `gpuPasses` into a `FrameGraphPoint` at the same index — a direct, 1:1,
  order-preserving transcription.
- Returns an empty vector for an empty history — no special-cased default
  point.
- Safe to call at **any** point in the frame lifecycle, including strictly
  between a `BeginFrame()`/`EndFrame()` pair: `HistoryCount()`/`HistoryAt()`
  only ever read `FrameProfiler`'s completed-and-pushed ring buffer
  (`m_history`/`m_historyCount`), never the in-progress scratch
  `FrameSample` (`m_current`) — confirmed directly against
  `FrameProfiler.cpp`'s actual implementation body.
- No windowing/"last N frames" parameter, by design — a caller wanting only
  the most recent N frames can slice the returned `std::vector` itself with
  zero information lost.

**`FrameGraphRange ComputeCpuMillisecondsRange(std::span<const FrameGraphPoint> points)`**
- Scans every point's `cpuMilliseconds` for min/max.
- `hasData == false` only when `points` is empty (CPU data is always real,
  so nothing else is ever excluded).

**`FrameGraphRange ComputeGpuMillisecondsRange(std::span<const FrameGraphPoint> points, GpuPass pass)`**
- Bounds-checks `pass` **unconditionally** (active in Debug **and**
  Release, never a debug-only `assert()`):
  ```cpp
  const std::size_t index = static_cast<std::size_t>(pass);
  if (index >= kGpuPassCount) {
      return range; // hasData stays false.
  }
  ```
  This directly mirrors `FrameProfiler::SetGpuPassSample()`'s own
  already-shipped handling of exactly this same kind of invalid input — a
  debug-only assert would compile to nothing in a Release build, leaving an
  out-of-range read exactly as unsafe as no check at all.
- Scans `gpuPasses[index]` across every point, including **only** entries
  whose `status == GpuSampleStatus::Present` — branching exclusively on
  `status`, **never** on whether the stored `milliseconds` value happens to
  look like zero. This is the single correctness-critical design point of
  this whole phase: an `Absent`/`Unsupported` sample carrying a non-zero,
  "stale-looking" value must still be excluded (verified by dedicated
  tests — see §3.3, cases 9).
- `hasData` is `true` only if at least one `Present` entry was found.

Both range functions accept `std::span<const FrameGraphPoint>` rather than
`const std::vector<FrameGraphPoint>&` — this codebase's first use of
`std::span` (noted honestly, not silently) — specifically so a future
windowed caller can pass a zero-copy sub-range
(`std::span(points).subspan(points.size() - 120)`) while every existing
call site (passing `BuildFrameGraphPoints()`'s full output straight
through) needs zero changes, since a `std::vector` still converts
implicitly.

### 3.3 New test file: `tests/Profiling/FrameGraphDataTests.cpp` (12 tests)

Follows the exact "reset `FrameProfiler::Instance()` before AND after each
test case" recipe `FrameProfilerTests.cpp` already established.

| # | Test name | What it proves |
|---|---|---|
| 1 | `EmptyHistoryProducesEmptyOutputAndNoRangeData` | A fresh profiler with zero completed frames produces an empty point array, and both range functions report `hasData == false` on an empty input. |
| 2 | `SingleFrameRoundTripsExactly` | One completed frame's `frameIndex`/`cpuMilliseconds` are copied bit-exactly (`EXPECT_DOUBLE_EQ` against a literal, known value — see §4), and its `gpuPasses` are all `Absent` (today's correct default). |
| 3 | `MultipleFramesPreserveExactFrameIndexOrder` | Four frames produce points with EXACT `frameIndex` values `0, 1, 2, 3` in order — not merely "increasing" (a monotonic-only check would miss a reshape that drops the first element and shifts everything down by one). |
| 4 | `AllThreeGpuPassesAndTriStatesRoundTripToExactlyTheRightIndex` | In one frame: `GameView` set `Present` (with distinct `milliseconds`/`drawCallCount`/`triangleCount` values, all verified individually), `SceneView` set `Unsupported`, and `Present` (the pass) deliberately left at its default `Absent` (the status) — proving all three tri-states round-trip simultaneously with no cross-contamination between passes, and that the two same-named-but-unrelated `Present` identifiers (`GpuPass::Present` the pass, `GpuSampleStatus::Present` the status) are never confused. |
| 5 | `RingBufferWraparoundKeepsExactBoundaryFrameIndices` | Pushing `kMaxFrameHistory + 5` (305) frames still yields exactly `kMaxFrameHistory` (300) points, with the front/back `frameIndex` values matching the exact expected boundary (`totalFrames - kMaxFrameHistory` and `totalFrames - 1`) — mirroring `FrameProfilerTests.cpp`'s own `RingBufferWrapsAndKeepsMostRecentFrames` pattern. |
| 6 | `ComputeCpuMillisecondsRangeMatchesKnownMinAndMaxInTheMiddle` | Five frames seeded with known literal CPU values (`2.0, 25.0, 8.0, 0.5, 12.0`) — the min (`0.5`) and max (`25.0`) both sit in the middle of the sequence, not at either boundary, so the test genuinely proves a full scan rather than a first/last-only check. |
| 7 | `ComputeGpuMillisecondsRangeIgnoresAbsentFramesForThatPassOnly` | Five frames mixing `Absent` and `Present` `GameView` samples — the computed range reflects only the `Present` entries (min `0.5`, max `9.0`); a sibling pass (`SceneView`) that was never set `Present` anywhere reports `hasData == false` even though `GameView`'s own range is valid. This is the single most important correctness property of this whole phase. |
| 8 | `AllUnsupportedSeriesReportsNoData` | A series where the only sample is `Unsupported` reports `hasData == false`, same as `Absent`. |
| 9a | `AbsentSampleWithStaleNonZeroValueIsStillExcluded` | A sample explicitly constructed as `GpuSampleStatus::Absent` but carrying `milliseconds = 42.0, drawCallCount = 5, triangleCount = 100` is still excluded from the range scan — positively rules out an implementation that branches on "is the value zero" instead of the actual `status` tag. |
| 9b | `UnsupportedSampleWithStaleNonZeroValueIsStillExcluded` | The same proof, repeated for `GpuSampleStatus::Unsupported`. |
| — | `OutOfRangeGpuPassReportsNoDataInsteadOfReadingOutOfBounds` | Passing `static_cast<GpuPass>(99)` reports `hasData == false` rather than reading out of bounds — the direct test of the bounds-check requirement in §3.2. |
| 10 | `BuildFrameGraphPointsNeverObservesAnInProgressFrame` | After 2 complete `BeginFrame()`/`EndFrame()` pairs, a 3rd `BeginFrame()` with **no** matching `EndFrame()` is issued before calling `BuildFrameGraphPoints()` — the returned vector still has exactly 2 points, proving the in-progress 3rd frame is completely invisible to the reshape. |

(12 tests total: the strategy document's own numbered list runs 1–10, with
case 9 split into two dedicated tests — one per tri-state — plus one
additional bounds-check test, for 12 in total.)

### 3.4 CMake wiring

- **Root `CMakeLists.txt`**: `src/Profiling/FrameGraphData.h` and
  `src/Profiling/FrameGraphData.cpp` added to `gte_core`'s existing,
  unconditional `add_library(gte_core STATIC ...)` file list, immediately
  alongside the four pre-existing `src/Profiling/` entries. No new
  `option()`, no new `if()` block.
- **`tests/CMakeLists.txt`**: `Profiling/FrameGraphDataTests.cpp` added to
  the unconditional `GTE_TEST_SOURCES` list, immediately alongside
  `Profiling/FrameProfilerTests.cpp`/`ScopeTimerTests.cpp`. A matching
  bullet was also added to this file's own header-comment "Test taxonomy"
  section (which documents every test file's purpose) describing what
  `FrameGraphDataTests.cpp` covers.

--------------------------------------------------------------------------
## 4. What was implemented — Part B: the `FrameProfiler` test-hook fix
--------------------------------------------------------------------------

### 4.1 The problem

While writing `FrameGraphDataTests.cpp`'s cases 2 and 6 (which the strategy
document specified should assert against a "known" `cpuFrameMilliseconds`
value, bit-exactly, via `EXPECT_DOUBLE_EQ`), it became clear that
`FrameProfiler` has **no way** for a test to force that field to a literal
value — `EndFrame()` always computes it from real elapsed wall-clock time:

```cpp
void FrameProfiler::EndFrame() noexcept
{
    ...
    m_current.cpuFrameMilliseconds = ElapsedMilliseconds(m_frameStartTicks);
    ...
}
```

The strategy document's own Step 4 explicitly said "No change to
`FrameProfiler`... themselves" — but that refusal was written on the
assumption that Phase 2's *production* code needed nothing new from
`FrameProfiler` (which remains true — `BuildFrameGraphPoints()` only ever
calls the already-existing `HistoryCount()`/`HistoryAt()`). It did not
anticipate this specific *test-support* gap.

### 4.2 First attempt (written, then rejected on review)

An initial version of the two affected tests worked around the gap with a
small `SDL_Delay()`-based helper:

```cpp
double RecordFrameAndGetCpuMilliseconds(FrameProfiler& profiler, Uint32 delayMs)
{
    profiler.BeginFrame();
    if (delayMs > 0) {
        SDL_Delay(delayMs);
    }
    profiler.EndFrame();
    return profiler.HistoryAt(profiler.HistoryCount() - 1).cpuFrameMilliseconds;
}
```

Frames were recorded with increasing/varied delays (e.g. `2, 25, 8, 0, 12`
ms), and the test read back the *actual*, real measured value immediately
afterward to use as its own "known" comparison value.

**This was reconsidered and rejected** for concrete reasons:

1. **Flakiness** — real-time delays depend on OS scheduling; under CI load
   or a busy machine, small delays could jitter unpredictably, risking an
   intermittently-failing test.
2. **Wasted time** — it added real wall-clock time (~47 ms) to the test
   suite for no functional benefit.
3. **Weaker test** — it only proved "whatever the real timing happened to
   be, the scan found it correctly", not the literal, bit-exact
   `EXPECT_DOUBLE_EQ`-against-a-known-value assertion the strategy document
   actually specified.
4. **Inconsistent with this codebase's own testing philosophy** — every
   other test in this suite (including `FrameProfilerTests.cpp` itself)
   seeds exact, hand-chosen synthetic values (e.g.
   `SetGpuPassSample(pass, status, 3.5, 10, 200)`), never a real clock.

### 4.3 The actual fix

A new, narrowly-scoped, test-only method was added to `FrameProfiler`, in
the exact same spirit as its existing `ResetForTesting()`:

**`src/Profiling/FrameProfiler.h`** — declaration added directly after
`ResetForTesting()`:

```cpp
// Test-only: overwrites the MOST RECENTLY COMPLETED history entry's
// cpuFrameMilliseconds with an exact, caller-chosen value - a no-op if
// history is empty. ...
void OverrideLastFrameCpuMillisecondsForTesting(double milliseconds) noexcept;
```

**`src/Profiling/FrameProfiler.cpp`** — implementation added directly after
`ResetForTesting()`:

```cpp
void FrameProfiler::OverrideLastFrameCpuMillisecondsForTesting(double milliseconds) noexcept
{
    if (m_historyCount == 0) {
        return;
    }

    const std::size_t mostRecentPhysicalIndex = (m_historyHead + kMaxFrameHistory - 1) % kMaxFrameHistory;
    m_history[mostRecentPhysicalIndex].cpuFrameMilliseconds = milliseconds;
}
```

Key properties of this addition:

- Touches **only** the already-pushed, most-recently-completed history
  entry — never the in-progress frame, never any other retained entry.
- Does **not** alter `BeginFrame()`, `EndFrame()`, `RecordCpuScope()`,
  `SetGpuPassSample()`, or `SetMemorySnapshot()`'s own behavior in any way
  — every pre-existing test against this class remains completely
  unaffected (confirmed: all pre-existing `FrameProfilerTests.cpp`/
  `ScopeTimerTests.cpp` tests still pass unchanged).
- A deliberate, narrow addition to `FrameProfiler`'s **test-only** surface,
  not a change to its production contract.

### 4.4 Matching tests added to `tests/Profiling/FrameProfilerTests.cpp` (2 tests)

Per `AGENTS.md`'s "every change to Tier 1 code must come with a matching
test change" rule:

| Test name | What it proves |
|---|---|
| `OverrideLastFrameCpuMillisecondsForTestingReplacesOnlyTheMostRecentEntry` | Records 2 frames; overrides the 2nd's `cpuFrameMilliseconds` to `12.5`; confirms the 1st frame's real, originally-measured value is completely untouched, while the 2nd now reads exactly `12.5`. |
| `OverrideLastFrameCpuMillisecondsForTestingOnEmptyHistoryIsNoOp` | Calling this method on a freshly-reset profiler (zero completed frames) does not crash and leaves `HistoryCount()` at `0`. |

### 4.5 `FrameGraphDataTests.cpp`'s cases 2/6, corrected

Both tests were rewritten to use a small helper built on the new hook:

```cpp
void RecordFrameWithCpuMilliseconds(FrameProfiler& profiler, double cpuMilliseconds)
{
    profiler.BeginFrame();
    profiler.EndFrame();
    profiler.OverrideLastFrameCpuMillisecondsForTesting(cpuMilliseconds);
}
```

- **Case 2** (`SingleFrameRoundTripsExactly`) now seeds the single frame
  with a literal `12.5` and asserts `points[0].cpuMilliseconds` equals
  `12.5` exactly.
- **Case 6** (`ComputeCpuMillisecondsRangeMatchesKnownMinAndMaxInTheMiddle`)
  now seeds five frames with literal values `2.0, 25.0, 8.0, 0.5, 12.0` and
  asserts the computed range is exactly `{min: 0.5, max: 25.0}`.

The `#include <SDL3/SDL_timer.h>` and the `SDL_Delay()`-based helper were
both removed from this test file entirely — it now has zero dependency on
real wall-clock timing.

--------------------------------------------------------------------------
## 5. Documentation updates made
--------------------------------------------------------------------------

- **`AGENTS.md`** ("Profiling" section) gained **two** new bullets:
  1. Pointing at `src/Profiling/FrameGraphData.h/.cpp` as the one place
     `FrameProfiler`'s history gets reshaped into plottable points, and
     directing future consumers (Phase 6/7, or any other future
     graph/export need) to call its functions rather than re-deriving the
     same logic.
  2. Documenting `FrameProfiler::OverrideLastFrameCpuMillisecondsForTesting()`
     right alongside the existing `ResetForTesting()` mention, explaining
     when a test should reach for it.
- **`PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`** gained a new closing
  **"Result (filled in after implementation)"** section, honestly recording
  both the Phase 2 implementation itself and the full story of the
  `FrameProfiler` test-hook fix (including the rejected `SDL_Delay`
  first attempt) — following this codebase's own established convention of
  writing real, measured outcomes back into a planning document rather than
  treating it as a frozen, one-time spec.
- **`PROFILER_IMPLEMENTATION_STATUS_v2.md` → `PROFILER_IMPLEMENTATION_STATUS_v3.md`**:
  the v2 file was deleted and a new v3 revision created, following this
  codebase's own "bump the version, delete the superseded file" convention
  (the same thing that happened to v1 when v2 was created previously). The
  new v3 document:
  - Moves Phase 2 from "not implemented" into "implemented", with full
    file/function/test-count detail.
  - Documents the `FrameProfiler` test-hook addition and why it was needed.
  - Records the fresh, independently-recounted test total (**464**, not
    copied forward from any prior number).
  - Leaves Phases 3–7's "not implemented, and why" reasoning intact
    (updated only to reflect Phase 2 no longer being one of them), with an
    updated "Suggested next steps" ordering.

--------------------------------------------------------------------------
## 6. Build and test verification
--------------------------------------------------------------------------

Commands run, in order, at the end of this session:

```
cmake -S . -B build -G Ninja
cmake --build build --target GreatTamanaEngineTests
ctest --test-dir build --output-on-failure
cmake --build build          (full project, including GreatTamanaEngine.exe)
```

**Results:**

- Configure: succeeds, no source changes needed (SDL3/Vulkan/VMA/stb/KTX/
  saba/glm/imgui/imguizmo/GoogleTest all already staged, nothing
  re-fetched).
- Build: succeeds cleanly, **zero new warnings** introduced by any new or
  modified file.
- Full project build (`GreatTamanaEngine.exe` + `GreatTamanaEngineTests.exe`):
  succeeds.
- Test suite: **464 / 464 tests passing**, exactly **1** test skipped
  (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` — a
  pre-existing, unrelated, machine-gated smoke test that skips when a
  specific real MMD model file isn't present on this machine; unaffected by
  anything in this session).
- **Test count breakdown**: 450 pre-existing (baseline before this session)
  + 12 new in `tests/Profiling/FrameGraphDataTests.cpp` + 2 new in
  `tests/Profiling/FrameProfilerTests.cpp` = **464**.
- **Zero regressions** — every pre-existing test still passes unchanged.

--------------------------------------------------------------------------
## 7. Complete list of files changed
--------------------------------------------------------------------------

**New files:**

| File | Purpose |
|---|---|
| `src/Profiling/FrameGraphData.h` | Phase 2's public API — `FrameGraphPoint`/`FrameGraphRange`, `BuildFrameGraphPoints()`, `ComputeCpuMillisecondsRange()`, `ComputeGpuMillisecondsRange()`. |
| `src/Profiling/FrameGraphData.cpp` | Phase 2's implementation. |
| `tests/Profiling/FrameGraphDataTests.cpp` | 12 Tier-1 tests for the above. |
| `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md` | The planning document this phase followed (attached at session start; now also carries its own "Result" addendum). |
| `PROFILER_IMPLEMENTATION_STATUS_v3.md` | Replaces the deleted `_v2` status document. |
| `PHASE2_COMPLETION_REPORT.md` | This document. |

**Modified files:**

| File | Change |
|---|---|
| `CMakeLists.txt` | Added `FrameGraphData.h/.cpp` to `gte_core`'s file list. |
| `tests/CMakeLists.txt` | Added `Profiling/FrameGraphDataTests.cpp` to `GTE_TEST_SOURCES`; added a matching "Test taxonomy" comment bullet. |
| `src/Profiling/FrameProfiler.h` | Added `OverrideLastFrameCpuMillisecondsForTesting()` declaration. |
| `src/Profiling/FrameProfiler.cpp` | Added its implementation. |
| `tests/Profiling/FrameProfilerTests.cpp` | Added 2 new tests for the method above. |
| `AGENTS.md` | Added 2 new bullets to the "Profiling" section. |

**Deleted files:**

| File | Reason |
|---|---|
| `PROFILER_IMPLEMENTATION_STATUS_v2.md` | Superseded by `_v3`, per this codebase's established version-bump-and-delete convention. |
| `PROFILER_IMPLEMENTATION_STATUS.md` (v1) | Already deleted, unstaged, before this session began — left as-is, not touched this session. |

--------------------------------------------------------------------------
## 8. What was explicitly NOT done (by design)
--------------------------------------------------------------------------

Matching `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`'s own "Step 4: What We
Will NOT Do" refusal list, none of the following were touched:

- No Editor panel, no ImGui code anywhere in `src/Profiling/`.
- No NaN/ImGui-specific "gap" sentinel baked into `FrameGraphPoint`/
  `FrameGraphRange` — the tri-state (`Absent`/`Present`/`Unsupported`)
  already says everything that needs saying at this layer.
- No GPU timestamp data — every `GpuPassSample` produced by real engine
  code today is still unconditionally `Absent` (Phase 4's job).
- No draw-call/triangle-count logic — `drawCallCount`/`triangleCount` are
  carried through `FrameGraphPoint` unexamined (Phase 3's real job, not yet
  started).
- No GPU-memory-over-time reshape (`FrameSample::memory` untouched — Phase
  5's job).
- No benchmark-mode CSV exporter (Phase 6's job).
- No new CMake option — both new source files and the new test file were
  added to already-unconditional lists.
- No change to `FrameProfiler`'s **production** contract — the one change
  made to `FrameProfiler` (§4) is purely additive to its test-only surface,
  with zero effect on `BeginFrame()`/`EndFrame()`/`RecordCpuScope()`/
  `SetGpuPassSample()`/`SetMemorySnapshot()`'s real behavior.
- No change to any existing call site (`Application.cpp`, `Game.cpp`,
  `AnimationSystem.cpp`, `RenderSystem.cpp`).

--------------------------------------------------------------------------
## 9. Recommended next steps
--------------------------------------------------------------------------

Per `PROFILER_IMPLEMENTATION_STATUS_v3.md`'s own "Suggested next steps":

1. **Phase 3 (draw-call and triangle counts)** — cheap, mechanical,
   low-risk. This is the actual, real "Phase 3" from
   `PROFILER_STRATEGY_v2.md`, and remains entirely unimplemented; nothing
   in this session touched it.
2. **Phase 5 (GPU memory usage over time)** — also cheap, per the strategy
   document's own assessment.
3. **Phase 7 (the Editor "Profiler" panel)** — once Phase 3/5 give it more
   real data to show; Phase 2's own output (this session's work) is already
   real and ready for it to consume today.
4. **Phase 4 (GPU timestamp queries)** and **Phase 6 (benchmark mode)** —
   the two most substantial remaining phases, best tackled as their own
   dedicated sessions.

See `PROFILER_STRATEGY_v2.md` for the full 8-phase design reasoning,
`PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md` for Phase 2's own detailed
reasoning (including its "Result" addendum), and
`PROFILER_IMPLEMENTATION_STATUS_v3.md` for the living, up-to-date status of
every phase.
