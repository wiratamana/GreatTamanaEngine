# B1_REAL_GPU_TIMING_COMPLETION_REPORT.md

## Summary

Implements `B1_REAL_GPU_TIMING_STRATEGY_v1.md` in full: `gte::rg::RenderGraph`
now measures genuine, driver-reported GPU execution time for every named
render-graph pass (`GameView`, `SceneView`, `Present` today, and any future
pass a later milestone declares), across both of this engine's execution
regimes (the synchronous offscreen Game+Scene regime and the pipelined
swapchain Present regime), using the exact same tri-state contract every
other real-data producer in this engine already honors
(`GpuTimingSample::Status::Present`/`Absent`/`Unsupported` — never a
fabricated `0.00 ms`).

Branch: `fix/profiler-render-graph-gpu-timing`.

## What changed

### New files

- **`src/Renderer/RenderGraph/RenderGraphTimestampPool.h/.cpp`** — a
  brand-new, dedicated class owning two independent, fixed-size
  `VkQueryPool`s (one per `ExecuteTimingMode` regime), keyed by an
  arbitrary, name-assigned slot index rather than a fixed 3-value enum.
  Reuses `gte::VulkanQueryPool` (`Vulkan/VulkanQueryPool.h`) as the actual
  RAII `VkQueryPool` owner for both regimes, and mirrors
  `GpuTimingService`'s own two-layer on/off gate (`GTE_ENABLE_PROFILER` at
  compile time, `SetCaptureEnabled()` at runtime) and per-call
  reset-then-write convention exactly. Deliberately a *new* class rather
  than an in-place extension of `GpuTimingService` — see "Milestone 4"
  below for why.

### Modified files

- **`src/Renderer/RenderGraph/RenderGraphNameSlotTable.h`** — added
  `NameAtSlot(std::int32_t slot)`, the exact inverse of `AssignOrGetSlot()`,
  plus three new Tier-1 tests in
  `tests/Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp`.
- **`src/Renderer/Renderer.h/.cpp`** — `Renderer::VulkanContextInfo` gained
  a `timestampCapability` field (`VulkanDevice::TimestampCapability()`,
  queried once already for `GpuTimingService`'s own construction), so
  `RenderGraph` can build its own `RenderGraphTimestampPool` against the
  same source of truth without needing direct access to `VulkanDevice`.
- **`src/Renderer/RenderGraph/RenderGraph.h/.cpp`** — the actual wiring:
  - Constructor now builds a `RenderGraphTimestampPool` from
    `Renderer::GetVulkanContextInfo()`.
  - `ExecuteCompiledGraph()`'s pass loop now writes a real
    begin/end timestamp pair around every surviving pass (bracketing its
    whole recorded body — barriers already applied, `vkCmdBeginRendering`/
    `execute()`/`vkCmdEndRendering` if any), keyed by
    `RenderGraphNameSlotTable::AssignOrGetSlot()`'s already-existing
    per-regime name→slot assignment (previously computed and discarded via
    `(void)`).
  - Pipelined-regime calls now open with a readback **preamble**: before
    compiling/recording anything, it reads back whatever was written into
    the current frame-in-flight `bufferIndex` `kGpuTimingFramesInFlight`
    frames ago (tracked via a new, per-`(slot, bufferIndex)` "has this
    exact slice ever been written" flag array — the same warm-up-flag
    idea Phase 4D used for a single slot, now generalized to
    `kPipelinedTimingSlotBudget × kGpuTimingFramesInFlight` independent
    ones), then increments its own `m_pipelinedFrameCounter` at the very
    end of the call (only on a call that actually ran — never on a frame
    `PresentViaRenderGraph()` skipped entirely).
  - New public `FinalizeSynchronousGpuTiming()` — called once by
    `Application::Run()` immediately after
    `Renderer::EndOffscreenRenderGraphRecording()` returns — reads back
    every timestamp written during the just-completed synchronous
    Execute() call (safe because that call already fence-waited).
  - New public `SetGpuTimingCaptureEnabled(bool)` — the runtime layer of
    the two-layer gate, forwarding straight to the timestamp pool.
  - `RecordStatsFor()` was split into `UpdateDrawStatsFor()` (drawStats
    only) and `UpdateTimingFor()` (timing only) — mirrors
    `Profiling::GpuPassSample`'s own `timingStatus`/`countStatus` split
    (see `AGENTS.md`, "Profiling"): drawStats is still written inline, per
    pass, in the same loop iteration that issues its draws; timing is
    written from two entirely separate call sites (`FinalizeSynchronousGpuTiming()`
    / the pipelined preamble) — neither may ever clobber the other's
    already-correct data with a stale default.
- **`src/Application/Application.cpp`** — two additions to `Run()`:
  `m_renderGraph.SetGpuTimingCaptureEnabled(...)` alongside the existing
  `m_renderer.SetGpuTimingCaptureEnabled(...)` call, and
  `m_renderGraph.FinalizeSynchronousGpuTiming();` immediately after
  `m_renderer.EndOffscreenRenderGraphRecording();`.
- **`CMakeLists.txt`** — added the two new source files to `gte_core`.
- **`RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`** — Section
  B, item 1 marked closed, referencing this report.

## A discovery that simplified this work (Milestone 3 was already done)

The strategy document's Step 3.10 (Milestone 3) assumed the Editor's
"Profiler" panel was still reading GPU timing from
`Renderer::LastGpuTiming(GpuTimingSlot)` and would need to be switched over
to `RenderGraph::LastKnownStatsFor(...)`. Re-reading the actual, current
`Application.cpp` before writing any code showed this bridge **already
exists**: for all three named passes, `Application::Run()` already reads
`m_renderGraph.LastKnownStatsFor("GameView"/"SceneView"/"Present").timing`
and feeds it into `Profiling::FrameProfiler::SetGpuPassTiming()`, which the
"Profiler" panel already displays. This was scaffolded during the Phase 7
migration specifically so `RenderGraph`'s own timing (permanently `Absent`
at the time) would flow through the instant something started actually
writing it — which is exactly what this change does. **No change to
`Panels/ProfilerPanel.cpp` or to `Application.cpp`'s GameView/SceneView/
Present blocks was needed at all** — both panels now show real, identical
numbers automatically, for free, the moment `RenderGraph` itself started
producing real data.

