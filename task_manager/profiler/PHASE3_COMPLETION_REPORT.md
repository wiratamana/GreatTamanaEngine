# Phase 3 Completion Report — Draw-Call and Triangle Counts

Status: FINAL — this document is a complete, self-contained record of
everything actually implemented, tested, and verified in this working
session on branch `feature/profiler-impl`.

--------------------------------------------------------------------------
## 1. Executive summary
--------------------------------------------------------------------------

This session implemented **Phase 3** of the 8-phase profiler plan
(`PROFILER_STRATEGY_v2.md`), following its own dedicated planning document,
`PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md`, step by step. Phase 3
turns "how many `vkCmdDraw`/`vkCmdDrawIndexed` calls, and how many
triangles" into real, measured, per-`GpuPass` data sitting in
`FrameProfiler`'s ring buffer every frame — reachable by a unit test, a
throwaway diagnostic, or (already, with zero further work) Phase 2's own
`FrameGraphData.h`.

Per the strategy document's own explicit review-driven correction (the
"v1 -> v2" changelog at the top of that document), the accumulation is
**fused directly into `FrameRecorder::RecordFrame()`'s existing per-item
draw loop** rather than built as a separate pre-pass over the queue — this
makes "what was counted" and "what was actually drawn" structurally
impossible to diverge, rather than something a future edit could silently
break. The strategy document's own flagged design gap —
`GpuPassSample`'s single combined `status` field, which would have let
Phase 3 falsely imply Phase 4's (not-yet-existing) GPU timing was also
measured — was fixed in the same change, splitting it into independent
`timingStatus`/`countStatus` tri-states.

**End state**: one new always-compiled, Vulkan-free file pair
(`src/Renderer/DrawStats.h/.cpp`), a corrected `GpuPassSample` data model,
`FrameRecorder`/`FramePresenter`/`Renderer`'s `Present()`/`RenderOffscreen()`
all threading a real `DrawStats` result back up to `Application::Run()`,
11 new tests (7 in a new `tests/Renderer/DrawStatsTests.cpp`, 4 net new in
`tests/Profiling/`), every existing CMake list updated with no new build
option introduced, and full documentation updates. A clean build and a
fresh `ctest` run both succeed: **474/475 tests passing**, 1 pre-existing,
unrelated, machine-gated smoke test skipped, zero regressions.

--------------------------------------------------------------------------
## 2. Background and starting point
--------------------------------------------------------------------------

- `feature/profiler-impl` already had **Phase 0/1** (the profiling data
  model + CPU scope timers) and **Phase 2** (the frame-time graph data
  reshape) fully implemented and tested, per
  `PROFILER_IMPLEMENTATION_STATUS_v3.md`.
- The full test suite stood at **464 tests passing**, 1 skipped.
- Phases 3 through 7 were all explicitly "not implemented" gaps.
- Two planning documents existed for this phase,
  `PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v1.md` and its own reviewed
  revision `..._v2.md` — v2's own changelog documents exactly what changed
  from v1 after a "read it again before writing code" review pass (the
  fused-accumulation correction, the `TESTING.md` deliverable, the
  "re-verify every line number" caveat, and the two explicit documented
  assumptions).

The task for this session was: **read the (v2) strategy, implement exactly
what Phase 3 calls for, verify it, and document it**.

--------------------------------------------------------------------------
## 3. What was implemented
--------------------------------------------------------------------------

### 3.1 `src/Renderer/DrawStats.h/.cpp` (new)

- `DrawStats` — plain `{drawCallCount, triangleCount}`.
- `AccumulateDrawStats(DrawStats&, bool hasIndexBuffer, vertexCount, indexCount) noexcept`
  — the production entry point, called inline from inside
  `FrameRecorder::RecordFrame()`'s existing loop.
- `CountDrawStats(std::span<const CountableDrawItem>) noexcept` — a
  test-only batch wrapper.
- Added to `gte_core`'s unconditional file list (root `CMakeLists.txt`),
  right after `FrameRecorder.h` — no `GTE_ENABLE_EDITOR`/
  `GTE_ENABLE_PROFILER` gate.
