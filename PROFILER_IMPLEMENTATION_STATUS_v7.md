# Profiler Implementation Status (v7)

Status: LIVING DOCUMENT — reflects exactly what exists in the codebase as
of the Phase 4 (sub-phases 4A-4D) implementation sessions on
`feature/profiler-gpu-timestamp-query` (see "Changelog: v6 -> v7" below for
exactly what changed and why). Update this file (or fold it into
`TODO.md`/`README.md` and delete it) the next time a Profiler phase is
implemented, rather than letting it silently drift out of sync with
reality — see `PROFILER_STRATEGY_v2.md`'s own closing section for why this
codebase treats planning/status documents this way.

This file exists purely to answer, for anyone (human or AI agent) picking
this up next: **"what already works, what doesn't exist yet, and why was it
deliberately left for later rather than done now?"** It does not repeat
`PROFILER_STRATEGY_v2.md`'s own design reasoning in full, nor
`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`'s own Phase 4 reasoning in
full — read those documents for the *why behind the plan itself*; this
document is only about *how much of that plan is actually built*.

--------------------------------------------------------------------------
## Changelog: v6 -> v7 (read this first)
--------------------------------------------------------------------------

`PROFILER_IMPLEMENTATION_STATUS_v6.md` (v6) correctly described Phase
0/1/2/3/5/7 as implemented and Phase 4/6 as not implemented. **Across four
separate sessions, this branch (`feature/profiler-gpu-timestamp-query`)
implemented the entirety of Phase 4** (`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`'s
"Vulkan GPU timestamp queries"), split into its own four independently-
shippable sub-phases exactly as that document specifies — **4A** (capability
+ pure math), **4B** (RAII query-pool/service infrastructure), **4C**
(Game/Scene offscreen GPU timing), and **4D** (swapchain Present GPU
timing) — see "What was implemented" below. Phase 6 (benchmark mode)
remains exactly as v6 described it; that reasoning is not repeated here in
full, only re-listed under "What was NOT implemented".

Nothing in Phase 0/1/2/3/5/7's own implementation was touched across these
sessions — Phase 4 is purely additive instrumentation layered onto
`Renderer`/`FramePresenter`/`Application.cpp`, exactly as
`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`'s own "What We Will NOT Do"
section required.

--------------------------------------------------------------------------
## What was implemented (across four sessions)
--------------------------------------------------------------------------

### Phase 4A — Capability query + pure helper logic

New, Vulkan-header-free `src/Renderer/GpuTiming.h/.cpp` (mirroring
`DrawStats.h`'s own precedent exactly, so it stays trivially Tier-1-testable
with no live device):

- `GpuTimestampCapability` (`supported`/`timestampPeriodNs`/`validBits`) +
  `InterpretTimestampCapability()` — the pure DECISION logic behind device
  capability probing, extracted so it's unit-testable with hand-fabricated
  inputs. `VulkanDevice` gained `QueryTimestampCapability()` (called once
  in the constructor, right after `CreateLogicalDevice()`) and a
  `TimestampCapability()` accessor, mirroring `PickDepthFormat()`'s own
  "ask the device once, expose via accessor" shape. Never throws — an
  unsupported result is a completely normal outcome on real hardware.
- `GpuTimingSlot` (`Offscreen0`/`Offscreen1`/`SwapchainPresent`,
  deliberately GENERIC names — `Renderer`/`FramePresenter` must never know
  Editor-facing pass naming) + `GpuTimingSample` (a Renderer-local
  tri-state mirror of `Profiling::GpuSampleStatus`, a deliberately SEPARATE
  type so `Renderer` stays completely free of any `Profiling/` header).
- `ConvertTimestampDeltaToMilliseconds()` — tick-delta -> millisecond
  conversion, masking both ticks to `validBits` before subtracting so the
  result stays correct across exactly one counter wraparound (the single
  most important regression case in its own test file).
- `kGpuTimingFramesInFlight`/`PresentTimestampSlotBase()` — the Present
  path's per-frame-in-flight slot-indexing math.