## Milestone 4 — the grep, and its finding

Per the strategy document's own Step 3.1/3.12 (Milestone 4), a full
repository grep was performed *before* writing the new class, specifically
to decide whether `GpuTimingService`'s fixed-enum machinery could safely be
left untouched:

```
findstr /s /n /c:"GpuTimingSlot::" src\*.cpp src\*.h
findstr /s /n /c:"LastGpuTiming" src\*.cpp src\*.h
findstr /s /n /c:"RenderOffscreen(" src\*.cpp   (excluding Renderer.cpp/.h/FramePresenter)
findstr /s /n /c:"renderer.Present(" /c:"m_renderer.Present(" /c:".Present(frameRecorder" src\*.cpp
```

Findings:

- `GpuTimingSlot::` only ever appears in comments and in
  `GpuTimingService.cpp/.h`'s own internal implementation — never at a
  production call site anywhere else.
- `Renderer::LastGpuTiming()` is defined (`Renderer.cpp`) and declared
  (`Renderer.h`) but has **zero callers anywhere in `src/`** — not even
  inside `Renderer`/`FramePresenter` themselves.
- `Renderer::RenderOffscreen()` (the one production entry point that can
  ever pass a *real* `GpuTimingSlot` into `GpuTimingService`) is called
  from exactly two places, `src/Editor/AssetPreviewMesh.cpp` and
  `src/Editor/BoneViewerWindow.cpp` — **both pass `std::nullopt`
  explicitly**, exactly as their own doc comments already required.
- `Renderer::Present()` (the legacy `FrameRecorder`-based swapchain path)
  has **zero callers anywhere** — `Application::Run()` uses
  `PresentViaRenderGraph()` exclusively, confirming
  `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`'s own Section
  B item 3 ("confirmed dead code but never deleted").

**Conclusion: `GpuTimingSlot::Offscreen0/Offscreen1/SwapchainPresent` have
zero remaining real (non-`nullopt`) production callers today.** This
confirmed the strategy document's own prediction (Step 2.4) and justified
building `RenderGraphTimestampPool` as a brand-new class rather than
risking a change to `GpuTimingService`'s already-shipped, Tier-2
(manually-verified) production code. Per the strategy document's own
explicit instruction, this finding is *not* acted on further here (no
deletion) — `GpuTimingService`/`VulkanQueryPool`/`GpuTimingSlot` remain
exactly as they were, still serving `AssetPreviewMesh`/`BoneViewerWindow`'s
independent previews unaffected. A future, separately-scoped cleanup pass
can act on this finding with full confidence, using this report as its
paper trail.

## Design decisions vs. the strategy document