- Deliberately **not** gated behind `#if GTE_ENABLE_PROFILER` (documented
  reasoning directly in the header): `m_drawQueue` is already iterated
  unconditionally every frame regardless of that switch.

### 3.2 `ProfilingTypes.h` — the `timingStatus`/`countStatus` split

`GpuPassSample`'s single `status` field was replaced with two independent
fields, `timingStatus` (governs `milliseconds`) and `countStatus` (governs
`drawCallCount`/`triangleCount`) — see that file's own updated comment for
the full reasoning. Safe to do with zero production-behavior risk, since
the old `SetGpuPassSample()` had zero production callers before this
session.

### 3.3 `FrameProfiler.h/.cpp` — two focused setters replace one combined one

`SetGpuPassSample(pass, status, ms, drawCalls, triangles)` was replaced by:

- `SetGpuPassTiming(pass, status, milliseconds = 0.0)`
- `SetGpuPassDrawStats(pass, status, drawCallCount = 0, triangleCount = 0)`

Each writes only its own half of `GpuPassSample`, with the exact same
`m_captureEnabled`/`m_frameInProgress`/bounds-check guard the old combined
setter already had.

### 3.4 `FrameGraphData.cpp` — a pure rename

`ComputeGpuMillisecondsRange()`'s one branch changed from
`sample.status` to `sample.timingStatus` — zero behavioral change, verified
by every pre-existing test in `FrameGraphDataTests.cpp` still passing
unchanged (after migrating its own `SetGpuPassSample(...)` call sites to
the new setters).

### 3.5 `FrameRecorder::RecordFrame()` — fused accumulation + return value

