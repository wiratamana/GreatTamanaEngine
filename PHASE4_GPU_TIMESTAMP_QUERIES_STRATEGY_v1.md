# Phase 4 — Vulkan GPU Timestamp Queries — Implementation Strategy (v1)

Status: PROPOSAL / PLANNING DOCUMENT — no implementation yet.
Scope: exactly one phase of `PROFILER_STRATEGY_v2.md`'s eight-phase plan —
the one that document itself calls "the substantial technical phase" and
"the most substantial/risky remaining phase" in
`PROFILER_IMPLEMENTATION_STATUS_v6.md`. Every other phase (0/1/2/3/5/7) is
already implemented (see `PROFILER_IMPLEMENTATION_STATUS_v6.md`) — this is
the last piece standing between the Editor's "Profiler" panel showing an
honest **"GPU Timing: N/A"** line forever and it showing real, per-pass,
driver-measured milliseconds.

This document exists because Phase 4 is genuinely different in kind from
every phase already shipped: it is the only one that touches raw Vulkan
synchronization (query pools, command-buffer-recorded resets/writes, a
read-back that must never introduce a new GPU stall), across TWO
structurally different submission paths (`RenderOffscreen()`'s synchronous
fence-blocking path and `Present()`'s double-buffered frames-in-flight
path) that must each be reasoned about separately. Getting the ORDER of
operations wrong here doesn't just produce a wrong number on a graph — it
can silently corrupt validation-layer state, read garbage query data, or
(worse) introduce a stall that regresses the very frame time this feature
exists to measure. This is exactly why it is being planned as its own
document, chunked into four independently-shippable sub-phases (4A → 4B →
4C → 4D), rather than attempted as one big change.

This plan is written in the exact same code-free, "why before what"
spirit as `PROFILER_STRATEGY_v2.md` itself, and was produced by actually
reading the current source (`src/Profiling/ProfilingTypes.h`,
`FrameProfiler.h`, `src/Renderer/Vulkan/VulkanDevice.h`,
`src/Renderer/FramePresenter.h/.cpp`, `FrameRecorder.h`,
`Vulkan/VulkanFrameSync.h`, `Renderer.h`, `DrawStats.h`,
`Renderer/Memory/GpuMemoryTracker.h`, and `Application.cpp`'s actual
`Application::Run()` loop) before proposing anything — every concrete
detail below (exact call sites, exact existing fence-wait sequencing,
exact struct/method names) is grounded in what the engine's code actually
does today, not a generic Vulkan-timestamp tutorial pasted in from
nowhere.

--------------------------------------------------------------------------
## Step 1: The Goal (Where are we going?)
--------------------------------------------------------------------------

### 1.1 Vision

Today, the Editor's "Profiler" panel (`src/Editor/Panels/ProfilerPanel.cpp`,
shipped in the Phase 7 session) already renders a **"GPU Timing"** section
for all three named `Profiling::GpuPass` values —
`GameView`/`SceneView`/`Present` — and it is already wired, end to end,
to show a real per-pass millisecond value the INSTANT
`Profiling::FrameProfiler::SetGpuPassTiming(GpuPass, GpuSampleStatus,
double)` is ever called with `GpuSampleStatus::Present` for that pass.
Nobody has to touch a single line of Editor/data-model code to make real
GPU timing appear — that seam was deliberately built in Phase 0/7 and left
waiting. **Phase 4's entire job is to become the first, and only, real
production caller of `SetGpuPassTiming()`** — today it is called by
nothing except test code (`tests/Profiling/FrameProfilerTests.cpp`),
exactly the way `PROFILER_IMPLEMENTATION_STATUS_v6.md` describes it.

Concretely, when this phase is done:

- A developer looking at the "Profiler" panel's "GPU Timing" section sees
  three real numbers (or an honest "N/A" for whichever pass didn't run, or
  a permanent, honest "Unsupported" on a device that genuinely can't
  produce this measurement) instead of three permanent placeholders.
- Those numbers answer the actual question this whole Profiler effort was
  started for: "is my frame CPU-bound or GPU-bound, and if GPU-bound,
  which of my three recorded passes (drawing 'Game', drawing 'Scene',
  presenting/drawing Dear ImGui's own chrome) is actually costing GPU
  time" — a question NONE of Phases 0/1/2/3/5/7 could ever answer on their
  own, since every one of them is CPU-side measurement (wall-clock scope
  timers, or a memory snapshot) standing in for a GPU cost that may be
  very different from what the CPU took to record/submit it.
- This is delivered with **zero new stalls**: the frame-pacing behavior a
  user already experiences today (two frames in flight on the swapchain,
  a fully synchronous offscreen path for "Game"/"Scene") must be
  byte-for-byte unchanged — Phase 4 only ever reads data that was already
  guaranteed available by synchronization the engine already performs for
  unrelated reasons (an existing fence wait), never a wait added
  specifically to fetch a timestamp sooner.
- This is delivered with **zero new special-casing for hidden panels or
  minimized windows** — a "Game"/"Scene" panel that's tabbed-and-hidden,
  or a minimized main window, must continue to report GPU timing as
  `Absent` for that pass this frame, exactly the same tri-state discipline
  every other Profiler phase already respects (see
  `PROFILER_IMPLEMENTATION_STATUS_v6.md`'s own "no data this frame, never
  a fabricated 0.00 ms" rule, already true of the Panel's *placeholder*
  text and must stay true once it's showing *real* data).
- This is delivered with **honest degradation on a device that doesn't
  support timestamp queries at all** (rare on desktop, but real, e.g. some
  software rasterizers) — GPU Timing reports `Unsupported` forever on that
  machine, never a crash, never a silently-wrong number.

### 1.2 Concrete deliverables, mapped 1:1 to the four sub-phases

| Sub-phase | Deliverable | "Done" means... |
|---|---|---|
| **4A** | Device capability query + pure helper math, zero Vulkan resources created | The engine builds, every new pure function has a passing Tier-1 test, and an unsupported device is provably detected (without yet reporting anything to the Profiler, since nothing calls `SetGpuPassTiming()` yet). |
| **4B** | A real, RAII-owned `VkQueryPool` sized for every pass/path, wired into `Renderer`'s ownership graph | Normal rendering (Editor open, validation layers on) is completely unaffected — the pool exists and is correctly reset, but nothing reads from it or reports to the Profiler yet. |
| **4C** | Game View + Scene View GPU timing, using the already-synchronous offscreen fence | The "Profiler" panel shows real GPU milliseconds for whichever of "Game"/"Scene" is currently visible, and an honest `N/A` for whichever is hidden — never `0.00 ms`. |
| **4D** | Present-pass GPU timing, using the existing two-frames-in-flight fence rotation | Present's GPU Timing shows real milliseconds with zero new stalls, zero new validation errors, and zero regressions to resize/minimize behavior. |

### 1.3 Success criteria (how we will know this actually worked)

- **Correctness, cross-checked against an external tool.** Before this
  phase is considered done, the reported millisecond values for at least
  one representative frame are cross-checked against RenderDoc or NVIDIA
  Nsight Graphics attached to a debug build (the same "Tier 2, verified
  manually" acceptance `AGENTS.md`'s Testability section already extends
  to every other GPU-device-dependent piece of this engine) — agreement
  within the tool's own measurement granularity, not bit-exact (different
  tools/paths can legitimately round differently).
- **Zero added frame-pacing regression.** A frame-time comparison (Phase
  1's own CPU scope timers, plus a manual FPS/frame-time glance) between
  this engine built with `GTE_ENABLE_PROFILER=OFF` vs. `ON`-with-Phase-4-
  enabled shows no measurable new stall — the two existing fence waits
  Phase 4 piggybacks on (the offscreen fence, the per-frame-in-flight
  swapchain fence) are EXACTLY the same waits that exist today; nothing
  new is ever awaited.
- **The tri-state discipline holds under adversarial manual testing.**
  Toggling "Scene" tabbed-hidden-behind-"Game" (or vice versa) for a few
  seconds and watching the Profiler panel shows the hidden pass's GPU
  Timing line correctly go to (and stay at) `N/A`, never freeze on a
  stale number and never show `0.00 ms`. Minimizing the main window for a
  few seconds and restoring it shows the same for `Present`, and resizing
  the window repeatedly (including down to zero/minimized and back)
  produces no validation errors and no crash.
- **A genuinely unsupported device degrades gracefully.** Since the
  current development machine is expected to support timestamp queries
  (see Step 2.2), this specific criterion is verified by a forced-failure
  test path (see Phase 4A's testing section) rather than requiring access
  to real unsupported hardware — but the code path itself must exist and
  be exercised, not merely assumed correct by inspection.
- **The full existing test suite (502+ tests as of
  `PROFILER_IMPLEMENTATION_STATUS_v6.md`) still passes after every
  sub-phase**, and every new piece of PURE logic (capability
  interpretation, tick→millisecond conversion, slot-indexing math) ships
  with its own Tier-1 test in the same change — see Step 3's own
  per-sub-phase testing sections.
- **`AGENTS.md`'s "Profiling" section gains real, no-longer-hypothetical
  content**: today it says GPU timing is "unwired to anything real as of
  Phase 3" (`PROFILER_IMPLEMENTATION_STATUS_v6.md`) — this phase is what
  finally makes that sentence obsolete, and the document must be updated
  to say so, mirroring how every other phase's landing updated it.
- **Nothing about `Game`/`RenderSystem`/`AnimationSystem`/
  `MeshInstantiationSystem`'s public API changes at all** — this is purely
  additive Renderer/FramePresenter/Application-level instrumentation, per
  `PROFILER_STRATEGY_v2.md`'s own Step 4 refusal ("No rewrite of
  `FrameRecorder`/`FramePresenter`/the draw-submission pipeline").

--------------------------------------------------------------------------
## Step 2: The Situation / The Problem (Where are we now?)
--------------------------------------------------------------------------

### 2.1 What already exists, and is directly reusable

- **The data model this phase writes into already exists, fully, and is
  already correct for this purpose.**
  `src/Profiling/ProfilingTypes.h`'s `GpuPassSample` already has exactly
  the shape Phase 4 needs: `timingStatus`/`milliseconds`, DELIBERATELY
  split from `countStatus`/`drawCallCount`/`triangleCount` (Phase 3's own
  concern) — confirmed directly against the current header, which spells
  out in its own comment that this split exists precisely so "a
  draw-stats-only call must never imply real GPU timing data" (this is
  also directly regression-tested today, in
  `tests/Profiling/FrameProfilerTests.cpp`). `GpuPass` is already a fixed
  3-entry enum (`GameView = 0, SceneView = 1, Present = 2`,
  `kGpuPassCount = 3`) — exactly the three passes this phase needs to
  measure, no more, no fewer.
- **`FrameProfiler::SetGpuPassTiming(GpuPass pass, GpuSampleStatus status,
  double milliseconds = 0.0)` already exists, is already correctly
  implemented, and is already unit-tested** (`FrameProfiler.h`/`.cpp`,
  confirmed by direct reading) — Phase 4 needs to call this function with
  real arguments; it does not need to add, change, or even touch this
  function's signature or body.
- **The Editor's "Profiler" panel already renders a "GPU Timing" section
  for all three passes, already honestly showing `N/A` today, and needs
  ZERO code changes for real data to appear.** Confirmed via
  `PROFILER_IMPLEMENTATION_STATUS_v6.md`'s own words: "wired to start
  showing real numbers, per pass, the moment this phase lands, with zero
  further Editor-side changes needed." This means Phase 4's success is
  entirely measured by whether real values start flowing INTO
  `FrameProfiler`, not by any UI work at all — a materially simpler scope
  than any of Phases 0/1/2/3/5/7 had, each of which needed at least some
  new Editor-facing code.
- **This engine already requires Vulkan 1.3 with `synchronization2`
  enabled unconditionally** — confirmed directly in
  `src/Renderer/Vulkan/VulkanInstance.cpp` (`appInfo.apiVersion =
  VK_API_VERSION_1_3`) and `VulkanDevice.cpp`'s `CreateLogicalDevice()`
  (the `VkPhysicalDeviceVulkan13Features` struct already sets
  `dynamicRendering` — gated on by device selection — alongside
  `synchronization2`, both requested in the same feature chain). This
  means **`vkCmdWriteTimestamp2` and `VkPipelineStageFlagBits2` are safe
  to call directly, today, on every device this engine can already run
  on** — there is no separate extension negotiation to add for this
  phase, and no reason to fall back to the legacy
  `vkCmdWriteTimestamp`/`VkPipelineStageFlags` pair. `volk` (this engine's
  dynamic Vulkan meta-loader — see `BUILDING.md`) resolves
  `vkCmdWriteTimestamp2` as a normal core-1.3 entry point via
  `volkLoadDevice()`, already called once `VulkanDevice` exists — nothing
  new needed there either.
- **`VulkanDevice` (`src/Renderer/Vulkan/VulkanDevice.h/.cpp`) already
  has exactly the right shape and precedent to extend.**
  `PickDepthFormat()` is the existing "ask the device, don't hardcode a
  literal" pattern (queries `vkGetPhysicalDeviceFormatProperties()` once,
  exposes the result via a plain accessor) that a new timestamp
  capability query should copy exactly — not invent a new convention.
  `VulkanDevice` already exposes `Physical()`/`GraphicsQueueFamily()`,
  everything a capability query needs, with no new constructor parameters
  required.
- **`Renderer`'s three-collaborator split (`FramePresenter`/
  `GpuResourceFactory`/`FrameRecorder`) is already exactly where this
  phase's plumbing needs to live**, and the ownership precedent for a
  shared, cross-collaborator service is already established and directly
  copyable: `Renderer` constructs a single `std::shared_ptr<GpuMemoryTracker>`
  once (right after `m_device`, before `m_presenter`/`m_resources` are
  built) and hands a COPY of that `shared_ptr` into both
  `FramePresenter`'s constructor and `GpuResourceFactory`'s — confirmed
  directly against `Renderer.h`'s member-declaration-order comments and
  `FramePresenter.cpp`'s constructor, which already takes a
  `std::shared_ptr<GpuMemoryTracker> memoryTracker` parameter and moves it
  into a member. **A GPU-timing service should be constructed and shared
  the exact same way** — this is not a new pattern to invent, it is the
  same one already used for GPU memory tracking, applied to a second
  cross-cutting concern.
