# Phase 4D Completion Report — Swapchain Present GPU Timing

Status: **DONE**. Scope: exactly Phase 4D of
`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md` ("Swapchain Present GPU
timing — the hardest chunk"). Phases 4A (capability + pure math), 4B
(query-pool/service infrastructure), and 4C (Game/Scene offscreen timing)
were already implemented in prior sessions (see
`PHASE4A_COMPLETION_REPORT.md`/`PHASE4B_COMPLETION_REPORT.md`/
`PHASE4C_COMPLETION_REPORT.md`). This session's scope was **exactly 4D and
nothing else**, per the user's explicit instruction — with 4D landing, all
four sub-phases of `PROFILER_STRATEGY_v2.md`'s Phase 4 are now implemented.

This session picked up the engine on branch
`feature/profiler-gpu-timestamp-query`, with Phases 4A/4B/4C already landed
and verified (521 tests passing, `GpuTimingService` already fully
implemented — including its Present-path methods
`RecordPresentPassStart`/`RecordPresentPassEnd`/
`ReadPresentResultIfAvailable`/`MarkPresentSlotWritten`, all written during
Phase 4B but never called from anywhere in production code), and wired the
one remaining piece: `FramePresenter::Present()`'s own recording sequence,
plus `Application.cpp`'s reporting for `GpuPass::Present`.

## What was implemented

### 1. `FramePresenter::Present()`'s new sequencing (`src/Renderer/FramePresenter.cpp`)

The strategy document's exact before/after ordering was followed with no
deviation:

1. *(existing, unchanged)* Minimized/pending-resize early-return checks —
   nothing new here; if either returns early, `m_currentFrame` never
   advances and nothing Phase 4D added ever runs this call, which is
   exactly correct (no data to report, no slot skew).
2. *(existing, unchanged)* `const VkFence fence =
   m_frameSync.InFlightFence(m_currentFrame); vkWaitForFences(...)`.
3. **NEW**, immediately after that wait:
   `m_gpuTiming->ReadPresentResultIfAvailable(m_currentFrame)` — safe
   precisely because the fence wait immediately above already proves the
   submission that last wrote into this exact frame-in-flight slot
   (`kFramesInFlight == 2` frames ago) has fully completed. No new wait
   was added anywhere to make this data available sooner.
4. *(existing, unchanged)* `vkAcquireNextImageKHR`, its
   `VK_ERROR_OUT_OF_DATE_KHR`/other-failure handling, `vkResetFences`, the
   `needsDepth`/`EnsureDepthBuffersForSwapchain()` decision,
   `vkResetCommandBuffer`, `vkBeginCommandBuffer`.
5. **NEW**, immediately after `vkBeginCommandBuffer` succeeds, before
   `RenderTarget target` is built:
   `m_gpuTiming->RecordPresentPassStart(cmd, m_currentFrame)` — records a
   reset (both slots for this frame-in-flight index) plus a
   `TOP_OF_PIPE` timestamp write, recorded OUTSIDE the dynamic rendering
   instance `frameRecorder.RecordFrame()` opens/closes internally (a hard
   Vulkan validity rule — `vkCmdResetQueryPool` must never be recorded
   between `vkCmdBeginRendering`/`vkCmdEndRendering`). Safe to reset+write
   here specifically because the fence wait at the top of this function
   already proved the LAST use of this exact slot pair
   (`kFramesInFlight` frames ago) is complete.
6. *(existing, unchanged)* `frameRecorder.RecordFrame(cmd, target,
   ColorFormat(), m_depthFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
   recordExtra)` — this is what actually records Dear ImGui's own chrome
   (via `recordExtra`) alongside any direct-to-swapchain engine geometry.
7. **NEW**, immediately after `RecordFrame()` returns, before
   `vkEndCommandBuffer`:
   `m_gpuTiming->RecordPresentPassEnd(cmd, m_currentFrame)` (a
   `BOTTOM_OF_PIPE` timestamp write, bracketing the whole recorded pass
   including `recordExtra`) immediately followed by
   `m_gpuTiming->MarkPresentSlotWritten(m_currentFrame)` (the warm-up
   flag — see below — set only right after a real write actually
   happened, guarded by the exact same
   `!IsSupported() || !IsCaptureEnabled()` condition as the `Record*`
   calls, so it's safe to call unconditionally here with no extra `if`).
8. *(existing, unchanged)* `vkEndCommandBuffer`, submit, present, resize
   detection, `m_currentFrame = (m_currentFrame + 1) % kFramesInFlight`,
   `return drawStats`.

No new stall was added anywhere — every `Record*`/`Read*` call added in
this sub-phase either is a no-op (unsupported device, uncompiled-in build,
capture disabled, or — for the read — not yet warmed up) or piggybacks on
synchronization the function already performed for an unrelated,
pre-existing reason (the fence wait at the top of the function).

### 2. `Application.cpp`'s reporting for `GpuPass::Present`

Inside the existing `if (presentStats.has_value())` guard (the exact same
guard that already gates `SetGpuPassDrawStats(GpuPass::Present, ...)`),
right after it:

```cpp
const GpuTimingSample presentTiming = m_renderer.LastGpuTiming(GpuTimingSlot::SwapchainPresent);
Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::Present,
    ToProfilingGpuSampleStatus(presentTiming.status), presentTiming.milliseconds);
```

Reusing this exact guard is what correctly makes a minimized-window frame
(where `Present()` returned `std::nullopt` and therefore nothing Phase 4D
added ever ran) report `Present`'s GPU Timing as `Absent`, with zero extra
guard logic needed — the same pattern the Game/Scene blocks already
established in Phase 4C. `ToProfilingGpuSampleStatus()` (the tiny
Renderer-status → Profiling-status bridge, added in Phase 4C) needed no
changes — it already handles all three `GpuTimingSample::Status` values.

### 3. Why the Present-path warm-up flag (already built in Phase 4B) makes this correct from frame 1

`GpuTimingService::ReadPresentResultIfAvailable()` (fully implemented
since Phase 4B, first actually *called* in this session) returns (and
caches) `Absent` — without touching Vulkan at all — until
`m_presentSlotEverWritten[frameInFlightIndex]` has genuinely been set at
least once for that exact frame-in-flight slot. With
`kFramesInFlight == 2`, this means exactly the first two real `Present()`
calls of a session (or of a stretch immediately following a resize/
minimize/capture-toggle gap) correctly report `Absent` for that slot,
and every call after that reports real, freshly-read `Present` data — a
per-slot boolean, not a global frame-count heuristic, so it is correct by
construction regardless of how many minimized/pending-resize/
capture-disabled frames happened in between (see the strategy document's
own reasoning, Phase 4D, point 2). No new code was needed for this in this
session — it was already fully implemented and unit-tested
(`ResolveGpuTimingStatus`, Phase 4B) as part of laying the infrastructure;
this session is simply the first to actually reach it via a real
`Present()` call.

## What is now fully wired, closing out the whole Phase 4 effort

- `Renderer::LastGpuTiming(GpuTimingSlot::Offscreen0/Offscreen1/SwapchainPresent)`
  all now reflect real, driver-measured GPU time for their respective pass
  whenever that pass ran this frame, are `Unsupported` on a device/build
  that can never produce this measurement, and are `Absent` for a pass
  that didn't run this frame (hidden Game/Scene panel, minimized window,
  a Present-path slot not yet warmed up, or Capture currently disabled) —
  never a fabricated `0.00 ms`.
- The Editor's "Profiler" panel's "GPU Timing" section — unchanged since
  Phase 7, already correctly rendering all three tri-states — now shows
  real numbers for **all three** named passes (`Game View`, `Scene View`,
  `Present`) the instant each one runs, with zero further Editor-side code
  changes needed in this session, exactly as `PROFILER_STRATEGY_v2.md`
  predicted.

## What is deliberately NOT in this session

Per the user's explicit "4D only" instruction, and per this phase's own
"What We Will NOT Do" refusals:

- No update to `AGENTS.md`'s "Profiling" section, `README.md`'s "Status"
  section, or `PROFILER_IMPLEMENTATION_STATUS_v6.md` (a `_v7` bump) —
  the strategy document's own Step 5.4 calls for these once the *whole*
  Phase 4 effort (through 4D) ships, which is now true, but the user's
  instruction for this specific session was scoped narrowly to
  implementing 4D itself, matching the exact narrow-scoping precedent the
  4A/4B/4C completion reports already established for their own slices.
  **This is the one natural follow-up worth flagging**: with 4D now
  landed, updating those three living documents to reflect that GPU
  timing is real, production data (and to document
  `GTE_ENABLE_PROFILER`/`SetCaptureEnabled()`'s gating of it) is the
  logical next step whenever a session is scoped to do so.
- No per-draw-call GPU timing, no GPU-pass attribution for a secondary
  ImGui platform window or the Editor's own debug/preview rendering
  (`AssetPreviewMesh`/`BoneViewerWindow` — already handled correctly via
  `std::nullopt` since Phase 4C), no new Vulkan extension request, no
  rewrite of `FrameRecorder`/`FramePresenter`'s draw-submission pipeline,
  no Editor/UI code changes, no headless test fixture, no CSV export.

## Verification performed

- **Build**: `cmake --build build --config Debug` — both `gte_core`,
  `GreatTamanaEngineTests`, and the real `GreatTamanaEngine` executable
  link cleanly with no new warnings from either touched file
  (`FramePresenter.cpp`, `Application.cpp`).
- **Test suite**: `ctest -C Debug --output-on-failure` — **521 tests
  total, 520 passed, 1 pre-existing machine-gated smoke test skipped** —
  identical to the pre-session baseline (Phase 4D adds no new pure logic
  of its own; every piece of pure logic it relies on —
  `ConvertTimestampDeltaToMilliseconds`, `ResolveGpuTimingStatus`,
  `PresentTimestampSlotBase`, `kGpuTimingFramesInFlight` — was already
  implemented and unit-tested in Phase 4A/4B). Every pre-existing test
  still passes unchanged, confirming no regression.
- **Runtime smoke test (real Vulkan device, validation layers on — Debug
  build)**: launched the built `GreatTamanaEngine.exe` in the background
  and confirmed it stayed running (no crash, no immediate
  validation-layer abort, confirmed alive via `tasklist` after a ~12
  second window) — the concrete verification that the new
  `RecordPresentPassStart`/`RecordPresentPassEnd`/
  `ReadPresentResultIfAvailable`/`MarkPresentSlotWritten` sequence (a real
  `vkCmdResetQueryPool` + `vkCmdWriteTimestamp2` pair recorded around
  `FrameRecorder::RecordFrame()` for the swapchain Present pass, plus a
  real `vkGetQueryPoolResults` read-back gated by the per-frame-in-flight
  fence wait) executes correctly against live hardware every frame,
  through the normal Editor rendering path (Dear ImGui's own chrome
  recorded via `recordExtra`), with no validation error surfaced during
  that window.
- **Interactive manual verification NOT performed this session** (resize/
  minimize/restore cycles, dragging a panel into its own OS window, and
  toggling the Editor's "Capture" checkbox mid-session while watching the
  Profiler panel's `Present` GPU Timing line) — this automated session had
  no interactive UI-driving tool loaded to exercise those specific
  scenarios end-to-end. The code paths for all of them were reasoned
  through explicitly against the strategy document's own constraints
  (Step 2.3) and against the already-existing, already-correct
  `RecreateSwapchain()`/minimized-early-return/capture-toggle machinery
  this sub-phase deliberately does not touch — but a developer with an
  interactive session should still walk through the strategy document's
  own Phase 4D verification checklist (resize repeatedly, minimize/
  restore, drag a panel into its own OS window, toggle Capture) before
  treating this as fully sign-off-verified per that document's own Step
  1.3 success criteria.

## Notes / anomalies encountered (not a tool bug — the same class of
multi-line-edit hazard already flagged in the Phase 4B/4C reports)

Several `edit_line` calls in this session initially produced a
duplicated/missing blank line (once even a doubled blank line right after
an inserted comment block) when the replacement content's own line count
didn't line up exactly with the blank-line spacing already present in the
file at that point. Each occurrence was caught immediately by re-reading
the file right after the edit and corrected with an immediate follow-up
`edit_line` — no build was ever attempted with the stray blank-line
formatting still present, and none of these affected actual code
correctness (only whitespace). Consistent with the Phase 4B/4C reports'
own observation: keep `length` tightly scoped and always re-read
immediately after a multi-line replacement.