Signature changed from `void` to `DrawStats`. A single `DrawStats
drawStats;` is declared once, before the existing per-item loop; one
`AccumulateDrawStats(...)` call was added immediately after the existing
`vkCmdDraw`/`vkCmdDrawIndexed` branch, inside the same loop iteration —
confirmed (by re-reading the current loop body first, per the strategy
document's own instruction) that no skip/`continue` condition exists
between popping an item and issuing its draw call. The queue-clearing
contract (`m_drawQueue.clear()` right after the loop) is untouched.

### 3.6 `FramePresenter`/`Renderer` — two different return shapes, deliberately

- `FramePresenter::Present()` now returns `std::optional<DrawStats>` —
  `std::nullopt` on each of its three existing early-return paths
  (minimized window, still-pending resize, `VK_ERROR_OUT_OF_DATE_KHR`), a
  real `DrawStats` on success.
- `FramePresenter::RenderOffscreen()` now returns a plain `DrawStats` (no
  early-return path exists in it today — re-verified against the current
  source before making the change).
- `Renderer::Present()`/`RenderOffscreen()` mirror the same two shapes,
  each a one-line forward to `FramePresenter`'s equivalent.
- Confirmed non-breaking for the two existing bare-statement callers,
  `src/Editor/AssetPreviewMesh.cpp` and `src/Editor/BoneViewerWindow.cpp`
  (both discard the return value already), via a fresh repository-wide
  search for `RenderOffscreen(`/`.Present(` before and after the change.

### 3.7 `Application::Run()` — the one new production caller

Right after each `RenderOffscreen()`/`Present()` call, the returned
`DrawStats` is reported via `SetGpuPassDrawStats(...)` tagged with the
matching `GpuPass` (`GameView`/`SceneView`/`Present`) — the `Present` pass's
report is skipped entirely when the result is `std::nullopt`, leaving its
`countStatus` at the correct default `Absent` rather than fabricating a
`Present` with `{0, 0}`. Deliberately **not** wrapped in
`#if GTE_ENABLE_PROFILER`, matching this same file's own pre-existing,
unguarded `FrameProfiler::Instance().BeginFrame()`/`EndFrame()` calls.

--------------------------------------------------------------------------
## 4. Testing
--------------------------------------------------------------------------

- **`tests/Renderer/DrawStatsTests.cpp` (7 tests, new)** — empty queue, one
  non-indexed draw, one indexed draw, multiple mixed draws, a count not
  evenly divisible by 3 (truncates), a degenerate zero-vertex/zero-index
  draw (still counts the draw call), and a case proving the inline
  accumulator and the batch wrapper agree exactly over an equivalent list.
- **`tests/Profiling/FrameProfilerTests.cpp`** — migrated every
  `SetGpuPassSample(...)` call site to the two new setters; the
  default-value test now asserts both `timingStatus` and `countStatus`;
  added `SetGpuPassTimingNeverTouchesCountStatusOrViceVersa` and the
  strategy document's own specified regression test,
  `DrawStatsAloneDoNotImplyRealTimingData`.
- **`tests/Profiling/FrameGraphDataTests.cpp`** — migrated every
  `SetGpuPassSample(...)` call site; added
  `DrawStatsOnlyPassReportsNoTimingData`, proving
  `ComputeGpuMillisecondsRange()` genuinely branches on `timingStatus`
  only, never `countStatus`.
- A repository-wide search for `SetGpuPassSample(` confirms zero remaining
  references anywhere in `src/` or `tests/`.
- **Verified**: full clean build (`cmake -S . -B build -G Ninja` then
  `cmake --build build`) succeeds with zero errors, and a fresh `ctest` run
  reports **474/475 passing, 1 skipped** (the same pre-existing,
  unrelated, machine-gated `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`).
  Test count: 464 (baseline before this session) + 7
  (`DrawStatsTests.cpp`) + 3 net new (`FrameProfilerTests.cpp`) + 1 new
  (`FrameGraphDataTests.cpp`) = 475.

--------------------------------------------------------------------------
## 5. Documentation updates made
--------------------------------------------------------------------------

- **`AGENTS.md`** ("Profiling" section) gained three new bullets: the
  `timingStatus`/`countStatus` split and the rule it enforces,
  `src/Renderer/DrawStats.h/.cpp` and why `AccumulateDrawStats()` is called
  inline, and an update to the pre-existing "GPU-side measurement" bullet
  noting draw-call/triangle counts are now real, wired-up data as of this
  phase.
- **`TESTING.md`** gained a bullet for `Renderer/DrawStatsTests.cpp`
  alongside `Renderer/ResourcePoolTests.cpp`, and — closing a pre-existing
  gap this phase's own strategy document called out — three new bullets
  for `tests/Profiling/*` (`FrameProfilerTests.cpp`/`ScopeTimerTests.cpp`/
  `FrameGraphDataTests.cpp`), which had never been documented there despite
  existing since Phase 0/1/2.
- **`PROFILER_IMPLEMENTATION_STATUS_v3.md` → `PROFILER_IMPLEMENTATION_STATUS_v4.md`**:
  the v3 file was deleted and a new v4 revision created, following this
  codebase's own "bump the version, delete the superseded file" convention.

--------------------------------------------------------------------------
## 6. Build and test verification
--------------------------------------------------------------------------

Commands run, in order, at the end of this session:

```
cmake -S . -B build -G Ninja
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

**Results:**

- Configure: succeeds, no new dependency downloads needed.
- Build: succeeds cleanly, all 27 build steps complete with no errors.
- Test suite: **474 / 475 tests passing**, exactly **1** test skipped
  (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` —
  unaffected by anything in this session).
- **Zero regressions** — every pre-existing test still passes.

--------------------------------------------------------------------------
## 7. Complete list of files changed
--------------------------------------------------------------------------

**New files:**

| File | Purpose |
|---|---|
| `src/Renderer/DrawStats.h` | `DrawStats`/`AccumulateDrawStats()`/`CountDrawStats()`. |
| `src/Renderer/DrawStats.cpp` | `CountDrawStats()`'s implementation. |
| `tests/Renderer/DrawStatsTests.cpp` | 7 Tier-1 tests for the above. |
| `PROFILER_IMPLEMENTATION_STATUS_v4.md` | Replaces the deleted `_v3` status document. |
| `PHASE3_COMPLETION_REPORT.md` | This document. |