- 18 new Tier-1 tests in `tests/Renderer/GpuTimingTests.cpp`. No
  `VkQueryPool` created anywhere in this sub-phase. See
  `PHASE4A_COMPLETION_REPORT.md`.

### Phase 4B — RAII query-pool / service infrastructure

- `src/Renderer/Vulkan/VulkanQueryPool.h/.cpp` — a thin RAII wrapper around
  one `VK_QUERY_TYPE_TIMESTAMP` `VkQueryPool`, fixed slot count for its
  entire lifetime, non-copyable/move-safe, same convention as every other
  class under `Vulkan/`.
- `src/Renderer/GpuTimingService.h/.cpp` — owns the one `VulkanQueryPool`
  (when supported AND compiled in) plus every actual
  `vkCmdResetQueryPool`/`vkCmdWriteTimestamp2`/`vkGetQueryPoolResults` call
  site, at a fixed 8-slot layout (`Offscreen0`/`Offscreen1` start/end,
  `SwapchainPresent` frame-in-flight-0/1 start/end). Gated by BOTH a
  compile-time switch (`GTE_ENABLE_PROFILER` — forces the effective
  capability to `unsupported`, so a `GTE_ENABLE_PROFILER=OFF` build never
  creates a `VkQueryPool` at all) and a runtime switch
  (`SetCaptureEnabled()`, mirroring `Profiling::FrameProfiler`'s own name
  and shape exactly) — closing the single biggest gap the original v1
  strategy draft had (see `PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`'s
  own "Changelog: v1 -> v2").
- `ResolveGpuTimingStatus()` (`GpuTiming.h`) — the pure tri-state PRIORITY
  decision (`Unsupported` always wins over `Absent` wins over `Present`)
  `GpuTimingService`'s `Read*` methods build on, extracted so all 8
  combinations of its 3 boolean inputs are Tier-1-tested without a live
  device.
- `Renderer`/`FramePresenter` both gained a `std::shared_ptr<GpuTimingService>`
  member, constructed and shared the exact same way `GpuMemoryTracker`
  already is. `Renderer` gained `LastGpuTiming(GpuTimingSlot)` and
  `SetGpuTimingCaptureEnabled(bool)` — but nothing calls any of
  `GpuTimingService`'s `Record*`/`Read*` methods yet.
