# Profiling

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

`src/Profiling/` (`ProfilingTypes.h`, `FrameProfiler.h/.cpp`, `ScopeTimer.h`)
is the engine's CPU scope-timer instrumentation module - always compiled
(no `GTE_ENABLE_EDITOR` dependency at all, same tier as `src/Animation/`/
`src/Assets/`), gated by its OWN separate `GTE_ENABLE_PROFILER` CMake option
(`ON` by default - see `CMakeLists.txt`). See `PROFILER_STRATEGY_v2.md` for
the full multi-phase plan this module is Phase 0/1 of. Follow these rules
whenever touching profiling instrumentation, or adding a new call site:

- **`GTE_PROFILE_SCOPE("Name")` (`src/Profiling/ScopeTimer.h`) is the
  ONLY way to add a new CPU profiling call site - never call
  `FrameProfiler::RecordCpuScope()` directly, and never construct a
  `Profiling::ScopeTimer` by hand outside that macro.** It expands to a
  single local RAII object whose destructor fires at the natural end of
  the enclosing block - the same "acquire in constructor, release in
  destructor" discipline this file already mandates for every other
  resource (see [Coding Guidelines](../../AGENTS.md#coding-guidelines), RAII). `name` MUST be a string
  literal (or otherwise static-storage-duration) `const char*` - it is
  compared against every other scope's name via pointer/`strcmp()`
  equality every time (see `FrameProfiler::RecordCpuScope()`), so a
  temporary/stack-lifetime string would be a use-after-free risk for zero
  benefit. Never gate a scope name behind `GTE_ENABLE_EDITOR` - unlike a
  GPU resource's cosmetic debug name (see
  [GPU Resource Memory Tracking](gpu-resource-memory-tracking.md)
  above), a scope name is the PRIMARY payload here, needed in every build
  including a future headless benchmark run with no Editor compiled in at
  all.
- **The CPU scope model is deliberately FLAT, not a nested tree.** Every
  `GTE_PROFILE_SCOPE(name)` call anywhere in a frame - no matter how deeply
  nested inside another scope - contributes to the SAME name-keyed entry
  in that frame's `FrameSample::cpuScopes` (see `ProfilingTypes.h`),
  summed. This is a deliberate simplification (see
  `PROFILER_STRATEGY_v2.md`, Phase 0's own "hierarchy vs. flat list"
  design decision), not a limitation to work around - don't add parent/
  child tracking to `FrameProfiler` without first re-reading that
  document's own reasoning. Its one accepted, documented consequence: a
  scope that (directly or indirectly) calls itself within the same frame
  would have its self-time double-counted - fine today since no
  instrumented call site recurses, but don't be surprised by it if one
  ever does.
- **`SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()` is the
  ONE clock this whole module uses** (`ScopeTimer.h`, `FrameProfiler.cpp`)
  - never `std::chrono`, never a platform-specific API. SDL is already the
  one platform-abstraction layer this engine depends on for everything
  else timing-adjacent (`Application::Run()`'s own frame-delta
  computation), and is always linked regardless of `GTE_ENABLE_EDITOR` -
  see `PROFILER_STRATEGY_v2.md`, Step 3a.
- **Nothing in the per-frame hot path (a `ScopeTimer` construction/
  destruction, `FrameProfiler::RecordCpuScope()`, the ring buffer itself)
  may allocate on the heap.** `FrameSample::cpuScopes` and
  `FrameProfiler`'s own history ring buffer are both fixed-size
  `std::array`s, populated via plain POD writes - never
  `std::vector::push_back` past a reserved capacity, never a
  `std::string`. An allocator call has real, variable latency that would
  otherwise get baked into the very durations being measured, which a
  profiler must never itself exhibit - see `PROFILER_STRATEGY_v2.md`,
  Step 3a.
- **The on/off switch is genuinely two layers, and both matter.**
  `GTE_ENABLE_PROFILER=OFF` (a CMake option, `PUBLIC`-defined exactly like
  `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` - see `CMakeLists.txt`)
  compiles `ScopeTimer`'s constructor/destructor down to a true empty
  no-op with zero clock reads at all - the "genuinely zero cost" release
  branch. `FrameProfiler::SetCaptureEnabled(false)` (a runtime flag,
  independent of the compile-time switch) instead skips the clock
  read/ring-buffer write on every already-compiled-in `ScopeTimer` - the
  switch a future Editor "Profiler" panel/benchmark-mode CLI flag flips at
  runtime without needing a second build. Never conflate the two, and
  never remove either layer to simplify - see `PROFILER_STRATEGY_v2.md`,
  Phase 0b.
- **`FrameProfiler` (the data model + ring buffer) always compiles in,
  regardless of `GTE_ENABLE_PROFILER`** - only `ScopeTimer`'s body is
  gated. This is the exact same "the class stays available/testable even
  when its production call site is gated off" precedent `SdlMemoryTracker`
  already established (see
  [CPU Dependency Memory Tracking](cpu-dependency-memory-tracking.md) above) -
  don't wrap `FrameProfiler.h/.cpp` themselves in `#if GTE_ENABLE_PROFILER`.
- **`FrameProfiler::Instance()` is a process-wide singleton, same as
  `SdlMemoryTracker`'s static state** - not thread-safe (this engine is
  explicitly single-threaded throughout, see `GpuMemoryTracker`'s own
  class comment), and no thread-local/job-system-aware infrastructure
  should be added speculatively (see `PROFILER_STRATEGY_v2.md`'s own scope
  refusals). A test that touches `FrameProfiler::Instance()` must call
  `ResetForTesting()` before (and after) its own assertions, mirroring the
  "never assume a pristine baseline, since process-global state persists
  across every test in the same binary" convention already established
  for `SdlMemoryTracker`/`ImGuiMemoryTracker` - see
  `tests/Profiling/FrameProfilerTests.cpp`/`ScopeTimerTests.cpp`. A test
  needing a fully deterministic, KNOWN `cpuFrameMilliseconds` value (rather
  than whatever real `SDL_GetPerformanceCounter()`-measured duration a
  `BeginFrame()`/`EndFrame()` pair happens to produce) should use
  `FrameProfiler::OverrideLastFrameCpuMillisecondsForTesting()` - another
  narrowly-scoped, clearly-`ForTesting`-suffixed method in the same spirit
  as `ResetForTesting()`, added specifically so
  `tests/Profiling/FrameGraphDataTests.cpp` could assert exact, bit-precise
  min/max values instead of depending on real, inherently-jittery timing
  (e.g. via `SDL_Delay()`) to separate one frame's duration from another's.