Two deliberate, documented simplifications over the strategy document's
literal Step 3.3/3.5-3.7 text, both preserving every stated invariant:

1. **`RenderGraphTimestampPool::WriteBegin()` resets *and* writes in one
   call**, rather than the strategy document's separate `ResetRange()` +
   per-pass `WriteBegin()`/`WriteEnd()` shape. This is safe in *both*
   regimes because `RenderGraph::ExecuteCompiledGraph()` never reaches a
   given slot's `WriteBegin()` call until the caller has already,
   independently, proven (via a pre-existing fence wait it did not add for
   this purpose) that whatever that slot was last used for has fully
   completed — the synchronous regime's *previous* call already blocked on
   `EndOffscreenRenderGraphRecording()` before this frame's `Execute()`
   call could even begin recording; the pipelined regime's `bufferIndex`
   was already fence-waited on by `FramePresenter::PresentViaRenderGraph()`
   *before* it ever calls `RenderGraph::Execute()` at all. This mirrors
   `GpuTimingService::RecordOffscreenPassStart()`/`RecordPresentPassStart()`'s
   own already-proven reset-then-write convention exactly, and avoids a
   second, parallel "which slots need resetting this call" bookkeeping
   step.
2. **The `RenderGraphTimestampPool` constructor performs its own one-time,
   up-front, whole-pool warm-up reset** (mirroring
   `GpuTimingService::WarmUpResetEntirePool()`'s own reasoning, including a
   throwaway `VkCommandPool`/one-shot submission), for the same
   validation-layer-friendliness reason `GpuTimingService` already
   documents.

## Verification performed

- **Full clean incremental build** (`cmake --build build`, MinGW/Ninja,
  the machine's already-fetched dependency tree) — succeeds with zero
  warnings/errors introduced by this change.
- **Full test suite** (`ctest`, run from `build/`) — **626 tests, 625
  passing, 1 pre-existing machine-gated smoke test skipped
  (`PmxLoaderRealModelSmokeTest`)** — no regressions, and the three new
  `RenderGraphNameSlotTableTests.NameAtSlot*` cases pass.
- **Runtime smoke test against the real, local Vulkan device**: launched
  the built `GreatTamanaEngine.exe` directly (not through any test
  harness), confirmed it starts, stays alive, and shuts down cleanly with
  no crash — proving `RenderGraphTimestampPool`'s constructor (which
  creates two real `VkQueryPool`s and performs a real one-shot
  submit-and-wait warm-up reset against the live device/queue) does not
  fail or hang on this machine's actual GPU/driver.
- Did **not** perform a frame-by-frame visual comparison of the "Render
  Graph"/"Profiler" panels' displayed millisecond values in this session
  (no interactive display/input available in this environment) — this is
  the one piece of Tier-2 manual verification a future session with a
  real interactive desktop should still do, per the strategy document's
  own Step 3.11/3.12 Milestone 3 acceptance bar (open both panels
  simultaneously and confirm identical, real, non-zero numbers per pass,
  per frame). Everything upstream of that final visual check (compilation,
  the full existing automated test suite, and a live-device
  construction/teardown smoke test) is verified.

## What was deliberately NOT done (per the strategy document's own Step 4)

- `GpuTimingService`, `VulkanQueryPool`, and the `GpuTimingSlot` enum were
  not modified, extended, or deleted.
- No sub-pass-level (per-draw-call) GPU timing was added — this remains
  whole-pass begin/end timing only.
- No multi-color-attachment (MRT), async/multi-queue, or compute-pass-
  specific timing infrastructure was added.
- `RenderGraphNameSlotTable`'s fixed per-regime budgets
  (`kSynchronousTimingSlotBudget = 16`, `kPipelinedTimingSlotBudget = 8`)
  were not changed.

## Next steps (not part of this change)

- A future session with an interactive display should perform the visual
  cross-check described above (Milestone 3's acceptance bar) and, if
  desired, act on the Milestone 4 grep finding by deleting
  `GpuTimingService`'s now-fully-unreachable fixed-enum machinery as its
  own, separately-scoped cleanup change.
- Per `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`'s own
  Section D item 5, this was meant to be picked up "alongside" the
  GPU-driven-rendering milestone (`GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`)
  — with B.1 now closed, that milestone's own future compute-culling pass
  will get real, honest GPU timing from day one via this exact same
  mechanism (any new named pass it declares gets an `AssignOrGetSlot()`
  entry automatically, up to the existing fixed budget).