- 5 new Tier-1 tests added to `tests/Renderer/GpuTimingTests.cpp`
  (`ResolveGpuTimingStatus`'s full decision table). Verified manually: a
  normal Editor session with validation layers on shows zero new
  validation errors, and a `GTE_ENABLE_PROFILER=OFF` build creates no
  `VkQueryPool` at all. See `PHASE4B_COMPLETION_REPORT.md`.

### Phase 4C — Game/Scene offscreen GPU timing (first real data)

- `Renderer::RenderOffscreen()`/`FramePresenter::RenderOffscreen()` both
  gained a `std::optional<GpuTimingSlot> timingSlot` parameter — NO
  default value; `std::nullopt` is one of two equally explicit choices
  (participate via `Offscreen0`/`Offscreen1`, or opt out entirely), never
  an implicit fallback. This closed a real slot-collision bug the original
  v1 draft would have introduced: `RenderOffscreen()` is also called by
  `AssetPreviewMesh` (Inspector mesh preview) and `BoneViewerWindow` (its
  own viewport) — neither is one of the Profiler's three named passes, and
  both now pass `std::nullopt`.
- `FramePresenter::RenderOffscreen()`'s new sequencing: `RecordOffscreenPassStart()`
  right after the existing pre-recording `OffscreenFence()` wait (outside
  the dynamic rendering instance `RecordFrame()` opens/closes — a hard
  Vulkan validity rule), `RecordOffscreenPassEnd()` right after
  `RecordFrame()` returns, and `ReadOffscreenResultNow()` right after the
  function's own existing final fence wait — safe precisely because that
  wait already proves the submission (including its timestamp writes) is
  complete. No new wait added anywhere.
- `Application::Run()`'s "Game"/"Scene" blocks now pass
  `GpuTimingSlot::Offscreen0`/`Offscreen1` and report real timing via
  `Renderer::LastGpuTiming()` -> `FrameProfiler::SetGpuPassTiming()`,
  inside the exact same `if (gameTarget != nullptr)`/
  `if (sceneTarget != nullptr)` guards that already gate the existing
  draw-stats report. `Application::Run()` also started calling
  `Renderer::SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled())`
  once per frame — the Editor's existing "Capture" checkbox now genuinely
  gates this phase's Vulkan work, not just the ring-buffer bookkeeping.
- Verified: full test suite (521 tests, 520 passing + 1 pre-existing
  machine-gated skip), and a runtime smoke test confirming the engine
  stays running with real Game/Scene GPU timing flowing every frame. See
  `PHASE4C_COMPLETION_REPORT.md`.

### Phase 4D — Swapchain Present GPU timing (the hardest chunk)

- `FramePresenter::Present()`'s new sequencing: `ReadPresentResultIfAvailable(m_currentFrame)`
  right after the existing per-frame-in-flight fence wait (reading a PAST
  frame's result — `kFramesInFlight` frames ago — safely, since that wait
  already proves it's complete); `RecordPresentPassStart()` right after
  `vkBeginCommandBuffer` succeeds; `RecordPresentPassEnd()` +
  `MarkPresentSlotWritten()` right after `RecordFrame()` returns. No new
  wait added anywhere.
- The Present-path warm-up flag (`m_presentSlotEverWritten`, built in 4B,
  first actually exercised here) correctly reports `Absent` for exactly the
  first two real `Present()` calls of a session (or of any stretch
  following a resize/minimize/capture-toggle gap), then real data for
  every call after that — a per-slot boolean, not a frame-count heuristic,
  so it's correct by construction regardless of how many early-returns or
  capture-disabled frames happened in between.
- `Application::Run()`'s Present block now reports real timing for
  `GpuPass::Present` via `Renderer::LastGpuTiming(GpuTimingSlot::SwapchainPresent)`,
  inside the same `if (presentStats.has_value())` guard already used for
  draw-call stats.
- Verified: full test suite (521 tests, 520 passing + 1 pre-existing
  machine-gated skip — identical to the pre-4D baseline, no regressions),
  and a runtime smoke test (engine launched in the background, confirmed
  still running via `tasklist` after ~12 seconds). Interactive
  resize/minimize/capture-toggle verification was **not** performed in the
  4D session itself (no interactive UI-driving tool was available) — see
  "Known rough edges" below. See `PHASE4D_COMPLETION_REPORT.md`.

### What this means for the Editor's "Profiler" panel

**Zero Editor-side code changes were needed at any point across 4A-4D** —
the "GPU Timing" section already existed (Phase 7) and already correctly
rendered all three tri-states; it simply started receiving real data the
moment `SetGpuPassTiming()` became a genuine production call. All three
named passes (`Game View`/`Scene View`/`Present`) now show real,
driver-measured milliseconds whenever that pass ran this frame, an honest
"N/A" for a hidden/not-yet-warmed-up pass, and a permanent "Unsupported" on
a device/build that can never produce this measurement.

--------------------------------------------------------------------------
## What was NOT implemented, and why
--------------------------------------------------------------------------

### Phase 6 — Benchmark mode (headless-of-the-Editor CLI run)

**Not implemented.** No `--benchmark` CLI flag, no CSV/summary exporter, no
warm-up-frame handling. The "Profiler" panel's own "Export CSV" button is
still a deliberately disabled stub with a tooltip pointing at this phase.

**Why deferred:** Same reasoning as v2-v6 — a pure consumer of Phases 0-5/7's
data model, and now ALSO Phase 4's own real GPU timing data — it still
needs its own CLI/workload design, and now has a concrete, disabled
"Export CSV" button in the Editor already waiting for it to wire up to.
This is now the **only** remaining phase of `PROFILER_STRATEGY_v2.md`'s
original 8-phase plan.

--------------------------------------------------------------------------
## Known rough edges in what WAS implemented
--------------------------------------------------------------------------

- Everything `PROFILER_IMPLEMENTATION_STATUS_v6.md` listed here remains
  true and unchanged for Phases 0-3/5/7 — nothing about their own wiring
  was touched across the Phase 4 sessions.
- **GPU Timing's cross-check against an external tool (RenderDoc/Nsight)
  was not formally performed and documented as part of these sessions** —
  each sub-phase's own completion report instead relied on a runtime
  smoke test (the engine stays running, no validation-layer errors) as its
  Tier 2 verification, matching this codebase's own accepted "Tier 2,
  verified manually" bucket for GPU-device-dependent code. A developer
  with access to such a tool should still perform the cross-check
  `PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`'s own Step 1.3 success
  criterion calls for, at least once, before treating Phase 4 as fully
  sign-off-verified.
- **Interactive resize/minimize/restore/multi-viewport/Capture-toggle
  verification of the Present-path GPU timing was not performed in the 4D
  session** (no interactive UI-driving tool was loaded in that automated
  session) — the code paths were reasoned through explicitly against the
  strategy document's own constraints instead. Walk through
  `PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`'s own Phase 4D verification
  checklist interactively before considering this fully verified.
- **No CSV export yet** — the "Export CSV" button is a disabled stub
  pointing at Phase 6, by design (see above).

--------------------------------------------------------------------------
## How to verify the current state yourself
--------------------------------------------------------------------------

- Build: `cmake -S . -B build` then `cmake --build build --config Debug`.
- Test: `ctest --test-dir build -C Debug --output-on-failure` (expect 521
  total, 520 passing, 1 pre-existing machine-gated skip).
- Open the Editor, spawn a few primitive entities via "Hierarchy" -> "Create
  3D Object", and switch to the "Profiler" tab (tabbed alongside "Memory")
  to see the CPU frame-time graph, CPU scope table, draw-call/triangle
  counts, GPU memory sparkline, AND (new as of this document) the "GPU
  Timing" section all update live with real numbers for all three named
  passes. Tab "Scene"/"Game" together (hiding one) and confirm the hidden
  one's GPU Timing line reads "N/A", never a frozen stale number. Untick
  "Capture" and confirm all three GPU Timing lines go to "N/A" (verifiable
  with a validation-layer capture tool: no new `vkCmdResetQueryPool`/
  `vkCmdWriteTimestamp2` commands should appear while it stays unticked).

--------------------------------------------------------------------------
## Suggested next steps, in priority order
--------------------------------------------------------------------------

1. ~~Phase 2 (frame-time graph data reshape)~~ — DONE (v3 session).
2. ~~Phase 3 (draw-call/triangle counts)~~ — DONE (v4 session).
3. ~~Phase 5 (GPU memory history)~~ — DONE (v5 session).
4. ~~Phase 7 (the Editor "Profiler" panel)~~ — DONE (v6 session).
5. ~~Phase 4 (GPU timestamp queries, sub-phases 4A-4D)~~ — DONE as of this
   v7 document.
6. **Phase 6 (benchmark mode)** — the one remaining phase of
   `PROFILER_STRATEGY_v2.md`'s original 8-phase plan. The Editor panel it
   will eventually share a CSV exporter with already exists, with a
   disabled button waiting for it, and every data category it would need
   to export (CPU scopes, draw stats, GPU memory, AND now real GPU timing)
   is genuine production data today — nothing about Phase 6 is blocked on
   any prior phase any longer.

See `PROFILER_STRATEGY_v2.md` for the full 8-phase design reasoning,
`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md` for Phase 4's own detailed
reasoning, and `PHASE4A_COMPLETION_REPORT.md`/`PHASE4B_COMPLETION_REPORT.md`/
`PHASE4C_COMPLETION_REPORT.md`/`PHASE4D_COMPLETION_REPORT.md` for each
sub-phase's own session writeup.
