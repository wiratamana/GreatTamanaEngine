# Phase 4B Completion Report — GPU Query-Pool / Service Infrastructure

Status: **DONE**. Scope: exactly Phase 4B of
`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md` ("RAII query-pool
infrastructure — created, wired, not yet read"). Phase 4A (capability +
pure math) was already implemented in a prior session (see
`PHASE4A_COMPLETION_REPORT.md`). Phases 4C (Game/Scene offscreen timing)
and 4D (Present timing) are explicitly **NOT** part of this session and
were not touched.

This session picked up the engine on branch
`feature/profiler-gpu-timestamp-query`, with Phase 4A already landed and
verified (516 tests passing at that point), and implemented exactly the
scope the user asked for: 4B only.

## What was implemented

### 1. `src/Renderer/Vulkan/VulkanQueryPool.h/.cpp` (new files)

A thin RAII wrapper around one `VkQueryPool` of type
`VK_QUERY_TYPE_TIMESTAMP`, fixed slot count for its entire lifetime — never
resized/recreated. Deliberately "dumb": it has zero knowledge of what a
given slot index actually means semantically (that mapping lives one layer
up, in `GpuTimingService`) — the same division of labor `VulkanSwapchain`
already has relative to `FramePresenter`. Created in the constructor
(`vkCreateQueryPool`), destroyed in the destructor (`vkDestroyQueryPool`),
non-copyable, move-safe (`std::exchange`-based move ctor/assignment,
mirroring `VulkanFrameSync`'s own pattern exactly). No
`Reset()`/`WriteTimestamp()`/`GetResults()` convenience methods — those
calls need `GpuTimingService`'s own semantic slot-to-purpose mapping
anyway, so wrapping them here would be pure indirection with no real
encapsulation benefit.

### 2. `src/Renderer/GpuTimingService.h/.cpp` (new files)

Owns the one `VulkanQueryPool` (when supported and compiled in) plus every
actual `vkCmdResetQueryPool`/`vkCmdWriteTimestamp2`/`vkGetQueryPoolResults`
call site this whole GPU-timestamp effort will ever need:

- **Fixed 8-slot pool layout**, decided once: slots 0/1 =
  `GpuTimingSlot::Offscreen0` start/end, 2/3 = `Offscreen1` start/end, 4/5 =
  `SwapchainPresent` frame-in-flight-0 start/end, 6/7 = `SwapchainPresent`
  frame-in-flight-1 start/end — exactly the layout
  `PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md` specifies.
- **Two-layer on/off gate**, mirroring `AGENTS.md`'s "Profiling"
  convention: the constructor forces `m_capability.supported = false`
  whenever `GTE_ENABLE_PROFILER` is compiled `0`, regardless of what the
  device itself reports — this is the ONLY place the class reads that
  macro; every other method is unconditional and simply inert once
  `IsSupported()` is false, for whatever reason. `SetCaptureEnabled(bool)`/
  `IsCaptureEnabled()` are the runtime layer, mirroring
  `Profiling::FrameProfiler::SetCaptureEnabled()` by name and shape.
- **A one-time, up-front, whole-pool reset** at construction, via a
  throwaway, self-contained one-shot command pool/buffer/fence (NOT
  `Renderer::ImmediateSubmit()` — `GpuTimingService` is constructed before
  `GpuResourceFactory` even exists in `Renderer`'s own member order, so it
  cannot depend on it). This is what lets every later
  `RecordOffscreenPassStart()`/`RecordPresentPassStart()` call (Phase
  4C/4D) assume every slot already starts life reset.
- **Full public API for both the offscreen and Present paths already
  exists** (`RecordOffscreenPassStart/End`, `ReadOffscreenResultNow`,
  `RecordPresentPassStart/End`, `ReadPresentResultIfAvailable`,
  `MarkPresentSlotWritten`, `LastKnown`) — but, per this phase's own
  explicit scope, **nothing calls any of them yet**. They're implemented
  and ready for Phase 4C/4D to wire into `FramePresenter::Present()`/
  `RenderOffscreen()`.
- **`ReadOffscreenResultNow()`/`ReadPresentResultIfAvailable()`** both
  route their tri-state decision through a new pure function,
  `ResolveGpuTimingStatus()` (see below), rather than hand-rolling the same
  `if`-chain twice.

### 3. `src/Renderer/GpuTiming.h` — `ResolveGpuTimingStatus()` (new pure function)

The Phase 4B design explicitly calls for pulling out any small, pure
decision-table logic discovered while implementing `GpuTimingService` so it
stays Tier-1-testable even though the class hosting it is Tier 2. This is
exactly that: `ResolveGpuTimingStatus(bool supported, bool captureEnabled,
bool hasWrittenData) noexcept -> GpuTimingSample::Status`, with a fixed
priority order — `Unsupported` (a **permanent** condition) always wins over
`Absent` (a **temporary** one — capture off, or a Present slot not yet
warmed up) wins over `Present` (a real, freshly-read measurement). Lives in
the same Vulkan-header-free `GpuTiming.h` as `InterpretTimestampCapability()`
(Phase 4A's own pure decision function) — same file, same "extract the
decision, keep the live call site a thin wrapper" precedent.

### 4. Ownership wiring (`Renderer.h/.cpp`, `FramePresenter.h/.cpp`)

- `Renderer` gains a `std::shared_ptr<GpuTimingService> m_gpuTiming`,
  constructed explicitly in `Renderer`'s own constructor (right after
  `m_device`/`m_depthFormat`, before `m_allocator`/`m_presenter`) since
  `GpuTimingService` needs `m_device`'s already-resolved native
  handle/graphics queue+family/queried timestamp capability — the exact
  same "constructed here, shared via `shared_ptr`" pattern `m_memoryTracker`
  already establishes for `GpuMemoryTracker`, applied to a second
  cross-cutting concern. Threaded through `Renderer`'s hand-written
  move-assignment operator (`Renderer`'s move CONSTRUCTOR stays `= default`,
  unaffected, since every member — including the new one — is itself
  move-safe).
- `FramePresenter`'s constructor gains one new trailing parameter,
  `std::shared_ptr<GpuTimingService> gpuTiming`, stored into a new member
  `m_gpuTiming` — added the same way `memoryTracker` already is, threaded
  through `FramePresenter`'s hand-written move constructor/assignment too.
  Not yet called from `Present()`/`RenderOffscreen()`'s bodies — stored now
  so the whole ownership/move-plumbing graph is reviewed and landed once,
  in this sub-phase, rather than split awkwardly across 4B and 4C/4D.
- `Renderer` gains two new public methods:
  - `GpuTimingSample LastGpuTiming(GpuTimingSlot slot) const noexcept` —
    forwards to `m_gpuTiming->LastKnown(slot)`.
  - `void SetGpuTimingCaptureEnabled(bool enabled) noexcept` — forwards to
    `m_gpuTiming->SetCaptureEnabled(enabled)`. Takes a plain `bool` (never a
    `Profiling::`-namespaced type), keeping `Renderer` completely free of
    any `Profiling/` header — `Application.cpp` doesn't call this yet
    either; that starts in Phase 4C, alongside the first real per-frame
    recording code it's meant to gate.

### 5. Build wiring

- `CMakeLists.txt`: added `src/Renderer/GpuTimingService.cpp/.h` and
  `src/Renderer/Vulkan/VulkanQueryPool.cpp/.h` to `gte_core`'s source list.
- `tests/CMakeLists.txt`: updated the `Renderer/GpuTimingTests.cpp`
  description in the file's own Tier-1 taxonomy comment header to mention
  Phase 4B's `ResolveGpuTimingStatus()` addition.

### 6. Tests (`tests/Renderer/GpuTimingTests.cpp`, extended, still Tier 1)

5 new `TEST()` cases added to the existing Phase 4A test file, covering all
8 combinations of `ResolveGpuTimingStatus()`'s 3 boolean inputs: the
all-good `Present` case; `Absent` when capture is disabled; `Absent` when
not yet written (Present-path warm-up); `Absent` when both; and
`Unsupported` winning regardless of the other two inputs (all 4 remaining
combinations). Zero Vulkan/live-device involvement, same as every other
test in this file.

## Explicitly NOT done in this session (matches the strategy document's own Phase 4B scope exactly)

- `FramePresenter::Present()`/`RenderOffscreen()` do **not** call any of
  `GpuTimingService`'s `Record*`/`Read*` methods anywhere — the service
  exists, is correctly constructed/reset/destroyed/moved, but is completely
  unused by any real rendering code yet.
- No change to `Renderer::RenderOffscreen()`'s signature (the new
  `std::optional<GpuTimingSlot>` parameter is Phase 4C's addition).
- No `Profiling::FrameProfiler::SetGpuPassTiming()` call anywhere in
  production code — the Profiler panel's "GPU Timing" section is
  completely unaffected by this session and still honestly shows `N/A`,
  for the same reason as before (nothing reports to it yet).
- `Application::Run()` does **not** call
  `Renderer::SetGpuTimingCaptureEnabled()` — the method exists (reviewed
  and landed alongside the rest of this phase's public API surface), but
  its production call site is Phase 4C's job, once there's actual
  per-frame recording work for it to gate.

## Verification performed

- **Configure + build (default config)**: `cmake -S . -B build` then
  `cmake --build build --config Debug` — both `GreatTamanaEngineTests` and
  the real `GreatTamanaEngine` executable link cleanly with no new
  warnings from the touched/new files.
- **Test suite**: `ctest -C Debug --output-on-failure` — **521 tests
  total, 520 passed, 1 pre-existing machine-gated smoke test skipped** (up
  from the prior session's 515+1 baseline — the +5 delta is exactly this
  session's new `ResolveGpuTimingStatus` cases; every pre-existing test
  still passes unchanged).
- **Runtime smoke test (real Vulkan device)**: launched the built
  `GreatTamanaEngine.exe` and confirmed it stays running (doesn't crash) —
  this is the concrete verification that `GpuTimingService`'s constructor
  actually creates a real `VkQueryPool` and successfully performs its
  one-time, self-contained warm-up reset (a real `vkCreateCommandPool`/
  `vkAllocateCommandBuffers`/`vkCmdResetQueryPool`/`vkQueueSubmit`/
  `vkWaitForFences` sequence) against live hardware without any validation
  failure or hang.
- **`GTE_ENABLE_PROFILER=OFF` build**: configured and built a separate,
  throwaway build tree with `-DGTE_ENABLE_PROFILER=OFF -DGTE_BUILD_TESTS=OFF`
  — builds cleanly, and the resulting executable was also launched and
  confirmed to keep running (proving the constructor's `#if
  !GTE_ENABLE_PROFILER` branch — which forces `m_capability.supported =
  false` and skips `VkQueryPool` creation entirely — compiles and executes
  correctly, i.e. a release-style build never creates this Vulkan object at
  all). This throwaway build tree was deleted afterward; it is not part of
  the repository.

## Notes / anomalies encountered (not a tool bug — a real, pre-existing multi-line-edit hazard worth flagging)

Several `edit_line` calls in this session initially produced duplicated
trailing lines (a comment block, a struct member, a closing brace) when the
replacement content's own tail happened to repeat text that was already
present immediately after the replaced range. Each occurrence was caught by
immediately re-reading the file after the edit and was corrected with a
follow-up `edit_line` removing the duplicate — no build was attempted with
the duplication still present, so no broken state was ever committed. This
is a reminder to keep `length` and the replacement `contents` tightly
scoped to exactly what's changing, rather than including trailing context
that already exists unchanged past the replaced range.