- **A GPU-side or memory measurement that doesn't have a real value this
  frame is tagged `GpuSampleStatus::Absent`/`Unsupported`, never defaulted
  to a bare numeric `0`.** (`ProfilingTypes.h`'s `GpuSampleStatus`,
  `GpuPassSample`, `MemorySnapshot`.) A hidden Editor panel's pass not
  running this frame must never look, on a future graph/table, like it ran
  and cost nothing - see `PROFILER_STRATEGY_v2.md`, Step 2.3/3a. As of
  Phase 3 (`PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md`), this is
  real, wired-up behavior for draw-call/triangle counts specifically (see
  the `DrawStats.h`/`timingStatus`/`countStatus` bullets below) - as of
  Phase 5 (`PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md`), the memory
  snapshot is ALSO real, wired-up production data (see the
  `MemorySnapshotBuilder.h` bullet below) - and as of Phase 4
  (`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`, sub-phases 4A-4D), GPU
  TIMING itself is ALSO real, driver-measured production data now for all
  three named passes (see the `GpuTiming.h`/`GpuTimingService` bullet
  below) - every category `GpuPassSample`/`MemorySnapshot` can carry is now
  wired to genuine production data, with no synthetic-tests-only producer
  left.
- **`GpuPassSample` splits its tri-state into TWO INDEPENDENT fields,
  `timingStatus` and `countStatus` - never reintroduce a single combined
  `status`.** (`ProfilingTypes.h`.) `timingStatus`/`milliseconds` are
  governed exclusively by `FrameProfiler::SetGpuPassTiming()` (Phase 4's
  real Vulkan GPU timestamp queries as of `PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`'s
  sub-phases 4A-4D - unwired to anything real only historically, as of
  Phase 3); `countStatus`/`drawCallCount`/`triangleCount` are governed
  exclusively by `FrameProfiler::SetGpuPassDrawStats()` (Phase 3's own
  draw-call/triangle counts - real since that phase, see the `DrawStats.h`
  bullet below). This split exists because Phase 3 (cheap, self-contained)
  was deliberately implemented before Phase 4 (substantial/risky) - a
  single shared `status` field would have forced Phase 3's own call site
  to falsely claim GPU timing was also measured this frame the instant it
  reported a real count. The split remains just as load-bearing now that
  BOTH phases are real: `Application::Run()`'s Game/Scene/Present blocks
  each call `SetGpuPassDrawStats()` and `SetGpuPassTiming()` as two
  genuinely separate calls, so a future edit that skips one of them (e.g.
  a new offscreen pass that draws but is deliberately not GPU-timed, same
  as `AssetPreviewMesh`/`BoneViewerWindow`'s `std::nullopt` opt-out - see
  the `GpuTiming.h`/`GpuTimingService` bullet below) still can't
  accidentally imply the other. See
  `PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md`, Step 2.4, and its own
  regression test,
  `tests/Profiling/FrameProfilerTests.cpp`'s `DrawStatsAloneDoNotImplyRealTimingData`.
  `FrameGraphData.cpp`'s `ComputeGpuMillisecondsRange()` branches on
  `timingStatus` only, never `countStatus` - a pass whose only data this
  session is a draw-stats call correctly reports `hasData == false`
  for timing (see `tests/Profiling/FrameGraphDataTests.cpp`'s
  `DrawStatsOnlyPassReportsNoTimingData`).