- **`FramePresenter::Present()`/`RenderOffscreen()` already own the exact
  raw `VkCommandBuffer` this phase needs to record into, at exactly the
  granularity needed.** Confirmed directly against `FramePresenter.cpp`:
  both functions call `vkBeginCommandBuffer()`, then
  `frameRecorder.RecordFrame(cmd, ...)` (which does the actual
  `vkCmdBeginRendering`/draws/`vkCmdEndRendering`, PLUS the caller-supplied
  `recordExtra` — i.e. Dear ImGui's own chrome, for `Present()`), then
  `vkEndCommandBuffer()`. A GPU timestamp pair that should bracket "the
  WHOLE pass, including any Editor-chrome-drawing `recordExtra`" belongs
  exactly at this level — immediately after `vkBeginCommandBuffer()`
  succeeds, and immediately before `vkEndCommandBuffer()` — never inside
  `FrameRecorder::RecordFrame()` itself (see Step 2.3 for why).
- **`RenderOffscreen()`'s synchronization is ALREADY exactly what Phase 4C
  needs, unmodified.** Confirmed directly against `FramePresenter.cpp`'s
  `RenderOffscreen()`: it already (1) waits on `m_frameSync.OffscreenFence()`
  BEFORE recording (confirming the PREVIOUS use of this same command
  buffer/fence pair fully completed), and (2) waits on that SAME fence
  again AFTER submitting, before returning (fully synchronous — see the
  function's own existing comment, "Synchronous for now"). This means a
  query written between this function's `vkBeginCommandBuffer()`/
  `vkEndCommandBuffer()` is GUARANTEED complete and safely readable the
  MOMENT this function's own final `vkWaitForFences()` call returns — no
  new wait, no `VK_QUERY_RESULT_WAIT_BIT` even strictly required (though
  using it defensively costs nothing here, since the fence wait already
  makes the query complete by the time it would be reached).
- **`FramePresenter::Present()`'s existing two-frames-in-flight fence
  rotation ALREADY gives Phase 4D the exact synchronization point it
  needs, unmodified.** Confirmed directly against `FramePresenter.cpp`:
  `Present()` calls `vkWaitForFences(fence[m_currentFrame], ...)` BEFORE
  doing anything else (right after the minimized/resize early-return
  checks) — this fence signals when the LAST submission that used THIS
  SAME `m_currentFrame` slot (i.e., exactly `kFramesInFlight == 2` frames
  ago) has fully finished executing on the GPU. **This is precisely, and
  for free, the exact synchronization point Phase 4D needs to safely read
  back that slot's previous query results** — no new fence, no new wait,
  just reading at a point the code already reaches every single frame.
- **`Application::Run()` is already the ONE place that knows which named
  `GpuPass` a given `RenderOffscreen()`/`Present()` call corresponds to**,
  and it already calls `FrameProfiler::SetGpuPassDrawStats(GpuPass::...,
  GpuSampleStatus::Present, ...)` immediately after each one, INSIDE the
  exact `if (gameTarget != nullptr)` / `if (sceneTarget != nullptr)` /
  `if (presentStats.has_value())` guards that already correctly encode
  "did this pass actually run this frame." Confirmed directly against
  `Application.cpp`. **Phase 4 slots its own
  `SetGpuPassTiming(GpuPass::..., ...)` calls into these EXACT SAME
  guarded blocks** — this is not a new architectural seam to design, it
  is reusing, verbatim, the seam Phase 3 already proved out and left
  sitting right next to where the new calls belong.
- **`src/Renderer/DrawStats.h` is the direct structural precedent for how
  this phase's own pure math/data types should be shaped and where they
  should live**: a small, Vulkan-header-free (!) struct plus pure,
  allocation-free functions, living under `src/Renderer/` despite having
  zero Vulkan dependency, specifically so it stays trivially
  Tier-1-testable (`tests/Renderer/DrawStatsTests.cpp` needs no live
  device at all). Confirmed by reading `DrawStats.h` directly — it
  includes only `<cstdint>`/`<span>`, no `<volk.h>`. Phase 4's own pure
  helper module (capability struct, tick→millisecond conversion, slot
  indexing) should follow this exact same "lives under `Renderer/`, zero
  Vulkan headers" shape.

### 2.2 What is genuinely missing today (the actual gap)

- **`VulkanDevice` performs NO timestamp capability query of any kind.**
  Confirmed directly by reading `VulkanDevice.h`/`.cpp` in full — there is
  no read of `VkPhysicalDeviceLimits::timestampPeriod`/
  `timestampComputeAndGraphics`, and no read of
  `VkQueueFamilyProperties::timestampValidBits` anywhere in the codebase.
  This is new plumbing, not an extension of something half-built — but it
  is a small, well-precedented addition (see `PickDepthFormat()` above),
  not a risky one.
- **No `VkQueryPool` of any kind exists anywhere in the codebase today.**
  Confirmed by reading every file under `src/Renderer/Vulkan/` — no
  `VK_QUERY_TYPE_TIMESTAMP`, no `vkCreateQueryPool`, no
  `vkCmdWriteTimestamp2`/`vkGetQueryPoolResults` call anywhere. This phase
  introduces this concept to the engine for the first time.
- **`FramePresenter`/`Renderer` have no shared "GPU timing" collaborator
  analogous to `GpuMemoryTracker`.** The ownership PATTERN already exists
  (see 2.1) but the actual class does not — Phase 4B is what creates it.
- **`Renderer::RenderOffscreen()`/`Present()` have no way for a caller to
  say WHICH of possibly-several logical passes a given call corresponds
  to**, at the Renderer/FramePresenter level — today, `Application.cpp`
  calls the exact same `Renderer::RenderOffscreen(RenderTexture&)`
  overload for BOTH the Game view and the Scene view, back to back, and
  the only thing that currently distinguishes "this was the Game one" vs.
  "this was the Scene one" is which literal source-code block the call
  sits inside (see `Application.cpp`'s `if (gameTarget != nullptr)` /
  `if (sceneTarget != nullptr)` blocks). This is perfectly sufficient for
  Phase 3's draw-stats reporting (the return value is consumed
  immediately, inline, in the right block) but Phase 4 additionally needs
  FramePresenter itself to know which of (at least) two independent query-
  pool slot identities to write into for a GIVEN offscreen call — today
  there is no parameter for this at all. This is a real, new, small API
  surface this phase must add (see Step 3, Phase 4C).
- **There is no concept anywhere in the engine today of "read back a
  measurement from N frames ago, at the exact point synchronization
  already proves it's safe to."** Every existing per-frame measurement
  (CPU scope timers, draw-call counts, memory snapshots) is entirely
  synchronous/immediate — computed and reported within the SAME frame it
  describes. Present-pass GPU timing is structurally different: because
  of the two-frames-in-flight design, the data Phase 4D reports on a given
  frame N is unavoidably about frame N-2's Present pass, not frame N's own
  (which hasn't finished on the GPU yet). This is a genuinely new shape of
  data flow for this codebase, and must be designed deliberately (see Step
  2.3's own constraint below), not bolted on by accident.

### 2.3 Constraints discovered while reading the code (must be respected)

- **`FrameRecorder` must NOT gain any awareness of query pools, GPU
  timing, or `GpuPass` identity.** Its own class comment is explicit that
  it "deliberately holds no Vulkan device/queue/command-pool state of its
  own" and is the SAME shared recording routine for both `Present()` and
  `RenderOffscreen()`. Threading a `VkQueryPool`/slot-identity parameter
  through `RecordFrame()` would entangle a class whose entire reason for
  existing is to be a thin, reusable, state-free recording routine with a
  concern (which of several possible GPU-timing slot identities is this
  particular call) that only `FramePresenter` (which already knows
  whether it's being called for `Present()` or `RenderOffscreen()`, and,
  after this phase's own small API addition, WHICH offscreen slot) has
  any business knowing. This mirrors exactly why `DrawStats` counting is
  fused INLINE into `RecordFrame()`'s existing per-item draw loop (a
  correctness requirement, per `DrawStats.h`'s own comment) while GPU
  timestamp writes are NOT — the two are fundamentally different in kind:
  draw-call counting is a per-item accumulation that must stay perfectly
  synchronized with what's actually drawn, item by item, inside the loop
  `RecordFrame()` already owns; a GPU timestamp pair is a single
  bracket around the WHOLE recorded pass (including `recordExtra`, which
  `RecordFrame()` also already owns calling) — so it belongs at the
  caller level (`FramePresenter::Present()`/`RenderOffscreen()`, which
  already wrap `RecordFrame()` between their own `vkBeginCommandBuffer()`/
  `vkEndCommandBuffer()` pair), not inside it.
- **A `VkQueryPool` slot must be reset (`vkCmdResetQueryPool`) before
  every reuse — this is a hard Vulkan validity rule, not an optimization.**
  Writing a timestamp into a query slot that was never reset since its
  last use (or never reset at all, on its very first use) is invalid per
  the spec and will surface as a validation error (and, on some drivers,
  garbage data) rather than a clean failure. The reset MUST be recorded
  into the SAME command buffer as the writes that will reuse that slot
  (there is no host-side `vkResetQueryPool` available here — that
  requires `VK_EXT_host_query_reset`, which this engine does not, and
  should not, request solely for this — see Step 4's own refusal on
  scope-creeping extension requests). This means every code path that
  writes a timestamp pair must ALSO record a reset for that exact slot
  range immediately beforehand, in the same command buffer, and — for the
  offscreen (Game/Scene) path specifically — only after this phase's own
  synchronization has already confirmed the PREVIOUS use of that slot is
  no longer executing (see the offscreen fence-wait ordering above).
- **The offscreen path (Game/Scene) and the swapchain Present path need
  GENUINELY DIFFERENT query-pool sizing/multiplexing strategies — treating
  them identically would be a real correctness bug, not just a missed
  optimization.** `RenderOffscreen()` is already fully synchronous (see
  2.1) — a single set of slots per named offscreen pass, reused every
  call with no additional buffering, is completely safe: by the time this
  phase's own new reset+write code for a GIVEN call executes, the
  PREVIOUS use of that exact slot pair (from the previous frame's call for
  the SAME logical pass) is already guaranteed complete, because
  `RenderOffscreen()`'s own existing pre-recording fence wait already
  proved it. The swapchain Present path is different: `kFramesInFlight ==
  2` (confirmed in `FramePresenter.h`) means TWO logically-concurrent
  "in-flight" submissions can exist for Present at once, so its query pool
  needs `kFramesInFlight` independent sets of slots (one per
  frame-in-flight index), not one — using only one set here WOULD be
  reading/writing a slot that a still-executing submission might still be
  using, a genuine race/validation violation, not merely a design
  suboptimality.
- **Reading back a Present-pass query result must happen exactly where
  the existing per-frame-in-flight fence wait already happens — never
  earlier (unsafe/undefined data) and never via a NEW wait (a stall this
  phase must never introduce).** `FramePresenter::Present()`'s existing
  `vkWaitForFences(fence[m_currentFrame])` call, right at the top of the
  function, is the ONLY point in the entire frame where "the submission
  that most recently wrote into query slot set `m_currentFrame` has
  definitely finished" is both TRUE and already being waited for anyway,
  for a completely unrelated existing reason (reusing that same frame's
  command buffer safely). Phase 4D's read-back must be inserted at
  EXACTLY this point, reusing this exact wait — inserting it anywhere
  else would either read stale/in-flight data or require adding a
  redundant second wait that duplicates one already being performed.
- **Neither `RenderOffscreen()` nor `Present()` may gain a NEW GPU wait
  purely to make a timestamp result available sooner than the engine
  would otherwise have confirmed it.** This is explicitly called out in
  `PROFILER_STRATEGY_v2.md`'s own Phase 4 design ("Do not add a new GPU
  wait just to get timing results earlier" — also directly present in the
  attached `Phase4.md`'s own Phase 4D description) and is the single
  easiest mistake to make when a first-time Vulkan-query implementer
  reaches for `VK_QUERY_RESULT_WAIT_BIT` indiscriminately, or is tempted
  to `vkDeviceWaitIdle()` "just to be safe" before reading results. Every
  read in this plan is positioned at a point synchronization the engine
  ALREADY performs (for unrelated reasons) has already made safe — this
  constraint is what makes that true, and must not be violated by a future
  edit "simplifying" the read-back logic.
- **`FrameRecorder::HasQueuedDraws()`/the swapchain's lazily-allocated
  per-image `DepthBuffer`s are UNRELATED to this phase and must not be
  disturbed.** `Present()`'s existing `needsDepth` branch
  (`EnsureDepthBuffersForSwapchain()`) is orthogonal machinery this phase
  must record its timestamp writes AROUND, not interact with — a
  timestamp write does not care whether the pass being measured used a
  depth attachment or not; it only cares about wall-clock GPU execution
  time for the bracketed command range.
- **A minimized window (`m_pendingWidth <= 0 || m_pendingHeight <= 0`)
  already causes `Present()` to `return std::nullopt` BEFORE reaching the
  fence wait at all** — confirmed directly in `FramePresenter.cpp`. This
  means `m_currentFrame` does NOT advance on a minimized frame, and
  neither does anything Phase 4D adds — there is no slot-index skew risk
  here (the same slot that would have been used this frame is simply used
  again, unchanged, the next time `Present()` actually runs), and no GPU
  Timing data is reported for `Present` that frame (consistent with
  `Application.cpp`'s existing `if (presentStats.has_value())` guard,
  which already correctly skips `SetGpuPassDrawStats()` on this exact
  path — Phase 4D's own `SetGpuPassTiming()` call must live inside this
  SAME guard for the same reason).
- **A resize (`m_resizeRequested`) triggers `RecreateSwapchain()`, which
  rebuilds `m_swapchain`, `m_frameSync`'s per-swapchain-image render-
  finished semaphores, and (conditionally) the per-swapchain-image depth
  buffers — but does NOT touch `m_frameSync`'s per-frame-in-flight
  fences/image-available semaphores, and must not need to touch a Present-
  pass query pool either.** Confirmed directly in `FramePresenter.cpp`'s
  `RecreateSwapchain()`. Since the Present-pass query pool this phase adds
  is indexed purely by `m_currentFrame` (0 or 1), NOT by swapchain image
  index or swapchain image COUNT, a resize has no bearing on its sizing or
  validity at all — it must be constructed once (Phase 4B) and never
  recreated for the lifetime of the `FramePresenter`, exactly like the
  per-frame-in-flight fences themselves.
- **`Renderer`'s own move-assignment (`operator=(Renderer&&)`) and
  `FramePresenter`'s hand-written move constructor/assignment already
  exist and must keep working.** Any new member this phase adds to
  `FramePresenter` (a `shared_ptr` to the new GPU-timing collaborator) must
  be threaded through those existing hand-written move operations
  (`std::move`, matching how `m_memoryTracker` already is) — a forgotten
  member in a hand-written move function is a classic, easy-to-miss
  regression class this codebase already has to be careful about (see
  `FramePresenter.cpp`'s move constructor/assignment, which already lists
  every member explicitly).
- **This is Tier 2 work by nature, and that is an ACCEPTED, already-
  documented condition of this codebase, not a gap Phase 4 is expected to
  close.** `Buffer`/`RenderTexture`/`Pipeline`/`GpuResourceFactory`/
  `FramePresenter`/`FrameRecorder` already have "no automated test
  coverage" per `TESTING.md`'s own words, verified instead by "build and
  run against a real GPU/window as a sanity check" per `AGENTS.md`'s
  Testability section. The new `VkQueryPool` wrapper and its actual
  read-back correctness fall squarely into this same bucket — what MUST
  still be Tier-1-tested is every PURE piece extracted alongside it (see
  Step 3, Phase 4A) — this phase does not get a pass on that half of the
  work merely because the Vulkan-touching half is accepted as Tier 2.

--------------------------------------------------------------------------
## Step 3: The Plan (How will we get there?)
--------------------------------------------------------------------------

Four sub-phases, in strict order — **4A → 4B → 4C → 4D**, exactly as
named in the attached `Phase4.md`. Each is independently buildable,
independently testable, and each leaves the engine in a fully working,
fully test-suite-green state — including the ability to STOP after 4A,
4B, or 4C and still have shipped something real and valuable (see Step
4's risk/rollback framing). Nothing in a later sub-phase requires
redesigning an earlier one; each strictly adds to what came before.

### Overall API surface this phase introduces (read before the per-phase detail below)

To avoid re-deriving the same names in every sub-phase's own text, here is
the complete set of new types/functions this whole phase adds, decided
once, up front (each is introduced by exactly the sub-phase named):

- **`gte::GpuTimestampCapability`** (struct, Phase 4A) — `bool supported`,
  `float timestampPeriodNs`, `std::uint32_t validBits`. Lives in a new,
  Vulkan-header-FREE file, `src/Renderer/GpuTiming.h/.cpp`, mirroring
  `DrawStats.h`'s own precedent exactly (see Step 2.1) — pure data/pure
  functions only.
- **`gte::ConvertTimestampDeltaToMilliseconds(std::uint64_t startTicks,
  std::uint64_t endTicks, float timestampPeriodNs, std::uint32_t
  validBits) noexcept -> double`** (pure function, Phase 4A, in the same
  file) — the tick-delta → millisecond conversion, wraparound-safe via
  `validBits` masking (see Phase 4A's own detail below).
- **`gte::GpuTimingSlot`** (enum class, Phase 4A) — `Offscreen0 = 0`,
  `Offscreen1 = 1`, `SwapchainPresent = 2`; `kGpuTimingSlotCount = 3`.
  Deliberately named GENERICALLY (not `GameView`/`SceneView`) —
  `Renderer`/`FramePresenter` must never need to know Editor-facing pass
  naming (that mapping is `Application.cpp`'s job alone, exactly as it
  already is for `Profiling::GpuPass` today — see Step 2.3's own
  `FrameRecorder` decoupling rule, applied one layer up here too).
- **`gte::GpuTimingSample`** (struct, Phase 4A) — `enum class Status :
  std::uint8_t { Absent, Present, Unsupported } status`; `double
  milliseconds`. Deliberately a Renderer-local tri-state mirror of
  `Profiling::GpuSampleStatus`, NOT that type itself — keeps `Renderer`
  fully free of any `Profiling/` include, exactly as it is today; bridging
  the two tri-states into an actual `Profiling::GpuPass`/
  `SetGpuPassTiming()` call is `Application.cpp`'s job alone (a two-line
  `switch`/mapping function, trivial and Tier-1-testable on its own if
  desired).
- **`gte::VulkanQueryPool`** (class, Phase 4B, `src/Renderer/Vulkan/
  VulkanQueryPool.h/.cpp`) — RAII wrapper around one `VkQueryPool` of type
  `VK_QUERY_TYPE_TIMESTAMP`, fixed slot count, non-copyable/move-safe,
  exactly like every other class under `Vulkan/`.
- **`gte::GpuTimingService`** (class, Phase 4B, `src/Renderer/
  GpuTimingService.h/.cpp`) — owns the one `VulkanQueryPool` (when
  supported) plus the capability struct plus per-slot cached
  `GpuTimingSample` results and Present-path warm-up bookkeeping; the
  actual `vkCmdResetQueryPool`/`vkCmdWriteTimestamp2`/
  `vkGetQueryPoolResults` call sites live here, never inline in
  `FramePresenter.cpp` itself (keeping `FramePresenter` focused on
  ORCHESTRATING when to call these, not the raw Vulkan-query mechanics
  themselves — the same division of labor `FramePresenter` already has
  with `VulkanSwapchain`/`VulkanFrameSync`).
- **`VulkanDevice::TimestampCapability() const noexcept ->
  const GpuTimestampCapability&`** (Phase 4A) — the new accessor, queried
  once in the constructor, mirroring `PickDepthFormat()`'s own "ask once,
  expose via accessor" shape.
- **`Renderer::RenderOffscreen(RenderTexture& target, GpuTimingSlot
  timingSlot, const std::function<void(VkCommandBuffer)>& recordExtra =
  {})`** (Phase 4C) — ONE new parameter added to the existing method,
  telling `FramePresenter` which of `Offscreen0`/`Offscreen1` this
  particular call's GPU timing belongs to. Every existing call site must
  be updated to pass one (see Phase 4C).
- **`Renderer::LastGpuTiming(GpuTimingSlot slot) const noexcept ->
  GpuTimingSample`** (Phase 4C for `Offscreen0`/`Offscreen1`, Phase 4D
  for `SwapchainPresent`) — a cheap, non-destructive read of whatever
  `GpuTimingService` most recently cached for that slot; safe to call any
  number of times, including zero times in a frame where the
  corresponding pass didn't run (in which case it simply still holds
  whatever it last held — callers must ONLY call this immediately after a
  call that they know actually ran that pass this frame, exactly mirroring
  how `Application.cpp` already only reads `DrawStats` return values
  inside the guard that proves the corresponding pass ran).
- **`Renderer::Present(...)` gains no new PARAMETER** (unlike
  `RenderOffscreen()`) — Present is always exactly one logical pass, so
  there is nothing to disambiguate; `LastGpuTiming(GpuTimingSlot::
  SwapchainPresent)` is simply called (Phase 4D) right after
  `Present()`'s existing `if (presentStats.has_value())` guard.

### Phase 4A — Capability query and pure helper logic (no `VkQueryPool` yet)

**Goal, restated precisely:** everything needed to know, ONCE, whether
this device can do this at all, and everything needed to convert raw
ticks into milliseconds and pick the right slot index — all as pure,
allocation-free, Tier-1-testable logic — with **zero Vulkan resources
created**. This mirrors `PROFILER_STRATEGY_v2.md`'s own Phase 0's
philosophy (data model + math before any GPU object exists) applied
specifically to this one feature.

**1. `VulkanDevice` capability query** (`src/Renderer/Vulkan/
VulkanDevice.h/.cpp`):

- Add `struct GpuTimestampCapability { bool supported = false; float
  timestampPeriodNs = 0.0f; std::uint32_t validBits = 0; };` — either in
  `VulkanDevice.h` itself or (preferred, so `Renderer/GpuTiming.h` and
  `VulkanDevice.h` share one definition rather than two structurally
  identical structs) defined once in `src/Renderer/GpuTiming.h` and simply
  `#include`d by `VulkanDevice.h`. Recommendation: define it in
  `GpuTiming.h` (a `Renderer`-level, Vulkan-free concern) and have
  `VulkanDevice.h` include it — `VulkanDevice` already sits "below"
  `Renderer` in the dependency graph in spirit (it's a leaf Vulkan
  wrapper), but a plain POD struct with zero Vulkan types in it creates no
  real coupling either way; pick whichever the actual header-include graph
  makes least awkward once implementation starts, and note the choice
  made in a code comment so it isn't re-litigated.
- Add a private method, `QueryTimestampCapability()`, called once from the
  constructor AFTER `m_physicalDevice`/`m_graphicsFamily` are already
  resolved (i.e., after `PickPhysicalDevice()` runs, alongside where
  `CreateLogicalDevice()` is called) — mirrors exactly how
  `PickDepthFormat()` is documented as "queried once from the physical
  device" and exposed via accessor, except this one is eagerly computed
  and cached (since every consumer needs the same fixed answer for the
  device's entire lifetime, unlike `PickDepthFormat()` which is
  deliberately callable multiple times/lazily by different render
  targets).
- Query sequence (all read-only, no allocation, cheap):
  1. `vkGetPhysicalDeviceProperties(m_physicalDevice, &props);` — read
     `props.limits.timestampComputeAndGraphics` (bool) and
     `props.limits.timestampPeriod` (float, nanoseconds per tick — **0
     itself is a valid "not supported" signal** per the Vulkan spec, must
     be checked, not assumed positive).
  2. `vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &count,
     nullptr)` then again with a `std::vector<VkQueueFamilyProperties>`
     sized to `count` — read
     `families[m_graphicsFamily].timestampValidBits` (a bit count; **0
     itself is a valid "this family can't do it" signal**, again must be
     checked explicitly, never assumed non-zero).
  3. `supported = timestampComputeAndGraphics && timestampPeriod > 0.0f
     && validBits > 0`.
  4. Store the result in a `GpuTimestampCapability m_timestampCapability`
     member; expose via `const GpuTimestampCapability&
     TimestampCapability() const noexcept { return m_timestampCapability;
     }`.
- **Never throw on an unsupported result** — unlike `PickDepthFormat()`
  (which throws, because the Vulkan spec GUARANTEES at least one depth
  format always works), timestamp support has no such guarantee, and
  "unsupported" must be a completely normal, silently-handled outcome —
  exactly the same "degrade gracefully" convention this codebase already
  applies everywhere else (corrupt asset files, missing textures, an
  unmatched animation bone).

**2. Pure helper math** (`src/Renderer/GpuTiming.h/.cpp` — new file,
Vulkan-header-FREE, mirroring `DrawStats.h`'s precedent exactly):

- `ConvertTimestampDeltaToMilliseconds(std::uint64_t startTicks,
  std::uint64_t endTicks, float timestampPeriodNs, std::uint32_t
  validBits) noexcept -> double`:
  - Mask BOTH `startTicks` and `endTicks` to `validBits` before
    subtracting: `const std::uint64_t mask = (validBits >= 64) ?
    ~std::uint64_t{0} : ((std::uint64_t{1} << validBits) - 1);` then work
    with `startTicks & mask`/`endTicks & mask`. This is what makes the
    function correctly handle a device whose timestamp counter wraps
    around within `validBits` (a real, spec-acknowledged possibility on
    some hardware, even though a full 64-bit wrap in practice would take
    an astronomically long time on any GPU clock rate) — a delta computed
    as `((end & mask) - (start & mask)) & mask` is correct even across
    exactly one wraparound, which is the only case that can plausibly
    ever be hit in one single frame's bracket.
  - `const std::uint64_t deltaTicks = ((endTicks & mask) - (startTicks &
    mask)) & mask;`
  - `return static_cast<double>(deltaTicks) * static_cast<double>(timestampPeriodNs)
    / 1'000'000.0;` (nanoseconds → milliseconds).
  - Defensive floor: if `timestampPeriodNs <= 0.0f`, return `0.0`
    immediately rather than producing a nonsensical/negative-adjacent
    result — this should never actually be reached in practice (callers
    are expected to check `GpuTimestampCapability::supported` first), but
    costs nothing to guard and removes an entire class of "what if this
    is called wrong" question for the Tier-1 tests to have to special-case
    around.
- **A small, fixed indexing helper for the Present path's per-frame-in-
  flight slot multiplexing** — pure integer math, no Vulkan types:
  - `constexpr std::uint32_t kGpuTimingFramesInFlight = 2;` (mirrors
    `FramePresenter::kFramesInFlight` — see the design-decision log below
    for why this is a SEPARATE constant rather than reaching across module
    boundaries to reuse `FramePresenter`'s private one directly).
  - `constexpr std::uint32_t PresentTimestampSlotBase(std::uint32_t
    frameInFlightIndex) noexcept { return frameInFlightIndex * 2; }` — the
    "start" slot for a given frame-in-flight index; "end" is always
    `PresentTimestampSlotBase(...) + 1`. A simple, obviously-correct,
    easily-unit-tested formula — deliberately NOT hidden behind a more
    "clever" bit-packing scheme, since there is no performance reason to
    make this harder to read than it needs to be.
  - Total pool layout (documented once, here, as the single source of
    truth every later sub-phase and every code comment should point back
    to): slots `0`/`1` = `Offscreen0` start/end; slots `2`/`3` =
    `Offscreen1` start/end; slots `4`/`5` = `SwapchainPresent`
    frame-in-flight-0 start/end; slots `6`/`7` = `SwapchainPresent`
    frame-in-flight-1 start/end. **8 slots total** — a small, fixed,
    entirely-known-up-front pool size, never resized, never reallocated.

**3. Explicitly NOT done in this sub-phase:**
- No `VkQueryPool` created anywhere.
- No `vkCmdWriteTimestamp2`/`vkCmdResetQueryPool`/`vkGetQueryPoolResults`
  call anywhere.
- No change to `FramePresenter`/`Renderer`'s public API.
- No call to `FrameProfiler::SetGpuPassTiming()` anywhere in production
  code yet — every `GpuPassSample::timingStatus` in a real running frame
  remains `Absent`, exactly as it is today, and the Profiler panel's "GPU
  Timing" section is unaffected by this sub-phase (still shows `N/A`,
  correctly, for a completely different reason than before — not because
  Phase 4 hasn't landed conceptually, but because this specific sub-phase
  deliberately hasn't wired anything to report yet).

**Testing (Phase 4A):**
- New `tests/Renderer/GpuTimingTests.cpp` (Tier 1, zero live device
  needed, mirroring `tests/Renderer/DrawStatsTests.cpp`'s own shape
  exactly):
  - `ConvertTimestampDeltaToMilliseconds()`: a known period (e.g. a
    realistic `1.0f` ns/tick, and a second case with a GPU-realistic
    non-round value like `0.641291f`, an actual reported value class on
    real hardware) and a known tick delta produce the exact expected
    millisecond value (hand-computed, same "exact hand-verified values"
    discipline `Math/Mat4Tests.cpp` already uses).
  - Zero delta (`start == end`) produces exactly `0.0`.
  - A delta that would be NEGATIVE without masking (i.e. `end < start` in
    raw integer terms, simulating a wraparound within `validBits`)
    produces the CORRECT positive wrapped-around delta, for at least one
    concrete `validBits` value (e.g. `validBits = 32`, forcing an actual
    modular-arithmetic wrap in the test's chosen numbers) — this is the
    single most important regression case in this whole sub-phase, since
    it is the one most likely to be silently wrong if masking is ever
    "simplified" away later.
  - `timestampPeriodNs <= 0.0f` returns `0.0` rather than a nonsensical
    value (the defensive-floor case).
  - `PresentTimestampSlotBase(0)`/`PresentTimestampSlotBase(1)` return the
    documented, exact expected values (`0`/`2`), and their "+1 for end"
    convention is asserted directly rather than assumed.
- `VulkanDevice::TimestampCapability()`'s actual VALUE against a real
  device is Tier 2 (needs a live `VkPhysicalDevice`) and is verified
  manually (print/log the resolved `GpuTimestampCapability` once at
  startup during development, confirm `supported == true` with a
  plausible `timestampPeriodNs` on the reference development machine,
  same "Tier 2, verified manually" acceptance as everywhere else in this
  codebase) — but the QUERY LOGIC's interpretation of a hand-fed, fake
  `VkPhysicalDeviceLimits`/`VkQueueFamilyProperties` pair (e.g. "given
  `timestampComputeAndGraphics == false`, `supported` must be `false`
  regardless of `timestampPeriod`/`validBits`") is exactly the kind of
  pure decision logic that CAN and SHOULD be pulled out into its own
  small, Tier-1-testable pure function (e.g. `InterpretTimestampCapability
  (bool timestampComputeAndGraphics, float timestampPeriod, std::uint32_t
  validBits) noexcept -> GpuTimestampCapability`) that `VulkanDevice`'s
  constructor calls with real, device-queried arguments, but that a test
  can call directly with hand-fabricated ones — this is the SAME "extract
  the pure decision, keep the live-device call site as a thin wrapper
  around it" discipline this codebase already applies throughout (e.g.
  `AccumulateDrawStats()` vs. `FrameRecorder::RecordFrame()`'s call site),
  and is how this sub-phase achieves REAL Tier-1 coverage of "device says
  unsupported → capability correctly reports unsupported" without needing
  actual unsupported hardware.

**Done when:** the engine builds; every new pure function/struct above has
a passing Tier-1 test; a temporary manual print of
`VulkanDevice::TimestampCapability()` at startup shows a plausible
`supported == true` result on the reference development machine; nothing
about the Profiler panel or any existing behavior has changed at all.

### Phase 4B — RAII query-pool infrastructure (created, wired, not yet read)

**Goal, restated precisely:** create the real `VkQueryPool` and its owning
service, wire it into `Renderer`'s existing ownership graph exactly the
way `GpuMemoryTracker` already is, and get it safely reset — but do NOT
yet write a single timestamp or read a single result. This isolates "does
creating/destroying/resetting this new Vulkan object work correctly,
under validation, across every existing code path (normal render, resize,
minimize, move/move-assign)" as its own, separately-verifiable step,
before any actual measurement logic touches it.

**1. `VulkanQueryPool`** (`src/Renderer/Vulkan/VulkanQueryPool.h/.cpp`,
new file):

- Constructor: `VulkanQueryPool(VkDevice device, std::uint32_t
  slotCount)` — calls `vkCreateQueryPool()` with
  `VkQueryPoolCreateInfo{ .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
  .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = slotCount }`, throws
  `std::runtime_error` on failure — same convention as every other
  `Vulkan/` wrapper (`VulkanFrameSync`, `VulkanSwapchain`, ...). Does NOT
  own the `VkDevice` passed in (must outlive this object) — same
  convention as every sibling wrapper.
- Destructor: `vkDestroyQueryPool()`, guarded against a moved-from
  `VK_NULL_HANDLE` state — same pattern as `VulkanFrameSync::Destroy()`.
- Non-copyable, move constructor/assignment implemented exactly like
  `VulkanFrameSync`'s own (`std::exchange` the raw handle out of the
  moved-from object).
- `VkQueryPool Native() const noexcept` and `std::uint32_t SlotCount()
  const noexcept` accessors — nothing more; this class is deliberately
  "dumb," a thin RAII shell, with ZERO knowledge of what a "slot" means
  semantically (that's `GpuTimingService`'s job, one layer up) — same
  division of labor `VulkanSwapchain` has relative to `FramePresenter`.
- **No `Reset()`/`WriteTimestamp()`/`GetResults()` convenience methods on
  this class itself** — deliberately left as plain `vkCmdResetQueryPool`/
  `vkCmdWriteTimestamp2`/`vkGetQueryPoolResults` calls made directly by
  `GpuTimingService` against `Native()`, since those calls need
  `GpuTimingService`'s own semantic slot-to-purpose mapping (from Phase
  4A) to pick the right sub-range anyway — wrapping them here would just
  be an unnecessary extra indirection with no real encapsulation benefit,
  the same judgment call already made for `VulkanSwapchain::Native()`
  being used directly by `FramePresenter` rather than every swapchain
  operation being individually wrapped.

**2. `GpuTimingService`** (`src/Renderer/GpuTimingService.h/.cpp`, new
file):

- Constructor: `GpuTimingService(VkDevice device, const
  GpuTimestampCapability& capability)` — if `capability.supported`,
  constructs `std::optional<VulkanQueryPool> m_queryPool` with
  `kGpuTimingSlotCount == 8` slots (Phase 4A's fixed layout) and performs
  an initial FULL reset of every slot via a one-shot command buffer (reuse
  `Renderer::ImmediateSubmit()` — already exactly the right tool for "a
  one-time-submit-and-wait command buffer... reusable for future one-off
  GPU work," per its own doc comment in `Renderer.h`) — this "reset
  everything once, up front, before the very first real frame" step is
  what makes it safe for `RenderOffscreen()`/`Present()`'s own per-call
  reset-then-write sequences to assume every slot starts life already
  reset, rather than needing a separate "is this the very first use"
  branch scattered through their own logic. If `!capability.supported`,
  `m_queryPool` stays empty (`std::nullopt`) and every other method below
  becomes a safe, cheap no-op/early-return.
- Stores `GpuTimestampCapability m_capability` (copied in).
- Stores a small cache: `std::array<GpuTimingSample, kGpuTimingSlotCount /
  2> m_lastKnown{}` (one cached `GpuTimingSample` per LOGICAL pass —
  `Offscreen0`/`Offscreen1`/`SwapchainPresent` — i.e. 3 entries, not 8;
  the 8 raw query slots are an internal detail `GpuTimingService` alone
  needs to know about).
- Stores Present-path warm-up bookkeeping: `std::array<bool,
  kGpuTimingFramesInFlight> m_presentSlotEverWritten{}` (both `false`
  initially) — see the exact sequencing below for how this is used.
- **Public methods, called from `FramePresenter`:**
  - `bool IsSupported() const noexcept` — trivial passthrough of
    `m_capability.supported`; every call site below is expected to check
    this first (or the methods themselves early-return safely if not —
    both are fine; recommend the methods themselves guard internally, so
    `FramePresenter`'s own call sites stay simple, unconditional calls
    with no `if (IsSupported())` boilerplate needed at every one of them).
  - `void RecordOffscreenPassStart(VkCommandBuffer cmd, GpuTimingSlot
    slot) noexcept` — no-op if unsupported; otherwise records
    `vkCmdResetQueryPool(cmd, pool, startSlotFor(slot), 2)` (resetting
    BOTH the start and end slot for this pass in one call — legal, and
    simpler than two separate reset calls) then
    `vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, pool,
    startSlotFor(slot))`.
  - `void RecordOffscreenPassEnd(VkCommandBuffer cmd, GpuTimingSlot slot)
    noexcept` — no-op if unsupported; otherwise
    `vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
    pool, endSlotFor(slot))`.
  - `GpuTimingSample ReadOffscreenResultNow(GpuTimingSlot slot) noexcept`
    — called ONLY after the caller has ALREADY confirmed (via its own
    fence wait) that the corresponding submission is complete. Calls
    `vkGetQueryPoolResults(..., VK_QUERY_RESULT_64_BIT)` (WAIT_BIT not
    required — see Step 2.1 — but harmless to include defensively) for
    the 2-slot range, converts via `ConvertTimestampDeltaToMilliseconds()`
    (Phase 4A), builds a `GpuTimingSample{ Status::Present, ms }`, stores
    it into `m_lastKnown[...]`, and returns it. If unsupported, returns
    (and caches) `GpuTimingSample{ Status::Unsupported, 0.0 }` without
    touching Vulkan at all.
  - `void RecordPresentPassStart(VkCommandBuffer cmd, std::uint32_t
    frameInFlightIndex) noexcept` / `RecordPresentPassEnd(...)` — same
    shape as the offscreen pair, but indexed via
    `PresentTimestampSlotBase(frameInFlightIndex)` (Phase 4A) rather than
    a `GpuTimingSlot`.
  - `GpuTimingSample ReadPresentResultIfAvailable(std::uint32_t
    frameInFlightIndex) noexcept` — called at the exact point Phase 4D
    specifies (right after `Present()`'s existing per-frame-in-flight
    fence wait). If `!m_presentSlotEverWritten[frameInFlightIndex]`,
    returns (and caches) `GpuTimingSample{ Status::Absent, 0.0 }` WITHOUT
    reading the pool at all (this is the deliberate warm-up-frame
    handling — see Phase 4D's own detailed sequencing for exactly why
    this flag, and not a frame-count heuristic, is the correct condition).
    Otherwise reads/converts/caches/returns a `Status::Present` sample
    exactly like the offscreen version.
  - `void MarkPresentSlotWritten(std::uint32_t frameInFlightIndex)
    noexcept` — sets `m_presentSlotEverWritten[frameInFlightIndex] = true`;
    called once, right after `RecordPresentPassStart()`/
    `RecordPresentPassEnd()` are recorded into the command buffer for that
    slot (i.e., unconditionally, every time `Present()` actually records a
    real frame — NOT on a minimized/pending-resize early return).
  - `GpuTimingSample LastKnown(GpuTimingSlot slot) const noexcept` — a
    pure, side-effect-free read of the cache (what `Renderer::
    LastGpuTiming()` forwards to) — never touches Vulkan.

**3. Ownership wiring** (`Renderer.h/.cpp`, `FramePresenter.h/.cpp`):

- `Renderer` gains `std::shared_ptr<GpuTimingService> m_gpuTiming =
  std::make_shared<GpuTimingService>(m_device.Native(),
  m_device.TimestampCapability());` — declared and constructed right
  alongside `m_memoryTracker` (after `m_device` exists, before
  `m_presenter`), mirroring that member's own declaration-order comment
  exactly ("constructed here (no dependencies of its own) so both
  collaborators below get the SAME instance").
- `FramePresenter`'s constructor gains one new trailing parameter,
  `std::shared_ptr<GpuTimingService> gpuTiming`, stored into a new member
  `m_gpuTiming` — added the SAME way `memoryTracker` already is, at the
  SAME call site (`Renderer`'s own construction of `m_presenter`).
  `FramePresenter`'s hand-written move constructor/assignment must add
  `m_gpuTiming` to their existing member lists (see Step 2.3's own
  warning about this exact class of mistake).
- `Renderer` gains `GpuTimingSample LastGpuTiming(GpuTimingSlot slot)
  const noexcept { return m_gpuTiming->LastKnown(slot); }` — a trivial,
  always-safe-to-call forwarding accessor, added now (even though nothing
  populates real data until Phase 4C/4D) so the public API surface change
  is reviewed/landed as part of THIS sub-phase rather than split awkwardly
  across two.

**4. Explicitly NOT done in this sub-phase:**
- `FramePresenter::Present()`/`RenderOffscreen()` do NOT yet call any of
  `GpuTimingService`'s per-pass record/read methods — the service exists,
  is correctly constructed/reset/destroyed/moved, but is completely
  unused by any real rendering code yet.
- No change to `Renderer::RenderOffscreen()`'s signature yet (the new
  `GpuTimingSlot` parameter is Phase 4C's addition, since it has no
  purpose until something actually consumes it).
- No `FrameProfiler::SetGpuPassTiming()` call anywhere yet.

**Testing (Phase 4B):**
- The RAII construction/destruction/move correctness of `VulkanQueryPool`
  and `GpuTimingService` is Tier 2 (needs a real `VkDevice`) — explicitly,
  deliberately accepted into the exact same bucket as `Buffer`/
  `RenderTexture`/`Pipeline` (`TESTING.md`), verified manually: build and
  run the Editor with validation layers enabled, confirm no new
  validation errors/warnings appear anywhere in a normal session
  (including triggering at least one window resize and one minimize/
  restore cycle), and confirm via a debugger/log that `GpuTimingService`'s
  constructor-time full-pool reset actually runs exactly once at startup.
- What IS Tier-1-testable here and must be tested as such: the internal
  slot-index-to-`GpuTimingSlot`/frame-in-flight-index mapping functions
  from Phase 4A (already covered there) — this sub-phase adds no NEW pure
  logic beyond what 4A already introduced, since `GpuTimingService`'s own
  body is inherently Vulkan-call-shaped. If, during implementation, any
  additional pure decision logic is discovered inside `GpuTimingService`
  worth extracting (e.g. "given this capability and this slot, which raw
  index range do I touch" — currently planned as trivial arithmetic, but
  worth double-checking once real code is written), extract and test it
  the same way Phase 4A's own helpers were — per `AGENTS.md`'s standing
  "design new logic to be Tier-1-testable whenever the underlying problem
  allows it" rule.

**Done when:** the engine builds; a normal Editor session (spawn a few
primitives, resize the window, minimize/restore it, open/close panels)
runs with validation layers enabled and shows zero new validation errors/
warnings; nothing about the Profiler panel has changed (still shows
`N/A` for GPU Timing, same as before this sub-phase, for the same
"nothing reports to it yet" reason).

### Phase 4C — Offscreen GPU timing first (Game View + Scene View)

**Goal, restated precisely:** the FIRST sub-phase that produces a REAL
number on the Profiler panel. Chosen to go first (matching the attached
`Phase4.md`'s own ordering) because `RenderOffscreen()` is already fully
synchronous — the easiest, safest, least-synchronization-risk half of this
whole phase, and a fully self-contained, shippable deliverable on its own
even if Phase 4D is later descoped/delayed (see Step 4's risk framing).

**1. `Renderer::RenderOffscreen()` gains a `GpuTimingSlot` parameter:**

```
DrawStats RenderOffscreen(RenderTexture& target, GpuTimingSlot timingSlot,
    const std::function<void(VkCommandBuffer)>& recordExtra = {});
```

Threaded straight through to `FramePresenter::RenderOffscreen()`, which
gains the same new parameter. `Renderer::CreateRenderTexture()`, `Clear()`,
`Submit()`, `Present()` etc. are all completely unaffected — this is the
ONE existing method whose signature changes in this whole phase, and the
change is purely additive (one more required parameter, not a default,
since silently defaulting to e.g. `Offscreen0` for every caller would
risk two logically-different passes accidentally sharing one slot's
cached data without anyone noticing — making the caller state their
intent explicitly is the safer choice here).

**Existing call sites that must be updated** (both in
`Application.cpp`'s `Application::Run()`, confirmed by direct reading):
- The `if (gameTarget != nullptr)` block's `m_renderer.RenderOffscreen(*gameTarget)`
  becomes `m_renderer.RenderOffscreen(*gameTarget, GpuTimingSlot::Offscreen0)`.
- The `if (sceneTarget != nullptr)` block's `m_renderer.RenderOffscreen(*sceneTarget)`
  becomes `m_renderer.RenderOffscreen(*sceneTarget, GpuTimingSlot::Offscreen1)`.
- Any other `RenderOffscreen()` call site in the codebase (e.g. the
  Editor's `AssetPreviewMesh`/Bone Viewer, if either happens to route
  through this exact overload rather than a bespoke recording path — to
  be confirmed once implementation starts by grep-ing every
  `RenderOffscreen(` call site) must be updated too, or the build simply
  won't compile — a good, cheap forcing function that guarantees nothing
  is missed.

**2. `FramePresenter::RenderOffscreen()`'s new sequencing** (inserted
around the EXISTING code, shown here as the exact before/after ordering):

1. *(existing, unchanged)* `vkWaitForFences(offscreenFence)`;
   `vkResetFences(offscreenFence)`; `vkResetCommandBuffer(...)`;
   `vkBeginCommandBuffer(...)`.
2. **NEW:** `m_gpuTiming->RecordOffscreenPassStart(m_offscreenCommandBuffer,
   timingSlot);` — records the reset (for BOTH this pass's start/end
   slots) plus the start timestamp, into the SAME command buffer about to
   record the actual pass. Safe to do here specifically because step 1's
   existing `vkWaitForFences()` already proved the LAST use of this exact
   slot pair (last frame's call for this SAME logical pass) is complete.
3. *(existing, unchanged)* `frameRecorder.RecordFrame(m_offscreenCommandBuffer,
   target.Target(), ColorFormat(), m_depthFormat,
   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, recordExtra);` — the actual
   pass (clear, queued draws, `recordExtra`) is bracketed exactly here,
   between the two new timestamp writes, with no changes to this call
   itself at all.
4. **NEW:** `m_gpuTiming->RecordOffscreenPassEnd(m_offscreenCommandBuffer,
   timingSlot);`
5. *(existing, unchanged)* `vkEndCommandBuffer(...)`; build+submit
   `VkSubmitInfo` with `offscreenFence`; **existing final**
   `vkWaitForFences(offscreenFence)` (this is the function's own,
   already-there, "synchronous for now" wait).
6. **NEW, inserted immediately after that final existing wait, still
   inside `FramePresenter::RenderOffscreen()`, before returning:**
   `const GpuTimingSample timing = m_gpuTiming->ReadOffscreenResultNow(
   timingSlot);` — safe precisely because the wait in step 5 already
   guarantees this exact submission (including this exact pass's
   timestamp writes) has completed.
7. Return the existing `DrawStats drawStats` value UNCHANGED (this
   function's return type does not change — the new `GpuTimingSample` is
   retrieved by the caller afterward via `Renderer::LastGpuTiming()`, per
   this whole plan's own API-surface design above, NOT bundled into the
   return value).

**3. `Application.cpp`'s reporting, added right next to the EXISTING
`SetGpuPassDrawStats()` call in each block** (both already inside the
correct `if (gameTarget != nullptr)`/`if (sceneTarget != nullptr)` guard,
so the "did this pass actually run this frame" condition is already
correctly handled for free):

```
// Game view block, right after the existing SetGpuPassDrawStats() call:
const GpuTimingSample gameViewTiming = m_renderer.LastGpuTiming(GpuTimingSlot::Offscreen0);
Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::GameView,
    ToProfilingGpuSampleStatus(gameViewTiming.status), gameViewTiming.milliseconds);
```

...and the equivalent for the Scene block (`GpuTimingSlot::Offscreen1` →
`Profiling::GpuPass::SceneView`). `ToProfilingGpuSampleStatus()` is a
tiny, one-off, header-only mapping function (`GpuTimingSample::Status::
Absent → Profiling::GpuSampleStatus::Absent`, `::Present → ::Present`,
`::Unsupported → ::Unsupported`) — trivial, but worth naming and placing
somewhere sensible (a small anonymous-namespace helper directly in
`Application.cpp`, next to the existing `AspectRatioOf()` helper, is
exactly precedented and sufficient; it needs no test of its own beyond
what a straightforward `switch` already makes self-evidently correct, the
same judgment already applied to `AspectRatioOf()` itself).

**4. Handling the "pass didn't run this frame" case — already free.**
Because both new `SetGpuPassTiming()` calls live INSIDE the exact same
`if (gameTarget != nullptr)`/`if (sceneTarget != nullptr)` blocks the
EXISTING `SetGpuPassDrawStats()` calls already live in, a hidden/tabbed-
away "Game" or "Scene" panel correctly and automatically results in
`GpuPassSample::timingStatus` staying at its per-frame default
(`Absent`, since `FrameProfiler`'s in-progress `FrameSample` starts fresh
every `BeginFrame()`) — no new code is needed to make this case correct,
it falls out of reusing the existing guard.

**5. Handling an unsupported device.** `GpuTimingService::
ReadOffscreenResultNow()` (Phase 4B) already returns `Status::Unsupported`
without touching Vulkan when `!IsSupported()` — `ToProfilingGpuSampleStatus()`
maps that straight to `Profiling::GpuSampleStatus::Unsupported`, and the
Profiler panel already renders that tri-state distinctly from `Absent`
(confirmed: `ProfilerPanelData.h`'s `FormatGpuTimingLine()`, per
`PROFILER_IMPLEMENTATION_STATUS_v6.md`, already honestly distinguishes
"N/A" cases — verify its exact wording for `Unsupported` specifically
covers this at implementation time, and adjust its text if it currently
only special-cases `Absent`, since this phase is what first makes
`Unsupported` a REACHABLE state in a real running frame rather than a
theoretical one).

**Testing (Phase 4C):**
- `ToProfilingGpuSampleStatus()` (or wherever the status-mapping logic
  ends up) is trivial enough that a direct unit test is optional but
  cheap insurance — if it's pulled out as a standalone function (rather
  than inlined at each of the two call sites), a 3-case Tier-1 test
  (`Absent→Absent`, `Present→Present`, `Unsupported→Unsupported`) costs
  almost nothing and removes any chance of a copy-paste mismatch between
  the Game/Scene call sites.
- Everything else in this sub-phase is Tier 2 (needs a live device),
  verified manually: with the Editor open and BOTH "Game" and "Scene"
  panels split apart (both simultaneously visible/rendering), confirm the
  Profiler panel's GPU Timing lines for `Game View` and `Scene View` both
  show real, plausible, non-zero millisecond values that update every
  frame; tab them back together (only one visible) and confirm the
  now-hidden one's line reads `N/A`, not a frozen stale number and not
  `0.00 ms`; cross-check at least one frame's Game View number against
  RenderDoc/Nsight per this document's own Step 1.3 success criterion.

**Done when:** the Profiler panel shows real GPU milliseconds for
whichever of "Game"/"Scene" is currently visible, and hidden panels
correctly show `N/A`, never `0.00 ms` — exactly the `Phase4.md`-specified
"Done when" condition for this chunk, now delivered with the full
mechanical detail above.

### Phase 4D — Swapchain Present GPU timing (the hardest chunk)

**Goal, restated precisely:** the same measurement, for the ONE remaining
pass (`Present`), using the double-buffered frames-in-flight
synchronization the swapchain path already has — reading a result that is
inherently about a PAST frame's Present pass (not the current call's own),
at exactly the point the engine already proves that past data is safe to
read, with genuinely zero new stalls.

**1. `FramePresenter::Present()`'s new sequencing** (inserted around the
EXISTING code, shown as the exact before/after ordering — cross-reference
`FramePresenter.cpp`'s actual current body, read in full during Step 2):

1. *(existing, unchanged)* Minimized/pending-resize early-return checks
   (`if (m_pendingWidth <= 0 ...) return std::nullopt;` and the
   `if (m_resizeRequested) { RecreateSwapchain(); if (m_resizeRequested)
   return std::nullopt; }` block). **Nothing new here** — if either of
   these returns early, `m_currentFrame` never advances and NOTHING
   Phase 4D adds ever runs this call, which is exactly correct (no data
   to report, no slot skew — see Step 2.3).
2. *(existing, unchanged)* `const VkFence fence =
   m_frameSync.InFlightFence(m_currentFrame); vkWaitForFences(fence,
   ...);` — this is the EXACT existing wait Phase 4D's read-back piggy-
   backs on.
3. **NEW, inserted immediately after that wait (i.e. BEFORE
   `vkAcquireNextImageKHR()`, though the exact position relative to
   acquire doesn't matter for correctness since acquire is unrelated to
   the query pool — placing it right after the fence wait, before
   anything else, keeps the new code visually grouped with the exact
   synchronization fact it depends on):** `const GpuTimingSample
   presentTiming = m_gpuTiming->ReadPresentResultIfAvailable(
   m_currentFrame);` — store this local result; it will be exposed to the
   caller at the very end of this function (see step 9 below), since the
   function's control flow has several more existing early-return paths
   between here and its normal end (the `VK_ERROR_OUT_OF_DATE_KHR`
   acquire-failure path in particular) that must NOT lose this already-
   computed value if hit — actually, per the API design in this plan
   (`Renderer::LastGpuTiming()` is a SEPARATE pull accessor, not bundled
   into `Present()`'s return value), this local `presentTiming` doesn't
   need to be threaded through those early-return paths at all — it was
   ALREADY cached into `m_gpuTiming`'s own internal state by
   `ReadPresentResultIfAvailable()` the moment it was called, in step 3,
   regardless of what happens afterward in this function. This
   simplification (cache-as-a-side-effect, read back independently by the
   caller afterward) is EXACTLY why the "pull accessor" API shape was
   chosen over trying to bundle GPU timing into `Present()`'s existing
   `std::optional<DrawStats>` return value — see this document's own
   "Overall API surface" section above, and the design-decision log
   below.
4. *(existing, unchanged)* `vkAcquireNextImageKHR(...)` and its
   `VK_ERROR_OUT_OF_DATE_KHR`/other-failure handling, `vkResetFences(fence)`,
   the `needsDepth`/`EnsureDepthBuffersForSwapchain()` decision,
   `vkResetCommandBuffer(...)`, `vkBeginCommandBuffer(...)`.
5. **NEW:** `m_gpuTiming->RecordPresentPassStart(cmd, m_currentFrame);`
   — records the reset (for this frame-in-flight index's start/end slot
   pair) plus the start timestamp. Safe here because step 2's fence wait
   already proved the LAST use of THIS SAME slot pair (`kFramesInFlight ==
   2` frames ago) is complete — the query data step 3 just read (if any)
   came from that exact same prior use, and is now being safely
   overwritten.
6. *(existing, unchanged)* Build the `RenderTarget target` (swapchain
   image + optional depth), call `frameRecorder.RecordFrame(cmd, target,
   ColorFormat(), m_depthFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
   recordExtra);` — this is what actually records Dear ImGui's own chrome
   (via `recordExtra`) alongside any direct-to-swapchain engine geometry
   (the rare all-panels-hidden/release-build case) — bracketed exactly
   here, unchanged.
7. **NEW:** `m_gpuTiming->RecordPresentPassEnd(cmd, m_currentFrame);`
   immediately followed by **NEW:**
   `m_gpuTiming->MarkPresentSlotWritten(m_currentFrame);` (Phase 4B's
   warm-up-tracking flag — set unconditionally here, since reaching this
   line means a real frame genuinely was recorded and submitted for this
   slot).
8. *(existing, unchanged)* `vkEndCommandBuffer(...)`; build+submit
   `VkSubmitInfo` (waiting on the image-available semaphore, signaling
   render-finished, fenced on `fence`); `vkQueuePresentKHR(...)` and its
   resize-detection handling; `m_currentFrame = (m_currentFrame + 1) %
   kFramesInFlight;`; `return drawStats;` (existing, unchanged return
   value/type).
9. The caller (`Application.cpp`) reads the GPU timing result
   INDEPENDENTLY, via `Renderer::LastGpuTiming(GpuTimingSlot::
   SwapchainPresent)`, exactly like Phase 4C's Game/Scene calls — NOT
   from `Present()`'s own return value.

**2. Why the warm-up flag (`m_presentSlotEverWritten`), not a frame-count
heuristic, is the correct condition — spelled out explicitly since this
is the one place in this whole phase most likely to be "simplified"
incorrectly later:**

With `kFramesInFlight == 2` and `m_currentFrame` alternating `0, 1, 0, 1,
...`, the FIRST call to `Present()` that actually records a frame writes
into slot-pair 0 (and sets `m_presentSlotEverWritten[0] = true`). The
SECOND such call writes into slot-pair 1 (sets `[1] = true`). The THIRD
such call reuses slot-pair 0 — at this point, step 2's fence wait for
`fence[0]` is waiting on exactly the FIRST call's own submission, which by
now is virtually certain to have long since completed (a whole additional
frame's worth of GPU/CPU work happened in between) — so by the time the
THIRD call reaches step 3, `m_presentSlotEverWritten[0]` is already `true`
and the read is both SAFE and genuinely MEANINGFUL (real data from the
first call's Present pass). Only the very FIRST and SECOND calls ever see
their own slot's flag still `false` — exactly two "no data yet" frames at
the very start of a session, correctly reported as `Absent`, never a
crash/garbage read from an un-written query. **A frame-count-based
hesame/heuristic (e.g. "skip if `CompletedFrameCount() < 2`") would be
WRONG here** — it conflates "how many frames the whole engine has run"
with "how many times THIS SPECIFIC swapchain's `Present()` has actually
recorded a real frame," which can diverge (e.g. after a
resize/minimize/restore cycle where several `BeginFrame()`/`EndFrame()`
pairs happen while `Present()` itself keeps early-returning
`std::nullopt`) — the per-slot boolean flag is the only condition that is
correct BY CONSTRUCTION regardless of how many (if any) early-returns
happened in between.

**3. `Application.cpp`'s reporting, added right next to the EXISTING
`SetGpuPassDrawStats()` call inside `if (presentStats.has_value())`:**

```
if (presentStats.has_value()) {
    Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(...); // existing, unchanged
    const GpuTimingSample presentTiming = m_renderer.LastGpuTiming(GpuTimingSlot::SwapchainPresent);
    Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::Present,
        ToProfilingGpuSampleStatus(presentTiming.status), presentTiming.milliseconds);
}
```

Reusing the exact same `if (presentStats.has_value())` guard the existing
draw-stats call already lives in is what correctly makes a minimized-
window frame (where `Present()` returned `std::nullopt` and therefore
NOTHING Phase 4D added ever ran) report `Present`'s GPU Timing as
`Absent`, with no special-casing needed here either.

**4. Explicit resize/minimize verification checklist for this sub-phase**
(beyond the general Phase 4 success criteria in Step 1.3):
- Resize the window repeatedly, including rapid/successive resizes, and
  confirm no validation error appears and the reported Present timing
  keeps updating sensibly afterward (a couple of `Absent` frames
  immediately after a resize would NOT be expected — the query pool
  itself is untouched by `RecreateSwapchain()`, per Step 2.3 — but if any
  ARE observed, this is worth investigating specifically, since it would
  indicate the warm-up flag logic interacted with resize in an
  unanticipated way).
- Minimize the window, wait a few seconds, restore it, and confirm
  Present's GPU Timing resumes reporting real numbers within at most 2
  frames of the window becoming visible again (matching the same 2-frame
  warm-up behavior a fresh session exhibits, since `m_currentFrame`
  simply continues from wherever it left off — this is expected, not a
  bug).
- Drag a panel out into its own OS window (Dear ImGui multi-viewport) and
  confirm the MAIN window's Present GPU Timing is unaffected by whatever
  that secondary window's own independent swapchain is doing — per
  `PROFILER_STRATEGY_v2.md`'s own explicit Step 4 scope refusal, a
  secondary platform window's own present is NOT measured by this phase
  at all, and this should be verified as a non-event (no crash, no
  garbage number), not silently assumed.

**Testing (Phase 4D):**
- The warm-up-flag reasoning above (step 2) is itself pure, sequential
  boolean logic and should be captured as a small, explicit Tier-1 test
  against `GpuTimingService` in isolation if its internals are structured
  to allow constructing one with a fake/no-op query pool for pure
  state-machine testing (worth attempting during implementation; if the
  real `VkQueryPool` dependency makes this impractical without a live
  device, this specific piece is acceptable to fold into the same Tier 2
  manual-verification bucket as the rest of `GpuTimingService` — but the
  ATTEMPT to extract it as pure logic should be made first, per
  `AGENTS.md`'s standing rule, rather than skipped by default).
- Everything else is Tier 2 / manual, per the checklist above, plus the
  same RenderDoc/Nsight cross-check from Step 1.3 (this time for a
  Present-pass capture specifically — note that a Present-pass RenderDoc
  capture will show mostly Dear ImGui's own overlay draw calls in the
  common Editor case, exactly as `AGENTS.md`'s "Profiling" section already
  describes for the `Present` pass's draw-call/triangle counts today —
  the GPU-time number is expected to be small-but-real in that case, not
  necessarily large).

**Done when:** Present timings appear on the Profiler panel without
causing new frame stalls, without new Vulkan validation errors, and
without any regression to swapchain resize/minimize behavior — exactly
the `Phase4.md`-specified "Done when" condition for this chunk.

### Design decision log (decided once here, not to be re-litigated per sub-phase)

- **GPU timing is reported via a Renderer-local `GpuTimingSample`/
  `GpuTimingSlot` pair, never by having `Renderer`/`FramePresenter`
  include `Profiling/ProfilingTypes.h` directly.** `Application.cpp`
  remains the ONE place that bridges Renderer-level plain data into
  `Profiling::GpuPass`-named calls — exactly the same seam Phase 3
  already established for `DrawStats` → `SetGpuPassDrawStats()`, applied
  consistently here. This keeps `Renderer` exactly as decoupled from the
  Profiling module as it is from ECS today.
- **`Renderer::RenderOffscreen()`'s new `GpuTimingSlot` parameter has NO
  default value.** Every caller must state which slot it means,
  explicitly — a silent default would risk two unrelated callers
  accidentally sharing cached GPU-timing data with no compiler error to
  catch it.
- **GPU timing results are exposed via a separate, pull-style
  `Renderer::LastGpuTiming(slot)` accessor, not bundled into
  `RenderOffscreen()`/`Present()`'s own return values.** This is
  DELIBERATE, not an oversight: for `RenderOffscreen()` the timing IS
  available synchronously (could have been bundled), but for `Present()`
  it is inherently about a PAST frame's pass, which would be a confusing
  thing to attach to "this call's own" return value — using the SAME
  pull-accessor shape for both keeps the API consistent regardless of
  which path's timing is actually synchronous vs. delayed under the hood,
  and avoids ever having to explain "why does `Present()`'s return value
  sometimes describe a different frame than the one that just ran."
- **The Present-path query pool uses its own fixed `kGpuTimingFramesInFlight
  == 2` constant (`Renderer/GpuTiming.h`), rather than reaching into
  `FramePresenter`'s private `kFramesInFlight`.** These two constants must
  always agree in VALUE, but are deliberately kept as two separate
  named constants rather than one shared symbol, because `GpuTiming.h` is
  designed to have ZERO dependency on `FramePresenter.h` (it's a
  lower-level, Vulkan-free pure-math file `FramePresenter` itself
  depends on, not the other way around) — a static_assert or a code
  comment cross-referencing the two by name (in both files) is the
  correct way to keep them from silently drifting apart, not a shared
  `#include`.
- **A per-slot boolean "has this ever been written" flag, not a global
  frame-count threshold, governs Present-path warm-up.** See Phase 4D's
  own detailed reasoning above — this is the one design point in the
  whole phase most likely to be "simplified" incorrectly by a future
  editor who doesn't re-read that reasoning first.
- **The offscreen query pool uses ONE set of slots per logical pass
  (`Offscreen0`/`Offscreen1`), never multiplied by frames-in-flight**,
  because `RenderOffscreen()` itself is fully synchronous — multiplying
  its slot count the same way the Present path's is multiplied would be
  needless over-engineering copied from the wrong precedent (the same
  "don't reuse the wrong pattern from a superficially similar case"
  judgment call `PROFILER_STRATEGY_v2.md`'s own Step 2.3 already makes
  about `GpuResourceHandle` vs. a small fixed-name enum).
- **The whole 8-slot query pool is created ONCE, sized for exactly
  `Offscreen0 + Offscreen1 + (SwapchainPresent × 2 frames-in-flight)`,
  and never resized/recreated for the lifetime of a `GpuTimingService`.**
  A resize/minimize/restore cycle never touches it (Step 2.3) — the ONLY
  thing that ever changes its validity is device support itself, which is
  queried once at construction and never re-checked afterward (Vulkan
  device capabilities do not change at runtime).

--------------------------------------------------------------------------
## Step 4: What We Will NOT Do (Focus)
--------------------------------------------------------------------------

- **No per-draw-call GPU timing.** Exactly as `PROFILER_STRATEGY_v2.md`'s
  own Step 4 already refuses: a timestamp per individual `vkCmdDraw`/
  `vkCmdDrawIndexed` would need an unbounded, scene-dependent number of
  query slots and would materially perturb GPU scheduling just by
  existing in volume. This phase measures exactly three, whole, named
  passes — nothing finer-grained.
- **No GPU-pass attribution for a secondary ImGui platform window**
  (multi-viewport, `ImGuiConfigFlags_ViewportsEnable`,
  `RenderPlatformWindows()`). Only the MAIN viewport's `Present()` is
  measured — a panel dragged out into its own OS window has its own,
  completely independent swapchain/present call that this phase does not
  instrument, exactly as already logged in `PROFILER_STRATEGY_v2.md`'s own
  Step 4 refusal list.
- **No new stall, ever, added purely to make a query result available
  sooner.** This is the single most important refusal in this entire
  document (see Step 2.3) — every read in this plan piggybacks on
  synchronization the engine already performs for an unrelated, existing
  reason. If a future edit is ever tempted to add
  `VK_QUERY_RESULT_WAIT_BIT` somewhere it doesn't already have a
  preceding fence wait proving safety, or to call `vkDeviceWaitIdle()`
  "just to be safe," that is a regression against this document's own
  stated design, not an acceptable simplification.
- **No `VK_EXT_host_query_reset` (or any other new Vulkan extension
  request) for this phase.** The engine already has everything it needs
  (Vulkan 1.3 core + `synchronization2`, already unconditionally enabled —
  see Step 2.1) — command-buffer-recorded `vkCmdResetQueryPool` is used
  throughout instead of a host-side reset, deliberately avoiding any new
  device-feature negotiation, capability check, or fallback path for a
  feature this phase does not actually need.
- **No rewrite of `FrameRecorder`/`FramePresenter`/the draw-submission
  pipeline.** Every change in this phase is additive instrumentation
  layered onto the existing, working recording/submission code — the
  existing `AGENTS.md` "Render Target Format Matching" assertions, the
  lazy swapchain-depth-buffer allocation, the two-frames-in-flight
  double-buffering, and every other piece of already-reasoned-through
  behavior in `FramePresenter.cpp`/`FrameRecorder.cpp` stays completely
  untouched in its own right — this phase only brackets it with new
  timestamp writes and reads, never alters what it does.
- **No change to `Game`/`RenderSystem`/`AnimationSystem`/
  `MeshInstantiationSystem`'s public APIs or behavior.** This phase is
  entirely contained within `Renderer`/`FramePresenter`/`Application.cpp`
  — nothing about the ECS, gameplay systems, or the Editor's non-Profiler
  panels needs to know this phase exists at all.
- **No Editor/UI work of any kind.** The Profiler panel's "GPU Timing"
  section already exists and already renders correctly for every tri-state
  outcome this phase can produce (per Step 2.1) — this phase's entire
  scope is making real data flow INTO `FrameProfiler`, never touching a
  single ImGui call.
- **No headless (`VK_EXT_headless_surface`) test fixture built as part of
  this effort.** Already an accepted, separately-tracked `TODO.md` backlog
  item, explicitly not a blocker for this or any other feature work — this
  phase's own Tier 2 gaps are the same already-accepted kind, not a new
  bucket invented for this occasion.
- **No automated performance-regression CI gate.** This phase delivers the
  MEASUREMENT only — turning "GPU time went up" into an automated
  pass/fail build gate is explicitly out of scope here, same refusal
  `PROFILER_STRATEGY_v2.md` already logged for the whole Profiler effort.
- **No attempt to make the reported millisecond values bit-exact against
  an external tool.** Step 1.3's own success criterion is agreement
  "within the tool's own measurement granularity" — different
  measurement paths/tools can legitimately round or bucket differently;
  chasing exact bit-for-bit agreement would be solving a problem that
  doesn't need solving.
- **No CSV export/benchmark-mode work.** That is Phase 6 of
  `PROFILER_STRATEGY_v2.md`, a separate, already-identified future
  session — this phase only makes sure Phase 6's eventual CSV rows have
  something real to put in their GPU-timing columns, it does not build
  Phase 6 itself.

--------------------------------------------------------------------------
## Step 5: Their Role (What does this mean for you?)
--------------------------------------------------------------------------

This section is written for whoever actually implements this — human or
AI agent — picking this document up next.

### 5.1 How to start

1. Read this document in full, then re-read `AGENTS.md`'s "Profiling"
   section and "Render Target Format Matching" section again — Phase 4
   sits at the intersection of both conventions, and violating either
   here would be a real regression in project consistency, not a
   stylistic nit.
2. Re-read the actual current source of every file named in Step 2.1/2.3
   before writing a single line (`VulkanDevice.h/.cpp`,
   `FramePresenter.h/.cpp`, `FrameRecorder.h`, `Renderer.h`,
   `DrawStats.h`, `Application.cpp`) — this document was written from a
   real reading of that exact code, and it may have changed since; if
   anything here disagrees with what the source actually says by the time
   implementation starts, TRUST THE SOURCE and update this document's own
   text to match, the same "living document" discipline
   `PROFILER_STRATEGY_v2.md`/`TODO.md` already establish for themselves.
3. Implement the four sub-phases **strictly in order: 4A → 4B → 4C → 4D**.
   Do not begin 4C before 4A/4B are both merged and their own done-when
   criteria verified; do not begin 4D before 4C's Profiler-panel-shows-
   real-numbers criterion is actually seen working. Each sub-phase is a
   deliberate, independent checkpoint — treat it as such, matching
   `PROFILER_STRATEGY_v2.md`'s own "Implement phases in order" instruction
   for its own eight phases.
4. Build and run the FULL existing test suite after every sub-phase, not
   just at the very end — `ctest --test-dir build -C Debug
   --output-on-failure` — per `AGENTS.md`'s "Run the actual test suite
   before considering any change to `gte_core` done" rule. A sub-phase
   that merely compiles is not done.

### 5.2 Non-negotiable checklist per sub-phase

- [ ] Every new PURE function (Phase 4A's capability interpretation/tick
      conversion/slot indexing) has a matching Tier-1 test added in the
      SAME change, per `AGENTS.md`'s "Every change to Tier 1 code must
      come with a matching test change."
- [ ] Every new type lives inside `namespace gte` (or `gte::Profiling`
      only for the pre-existing types this phase merely calls into, never
      for anything NEW this phase introduces — everything new belongs in
      plain `gte`, since it is Renderer/Vulkan-level, not Profiling-level).
- [ ] `VulkanQueryPool`/`GpuTimingService` follow RAII exactly — acquired
      in the constructor, released in the destructor, non-copyable,
      correctly move-safe (every new member threaded through
      `FramePresenter`'s existing hand-written move constructor/
      assignment — see Step 2.3's own explicit warning about this).
- [ ] Nothing under `src/Renderer/` (specifically `GpuTiming.h/.cpp`,
      `GpuTimingService.h/.cpp`, `VulkanQueryPool.h/.cpp`) includes a
      `Profiling/` header — the bridge into `Profiling::GpuPass`/
      `SetGpuPassTiming()` lives ONLY in `Application.cpp`.
- [ ] `GpuTiming.h` itself stays completely Vulkan-header-free (no
      `<volk.h>`, no `vulkan/vulkan.h`), mirroring `DrawStats.h`'s own
      established precedent — verified by literally checking its
      `#include` list, not just intent.
- [ ] No new stall/wait is added anywhere that wasn't already present
      before this phase — spot-check this specifically by grepping the
      diff for every new `vkWaitForFences`/`vkDeviceWaitIdle`/
      `VK_QUERY_RESULT_WAIT_BIT` occurrence and confirming each one is
      either (a) not actually new, or (b) justified in a code comment
      referencing the exact pre-existing synchronization fact it's
      piggybacking on.
- [ ] Every place that reports a GPU pass's timing correctly distinguishes
      `Absent`/`Present`/`Unsupported` — spot-check by hiding the Scene
      panel for a few seconds and confirming the Profiler shows a gap
      (`N/A`), not a dip to `0.00 ms` or a frozen stale number.
- [ ] The full test suite is built and run (not just "it compiles") before
      each sub-phase is considered complete.
- [ ] `AGENTS.md`'s "Profiling" section is updated once this phase lands
      (at minimum once, after 4D — updating incrementally after 4A/4B/4C
      too is fine and arguably better) to state plainly that GPU timing is
      now real, wired, production data — mirroring exactly how that
      section was updated for Phase 3/5's own landings.
- [ ] `README.md`'s "Status" section and
      `PROFILER_IMPLEMENTATION_STATUS_v6.md` (or a new `_v7` revision,
      following that document's own existing versioning convention) are
      updated once this phase lands, describing exactly what was built,
      what (if anything) was descoped, and what was verified — the same
      level of honest, specific detail every prior phase's landing already
      demonstrates in that document's history.

### 5.3 Risk / rollback framing (read this before starting 4D specifically)

Sub-phases 4A/4B/4C are individually low-risk and independently valuable
even in isolation — 4C alone already delivers real GPU timing for TWO of
the three named passes (Game View, Scene View), which is most of this
whole feature's practical value for a developer actively iterating on
gameplay/rendering code, since those two passes are where actual scene
geometry is drawn.

**4D is the one piece of this whole phase carrying genuine, driver-
specific risk** — it is the only sub-phase touching the double-buffered,
higher-throughput swapchain present path, where a subtle synchronization
mistake is most likely to surface as an intermittent validation warning
or an occasional garbage read, rather than a clean, obvious failure. If
4D proves disproportionately time-consuming to get right (a genuinely
driver-specific quirk, an edge case in the warm-up-flag logic under some
resize/minimize sequence not anticipated here, or similar), **the correct
response is NOT to block or revert 4A/4B/4C** — ship those three as a
complete, honest, partial win (Present's GPU Timing simply stays at its
pre-Phase-4 "N/A" state a while longer, which the Profiler panel already
renders correctly and is not a regression from today), log the specific
blocker encountered in this document's own changelog (add one, mirroring
`PROFILER_STRATEGY_v2.md`'s own "Changelog: v1 -> v2" section at the top
of this file, the next time this document is revised), and revisit 4D as
its own, later, dedicated follow-up. This mirrors
`PROFILER_STRATEGY_v2.md`'s own explicit risk/rollback note for this exact
phase, now made concrete now that the four sub-phases actually exist to
roll back TO.

### 5.4 What "done" looks like for the whole phase, and what happens after

When 4D lands and its own done-when criteria are verified, revisit this
document's own Step 1.3 success criteria one by one against the real,
running engine — not assumed. Update
`PROFILER_IMPLEMENTATION_STATUS_v6.md` (bump to `_v7`, following its own
established versioning convention) to move Phase 4 out of "What was NOT
implemented, and why" and into a new "What was implemented this session"
entry, with the same level of concrete, specific detail (exact new
files/classes/call sites, exact test counts before/after, exact manual
verification performed) every prior phase's own landing entry in that
document already demonstrates. At that point, of `PROFILER_STRATEGY_v2.md`'s
original eight phases, only **Phase 6 (benchmark mode)** remains — and
Phase 4's own newly-real GPU timing data is exactly what Phase 6's
eventual CSV export will finally have a genuinely complete data model to
dump, closing the loop this whole multi-session Profiler effort was
originally planned around.
