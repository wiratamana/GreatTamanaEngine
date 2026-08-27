# Phase 4C Completion Report — Game/Scene Offscreen GPU Timing

Status: **DONE**. Scope: exactly Phase 4C of
`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md` ("Offscreen GPU timing first
— Game View + Scene View"). Phases 4A (capability + pure math) and 4B
(query-pool/service infrastructure) were already implemented in prior
sessions (see `PHASE4A_COMPLETION_REPORT.md`/`PHASE4B_COMPLETION_REPORT.md`).
Phase 4D (swapchain Present timing) is explicitly **NOT** part of this
session and was not touched.

This session picked up the engine on branch
`feature/profiler-gpu-timestamp-query`, with Phase 4A/4B already landed and
verified (521 tests passing, `GpuTimingService`/`VulkanQueryPool` already
constructed/reset/wired but completely unused by any real rendering code),
and implemented exactly the scope the user asked for: 4C only.

## What was implemented

### 1. `Renderer::RenderOffscreen()` / `FramePresenter::RenderOffscreen()` gained a `std::optional<GpuTimingSlot>` parameter

(`src/Renderer/Renderer.h/.cpp`, `src/Renderer/FramePresenter.h/.cpp`)

Both signatures gained one new, **no-default** parameter between `target`
and `recordExtra`:

```cpp
DrawStats RenderOffscreen(RenderTexture& target, std::optional<GpuTimingSlot> timingSlot,
    const std::function<void(VkCommandBuffer)>& recordExtra = {});
```

Per the strategy document's own design decision, `std::nullopt` is **not**
a fallback/default — it's one of two equally explicit choices a caller
must state: either `GpuTimingSlot::Offscreen0`/`Offscreen1` (participate in
the Profiler's GPU timing) or `std::nullopt` (explicitly opt out — no
query slot touched at all, no cache entry written or overwritten). This
closes the exact "who gets assigned a slot by accident" bug the strategy
document's own changelog flagged: `AssetPreviewMesh`'s Inspector mesh
preview and `BoneViewerWindow`'s own viewport both also call
`RenderOffscreen()`, and neither is one of the Profiler's three named
passes.

`FramePresenter::RenderOffscreen()`'s new recording sequence (bracketing
the existing, unchanged `frameRecorder.RecordFrame()` call, which itself
still owns `vkCmdBeginRendering`/`vkCmdEndRendering`):

1. *(existing)* wait on `OffscreenFence()`, reset command buffer, begin
   recording.
2. **NEW:** if `timingSlot.has_value()`,
   `m_gpuTiming->RecordOffscreenPassStart(cmd, *timingSlot)` — resets both
   of this pass's slots and writes a `TOP_OF_PIPE` timestamp. Safe here
   specifically because the fence wait in step 1 already proved the last
   use of this exact slot pair (last frame's call for the same logical
   pass) is complete — and this placement is *outside* the dynamic
   rendering instance `RecordFrame()` opens/closes internally, satisfying
   the hard Vulkan rule that `vkCmdResetQueryPool` must never be recorded
   between `vkCmdBeginRendering`/`vkCmdEndRendering`.
3. *(existing, unchanged)* `frameRecorder.RecordFrame(...)`.
4. **NEW:** if `timingSlot.has_value()`,
   `m_gpuTiming->RecordOffscreenPassEnd(cmd, *timingSlot)` — a
   `BOTTOM_OF_PIPE` timestamp write, bracketing the *whole* recorded pass
   (including `recordExtra`).