- **`src/Renderer/DrawStats.h/.cpp`** (Phase 3 -
  `PHASE3_DRAW_CALL_TRIANGLE_COUNT_STRATEGY_v2.md`) is the always-compiled,
  Vulkan-free pure accumulator behind the draw-call/triangle counts above:
  `AccumulateDrawStats()` turns one queued draw's shape
  (`hasIndexBuffer`/`vertexCount`/`indexCount`) into an incremental
  `{drawCallCount, triangleCount}` contribution, and is called INLINE from
  inside `FrameRecorder::RecordFrame()`'s existing per-item loop - on the
  exact same code path that already issues the real
  `vkCmdDraw`/`vkCmdDrawIndexed` for that item, immediately after it -
  never from a separate pass over `m_drawQueue`. This is a correctness
  decision, not a style preference: a separate counting pass would be a
  second, independent place that has to keep agreeing with whatever the
  real recording loop actually does, including any future skip/validity
  branch added there - fusing the two into one loop makes divergence
  between "what was counted" and "what was actually drawn" structurally
  impossible. `CountDrawStats()` (a batch wrapper over
  `AccumulateDrawStats()`) exists purely so
  `tests/Renderer/DrawStatsTests.cpp` can write table-driven tests without
  a live `FrameRecorder` - production code always calls
  `AccumulateDrawStats()` directly, never `CountDrawStats()`. Triangle
  counting (`(indexed ? indexCount : vertexCount) / 3`) assumes every
  `Pipeline` is `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` (true today - see
  `Pipeline.cpp`) and every draw has `instanceCount == 1` (no instancing
  exists anywhere in this engine yet) - both assumptions are documented
  directly in `DrawStats.h` and must be revisited together if either ever
  changes. Deliberately NOT gated behind `#if GTE_ENABLE_PROFILER` (unlike
  `ScopeTimer`'s per-scope clock read) - `m_drawQueue` is already iterated
  unconditionally every frame to issue the real draw calls regardless of
  that switch, so this accumulation rides along on that same,
  already-necessary iteration at effectively no extra measurable cost.
  `FrameRecorder::RecordFrame()`/`FramePresenter::Present()`/
  `RenderOffscreen()`/`Renderer::Present()`/`RenderOffscreen()` all thread
  this `DrawStats` result back up to `Application::Run()`, the one place
  that knows which named `GpuPass` a given recording corresponds to -
  `FramePresenter::Present()`'s several early-return paths (minimized
  window, pending resize, just-recreated swapchain) return
  `std::optional<DrawStats>` as `std::nullopt` specifically so a frame
  that recorded nothing is never confused with one that recorded and drew
  zero queued items - `RenderOffscreen()` has no such early-return path
  and always returns a real `DrawStats`.