**Modified files:**

| File | Change |
|---|---|
| `CMakeLists.txt` | Added `DrawStats.h/.cpp` to `gte_core`'s file list. |
| `tests/CMakeLists.txt` | Added `Renderer/DrawStatsTests.cpp` to `GTE_TEST_SOURCES`; updated its "Test taxonomy" comment. |
| `src/Profiling/ProfilingTypes.h` | Split `GpuPassSample::status` into `timingStatus`/`countStatus`. |
| `src/Profiling/FrameProfiler.h/.cpp` | Replaced `SetGpuPassSample()` with `SetGpuPassTiming()`/`SetGpuPassDrawStats()`. |
| `src/Profiling/FrameGraphData.h/.cpp` | Renamed `.status` reads to `.timingStatus`. |
| `src/Renderer/FrameRecorder.h/.cpp` | `RecordFrame()` now returns `DrawStats`, accumulated inline in its existing loop. |
| `src/Renderer/FramePresenter.h/.cpp` | `Present()` returns `std::optional<DrawStats>`; `RenderOffscreen()` returns `DrawStats`. |
| `src/Renderer/Renderer.h/.cpp` | Mirrors the same two return shapes, forwarding to `FramePresenter`. |
| `src/Application/Application.cpp` | New `SetGpuPassDrawStats(...)` calls for `GameView`/`SceneView`/`Present`. |
| `tests/Profiling/FrameProfilerTests.cpp` | Migrated to the new setters; 3 net new tests. |
| `tests/Profiling/FrameGraphDataTests.cpp` | Migrated to the new setters; 1 new test. |
| `AGENTS.md` | 3 new bullets in "Profiling". |
| `TESTING.md` | New bullets for `DrawStatsTests.cpp` and `tests/Profiling/*`. |

**Deleted files:**

| File | Reason |
|---|---|
| `PROFILER_IMPLEMENTATION_STATUS_v3.md` | Superseded by `_v4`. |

--------------------------------------------------------------------------
## 8. What was explicitly NOT done (by design)
--------------------------------------------------------------------------

Matching `PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md`'s own "Step 4:
What We Will NOT Do" refusal list:

- No GPU timestamp queries/`VkQueryPool` (Phase 4's job).
- No Editor "Profiler" panel, no ImGui code anywhere in this phase.
- No per-draw-call GPU timing, no per-material/per-mesh count breakdown.
- No attempt to count geometry drawn via a `recordExtra` callback (Dear
  ImGui's overlay, `AssetPreviewMesh`/`BoneViewerWindow`'s previews).
- No topology-awareness or instancing support beyond the two documented
  assumptions (triangle-list only, `instanceCount == 1`).
- No change to `Game`/`RenderSystem`/`MeshInstantiationSystem`/
  `AnimationSystem`/ECS in any way.
- No new CMake option.
- No attempt to preserve `SetGpuPassSample()`'s old combined signature.
- No `#if GTE_ENABLE_PROFILER` gate around `AccumulateDrawStats()`'s call
  site.

--------------------------------------------------------------------------
## 9. Recommended next steps
--------------------------------------------------------------------------

Per `PROFILER_IMPLEMENTATION_STATUS_v4.md`'s own "Suggested next steps":

1. **Phase 5 (GPU memory usage over time)** — cheap, per the strategy
   document's own assessment.
2. **Phase 7 (the Editor "Profiler" panel)** — now has both Phase 2's
   frame-time graph data and Phase 3's draw-call/triangle counts ready to
   consume.
3. **Phase 4 (GPU timestamp queries)** and **Phase 6 (benchmark mode)** —
   the two most substantial remaining phases, best tackled as their own
   dedicated sessions.

See `PROFILER_STRATEGY_v2.md` for the full 8-phase design reasoning,
`PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md` for Phase 3's own detailed
reasoning, and `PROFILER_IMPLEMENTATION_STATUS_v4.md` for the living,
up-to-date status of every phase.