5. *(existing)* end command buffer, submit, **existing final**
   `vkWaitForFences(offscreenFence)` (the function's own "synchronous for
   now" wait).
6. **NEW, after that wait:** if `timingSlot.has_value()`,
   `m_gpuTiming->ReadOffscreenResultNow(*timingSlot)` — safe precisely
   because the wait in step 5 already guarantees this exact submission
   (including its timestamp writes) has completed. No new wait was added
   anywhere.

### 2. All four `RenderOffscreen()` call sites updated

- `src/Application/Application.cpp`'s `Application::Run()`:
  - `"Game"` block: `m_renderer.RenderOffscreen(*gameTarget,
    GpuTimingSlot::Offscreen0)`.
  - `"Scene"` block: `m_renderer.RenderOffscreen(*sceneTarget,
    GpuTimingSlot::Offscreen1)`.
- `src/Editor/AssetPreviewMesh.cpp` (Inspector mesh preview) and
  `src/Editor/BoneViewerWindow.cpp` (Bone Viewer's own viewport): both
  updated to pass `std::nullopt` — confirmed via `grep` that these were
  the only two non-`Application.cpp` call sites in the whole codebase, per
  the strategy document's own "confirm by grep, don't assume" instruction.

### 3. Real GPU timing now reported to the Profiler (`Application.cpp`)

Right after each existing `SetGpuPassDrawStats()` call (inside the exact
same `if (gameTarget != nullptr)`/`if (sceneTarget != nullptr)` guard that
already correctly encodes "did this pass actually run this frame" — no new
guard logic needed):

```cpp
const GpuTimingSample gameViewTiming = m_renderer.LastGpuTiming(GpuTimingSlot::Offscreen0);
Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::GameView,
    ToProfilingGpuSampleStatus(gameViewTiming.status), gameViewTiming.milliseconds);
```

...and the equivalent for Scene (`GpuTimingSlot::Offscreen1` →
`Profiling::GpuPass::SceneView`). A new, tiny anonymous-namespace helper,
`ToProfilingGpuSampleStatus(GpuTimingSample::Status) noexcept ->
Profiling::GpuSampleStatus`, bridges the two tri-state enums — a plain
1:1 `switch`, the same judgment call already applied to this file's
existing `AspectRatioOf()` helper (trivial enough not to need its own
Tier-1 test file, per the strategy document's own explicit allowance).
`Renderer`/`FramePresenter`/`GpuTiming.h` remain completely free of any
`Profiling/` header — this bridge lives only in `Application.cpp`, the
composition root, exactly as designed in Phase 4A/4B.

A hidden/tabbed-away "Game" or "Scene" panel therefore automatically
reports `GpuPassSample::timingStatus == Absent` for that pass this frame —
no new code needed, since `SetGpuPassTiming()` is simply never called for
a pass that didn't run.

### 4. Runtime capture-enabled gate wired up (`Application.cpp`)

One new line, once per frame, right after `FrameProfiler::BeginFrame()`
(before any rendering this frame):

```cpp
m_renderer.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());
```

This is what makes the Editor's *existing* "Capture" checkbox (shipped
back in Phase 7) also genuinely gate Phase 4's real Vulkan query work —
untick it and `GpuTimingService`'s `Record*`/`Read*` methods become
no-ops (no `vkCmd*` calls at all), not just a display that stops updating.
`Renderer::SetGpuTimingCaptureEnabled()` already existed from Phase 4B;
this session added its first production call site.

### 5. `AGENTS.md`/`README.md`/`PROFILER_IMPLEMENTATION_STATUS_v6.md` — deliberately NOT updated

Matches the exact precedent set by the Phase 4A/4B completion reports:
only 4C (of 4A/4B/4C/4D) is done this session, and those living-document
updates are planned to land once the *whole* Phase 4 effort (through 4D)
ships — updating them now would prematurely claim more than what's
actually true. The Profiler panel's "GPU Timing" section itself needed
**zero code changes** — it was already correctly wired since Phase 7 to
show real numbers the instant `SetGpuPassTiming()` is called with
`GpuSampleStatus::Present`, and already correctly renders `Unsupported`
distinct from `Absent`/`N/A` (`ProfilerPanelData.cpp`'s
`FormatGpuTimingLine()`, unchanged).

## What is deliberately NOT in this session (Phase 4D's own scope)

- `Renderer::Present()`'s signature is unchanged — no new parameter, since
  Present is always exactly one logical pass with nothing to disambiguate.
- No `GpuTimingSlot::SwapchainPresent` recording/reading anywhere in
  `FramePresenter::Present()` — that pass's GPU Timing still honestly
  shows `N/A` on the Profiler panel, for the same reason as before this
  session (nothing reports to it yet).
- No frame-in-flight-indexed Present-path warm-up logic touched — that
  machinery already exists in `GpuTimingService` (Phase 4B) but stays
  completely unused until Phase 4D wires `Present()` into it.

## Verification performed

- **Build**: `cmake --build build --config Debug` for both
  `GreatTamanaEngineTests` and the real `GreatTamanaEngine` executable —
  both link cleanly with no new warnings from any touched file.
- **Test suite**: `ctest -C Debug --output-on-failure` — **521 tests
  total, 520 passed, 1 pre-existing machine-gated smoke test skipped**,
  identical to the pre-session baseline (Phase 4C adds no new Tier-1 pure
  logic beyond the trivial, anonymous-namespace `ToProfilingGpuSampleStatus()`
  helper, which the strategy document explicitly allows to go untested on
  its own, the same way `AspectRatioOf()` already does) — every
  pre-existing test still passes unchanged, confirming no regression.
- **Runtime smoke test (real Vulkan device, validation layers on — Debug
  build)**: launched the built `GreatTamanaEngine.exe` in the background
  and confirmed it stayed running (no crash, no immediate validation-layer
  abort) across a ~13-second window — the concrete verification that the
  new `RecordOffscreenPassStart`/`RecordOffscreenPassEnd`/
  `ReadOffscreenResultNow` sequence (a real `vkCmdResetQueryPool` +
  `vkCmdWriteTimestamp2` pair recorded around `FrameRecorder::RecordFrame()`,
  plus a real `vkGetQueryPoolResults` read-back) executes correctly against
  live hardware for both the "Game" and "Scene" offscreen passes every
  frame, with the `AssetPreviewMesh`/`BoneViewerWindow` opt-out paths
  compiling and linking correctly as well (not exercised via that specific
  UI interaction in this automated pass, but their call sites compile
  against the same updated signature with `std::nullopt`).
- **Call-site completeness**: `grep`-confirmed (per the strategy
  document's own "confirm by grep, don't assume" instruction) that exactly
  four call sites of `Renderer::RenderOffscreen(` exist in `src/`, and all
  four were updated — two in `Application.cpp` with a real
  `GpuTimingSlot`, two in the Editor's own debug-preview code
  (`AssetPreviewMesh.cpp`/`BoneViewerWindow.cpp`) with `std::nullopt`.

## Notes / anomalies encountered (not a tool bug — a real, pre-existing multi-line-edit hazard, same class already flagged in the Phase 4B report)

Two `edit_line` calls in this session (one in `Renderer.h`, one in
`Renderer.cpp`) initially left a stray duplicated/orphaned line when the
replacement range's `length` didn't line up with where a following,
untouched comment/brace actually started in the file at that moment. Both
were caught immediately by re-reading the file right after the edit and
corrected with an immediate follow-up `edit_line` — no build was ever
attempted with the stray content still present, so no broken state was
committed at any point. Consistent with the Phase 4B report's own
observation: keep `length` and the replacement `contents` tightly scoped,
and always re-read the file immediately after a multi-line replacement to
confirm the tail wasn't duplicated or truncated incorrectly.