- **`src/Profiling/FrameGraphData.h/.cpp`** (Phase 2 -
  `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md`) is the one place
  `FrameProfiler`'s ring buffer gets reshaped into plottable points -
  `BuildFrameGraphPoints()` (history -> an ordered `FrameGraphPoint` array,
  each carrying `frameIndex`/`cpuMilliseconds`/all three `GpuPassSample`
  entries verbatim) plus `ComputeCpuMillisecondsRange()`/
  `ComputeGpuMillisecondsRange()` (a Y-axis min/max helper that correctly
  ignores `Absent`/`Unsupported` GPU entries, branching only on `status`,
  never on whatever numeric value happens to be stored alongside it).
  Always-compiled and ImGui-free, exactly like `FrameProfiler` itself (no
  `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROFILER` dependency at all) - a future
  consumer (Phase 6's benchmark-mode CSV exporter, Phase 7's Editor
  "Profiler" panel, or any other future graph/export need) must call these
  functions rather than re-deriving the same history-walk/tri-state-scan
  logic a second time.
- **`src/Application/MemorySnapshotBuilder.h`** (Phase 5 -
  `PHASE5_GPU_MEMORY_HISTORY_STRATEGY_v2.md`) is the one, small,
  Tier-1-tested place that reshapes `Renderer::GetMemoryTotals()`'s result
  (`GpuMemoryTracker::Totals`, a Vulkan-tied type) into a
  `Profiling::MemorySnapshot` (a plain, Vulkan-free type) - deliberately
  its OWN header rather than an anonymous-namespace helper inlined into
  `Application.cpp`, specifically so `BuildMemorySnapshot()` itself can be
  called directly from `tests/Application/MemorySnapshotBuilderTests.cpp`
  (a bug transposing two of its eight fields would otherwise be invisible
  to every `FrameProfiler`-level test, which all hand-construct a
  `MemorySnapshot` directly and never call this function). `Application::Run()`
  is the ONE production call site: it calls this once per frame,
  unconditionally (not `#if GTE_ENABLE_PROFILER`/`GTE_ENABLE_EDITOR`-gated,
  matching this same function's own `BeginFrame()`/`EndFrame()`/
  `SetGpuPassDrawStats()` calls), as late as possible in the frame (right
  before `EndFrame()`) so it reflects every GPU resource created/destroyed
  anywhere that frame, and always with `status == GpuSampleStatus::Present`
  - unlike a `GpuPass`'s draw-call/triangle count, `Renderer::GetMemoryTotals()`
  has no "didn't run this frame" concept at all; it is always a valid,
  meaningful O(1) read for as long as a live `Renderer` exists.
- **`FrameGraphPoint` (Phase 2, above) gained a `memory` field, and
  `FrameGraphData.h/.cpp` gained `ComputeMemoryBytesRange()`, as part of
  Phase 7** (`PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md`) - the Editor
  "Profiler" panel's GPU-memory-over-time sparkline (see
  [Editor Module Structure](editor-module-structure.md) below) is what
  first needed this, but it lives in this
  always-compiled, Editor-independent module (not `src/Editor/
  ProfilerPanelData.h`) for the exact same reason `FrameGraphPoint`/
  `ComputeCpuMillisecondsRange()`/`ComputeGpuMillisecondsRange()` already do
  - so a future Phase 6 benchmark-mode CSV exporter consumes the SAME
  reshape, never a second copy. `ComputeMemoryBytesRange()` mirrors
  `ComputeGpuMillisecondsRange()`'s own "branch on status, never on the
  value" rule exactly: only entries whose `memory.status ==
  GpuSampleStatus::Present` contribute to the min/max scan.
- **`src/Renderer/GpuTiming.h/.cpp`, `src/Renderer/GpuTimingService.h/.cpp`,
  `src/Renderer/Vulkan/VulkanQueryPool.h/.cpp`** (Phase 4 -
  `PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`, sub-phases 4A-4D - see
  `PHASE4A_COMPLETION_REPORT.md`/`PHASE4B_COMPLETION_REPORT.md`/
  `PHASE4C_COMPLETION_REPORT.md`/`PHASE4D_COMPLETION_REPORT.md`) are what
  finally make `SetGpuPassTiming()` above a genuine production call
  instead of a test-only one. `GpuTiming.h` is the always-compiled,
  Vulkan-header-FREE pure-data/pure-math half (mirroring `DrawStats.h`'s
  own precedent exactly) - `GpuTimestampCapability`/
  `InterpretTimestampCapability()` (device capability probing, queried
  once by `VulkanDevice::TimestampCapability()`), `GpuTimingSlot`
  (`Offscreen0`/`Offscreen1`/`SwapchainPresent` - deliberately GENERIC
  names, never `GameView`/`SceneView`, since `Renderer`/`FramePresenter`
  must never know Editor-facing pass naming), `GpuTimingSample` (a
  Renderer-local tri-state mirror of `Profiling::GpuSampleStatus`,
  deliberately a SEPARATE type so `Renderer` stays completely free of any
  `Profiling/` header), `ConvertTimestampDeltaToMilliseconds()` (tick-delta
  -> millisecond conversion, wraparound-safe via `validBits` masking), and
  `ResolveGpuTimingStatus()` (the pure tri-state PRIORITY decision -
  `Unsupported` always wins over `Absent` wins over `Present`). `VulkanQueryPool`
  (`Vulkan/`) is a thin RAII wrapper around one `VK_QUERY_TYPE_TIMESTAMP`
  `VkQueryPool`, fixed 8-slot layout, never resized/recreated.
  `GpuTimingService` owns that pool and every actual
  `vkCmdResetQueryPool`/`vkCmdWriteTimestamp2`/`vkGetQueryPoolResults` call
  site - `FramePresenter` only ever calls INTO it (`RecordOffscreenPassStart/
  End`/`ReadOffscreenResultNow` for `RenderOffscreen()`,
  `RecordPresentPassStart/End`/`ReadPresentResultIfAvailable`/
  `MarkPresentSlotWritten` for `Present()`), never issuing a raw query call
  itself, mirroring the same division of labor `FramePresenter` already has
  with `VulkanSwapchain`/`VulkanFrameSync`. **Gated by BOTH a compile-time
  switch (`GTE_ENABLE_PROFILER` - forces `GpuTimingService`'s effective
  capability to `unsupported`, so a `GTE_ENABLE_PROFILER=OFF` build never
  creates a `VkQueryPool` at all) AND a runtime switch
  (`GpuTimingService::SetCaptureEnabled()`, driven every frame by
  `Renderer::SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled())`
  in `Application::Run()` - the exact same two-layer on/off convention
  this section already establishes for `ScopeTimer` above, now applied to
  a genuinely non-free per-frame GPU/driver cost rather than a CPU clock
  read.** `Renderer::RenderOffscreen()`'s `std::optional<GpuTimingSlot>`
  parameter has NO default - every caller must explicitly say
  `GpuTimingSlot::Offscreen0`/`Offscreen1` (Game/Scene, `Application.cpp`)
  or `std::nullopt` (any call with nothing to do with the Profiler's three
  named passes - `AssetPreviewMesh`'s Inspector mesh preview,
  `BoneViewerWindow`'s own viewport) - `std::nullopt` is one of two equally
  explicit choices, never an implicit fallback, specifically so a future
  Editor debug-preview caller can never silently share a query slot with,
  and corrupt, "Game View"/"Scene View" GPU timing. Every read (both the
  offscreen path's post-fence-wait read and the Present path's
  per-frame-in-flight-slot read) is positioned at a point synchronization
  the engine ALREADY performs for an unrelated, pre-existing reason - no
  new GPU wait was ever added anywhere purely to fetch a timing result
  sooner; see `PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`'s own Step 2.3
  for the exact reasoning this must never be weakened against.
