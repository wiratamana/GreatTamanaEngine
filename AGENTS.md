# AGENTS.md

Instructions for LLM/AI agents working on this codebase.

## Coding Guidelines

- **Clean Architecture**: Write clean architecture code. Keep clear
  separation of concerns between layers (e.g. SDL -> Application -> Window
  and Renderer -> Game). Lower-level/core layers must not depend on
  higher-level or framework-specific details. Only the `Application` layer
  should know about SDL directly; other layers must go through the custom
  abstraction objects (Window, Renderer, etc.).
- **RAII**: Every resource-owning piece of code must use RAII (Resource
  Acquisition Is Initialization). Resources (SDL handles, memory, file
  handles, GPU objects, etc.) must be acquired in constructors and released
  in destructors, so lifetime is tied to object scope and cleanup is
  automatic and exception/error-safe. Avoid manual/explicit cleanup calls
  scattered through the code — wrap raw resources in owning types instead.
- **Namespace**: Every new script (every class/function/type this project
  defines) must live inside the `gte` namespace (short for Great Tamana
  Engine), e.g. `namespace gte { class Window { ... }; }`. This keeps engine
  symbols from colliding with SDL's or third-party globals.

## GPU Resource Memory Tracking

Every GPU resource type (`Buffer`, `RenderTexture`, and any future type -
vertex/index/uniform buffers, textures, etc.) must register with
`GpuMemoryTracker` (`src/Renderer/Memory/GpuMemoryTracker.h`) so the engine
always has an accurate, live picture of exactly what GPU memory is
allocated, of what kind, and where - a Unity-Memory-Profiler-style live
object registry, not just an aggregate byte counter. Follow these rules
whenever touching GPU resource lifetime code:

- **Identify resources by handle, never by pointer or string.**
  `GpuResourceHandle` is a cheap 8-byte POD (index + generation), generated
  automatically by `GpuMemoryTracker::Track()` - calling code never
  invents/assigns its own id. Handles are meant to be copied/compared/
  stored by the thousands with no real cost, unlike a `std::string`, which
  is comparatively large and unpredictable memory-wise.
- **The tracked record must always reflect the CURRENT actual allocation -
  never a stale, construction-time snapshot.** Any lifecycle method that
  destroys and recreates a resource's underlying VMA allocation (e.g.
  `RenderTexture::Resize()`, which internally does `Destroy()` +
  `Create()`) is creating a genuinely new allocation, and MUST `Untrack()`
  the old handle and `Track()` a fresh one reflecting the new size/location
  as part of that same operation. A resource's `Handle()` is therefore NOT
  guaranteed stable across its lifetime - only guaranteed valid for
  whatever the resource's CURRENT allocation actually is. Never assume a
  handle captured once stays correct after a resize/recreate; always read
  `Handle()` again afterwards if you need it. This was verified with a
  dedicated runtime test (create -> resize -> confirm the old handle is
  gone, the new one is tracked, and the byte count reflects the new size,
  with no duplicate/leaked entry) - re-verify this way whenever this code
  path changes.
- **Track the size VMA actually gave you, not the size you requested.**
  Use the `VmaAllocationInfo::size` returned by `vmaCreateBuffer`/
  `vmaCreateImage` (VMA may allocate more than requested due to alignment),
  and classify the real memory location via `ClassifyGpuMemoryLocation()`
  (reads the allocation's actual `VkMemoryPropertyFlags` from VMA) rather
  than assuming it matches whatever `BufferMemoryUsage` was requested -
  VMA's actual choice can legitimately differ (e.g. falling back to plain
  host-visible system RAM instead of a shared device-local+host-visible
  heap).
- **Human-readable debug names are Editor-only and live in a completely
  separate table from the hot resource record.** Pass names as a plain
  `const char*` (never `std::string`) through an optional `debugName`
  parameter, and only ever store/attach them via
  `GpuMemoryTracker::SetDebugName()`, which is guarded by
  `#if GTE_ENABLE_EDITOR` in `GpuMemoryTracker.h` - this compiles the name
  table out ENTIRELY (not just unused) in a non-Editor/release build, so a
  shipped game carries zero string cost for this. Never add a name/string
  field to `GpuResourceRecord` itself. If a resource's debug name must
  survive a resize/recreate (see above), store the `const char*` on the
  resource itself and re-apply it via `SetDebugName()` every time it
  re-tracks - this requires the caller-supplied string to have static
  storage duration (e.g. a string literal), since only the pointer is kept,
  not a copy.
- **Own the tracker via `std::shared_ptr`, never a raw pointer/reference.**
  `Renderer` owns the one `GpuMemoryTracker` and hands a `shared_ptr` copy
  to every `Buffer`/`RenderTexture` it creates, so tracking stays valid no
  matter how `Renderer`/`VulkanAllocator` get moved later - a raw
  pointer/reference into `VulkanAllocator` or `Renderer` itself would risk
  dangling after a move (the underlying Vulkan handles survive moves fine,
  but the C++ wrapper objects can relocate). Any new GPU resource type
  added later should follow this same pattern, not invent its own.

## CPU Dependency Memory Tracking

Alongside `GpuMemoryTracker` (above), the engine also tracks how much CPU
(host) memory its own third-party dependencies are using - `SdlMemoryTracker`
(`src/Memory/SdlMemoryTracker.h`, always compiled) for SDL, and
`ImGuiMemoryTracker` (`src/Editor/ImGuiMemoryTracker.h`, Editor-only) for Dear
ImGui - both surfaced by the Editor's "Memory" panel
(`src/Editor/Panels/MemoryPanel.cpp`) as their own named CPU buckets,
alongside `Renderer::GetVmaHeapBudgets()` (the real, driver-reported GPU heap
usage/budget, distinct from `GpuMemoryTracker`'s own tally). Follow these
rules whenever touching this code or adding a tracker for a future
dependency:

- **Install the tracking allocator before the dependency's first call of any
  kind, not just before some "main" entry point.** Both SDL
  (`SDL_SetMemoryFunctions()`) and Dear ImGui
  (`ImGui::SetAllocatorFunctions()`) document this same constraint: swapping
  allocators after the library has already allocated something risks a later
  free using a DIFFERENT allocator than whatever alloc call originally served
  that pointer. `SdlMemoryTracker::Install()` is called at the very top of
  `Application::SdlContext`'s constructor (`Application.cpp`), before
  `SDL_Init()`; `ImGuiMemoryTracker::Install()` is called at the very top of
  `ImGuiEditorLayer`'s constructor (`ImGuiEditorLayer.cpp`), before
  `ImGui::CreateContext()`. A future tracker for a new dependency must find
  and hook that same "first call" point, not an approximate/later one.
- **The production `Install()` call site must itself be gated behind
  `#if GTE_ENABLE_EDITOR`, even if the tracker CLASS compiles in every
  build.** These trackers exist purely to feed the Editor's "Memory" panel -
  a release/shipped build has no panel to display them and must not pay
  their real per-allocation cost (an extra header write + atomic increment
  on EVERY single alloc/free of that dependency, for the rest of the
  process's lifetime) for nothing. `ImGuiMemoryTracker::Install()` gets this
  for free (its call site, `ImGuiEditorLayer`'s constructor, is only ever
  compiled when `GTE_ENABLE_EDITOR` is ON in the first place - see "Editor
  Module Structure"). `SdlMemoryTracker::Install()` does NOT get this for
  free, since `Application.cpp` compiles in every build - its call site in
  `Application::SdlContext`'s constructor is explicitly wrapped in
  `#if GTE_ENABLE_EDITOR` for exactly this reason. A future tracker for a
  dependency used outside `src/Editor/` (i.e. one whose install call site
  isn't naturally Editor-only compiled) must add this same explicit guard at
  its call site - don't assume "the class only matters to the Editor" is
  enough on its own to keep it out of a release build's runtime behavior.
- **`Install()` must be idempotent.** Both trackers guard themselves with a
  local `static bool installed` - calling `Install()` more than once (e.g.
  from a test, or if a future call site is added) is always a safe no-op, so
  no caller ever needs to guard its own call site.
- **These are necessarily static/process-global, not instance-based like
  `GpuMemoryTracker`.** `SDL_malloc_func`/`SDL_free_func` and
  `ImGuiMemAllocFunc`/`ImGuiMemFreeFunc` are plain C function pointers with
  no (or, for ImGui, an engine-unused) userdata slot to stash a `this` in -
  there is nowhere else the byte/count totals could live. This matches a
  constraint SDL's own `SDL_GetNumAllocations()` already has.
- **A hidden per-allocation header carries the LOGICAL size, since free-side
  callbacks are only ever handed the pointer, never a size.** Both trackers
  use the same fixed 16-byte header trick (see `SdlMemoryTracker.cpp`'s
  `AllocHeader`/`HeaderEncode()`/`HeaderDecode()`) - 16 bytes because that is
  this Windows target's guaranteed allocation alignment (the smaller of
  `alignof(std::max_align_t)` or `2*sizeof(void*)`), so offsetting the
  underlying allocator's own aligned block by exactly that many bytes
  preserves its alignment guarantee for the pointer handed back to the
  caller. A future tracker copying this pattern must keep the header size a
  multiple of that alignment, not just `sizeof(size_t)`.
- **Both trackers are genuinely Tier-1-testable despite touching a
  third-party library's own allocator.** Neither `SDL_malloc()`/`SDL_free()`
  nor `ImGui::MemAlloc()`/`MemFree()` need `SDL_Init()`, a live window, or an
  `ImGuiContext` to be called safely - see
  `tests/Memory/SdlMemoryTrackerTests.cpp`/
  `tests/Editor/ImGuiMemoryTrackerTests.cpp` for the pattern: capture
  `LiveBytes()`/`LiveAllocationCount()` BEFORE each test's own alloc/free
  calls and assert on the DELTA, never an assumed absolute baseline, since
  `Install()`'s process-global state persists across every test in the same
  binary.

## Profiling

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
  resource (see "Coding Guidelines", RAII). `name` MUST be a string
  literal (or otherwise static-storage-duration) `const char*` - it is
  compared against every other scope's name via pointer/`strcmp()`
  equality every time (see `FrameProfiler::RecordCpuScope()`), so a
  temporary/stack-lifetime string would be a use-after-free risk for zero
  benefit. Never gate a scope name behind `GTE_ENABLE_EDITOR` - unlike a
  GPU resource's cosmetic debug name (see "GPU Resource Memory Tracking"
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
  already established (see "CPU Dependency Memory Tracking" above) -
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
  "Profiler" panel's GPU-memory-over-time sparkline (see "Editor Module
  Structure" below) is what first needed this, but it lives in this
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

## Job System

`src/Jobs/` (`JobTypes.h`, `JobQueue.h/.cpp`, `JobSystem.h/.cpp`,
`JobDispatch.h/.cpp`, `JobContinuation.h/.cpp`) is the engine's general-purpose
worker-thread pool - see `task_manager/job_system/JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md`
for the full multi-phase campaign this is Phases 1-6 of,
`task_manager/job_system/JOB_SYSTEM_PHASE1_COMPLETION_REPORT.md` for Phase
1's own detailed writeup, `task_manager/job_system/JOB_SYSTEM_PHASE2_COMPLETION_REPORT.md`
for Phase 2's, `task_manager/job_system/JOB_SYSTEM_PHASE3_COMPLETION_REPORT.md`
for Phase 3's, `task_manager/job_system/JOB_SYSTEM_PHASE4_COMPLETION_REPORT.md`
for Phase 4's (the Thread-Safety Audit),
`task_manager/job_system/JOB_SYSTEM_PHASE5_COMPLETION_REPORT.md` for Phase
5's (Profiler Integration - Worker Timeline), and
`task_manager/job_system/JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md` for Phase
6's (First Production Consumer - Animation / Vertex Skinning). As of Phase
6, this module provides the minimal `JobHandle`/`Schedule()`/`WaitForJobs()`/
`WorkerCount()` primitive (Phase 1), the batch/parallel-for API, `Dispatch()`/
`ComputeBatchRanges()` (Phase 2), job dependencies/continuations,
`ScheduleAfter()`/`DispatchAfter()` (Phase 3 - see this section's own
dedicated Phase 3 bullets further below), a written, reviewable
thread-safety classification (NEVER/READ-SAFE/JOB-SAFE) of every existing
shared/global/singleton piece of engine state a future job body could reach
into (Phase 4 - see this section's own dedicated "Phase 4 - Thread-Safety
Audit" bullets further below), a genuinely thread-safe way for a job
body to record its own CPU scope into `Profiling::FrameProfiler`,
`GTE_PROFILE_JOB_SCOPE`/`Profiling::JobScopeTimer` (Phase 5 - see this
section's own dedicated Phase 5 bullets further below), and its first real
production consumer - `AnimationSystem::Update()`
(`src/Game/Animation/AnimationSystem.cpp`) now dispatches CPU vertex
skinning (`Animation/VertexSkinning.h`'s `SkinVertexRange()`) across the
worker pool for a sufficiently large rigged model (Phase 6 - see this
section's own dedicated Phase 6 bullets further below). Nothing else in
the engine calls `Schedule()`/`Dispatch()`/`ScheduleAfter()`/`DispatchAfter()`
yet - `AnimationSystem::Update()` is still the only real, non-test call site.
Follow these rules whenever touching this module or building a later phase on top of it:

- **`gte::Jobs::JobSystem::Instance()` is a Meyers singleton that starts
  lazily, on its FIRST call from anywhere in the process** - the exact same
  pattern `Profiling::FrameProfiler::Instance()` already uses (see
  "Profiling" above). This means the worker pool - real OS `std::thread`s -
  does not exist at all, and none are ever created, until the first genuine
  `Schedule()` call happens to run somewhere in the engine. Since nothing
  calls `Schedule()` in production yet (only this module's own tests do),
  a plain build/run of the engine today never spins up a single worker
  thread - don't be surprised if a profiling capture shows zero job/worker
  activity before Phase 6 lands; that's expected, not a bug.
- **`JobSystem` (and `JobQueue`) always compile, unconditionally, regardless
  of `GTE_ENABLE_JOB_SYSTEM`** - the same "the class stays available/
  testable even when its production behavior is gated off" precedent
  `SdlMemoryTracker`/`FrameProfiler` already established. Only the
  *internal* behavior differs per that switch: `GTE_ENABLE_JOB_SYSTEM=ON`
  (the default) runs `Schedule()`'d work on a real worker-thread pool sized
  from `std::thread::hardware_concurrency()` (falling back to 1 if that
  returns 0); `=OFF` runs `Schedule()`'d work IMMEDIATELY, synchronously, on
  the calling thread, with no `std::thread` ever created - the public API's
  observable contract (a `JobHandle` eventually becomes complete;
  `WaitForJobs()` returns once it is) is identical either way. This is the
  exact same two-branch, ODR-safe "gate at the .cpp level, never at the
  call site" convention `GTE_ENABLE_EDITOR`'s `ImGuiEditorLayer.cpp`/
  `NullEditorLayer.cpp` split already established.
- **`JobHandle` is backed by a `std::shared_ptr<detail::JobHandleState>` -
  exactly ONE heap allocation at `JobHandle` construction, never one per
  job scheduled against it.** A single `JobHandle` is meant to be reused
  across MANY `Schedule()` calls (Phase 2's whole batch-`Dispatch()` design
  shares one `JobHandle` across every batch of a single call) -
  `JobSystem::Schedule()` itself never allocates on the heap in its
  steady-state path (`JobQueue` is a fixed-capacity ring buffer, sized once
  at construction - see `JobQueue.h`'s own comment on why this is a fixed
  size rather than a growable container, mirroring
  `kMaxCpuScopesPerFrame`/`kMaxFrameHistory`'s precedent in
  `src/Profiling/`). A full queue is handled by `Schedule()` falling back
  to running the job immediately, inline, on the calling thread - never by
  blocking, growing the buffer, or dropping the job silently.
- **A worker's pending-count decrement MUST be bracketed by the SAME mutex
  `WaitForJobs()` holds while checking its own predicate
  (`JobSystem::m_completionMutex`) - never just an atomic write on its
  own.** This is a real, confirmed-in-practice classic
  `condition_variable` lost-wakeup race, not a theoretical concern: a
  waiter can check `IsComplete()` (see it as still false, while still
  holding the mutex) and, before it finishes registering itself as a
  waiter on `m_completionCondition`, a worker's decrement-then-
  `notify_all()` sequence can run to completion on another thread and find
  no one registered yet to wake - the waiter then blocks forever waiting
  for a notification that already happened moments earlier. This was
  reproduced intermittently (roughly 1 run in 4) under a stress test
  scheduling 256 jobs against one shared handle, before the fix (holding
  `m_completionMutex` around the `fetch_sub`, in both `JobSystem::
  WorkerLoop()` and `Schedule()`'s own full-queue fallback path) closed it
  - a 100-iteration `--gtest_repeat` stress run showed zero hangs
  afterward. Any FUTURE piece of code that mutates state a
  `condition_variable` wait's predicate depends on must follow this same
  "bracket the mutation with the SAME mutex the waiter holds while
  checking" rule - see `cppreference`'s own
  `condition_variable::notify_all()` documentation ("even though the
  shared variable is atomic, it must be modified while owning the mutex to
  correctly publish the modification to the waiting thread").
- **Never gate a new `src/Jobs/` test file behind `GTE_ENABLE_EDITOR`/
  `GTE_ENABLE_JOB_SYSTEM` in `tests/CMakeLists.txt`.** `JobQueue`/
  `JobSystem` both always compile (see above), so
  `tests/Jobs/JobQueueTests.cpp`/`JobSystemTests.cpp` are added to
  `GTE_TEST_SOURCES` unconditionally, the same "always built" bucket as
  `Profiling/FrameProfilerTests.cpp`/`ScopeTimerTests.cpp` - and both pass
  identically whether `GTE_ENABLE_JOB_SYSTEM` is `ON` or `OFF` (verified:
  the full suite passes in both configurations, and separately under a
  completely different toolchain - MinGW/GCC vs. this project's usual
  MSVC/Ninja build - as an extra cross-check for this module specifically,
  since it's the engine's first genuinely multi-threaded code).
- **A concurrency bug in this module can pass by luck on a single test
  run.** Any new test that exercises real cross-thread interaction (not
  just `JobQueue`'s own single-threaded ring-buffer logic) should be
  stress-repeated (e.g. `--gtest_repeat=50` or more) at least once before
  being trusted, mirroring the discipline
  `JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md` already calls for
  future phases - a single green run is not sufficient evidence for
  genuinely concurrent code, as this phase's own lost-wakeup bug
  demonstrated directly.
- **Phase 2's `Dispatch(fn, itemCount, payload, handle, minItemsPerBatch)`
  (`src/Jobs/JobDispatch.h/.cpp`) turns Phase 1's "one job per `Schedule()`
  call" primitive into an ergonomic parallel-for/batch API, built ENTIRELY
  on top of `Schedule()`/`WaitForJobs()`/`JobHandle` - no second scheduler,
  no second queue, no parallel bookkeeping duplicated.** `Dispatch()` splits
  `[0, itemCount)` into a bounded number of contiguous batches
  (`ComputeBatchRanges()` - never more than `JobSystem::Instance().WorkerCount()`
  batches, and never smaller than the caller's own `minItemsPerBatch` floor
  unless `itemCount` itself is smaller, in which case it collapses to
  exactly one batch) and calls `Schedule()` once per batch, every batch
  sharing the SAME `payload` pointer (read-only) and the SAME `handle` (so
  one `WaitForJobs(handle)` call waits for the whole dispatch, never one
  per batch).
- **BATCHES, not items, are the unit of scheduling - `Dispatch()` never
  schedules one job per array element.** Every `Schedule()` call touches
  the shared, mutex-guarded `JobQueue` plus an atomic increment on the
  handle's counter - for genuinely tiny per-item work, scheduling one job
  PER ITEM would spend more total time on scheduling overhead than on the
  actual work (the classic "over-parallelized until it's slower than
  serial" trap). `Dispatch()`'s own batch count is derived automatically
  from `WorkerCount()`, never something a caller has to compute by hand -
  `minItemsPerBatch` (default 1) is the one knob a caller with real
  per-item-cost knowledge can use to floor the batch size (e.g. "never
  split fewer than 8 vertices' worth of work into their own batch" for a
  future vertex-skinning call site) - `Dispatch()` never second-guesses
  that floor by splitting smaller anyway.
- **A NECESSARY, DELIBERATE, DOCUMENTED exception to Phase 1's own "zero
  heap allocation in the steady-state per-job path" guarantee: each batch
  gets its own small, heap-allocated `DispatchJobContext`
  (`JobDispatch.cpp`, anonymous namespace) - the function pointer + user
  payload pointer + that batch's own `BatchRange` - freed by the batch job
  itself (`RunBatchJobTrampoline()`) right before it returns.** This is
  required because each batch needs its OWN distinct `[begin, end)` range,
  and `Dispatch()` itself does not block (it returns immediately), so that
  range has to live somewhere between `Dispatch()` returning and the batch
  job actually running - a plain `Schedule()` call can hand the caller's
  own long-lived `payload` straight through with zero allocation, but
  `Dispatch()` cannot, since no single long-lived object naturally holds
  N different ranges. The number of these allocations per `Dispatch()`
  call is bounded by `ComputeBatchRanges()`'s own batch-count ceiling (at
  most `WorkerCount()` - a handful, never one per array element) - a
  small, BOUNDED, explicitly-reviewed trade for API ergonomics, not an
  accidental violation of Phase 1's guarantee. If this were ever found to
  matter in practice, the fix is a small fixed-size pool of reusable
  `DispatchJobContext` slots (mirroring `JobQueue`'s own fixed-capacity
  philosophy) - deliberately NOT built preemptively.
- **`ComputeBatchRanges()` (the pure batch-splitting math) is
  Tier-1-tested completely separately from `Dispatch()` itself
  (`tests/Jobs/JobDispatchMathTests.cpp` vs. `JobDispatchTests.cpp`)** -
  the same "test the pure math in isolation before wiring it into anything
  stateful" discipline this codebase already applies elsewhere
  (`DrawStats.h` before `FrameRecorder`, `GpuTiming.h` before
  `GpuTimingService`). The three invariants every test in
  `JobDispatchMathTests.cpp` checks - ranges are contiguous/gap-free, never
  overlap, and their union is exactly `[0, itemCount)` - are the single
  most important property this function guarantees; a future consumer
  (e.g. Phase 6's CPU vertex skinning) would silently corrupt or skip data
  if any of them were ever violated.
- **Phase 3 (Job Dependencies / Continuations - `src/Jobs/JobContinuation.h/.cpp`,
  see `task_manager/job_system/JOBSYSTEM_PHASE3_JOB_DEPENDENCIES_CONTINUATIONS.md`)
  adds `ScheduleAfter()`/`DispatchAfter()` - the ability to say "run this
  job/batch dispatch only once these OTHER handles have completed" -
  without the main thread ever having to call `WaitForJobs()` in between
  and manually stitch stages together.** Built ENTIRELY on top of Phases
  1-2's existing `JobSystem::Schedule()`/`WaitForJobs()`/`JobHandle`/
  `Dispatch()` - no second scheduler, no second queue, no parallel
  bookkeeping duplicated. If EVERY handle in `dependencies` is already
  complete at call time (the common case - a dependency that finished
  earlier in the same frame), this degrades to an ordinary `Schedule()`
  call with zero continuation bookkeeping at all - explicit dependencies
  only, there is no automatic data-flow dependency inference anywhere in
  this module.
- **A deferred continuation's `handle` becomes "incomplete" the INSTANT
  `ScheduleAfter()`/`DispatchAfter()` returns - not lazily, once the first
  dependency clears - via `JobHandle::AddPendingUnit()` (a manual pending-
  counter increment, paired 1:1 with `JobSystem::ScheduleAlreadyPending()`'s
  own decrement once the deferred work actually finishes running).** This
  is what makes it safe for a caller to call `WaitForJobs(handle)`
  immediately after `ScheduleAfter()` returns and correctly block until the
  real work has run, no matter how long its dependencies take to clear - a
  naive "decrement then later increment again via a normal `Schedule()`
  call" approach would risk a transient false-complete gap a concurrent
  `WaitForJobs()` caller could observe. `ScheduleAlreadyPending()` is
  deliberately a SEPARATE method from `Schedule()` (never increments
  `pending` itself) purely for this reason - production/test code
  scheduling ordinary, non-continuation work must always call `Schedule()`/
  `Dispatch()` directly instead.
- **A dependency handle's watcher list (`detail::JobHandleState::
  watcherFns`/`watcherContexts`, `JobTypes.h`) is a small, FIXED-CAPACITY
  array (`detail::kMaxWatchersPerHandle`, 8 by default) - never a growable
  container.** `JobHandle::AddCompletionWatcher()` registers a callback to
  run once that ONE handle's `pending` reaches zero (or calls it
  immediately, synchronously, if already zero) - guarded by the same
  mutex-bracketing discipline Phase 1's own lost-wakeup-race fix already
  established for `m_completionMutex` (see above), applied here to a
  SEPARATE, per-handle `watcherMutex` instead: the pending-zero check and
  the watcher-list mutation happen under the same lock `FireWatchers()`
  takes to read/clear the list, so a registration can never be "too late"
  to see a completion that raced it. Firing every registered watcher
  (`JobHandleState::FireWatchers()`, called by whichever thread performs
  the decrement of `pending` down to zero - see `JobSystem::WorkerLoop()`/
  `Schedule()`/`ScheduleAlreadyPending()`'s own full-queue-fallback paths)
  is deliberately done AFTER releasing that lock, since a fired watcher
  may itself call back into `JobSystem::Schedule()`/`ScheduleAlreadyPending()`.
- **Once a single handle already has `kMaxWatchersPerHandle` OTHER
  continuations registered against it, any further dependent falls back to
  a dedicated, DETACHED `std::thread` that busy-polls
  (`JobContinuation.cpp`'s `WatchDependencyWithFallback()`/
  `RunPollingFallbackJob()`) - never silently dropped, and, just as
  importantly, NEVER routed through `JobSystem::Schedule()`.** This is a
  real, confirmed-in-practice correctness fix, not a style preference: with
  `GTE_ENABLE_JOB_SYSTEM=OFF`, `Schedule()` runs its job IMMEDIATELY,
  SYNCHRONOUSLY, on whichever thread calls it (see Phase 1's own OFF-mode
  contract) - if the overflow fallback's poll job were scheduled that way
  and the dependency it's polling can only ever be completed by something
  running concurrently on ANOTHER thread (the only way it could still be
  genuinely pending after 8 other watchers are already ahead of it), that
  `Schedule()` call would spin forever on the calling thread instead of
  yielding it back - a genuine deadlock, reproduced directly by this
  phase's own `JobContinuationTests.OverflowingWatcherCapacityStillRunsEveryContinuation`
  test before the fix. A raw, dedicated thread sidesteps this entirely,
  regardless of `GTE_ENABLE_JOB_SYSTEM`.
- **`JobSystem::WaitForJobs()`'s `GTE_ENABLE_JOB_SYSTEM=OFF` branch is a
  real spin-wait (`while (!handle.IsComplete()) { std::this_thread::yield(); }`),
  NOT the historical `(void)handle;` no-op Phases 1-2 could get away with.**
  Before Phase 3, every `Schedule()`/`Dispatch()` call in the OFF
  configuration ran its job(s) synchronously to completion before
  returning, so a handle was always already complete by the time any
  caller could reach `WaitForJobs()` - nothing was ever left "in flight"
  for a caller on a different thread to wait for. `JobHandle::AddPendingUnit()`
  breaks that assumption: a handle can be marked incomplete well BEFORE the
  work that will eventually complete it is actually scheduled, and that
  work may run to completion on a genuinely different thread (e.g. one
  concurrently calling `Schedule()`/`ScheduleAlreadyPending()` for the
  handle's own dependency). Reproduced directly by this phase's own
  `JobContinuationTests.HandleStaysIncompleteUntilPendingDependencyClears`/
  `FanInWaitsForEveryDependencyBeforeRunning` tests hanging/failing under
  `GTE_ENABLE_JOB_SYSTEM=OFF` before this fix - fixed, and re-verified
  clean (including a 15-iteration `--gtest_repeat` stress run) under a
  full, separate MinGW/GCC `GTE_ENABLE_JOB_SYSTEM=OFF` configure+build+test
  run, alongside the default `GTE_ENABLE_JOB_SYSTEM=ON` configuration.
- **A test that needs to hold a dependency handle "genuinely pending" for a
  controlled duration must NEVER call `Schedule()` with a blocking/spin-
  waiting job directly from the test's own (main) thread - only from a
  dedicated `std::thread` it spawns for exactly that purpose** (see
  `tests/Jobs/JobContinuationTests.cpp`'s own `StartHeldDependency()`/
  `WaitUntilPending()` helpers and their header comment). Doing it directly
  from the main thread deadlocks immediately under
  `GTE_ENABLE_JOB_SYSTEM=OFF`, for the exact same reason described above -
  `Schedule()` would block that same thread forever waiting for a release
  flag only that thread could ever set. This is now the established
  pattern for any FUTURE `src/Jobs/` test that needs a genuinely
  long-pending dependency, in either build configuration.

- **Phase 4 (Thread-Safety Audit + Integration Point Whitelist - see
  `task_manager/job_system/JOBSYSTEM_PHASE4_THREAD_SAFETY_AUDIT_INTEGRATION_POINTS_v2.md`
  and `task_manager/job_system/JOB_SYSTEM_PHASE4_COMPLETION_REPORT.md`) is a
  deliberately documentation/verification-heavy phase, not a new API
  surface: it produces a definitive, written answer to "if a job body,
  running on a worker thread, tries to touch THIS engine subsystem, is that
  safe" for every existing shared/global/singleton piece of engine state,
  so Phase 6's real production migration (CPU vertex skinning) has a
  reviewed whitelist to build against instead of each future call site
  re-deriving its own answer by inspection.** The classification table
  below uses the same three buckets the strategy document defines:
  **NEVER** (a job body must never touch this at all, not even read-only,
  without dedicated new synchronization this campaign has not added);
  **READ-SAFE** (safe for a job body to READ, but only for as long as
  nothing - including the main thread itself - concurrently MUTATES the
  same data for the duration of the `Dispatch()`/`WaitForJobs()` bracket
  the job body runs inside); and **JOB-SAFE** (genuinely safe to call
  concurrently, from any number of threads at once, no external
  synchronization needed at all - either because it is pure logic with no
  shared mutable state, or because it was specifically built with its own
  internal synchronization for exactly this purpose, e.g. `JobSystem`
  itself).

  | Subsystem | Classification | Why |
  |---|---|---|
  | `gte::Jobs::JobSystem`/`detail::JobQueue`/`detail::JobHandleState` (`src/Jobs/`) | **JOB-SAFE** | The entire point of Phases 1-3 - `Schedule()`/`Dispatch()`/`ScheduleAfter()`/the queue's mutex+condition_variable/the handle's atomic pending-counter and mutex-guarded watcher list are all specifically built, and stress-tested (see this section's own lost-wakeup-race bullets above), to be called concurrently from many threads at once. A job body scheduling MORE work via `JobSystem::Instance().Schedule()`/`Dispatch()` from inside another job is safe by this same design (not exercised by a real call site yet, but nothing about the implementation assumes single-threaded access). |
  | `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()` (`<SDL3/SDL_timer.h>`, used by `src/Profiling/ScopeTimer.h`) | **JOB-SAFE** | This phase's own required verification item (see the strategy document's Step 3.2) - confirmed by a new, dedicated multi-threaded test, `tests/Jobs/JobSystemSdlClockThreadSafetyTests.cpp`: several threads released at the same instant via a shared start barrier all observe the exact same, non-zero `SDL_GetPerformanceFrequency()`, and each thread's own sequence of `SDL_GetPerformanceCounter()` reads stays strictly non-decreasing under concurrent load from every other thread. Consistent with SDL3's own documented contract - both are stateless queries against a platform-level monotonic counter/its fixed frequency, with no shared engine-owned mutable state involved in servicing the call. This is what lets Phase 5's planned `JobScopeTimer` safely call both functions from an arbitrary worker thread while the main thread's own `ScopeTimer` scopes call the identical functions concurrently. |
  | `Profiling::FrameProfiler` (`src/Profiling/FrameProfiler.h/.cpp`) | **NEVER, except `RecordWorkerJobSample()` specifically - JOB-SAFE** | `RecordCpuScope()`'s linear scan + non-atomic `++m_current.cpuScopeCount`, and `BeginFrame()`/`EndFrame()`'s ring-buffer bookkeeping, remain completely unsynchronized by design - a job body must still never call `GTE_PROFILE_SCOPE`/touch any OTHER `FrameProfiler::Instance()` method directly. As of Phase 5, `RecordWorkerJobSample()` is a genuinely separate, thread-safe write path (an atomic fetch-and-increment reservation into its own dedicated array, `m_captureEnabled` itself now `std::atomic<bool>`) - see this section's own dedicated Phase 5 bullets below for the full design - so `GTE_PROFILE_JOB_SCOPE` (`src/Profiling/JobScopeTimer.h`), which routes through it, is the one sanctioned way for a job body to record its own CPU scope. |
  | `GpuMemoryTracker`, `Renderer`/`Vulkan/*` (`VulkanInstance`/`VulkanDevice`/`VulkanSwapchain`/`VulkanAllocator`/`Buffer`/`RenderTexture`/`Pipeline`/`FramePresenter`/`FrameRecorder`/`GpuResourceFactory`), `GpuTimingService`/`VulkanQueryPool` | **NEVER** | `GpuMemoryTracker`'s own class comment already says "Not thread-safe" outright, and nothing under `Renderer`/`Vulkan/` was ever built with any synchronization in mind - every `VkCommandBuffer`/`VkQueue`/`VmaAllocator` call in this engine assumes single-threaded, main-thread-only access. A job body must never touch a live GPU resource, submit Vulkan work, or read/write `GpuMemoryTracker`'s tables directly - any future GPU-adjacent job (e.g. CPU vertex skinning writing into a `Mesh`'s host-visible buffer, Phase 6's actual target) resolves the destination pointer/buffer on the MAIN thread first and hands job bodies only a plain, disjoint output span to write into, never a `Mesh*`/`Renderer&`/Vulkan handle itself. |
  | `src/Renderer/RenderGraph/*` (`RenderGraph`, `RenderGraphBuilder`, `RenderGraphCompiler`, `RenderGraphResourcePool`, `RenderGraphBarrierPlanner`, `RenderGraphTimestampPool`) | **NEVER** | Every one of these either directly issues Vulkan calls, touches `GpuMemoryTracker`-tracked resources, or mutates shared, unsynchronized compiler/pool state (`RenderGraphResourcePool`'s frame-to-frame texture reuse bookkeeping) - the same GPU-resource-adjacent reasoning as the row above. No job in this campaign's current or planned scope has any reason to touch any of it, and none ever should without a fresh, dedicated audit of its own. |
  | `src/Editor/*` (the ImGui context, `EditorContext`, every `Panels/*Panel`) | **NEVER** | Dear ImGui's own context (`ImGuiContext`) is explicitly documented upstream as unsafe for concurrent access from multiple threads, and every Editor panel in this engine already only ever runs on the main thread inside `IEditorLayer::BuildUI()`/`Render()`. No job body has any legitimate reason to touch ImGui state directly - a future Editor "Jobs" panel (Phase 7) reads job/profiler DATA that a job body already finished writing (via `FrameProfiler`'s own thread-safe write path, Phase 5), never ImGui state from a worker thread. |
  | `Registry`/`EntityManager`/`ComponentStorage<T>` (`src/ECS/`) | **mutation: NEVER; read: READ-SAFE** | `EntityManager::Create()`/`Destroy()` and `ComponentStorage<T>::Add()`/`Remove()` mutate unsynchronized vectors/free-lists/generation counters with zero locking - a job body must never call any mutating `Registry`/ECS method. Reading a component's plain data fields (e.g. a `Transform`'s `position`) from a job body is READ-SAFE, but only under the same rule Phase 6's own design already assumes: the main thread must not mutate that SAME `Registry` for the entire duration of the `Dispatch()`/`WaitForJobs()` bracket a job body reading it runs inside - in practice, the safest and simplest pattern (and the one Phase 6 actually uses) is to never hand a `Registry&`/component reference into a job body at all, extracting whatever plain values are needed into a copy/span on the main thread first. |
  | `AssetDatabase` (`src/Assets/AssetDatabase.h/.cpp`) | **NEVER** (not yet proven safe for reads either) | Backed by a plain `std::vector`/two `std::unordered_map`s with zero internal synchronization - `RefreshFromDirectory()`/`ImportAsset()` are main-thread-only calls today, and nothing in this campaign's current or planned scope needs a job body to read from it. Unlike `Registry` above, this is classified NEVER outright rather than "read-safe with a caveat," since no real call site has ever needed to reason through the caveat for this specific class - a future job that genuinely needs read access must have that specific call site re-audited and documented here first, not merely assume the same reasoning transfers automatically. |
  | `ResourcePool<T, HandleT>` (`src/Renderer/ResourcePool.h`, the `MeshHandle`/`PipelineHandle` pools owned by `RenderSystem`) | **NEVER** | Zero internal locking, and resolving a handle returns a live `Mesh*`/`Pipeline*` - a GPU-adjacent pointer that compounds directly with the `Renderer`/`Vulkan` NEVER row above. A job body must never call `RenderSystem::TryGetMesh()`/`TryGetPipeline()` (or any future equivalent) itself; the resolved pointer/buffer must always be resolved on the main thread and handed to job bodies only as a plain, disjoint output span, mirroring Phase 6's own boundary design exactly. |
  | `src/Game/Instantiation/*` GPU catalogs (`PrimitiveGpuCatalog`, `MaterialTextureGpuCache`, `MeshAssetGpuCatalog`) | **NEVER** | GPU-resource-creating/caching code, unsynchronized, main-thread-only by construction today (called only from `MeshInstantiationSystem`, itself called only from `Game`'s own main-thread methods). A job body must never touch these directly - this is exactly what Phase 6's boundary design (a job body only ever sees plain CPU-side spans, never a `Mesh`/GPU handle) is built to guarantee structurally, not just by convention. |
  | Pure `src/Animation/*` modules (`BoneChainResolver`, `BonePoseMath`, `SkeletonPose`, `IkSolver`, `AppendBoneSolver`, `MotionSampler`, `AnimationPoseEvaluator`, `VertexSkinning`) | **JOB-SAFE** | Every one of these is pure logic operating only on its own parameters - no static/global/singleton mutable state anywhere in this module (see `AGENTS.md`'s own "Skeletal Animation Pose Resolution" section) - safe to call concurrently from any number of threads at once, PROVIDED each individual call's own inputs/outputs (e.g. one model's own `skinnedPositions`/`skinnedNormals` output vectors) are never shared/aliased across two concurrent calls. This is exactly the pure-function foundation Phase 6's planned `SkinVertices()` migration depends on. |
  | `src/Math/*` (`Vec2`/`Vec3`/`Vec4`/`Mat4`/`Quat`) | **JOB-SAFE** | Plain value types - every operation is a pure function of its own operands, no shared/static state of any kind. |
  | `MeshData`/`SkeletonData`/`MotionData`/`MaterialData` (`src/Assets/*Data.h`), READ-ONLY | **READ-SAFE** | Plain data structs with no internal synchronization, but populated exactly once (at import/cache-load time, main-thread-only) and never mutated again for the lifetime of the cache entry that owns them (see `SkeletalRigCache`/`AnimationClipCache`'s own "load once, cache, never mutate again" design) - concurrent read-only access from multiple job bodies is safe as long as nothing concurrently mutates the SAME instance, which nothing in this engine's current design ever does after initial load. |
  | `SkeletalRigCache`/`AnimationClipCache`/`ResolvedAnimationBindingCache` (`src/Game/Animation/*`) - the CACHE CONTAINERS themselves (`GetOrLoad()`/`Register()`/`TryGet()`) | **NEVER** for concurrent mutation; a resolved lookup's VALUE is **READ-SAFE** under a REQUIRED ordering rule | The containers are plain `std::unordered_map`s with zero locking - must only ever be called from the main thread, exactly as today. Once a lookup returns a pointer/reference to an already-cached value, reading that value from a job body is safe under the same rule as `MeshData` above, but with one REQUIRED addition specific to these three caches: the main thread must not call `GetOrLoad()`/`Register()` again on the SAME cache while a job holding an earlier lookup's pointer is still running, since an `unordered_map` insertion can invalidate previously-returned references - this is why Phase 6's planned design resolves every cache lookup up front, before any `Dispatch()` call, and never touches the cache again until `WaitForJobs()` returns. This is a REQUIRED correctness rule, not a performance convenience, and must never be relaxed by a future edit that "just wants one more lookup mid-flight." |
  | Cross-entity/cross-instance shared GPU mesh buffers (the documented `README.md` limitation: two entities spawned from the same `*.gta` file share one underlying `Mesh`, including its CPU-side cached bind-pose vertex arrays and output skinning buffers) | **NEVER concurrently - sequential-only, main-thread-orchestrated** | Not a "touch this subsystem" rule like the rows above, but a cross-cutting HAZARD this table must call out explicitly: today this sharing is safe only because `AnimationSystem::Update()` (`src/Game/Animation/AnimationSystem.cpp`) processes every live `SkeletalAnimator` strictly one at a time, on one thread - at any given instant, at most one animator is ever touching that shared memory. The moment more than one animator's own `Dispatch()`/`WaitForJobs()` sequence is allowed to be in flight AT THE SAME TIME, two worker threads could write the same shared buffer concurrently - a genuine data race, not merely today's harmless "last write wins" visual bug. See `JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md`, Step 3.6, for the permanent mitigating rule (each model's own `Dispatch()`+`WaitForJobs()` pair must complete in full before the next model's begins) this campaign commits to - this row exists so that rule is discoverable from this table directly, not only from Phase 6's own document. |

  This table is not exhaustive of every symbol in the engine, but every row
  above was chosen because a future job body (starting with Phase 6's real
  CPU vertex skinning migration) would plausibly be tempted to reach for it
  - a future phase that needs to classify something not listed here should
  add a new row rather than assume an unlisted subsystem is safe by
  omission.

- **Phase 5 (Profiler Integration - Worker Timeline - see
  `task_manager/job_system/JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md`
  and `task_manager/job_system/JOB_SYSTEM_PHASE5_COMPLETION_REPORT.md`)
  extends `src/Profiling/` so a worker thread's own scopes show up as real,
  attributed data, closing the Phase 4 table's own `Profiling::FrameProfiler`
  row ("NEVER (until Phase 5)") for exactly one new, narrow, genuinely
  thread-safe write path - every OTHER `FrameProfiler` method remains
  main-thread-only, unchanged.** `ProfilingTypes.h` gained `WorkerJobSample`
  (`workerIndex`/`name`/`milliseconds`/`startTicks`) and
  `kMaxWorkerJobSamplesPerFrame` (1024 - deliberately far larger than
  `kMaxCpuScopesPerFrame`, since this is a raw per-CALL log, never
  summed/deduplicated by name like `cpuScopes`), plus `FrameSample` gained
  `frameStartTicks` (the raw `SDL_GetPerformanceCounter()` reading
  `BeginFrame()` took to start that frame) and a
  `workerJobs`/`workerJobCount` array - all still plain, fixed-size, POD
  fields, so `FrameSample` itself remains trivially copyable into
  `FrameProfiler`'s existing ring buffer with zero design change there.
- **`FrameProfiler::RecordWorkerJobSample(workerIndex, name, milliseconds,
  startTicks)` is the ONE method on `FrameProfiler` safe to call
  CONCURRENTLY, from any number of Job System worker threads at once** -
  every other method (`BeginFrame()`/`EndFrame()`/`RecordCpuScope()`/
  `SetGpuPassTiming()`/`SetGpuPassDrawStats()`/`SetMemorySnapshot()`)
  remains main-thread-only, exactly as the Phase 4 table already documents.
  Implemented as a single atomic fetch-and-increment reservation
  (`m_currentWorkerJobCount`, a `std::atomic<std::size_t>` kept SEPARATE
  from `FrameSample::workerJobCount` itself, precisely because
  `std::atomic` is neither copyable nor assignable and could therefore
  never live INSIDE `FrameSample` without breaking its "plain, copyable
  POD" contract) - each caller gets its own, never-repeated index, so
  concurrent writes always land on DISJOINT array elements; no lock, no
  allocation, ever. `BeginFrame()` resets this counter to 0; `EndFrame()`
  snapshots it (clamped to `kMaxWorkerJobSamplesPerFrame`, mirroring
  `RecordCpuScope()`'s own overflow-drop behavior) into
  `m_current.workerJobCount` right before `m_current` is copied into
  history.
- **`FrameProfiler::m_captureEnabled` is now `std::atomic<bool>`, not a
  plain `bool` like every other member - this is a real, deliberate
  correctness fix, not a style change.** `RecordWorkerJobSample()` is the
  one place this flag is genuinely read from a worker thread, possibly at
  the EXACT same instant `SetCaptureEnabled()` is called from the main
  thread (e.g. a user toggling the Editor's "Capture" checkbox while jobs
  are in flight) - a plain `bool` read/written across threads with no
  synchronization is undefined behavior, not just "probably fine". Every
  other read of this flag (`BeginFrame()`/`EndFrame()`/`RecordCpuScope()`/
  etc.) remains main-thread-only and unaffected by this change.
  `m_frameInProgress`, by contrast, DELIBERATELY stays a plain `bool` -
  reading it from a worker thread is safe without atomics ONLY because of
  the Job System's own caller obligation that every `Dispatch()`/
  `WaitForJobs()` bracket completes in full before the frame it belongs to
  ends, which establishes a real happens-before edge from
  `BeginFrame()`/`EndFrame()`'s own writes through to a job body's read,
  via the Job System's internal mutex/condition-variable synchronization -
  do not "fix" this one the same way; it would just be redundant.
- **`gte::Jobs::JobSystem::WorkerIndexForCurrentThread()` (returns
  `std::optional<std::size_t>`) is what `Profiling::JobScopeTimer`
  (`src/Profiling/JobScopeTimer.h`) uses to attribute a recorded scope to
  the worker that ran it - a genuinely NEW public method on `JobSystem`,
  backed by a `thread_local std::optional<std::size_t>` set exactly once,
  at the very top of `WorkerLoop()`, for the real worker thread running
  it.** Returns `std::nullopt` for any thread that is NOT one of this
  pool's own real worker threads (the main thread, a Phase 3
  polling-fallback thread, ...) WHEN `GTE_ENABLE_JOB_SYSTEM` is `ON` - this
  is what actually enforces the "never call `GTE_PROFILE_JOB_SCOPE` from
  the main thread" rule below (a violation silently records nothing rather
  than crashing or fabricating a worker index). When
  `GTE_ENABLE_JOB_SYSTEM` is `OFF`, this instead ALWAYS returns `0` (never
  `std::nullopt`) - mirroring `WorkerCount()`'s own "always >= 1, never 0"
  contract, since there is no real worker-thread pool in that
  configuration to distinguish "the main thread" from "a job body running
  inline" in the first place (they are, by construction, the exact same
  thread) - this is a deliberate design choice so an `OFF` build still
  produces meaningful (if trivially single-row) worker-timeline data
  instead of permanently blank data, at the honest cost of this specific
  misuse-detection rule only being genuinely enforced when
  `GTE_ENABLE_JOB_SYSTEM` is `ON`.
- **`GTE_PROFILE_JOB_SCOPE("Name")` (`src/Profiling/JobScopeTimer.h`) is the
  per-job-body counterpart of `GTE_PROFILE_SCOPE` - the ONLY correct way to
  profile code running INSIDE a job body.** Mirrors `ScopeTimer`'s own
  two-layer on/off convention exactly (compiles to a true empty no-op when
  `GTE_ENABLE_PROFILER` is `OFF`; skips the clock read at runtime when
  `FrameProfiler::IsCaptureEnabled()` is `false`), plus the one additional
  runtime check described above (`WorkerIndexForCurrentThread()` must
  return a value). NEVER call `GTE_PROFILE_SCOPE` from inside a job body
  (it is completely unsynchronized - see the Phase 4 table's own
  `FrameProfiler` row), and NEVER call `GTE_PROFILE_JOB_SCOPE` from the
  main thread (see the previous bullet for exactly what happens if you do,
  and why that enforcement is `GTE_ENABLE_JOB_SYSTEM`-dependent).
- **`Profiling::BuildWorkerTimelinePoints()`/`ComputeDistinctWorkerCount()`
  (`src/Profiling/WorkerTimelineData.h/.cpp`) is the pure, always-compiled,
  ImGui-free "one frame's raw `WorkerJobSample` log -> a per-worker
  timeline" reshape - mirrors `FrameGraphData.h`'s own "always-compiled
  reshape" precedent exactly, so a future Phase 7 "Jobs" panel (and any
  future benchmark-mode consumer) reads through this ONE function rather
  than re-deriving the same reshape logic independently.** Each returned
  `WorkerTimelinePoint::startMilliseconds` is computed relative to
  `FrameSample::frameStartTicks` (never a raw absolute tick count a future
  caller would otherwise have to re-derive the frame's own start from) via
  `SDL_GetPerformanceFrequency()` - the same clock/units this whole module
  standardizes on. Never re-sorts `FrameSample::workerJobs` - preserves
  recording order exactly, and only ever reads the first
  `workerJobCount` entries, never anything beyond it (stale/leftover array
  slots past that count are never touched).
- **Phase 6 (First Production Consumer - Animation / Vertex Skinning - see
  `task_manager/job_system/JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md`
  and `task_manager/job_system/JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md`) is
  the campaign's own production cut-over: `AnimationSystem::Update()`
  (`src/Game/Animation/AnimationSystem.cpp`) is now the first, and only,
  real (non-test) call site anywhere in the engine that calls
  `gte::Jobs::Dispatch()`/`WaitForJobs()`.** For each currently-playing
  `SkeletalAnimator`, CPU vertex skinning (previously always a single,
  serial `Animation/VertexSkinning.h::SkinVertices()` call covering the
  WHOLE model) now branches on vertex count: below
  `kMinVerticesToParallelize` (512) it still runs inline, serially, via a
  direct `SkinVertexRange(0, vertexCount, ...)` call (scheduling a
  `Dispatch()` for a genuinely tiny model would cost more in scheduling
  overhead than it saves); at or above that threshold, it is split into
  batches (floored at `kMinVerticesPerBatch`, 256, so `Dispatch()` never
  splits smaller than that) and skinned via a real
  `Jobs::Dispatch(&RunSkinningBatch, ...)` + exactly one
  `JobSystem::Instance().WaitForJobs(skinningHandle)` call before that
  model's parts are re-uploaded to the GPU.
- **`Animation/VertexSkinning.h`'s `SkinVertexRange(beginIndex, endIndex,
  ...)` is the new, pure, always-compiled function both the serial and
  parallel skinning paths above are built on - `SkinVertices()` itself is
  now implemented purely in terms of `SkinVertexRange(0,
  bindPositions.size(), ...)`, so there is exactly ONE copy of the actual
  per-vertex blending logic, never two independently-maintained copies.**
  Unlike `SkinVertices()`, `SkinVertexRange()` NEVER resizes its
  `outPositions`/`outNormals` vectors - the caller must size them to the
  full vertex count BEFORE dispatching any batches, since two concurrent
  batches writing into two different `[begin, end)` slices of the same
  vectors must never race a third, hidden reallocation triggered by one of
  them calling `resize()`. `endIndex` is defensively clamped internally
  against the real vertex/output-vector sizes - never reads/writes out of
  bounds even if a caller ever passed a bad range. This refactor is
  behavior-preserving only: every pre-existing
  `tests/Animation/VertexSkinningTests.cpp` test passes unchanged against
  it, and a new `tests/Animation/VertexSkinningParityTests.cpp` (Tier 1,
  real `JobSystem::Instance()`, no live Renderer/GPU involved - see below)
  proves a large, synthetic model skinned via several CONCURRENT
  `Dispatch()` batches produces results IDENTICAL, vertex-for-vertex, to
  the original serial `SkinVertices()` call.
- **`AnimationSystem::Update()`'s per-batch job-body trampoline
  (`RunSkinningBatch()`, an anonymous-namespace function local to
  `AnimationSystem.cpp`) wraps its call to `SkinVertexRange()` in
  `GTE_PROFILE_JOB_SCOPE("SkinVertices")`** - the one sanctioned way (per
  this section's own Phase 5 bullets above) to profile code running inside
  a job body. The GPU upload step that follows (`Mesh::UpdateVertexData()`
  via `RenderSystem::TryGetMesh()`) is completely unchanged and stays
  main-thread-only, unconditionally - exactly matching the Phase 4
  thread-safety audit table's own `Renderer`/`Mesh` NEVER row; a job body
  never sees a `Mesh*`/GPU handle of any kind, only the plain
  `SkinningBatchContext` (five plain pointers into read-only input data
  plus this call's own output vectors).
- **`AnimationSystem::Update()`'s OUTER loop over every live
  `SkeletalAnimator` MUST remain strictly sequential - one animator at a
  time - and this is now enforced by an explicit, prominent code comment
  directly at that loop's own call site, not merely documented here or in
  the strategy document.** Two entities spawned from the SAME `*.gta` file
  share one underlying GPU `Mesh` (see `README.md`'s own documented
  limitation, cross-referenced by this section's own Phase 4 table row
  above) - today this sharing is safe ONLY because this loop processes one
  animator's entire per-model sequence (skinning dispatch + wait + every
  part's GPU upload) to full completion before the next animator's own
  sequence begins. Restructuring this loop to fire off every animator's
  own `Dispatch()` up front and wait on all of them together - a
  natural-looking next optimization once this phase exists - would let two
  different worker threads write the SAME shared GPU buffer at the SAME
  time: a genuine, unsynchronized data race, strictly worse than today's
  harmless "last write wins" visual bug. This rule may only be lifted once
  every spawned model instance owns its own private GPU mesh buffers - a
  separate, unstarted piece of engine work (see `README.md`/`TODO.md`).

## Render Target Format Matching

Vulkan pipelines are built against an exact color format
(`VkPipelineRenderingCreateInfo::pColorAttachmentFormats`, since this engine
uses dynamic rendering - no `VkRenderPass`/`VkFramebuffer`) - binding a
pipeline built for one format to a target that actually has a different
format is invalid per the spec, and can silently misrender or crash
depending on the driver instead of failing loudly. Follow these rules
whenever adding a real graphics pipeline or a new render target:

- **`Renderer::ColorFormat()`** (`src/Renderer/Renderer.h/.cpp`) is the
  single source of truth for "the" color format this engine renders with -
  whatever `VulkanSwapchain` actually negotiated at runtime (see
  `ChooseSurfaceFormat` in `VulkanSwapchain.cpp`), which can legitimately
  differ across GPUs/drivers. Never hardcode a `VkFormat` literal (e.g.
  `VK_FORMAT_B8G8R8A8_UNORM`) into a pipeline's
  `VkPipelineRenderingCreateInfo` or into a `RenderTexture` you expect to
  share a pipeline with the swapchain - read it from `Renderer::ColorFormat()`
  instead.
- **`Renderer::CreateRenderTexture()`'s `format` parameter defaults to
  `VK_FORMAT_UNDEFINED`**, meaning "match `ColorFormat()` exactly" (resolved
  internally in `Renderer.cpp`, not baked into the default argument as a
  literal) - this is what lets a single pipeline built once against
  `ColorFormat()` legally draw into either the swapchain or a default-format
  `RenderTexture` (e.g. the Editor's "Game" view). Only pass an explicit
  format when a target is deliberately different (e.g. a future HDR
  intermediate or a shadow map) - that target needs its own dedicated
  pipeline variant built for its exact format, never the default pipeline.
- **`FrameRecorder::RecordFrame()` asserts (debug builds only) that
  every target it's given has `target.format == ColorFormat()`.** This is
  the one recording path shared by `Present()` and `RenderOffscreen()`, so
  it's the natural place a future pipeline-bound draw call (recorded via
  `recordExtra`) runs - the assert exists to catch a format mismatch loudly,
  right there, instead of a confusing validation-layer warning (or silent
  misrendering on a driver that happens to tolerate it). A deliberately
  different-format target (see above) needs its own recording path rather
  than going through this assert unmodified - don't weaken or delete the
  assert to make a special case fit.
- **This same discipline applies to DEPTH, not just color.**
  `Renderer::DepthFormat()` (`VulkanDevice::PickDepthFormat()`, queried once
  from the physical device rather than hardcoded) is depth's equivalent of
  `Renderer::ColorFormat()` - every `Pipeline` is built with
  `VkPipelineRenderingCreateInfo::depthAttachmentFormat` set to it, and every
  render target (the swapchain's own per-image `DepthBuffer`s in
  `FramePresenter`, or a `RenderTexture`'s own companion `DepthBuffer` - see
  `src/Renderer/DepthBuffer.h`) is created at that exact same format.
  `FrameRecorder::RecordFrame()` asserts `target.depthFormat ==
  DepthFormat()` right alongside its existing color-format assert, for
  exactly the same reason. This was added specifically because the engine's
  original hardcoded triangle demo was always flat/coplanar (no real
  occlusion to get wrong), so a genuinely 3D, depth-tested render target
  (needed once the built-in primitive shapes - `Renderer/Primitives/
  PrimitiveMeshGenerator.h` - introduced real overlapping-in-screen-space
  geometry) never existed until now - don't reintroduce a render target or
  pipeline that skips a depth attachment/depth test, even for something that
  "looks flat," without a specific reason.

## Skeletal Animation Pose Resolution

Every per-frame MMD skeletal-animation pose evaluation lives under
`src/Animation/` (`BoneLocalOffset.h`, `MotionSampler.h/.cpp`,
`IkSolver.h/.cpp`, `AppendBoneSolver.h/.cpp`, `SkeletonPose.h/.cpp`,
`VertexSkinning.h/.cpp`, `AnimationPoseEvaluator.h/.cpp` - see `README.md`,
"Status", for the full history of how this runtime was built up). Follow
these rules whenever touching bone-hierarchy-walking code in this module:

- **Never hand-roll a new cycle-guarded bone-ancestor-chain walk - use
  `Animation/BoneChainResolver.h`'s `ResolveBoneChain()`/
  `ResolveSingleBoneChain()` instead.** `SkeletonPose.cpp`'s whole-skeleton
  world-matrix pass, `AppendBoneSolver.cpp`'s append/grant-source
  resolution, and `IkSolver.cpp`'s per-CCD-iteration single-bone world-
  matrix query all build on these two generic, Tier-1-tested primitives
  (`tests/Animation/BoneChainResolverTests.cpp`) rather than each
  maintaining its own copy of the same cycle-guard/memoization logic - three
  independent, subtly different hand-rolled versions of this exact pattern
  is what this code used to be, before it was pulled out into one place.
  Pick `ResolveBoneChain()` (memoized, one pass over the WHOLE skeleton)
  when every bone genuinely needs resolving and the underlying pose data
  won't change mid-walk; pick `ResolveSingleBoneChain()` (recomputed fresh,
  no caching across calls) only when it might - e.g. a CCD solve mutating
  the very pose being queried between successive single-bone lookups (see
  `IkSolver.cpp`'s own comment on why it can't use the memoized flavor).
- **The bind-relative local-transform formula lives in exactly one place:
  `Animation/BonePoseMath.h`'s `ComputeBoneLocalMatrix()`.** Both
  `SkeletonPose.cpp` and `IkSolver.cpp` compose a bone's world matrix as
  `parentWorld * ComputeBoneLocalMatrix(...)` - never reintroduce a second
  copy of `bone.position - parentBindPosition` plus `Mat4::TRS(...)`
  anywhere else; add a parameter to this one function instead if a future
  caller needs a variation on it.
- **The animation pipeline's per-frame execution ORDER (sample -> IK ->
  append -> forward-kinematics) has exactly one home:
  `Animation/AnimationPoseEvaluator.h`'s `EvaluateAnimatedSkinningPose()`.**
  This order is correctness-critical (an append source that's also an IK
  link must already carry its IK-solved rotation - see
  `Animation/AppendBoneSolver.h`'s own file comment) and used to be
  reproduced by hand inside `Game::UpdateSkeletalAnimators()`
  (`src/Game/Game.cpp`) - the only call site at the time. Any new call site
  that needs a fully-resolved animated pose (e.g. the Bone Viewer's planned
  live-pose overlay - see `TODO.md`) must call this one function rather than
  re-inlining the same four-call sequence; if the pipeline itself ever needs
  a new stage (e.g. future morph blending), add it here, once, not at every
  caller. Covered by a genuine ordering-regression test
  (`tests/Animation/AnimationPoseEvaluatorTests.cpp`'s
  `AppendedBoneInheritsIkSolvedRotationNotRawBindPose`) that fails if a
  future edit ever swaps IK solving and append inheritance.

## GPU Vertex Skinning

An eight-phase campaign (`task_manager/gpu_skinning/`,
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md`) gave the engine a SECOND,
GPU-resident implementation of vertex skinning — a compute-shader mirror of
`Animation/VertexSkinning.h`'s CPU path (`SkinVertexRange()`), switchable at
runtime, specifically so the performance difference between the two tech
stacks can be observed (see `AnimationSystem::SkinningMode`,
`src/Game/Animation/AnimationSystem.h`, and the Editor "Jobs" panel's own
"Skinning Mode" toggle — `Panels/JobsPanel.cpp`, Phase 7). Follow these
rules whenever touching this feature:

- **The CPU path (`Animation/VertexSkinning.cpp`'s `SkinVertexRange()`) is
  the permanent ORACLE and must never be modified to "agree" with the GPU
  kernel.** Every GPU-side buffer layout (`src/Renderer/GpuSkinning/
  GpuSkinningTypes.h`) and both `.comp` kernels
  (`src/Shaders/SkinVerticesPositionNormal(Uv).comp`) were written by
  reading the CPU code line-by-line and mirroring its exact
  accumulate-then-normalize-once, no-valid-influence-falls-back-to-bind-pose
  behavior — see `GPU_SKINNING_PHASE2_COMPLETION_REPORT.md` for the exact
  differences found between an early illustrative sketch and the real CPU
  source. If the two paths ever disagree, the CPU path is right by
  definition and the GPU kernel is the one that needs fixing — never the
  reverse.
- **The graphics pass reading a GPU-skinned model's vertex buffer declares a
  "phantom" `ResourceAccess::VertexBufferRead`** even though it never
  actually reads that buffer through the render graph's own resolution
  machinery (it reads it via a real `vkCmdBindVertexBuffers` binding,
  outside `PassContext::resolveBuffer` entirely) — this exists PURELY to
  force the render graph's compiler/barrier planner to order the draw pass
  strictly after whichever compute pass wrote it this frame. Do not "clean
  this up" as dead code — see `RenderGraphTypes.h`'s own `ResourceAccess`
  doc comment and `GPU_SKINNING_PHASE3_COMPLETION_REPORT.md` for the
  write-after-write hazard this closes.
- **`GpuSkinningRigCache`'s `ComputeDescriptorSet::Rewrite()` is called
  EXACTLY ONCE per model, at registration time, never every frame** — a
  deliberate exception to `ComputeDescriptorSet`'s general "safe, and
  expected, to call every frame" convention, justified because every one of
  a GPU-skinned model's four buffers (bind pose, skin weights, bone
  matrices, output) has a permanently stable identity for the model's whole
  lifetime. Do not "fix" this into a per-frame rewrite.
- **A GPU-skinned model intentionally owns TWO separate `Mesh` objects for
  its whole lifetime — one CPU-mode, one GPU-mode — never a single Mesh
  that gets mutated in place when the mode switches.** This doubles that
  model's GPU memory footprint for as long as it exists, which is a
  deliberate, accepted trade (see `GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md`,
  Step 3.5) for keeping `MeshRenderer`/`RenderSystem` completely unaware
  skinning mode exists at all — do not attempt to "optimize this away" by
  sharing one Mesh between the two modes.
- **`AnimationSystem::Update()`'s outer per-`SkeletalAnimator` loop must
  remain STRICTLY SEQUENTIAL regardless of skinning mode** — this rule
  predates GPU skinning (see the Job System section above) but applies
  doubly here: two entities sharing one model's GPU output buffer, if their
  own `Dispatch()`/GPU-mode work were ever allowed to run concurrently
  instead of one-at-a-time, would be a genuine, unsynchronized GPU
  write-after-write race, not merely the CPU path's existing harmless
  "last write wins" visual limitation.
- **The Editor's CPU/GPU skinning-mode toggle lives in the "Jobs" panel,
  not "Render Graph"** (see `Panels/JobsPanel.cpp`'s own
  `BuildSkinningModeControl()`) — a deliberate placement decision (Phase 7,
  Step 3.1): a user watching this panel's own worker timeline is exactly
  who benefits from seeing, right next to it, the control that makes
  "SkinVertices" entries appear/disappear from it. Neither this panel nor
  "Render Graph" gained a new "N/A"/fabricated-value state for the mode
  that ISN'T currently active — the absence of a row/segment already IS
  the honest signal (see "Profiling" above) — the toggle's own tooltip
  (`JobsPanelData.h`'s `SkinningModeCrossReferenceHint()`) is what tells a
  user where to look instead, never a new profiling code path.

## Entity-Component-System (ECS)

The engine's Scene/World data model lives under `src/ECS/`: `Entity`
(`src/ECS/Entity.h`), `EntityManager` (`src/ECS/EntityManager.h/.cpp`),
`ComponentStorage<T>` (`src/ECS/ComponentStorage.h`), and `Registry`
(`src/ECS/Registry.h`), which owns one of each. This was deliberately rolled
by hand (not via a third-party library like EnTT) so the engine keeps
ownership of its core gameplay data model, the same way its math library
(`src/Math/`) was written from scratch rather than depending on GLM (see
`MathTypes.h`). Follow these rules whenever touching entity/component
lifetime code:

- **Identify entities by handle, never by pointer or string.** `Entity` is a
  cheap 8-byte POD (index + generation), generated automatically by
  `EntityManager::Create()` - calling code never invents/assigns its own id.
  This is the exact same shape and rationale as `GpuResourceHandle` (see
  "GPU Resource Memory Tracking" above): cheap to copy/store/compare by the
  thousands, and the `generation` field guards against a stale `Entity`
  silently referring to a different entity that was later created in the
  same (reused) slot - `EntityManager::Create()`/`Destroy()` use the exact
  same slot + free-list + generation-bump pattern as
  `GpuMemoryTracker::Track()`/`Untrack()`, on purpose, so there is only one
  such pattern in the codebase to understand, not two subtly different ones.
- **Components are plain data, never GPU/SDL-resource-owning types, and
  never carry virtual behavior of their own.** `Transform`
  (`src/ECS/Components/Transform.h`) is the pattern to copy: fields only,
  plus at most small pure-math helper methods (`LocalToWorldMatrix()`). A
  component that needs a live GPU resource - `MeshRenderer`
  (`src/ECS/Components/MeshRenderer.h`) is the first one - must reference it
  by handle/value data (`MeshHandle`/`PipelineHandle`,
  `src/Renderer/MeshHandle.h`/`PipelineHandle.h`), never by embedding a
  `Buffer`/`RenderTexture`/`Mesh`/`Pipeline`/raw Vulkan handle directly - the
  RAII-owning object stays behind a `ResourcePool<T, HandleT>`
  (`src/Renderer/ResourcePool.h`, owned by `RenderSystem` - see below),
  exactly as GPU resources are already addressed by `GpuResourceHandle`
  rather than a raw pointer.
- **A component that references ANOTHER entity (e.g. `Transform::parent`)
  stays plain data too - the logic that actually WALKS that reference lives
  in a separate free-function module, never on the component itself.**
  `Transform::parent` (an `Entity`, `kInvalidEntity` by default) plus
  `Transform::siblingIndex` are exactly this: plain fields, no different in
  kind from `position`/`rotation`/`scale` above. Resolving a full WORLD
  transform by walking the parent chain, cycle-safe reparenting, and sibling
  reordering all live in `src/ECS/TransformHierarchy.h/.cpp` instead
  (`ComputeWorldMatrix()`/`ComputeWorldTransform()`, `SetParent()`,
  `GetChildren()`/`SetSiblingIndex()`) - free functions that take a
  `Registry&` plus plain `Entity` values, same shape as `RenderSystem`'s own
  ECS-bridging functions below, just bridging ECS-to-ECS instead of
  ECS-to-Renderer. This keeps `Transform` itself trivially copyable/
  Tier-1-testable-by-construction while still allowing genuinely non-trivial
  hierarchy logic (cycle detection, world-position-preserving reparenting) to
  exist somewhere sensible - never add a Registry-dependent method to
  `Transform` (or any other component) directly, follow this same
  free-function pattern instead.
- **`ComponentStorage<T>` is a sparse set, addressed by `Entity::index`
  directly - never a hash lookup.** Adding/removing/querying a component is
  O(1) array indexing (`m_sparse`/`m_dense`), and `Remove()` uses
  swap-with-last to keep the dense array packed for cache-friendly
  iteration - dense iteration order is therefore NOT stable across a
  `Remove()` call, never rely on it. `Registry` picks each component type's
  numeric id via a per-type function-local static counter
  (`detail::ComponentTypeId<T>()`), not `std::type_index`/RTTI, so
  `Registry::Storage<T>()`/`AddComponent<T>()`/etc. stay a plain array
  lookup rather than a hash on every call - the same "no hashing on the hot
  path" philosophy as `GpuMemoryTracker`'s handle-indexed slot array.
- **`Registry::DestroyEntity()` must remove the entity from EVERY pool it
  has ever touched, not just the ones a caller happens to think of.** This
  is why `Registry` keeps a homogeneous `std::vector<std::unique_ptr<IComponentPool>>`
  and calls `IComponentPool::Remove()` (the type-erased virtual, not the
  typed `ComponentStorage<T>::Remove()`) on every pool before destroying the
  entity itself - an entity is never left with a dangling/orphaned component
  in some pool this forgot about. Any new component-holding structure added
  later must go through this same `IComponentPool` path, not invent a
  separate destroy-time cleanup step.
- **A `Registry`/`EntityManager`/`ComponentStorage<T>` is Tier-1-testable by
  construction, and must stay that way.** None of them touch a live
  `VkDevice`/`VmaAllocator`/SDL window - see `tests/ECS/` (`EntityManagerTests.cpp`,
  `ComponentStorageTests.cpp`, `RegistryTests.cpp`) for the pattern to copy
  when adding a new component type or Registry method: hand-built `Entity`
  values and plain component structs are enough, following the same
  Tier-1-testability rule already established below.
- **Only `RenderSystem` (`src/Game/RenderSystem.h/.cpp`), `MeshInstantiationSystem`
  (`src/Game/Instantiation/MeshInstantiationSystem.h/.cpp`), and `AnimationSystem`
  (`src/Game/Animation/AnimationSystem.h/.cpp`) are allowed to depend on both the ECS
  world (`Registry`/`Transform`/`MeshRenderer`) AND `Renderer`/`Mesh`/
  `Pipeline` - the same "only one layer crosses this boundary" rule this
  file already applies to SDL (see "Coding Guidelines", Clean Architecture:
  only `Application` touches SDL directly). This is not three unrelated
  exceptions: `Game` itself already crossed this boundary directly before
  its own instantiation/animation logic was extracted out of `Game.cpp`
  (see `GameInstantiationRefactorProposal.txt`) - `MeshInstantiationSystem`
  (spawning primitives/imported meshes, built on `PrimitiveGpuCatalog`/
  `MeshAssetGpuCatalog`/`EntityInstantiator`) and `AnimationSystem` (playing
  back skeletal animation, built on `SkeletalRigCache`/`AnimationClipCache`/
  `ResolvedAnimationBindingCache`) are `Game`'s own legitimate dual
  dependency decomposed into two named, focused sub-systems it owns, not a
  new architectural violation. `Renderer` itself must never gain a
  dependency on ECS in either direction - `Renderer::Submit()` takes plain
  `Mat4`s, never an `Entity`/`Registry`.
  `RenderSystem::CollectRenderables(Registry&)` is the pure ECS -> plain-data
  (`DrawCommand`: `MeshHandle`/`PipelineHandle`/`Mat4`, no live Mesh/Pipeline/
  Renderer involved) step - keep it that way when extending it, and put any
  new Renderer-touching logic in `RenderSystem::Draw()` (or a sibling
  non-pure method) instead, so `CollectRenderables()` stays Tier-1-testable
  (see `tests/Game/RenderSystemTests.cpp`). The same "keep the pure part
  pure" discipline applies to the other two: `EntityInstantiator`/
  `MeshVertexPacking`/`MeshMaterialPartitioner` (used by
  `MeshInstantiationSystem`) and the three animation caches above (used by
  `AnimationSystem`) all need nothing but plain data/a `Registry` and are
  Tier-1-tested under `tests/Game/`, while the GPU-touching catalogs
  (`PrimitiveGpuCatalog`/`MaterialTextureGpuCache`/`MeshAssetGpuCatalog`)
  fall into the same "Tier 2, no automated coverage yet" bucket as
  `RenderSystem::Draw()` itself (see "Testability & Regression Safety"
  below).
- **`Camera` (`src/ECS/Components/Camera.h`) never bakes an aspect ratio
  into itself.** `ProjectionMatrix(aspectWidthOverHeight)` always takes the
  aspect ratio as a parameter, resolved fresh by whoever is about to draw
  (`RenderSystem::ResolveActiveCameraViewProjection(Registry&,
  aspectWidthOverHeight)`), because the SAME `Camera` entity can legitimately
  render into multiple differently-sized/shaped targets in the same frame
  (the Editor's "Game" and "Scene" panels, each with their own
  `RenderTexture` - see "Editor Module Structure" below). Never cache a
  `Camera`'s resolved projection matrix keyed only by the component itself -
  always re-resolve it per render target/aspect ratio. `ViewMatrix()` is
  built from a plain `Transform` (via `Mat4::LookAtLH`, looking down its
  rotated `Vec3::Forward()`) rather than a bespoke eye/target/up API, so a
  camera entity is edited exactly like any other entity (Transform in the
  Inspector) - don't add a separate eye/target/up field set to `Camera`
  itself. `RenderSystem::ResolveActiveCameraViewProjection()` picks the
  FIRST entity (in `ComponentStorage<Camera>` order) with `active == true`
  and falls back to `Mat4::Identity()` if none exists - this is what
  preserves the engine's original "vertices already authored directly in
  clip space" behavior for a scene that hasn't added a `Camera` yet; don't
  change this fallback without checking `Shaders/Triangle.vert`'s
  `pc.viewProj * pc.model * ...` still makes sense for it. `Pipeline`'s one
  push constant range now carries a `model` `Mat4` immediately followed by a
  `viewProj` `Mat4` (128 bytes total - the guaranteed minimum
  `maxPushConstantsSize` on every conformant Vulkan implementation, see
  "Render Target Format Matching" above for the same "match the GPU side
  exactly" philosophy applied here) - grow this only by moving to a uniform
  buffer/descriptor set instead of growing the push constant range further,
  since 128 bytes is the only size guaranteed to fit everywhere without a
  per-GPU limit check.

## Editor Module Structure

`src/Editor/` is the Editor/Debug UI seam described in "Coding Guidelines"
(Clean Architecture) - the same boundary role `EventTranslator` plays for
SDL in the Application layer, but for ImGui. The boundary is the **folder,
compiled only under `GTE_ENABLE_EDITOR`** - not a single file. Only
`EditorLayer.h` (the pure `IEditorLayer` interface) and
`NullEditorLayer.cpp` (the release-build no-op implementation) must stay
completely free of ImGui/SDL/Vulkan-beyond-forward-declares; every other
file under `src/Editor/` is compiled exclusively when `GTE_ENABLE_EDITOR` is
ON (see `CMakeLists.txt`'s `target_sources(gte_core PRIVATE ...)` inside
that `if()` block) and is just as free to include ImGui/SDL headers
directly as `ImGuiEditorLayer.cpp` itself:

- **`ImGuiEditorLayer.cpp`** is the Editor's composition root, not a
  monolith holding every panel: it owns the ImGui context, the SDL3/Vulkan
  backend lifecycle, TWO `RenderTexture`s (`m_gameView`/`m_sceneView` - one
  per panel, never shared), and the shared `EditorContext` (below) -
  `BuildUI()` just calls out, in a fixed, deliberate order, to
  `DockLayout.cpp` and each `Panels/*.cpp` builder.
- **`EditorContext.h`** is a small plain-data struct (no behavior of its
  own, same philosophy as ECS components - see "Entity-Component-System"
  below) holding everything that needs to be shared across panels/frames:
  the Game-view/Scene-view ImGui descriptors, each panel's own desired
  render-texture extent (`desiredExtent`/`desiredSceneExtent`) and visibility
  flag (`gameViewVisible`/`sceneViewVisible`), the current Hierarchy/
  Inspector selection (`EditorContext::selection`, see `Selection.h` below),
  the exit-requested flag, and the dock-layout-ensured latch. Passed by
  reference into every panel/dock-layout function.
- **`Selection.h/.cpp`** is the single gate-keeper for every Hierarchy-entity
  / Project-asset selection change - `HierarchyPanel`/`ProjectPanel` never
  assign `EditorContext::selection`'s fields directly; they only ever call
  `Selection::SelectEntity()`/`SelectAsset()`/`ClearAssetIfPath()`, and every
  reader (`InspectorPanel`, `ScenePanel`'s gizmo) goes through its
  `Kind()`/`SelectedEntity()`/`SelectedAssetAbsolutePath()`/etc. accessors
  rather than reading a raw field. Deliberately pure logic with zero ImGui/
  SDL/Vulkan dependency (Tier-1-testable - see `tests/Editor/
  SelectionTests.cpp` - and "Testability & Regression Safety" below), and
  deliberately just a plain gate-keeper with no history/undo of its own -
  this is what gives a future Command-pattern implementation (undo-able
  selection changes, then edits in general) exactly one choke point to route
  through, instead of several panels each writing selection state directly
  (see `TODO.md`, "Editor / Debug UI"). Any future selectable "thing" (e.g. a
  multi-select set) should extend this same class rather than adding a new
  ad hoc field to `EditorContext` directly.
- **`gameViewVisible`/`sceneViewVisible` are written from `ImGui::Begin()`'s
  own return value** (`Panels/GamePanel.cpp`/`ScenePanel.cpp`) - `false`
  whenever that panel is an inactive/hidden dock tab (or collapsed), not
  just "exists somewhere" - and read by
  `ImGuiEditorLayer::GameViewTarget()`/`SceneViewTarget()` at the START of
  the NEXT frame to return `nullptr` outright for a currently-invisible
  panel, which is what makes `Application::Run()` skip that view's
  `Renderer::RenderOffscreen()` pass entirely (real GPU savings, not just a
  cosmetic skip) whenever "Scene"/"Game" are tabbed together and only one is
  actually on screen. A future panel with its own `RenderTexture` should
  follow this exact same pattern rather than always rendering unconditionally.
- **`DockLayout.h/.cpp`** builds the top menu bar + full-viewport DockSpace
  and the one-shot default Unity-style layout (Hierarchy left, Inspector
  right, Scene/Game tabbed center) - see its own comments for why rebuilding
  that layout must stay strictly one-shot, never re-checked every frame,
  or the user could never drag a panel loose from the default arrangement.
- **`Panels/HierarchyPanel.*`, `InspectorPanel.*`, `ScenePanel.*`,
  `GamePanel.*`, `MemoryPanel.*`** are each a single free function (`BuildXPanel(...)`)
  taking `EditorContext&` (plus `Registry&` where a panel needs the ECS
  world) - not classes, and NOT implementations of any common
  `IEditorPanel` interface. There is deliberately no polymorphic
  panel list/registry here: the dock layout above already addresses each
  panel by its literal, hardcoded name, so nothing ever needs to iterate
  over "the panels" generically - `ImGuiEditorLayer::BuildUI()` calls each
  one explicitly, by name, in a fixed order. Don't introduce an
  `IEditorPanel` abstraction preemptively; only reach for one if a genuine,
  stated requirement for runtime-registered/plugin panels shows up later.
- A **future panel that genuinely needs its own persistent state across
  frames** (e.g. a Console's scrollback buffer) may become a small class
  instead of a free function - it still gets called explicitly by name from
  `ImGuiEditorLayer::BuildUI()`, exactly like the stateless ones, with no
  interface needed for it either. Two real precedents already exist:
  `BoneViewerWindow` (`BoneViewerWindow.h` - a floating debug window with
  its own GPU buffers/camera/selection state) and `ProfilerPanel`
  (`Panels/ProfilerPanel.h` - Phase 7,
  `PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md`, holding its Pause control's
  frozen snapshot plus reusable `ImGui::PlotLines()` scratch buffers). Both
  are still called explicitly by name (`m_boneViewer.Build(...)`/
  `m_profilerPanel.Build(...)`), never through a shared interface.
- **Vulkan types (e.g. `EditorContext::gameViewDescriptor`,
  `VkExtent2D`) are fine to use directly anywhere in this folder** - this is
  not an architectural leak. `Renderer`'s own public API
  (`Renderer::GetVulkanContextInfo()`, `RenderTexture::Extent()`/`View()`/
  `Sampler()`) already hands out plain Vulkan handles on purpose, precisely
  so "an external Vulkan-based rendering backend... owned by the Editor
  module" (see `Renderer.h`) - i.e. Dear ImGui's own Vulkan backend - can
  use them directly; there is exactly one rendering backend in this engine
  and no plan to swap it, so wrapping these handles in a fake neutral type
  would add indirection with no real decoupling benefit. The boundary that
  actually matters and must stay intact is that `Renderer`'s *internal*
  RAII wrapper types (`VulkanInstance`, `VulkanDevice`, `VulkanSwapchain`,
  `VulkanAllocator`, `FramePresenter`, `FrameRecorder`, `GpuResourceFactory`
  - everything under `Renderer/Vulkan/` plus Renderer's private
  collaborators) never leak outside `Renderer`, and that `Game`/ECS never
  see a Vulkan type in either direction (see `RenderSystem`'s rule below).
- **`IEditorLayer::WantsCaptureMouse()`/`WantsCaptureKeyboard()` gate every
  translated mouse/keyboard `Event` before it ever reaches
  `InputState`/`Game::OnEvent()`.** `Application::Run()` checks these
  (backed by `ImGuiIO::WantCaptureMouse`/`WantCaptureKeyboard` in
  `ImGuiEditorLayer`, always `false` in `NullEditorLayer`) so
  clicking/dragging/typing into the Editor's own ImGui panels never ALSO
  registers as gameplay input underneath them - the classic
  ImGui-in-a-game-engine "click-through" problem. This is deliberately NOT
  solved with a separate Editor-side event broadcaster/receiver system:
  Dear ImGui already does all the hard part itself (per-frame, internal
  topmost-window-wins hit-testing/focus/modal-exclusivity across every one
  of its own panels, via the one `ImGuiContext` `ProcessEvent()`/
  `NewFrame()` already feed) - there is nothing left for engine code to
  arbitrate between ImGui's own panels. The only genuinely missing piece
  was ImGui-vs-gameplay leakage, and two `bool` query methods (mirroring
  the existing `WantsExit()` pattern) are enough to close that gap; don't
  reintroduce a broadcaster/registry to solve a problem ImGui already owns.
  `Quit`/`WindowResized` events bypass this check entirely (see
  `Application::Run()`) - they aren't gameplay input in this sense, and
  Renderer/the Editor's own resize handling must always see them regardless
  of what the Editor UI currently wants captured.

## Testability & Regression Safety

- **Design new logic to be Tier-1-testable whenever the underlying problem
  allows it.** Follow the split already established in `tests/CMakeLists.txt`:
  "Tier 1" code is pure logic that operates on plain data/enums/structs and
  needs no live `VkDevice`/`VmaAllocator`/`VkSurfaceKHR`/SDL window - see
  `EventTranslator` (takes a plain `SDL_Event` struct), `InputState` (takes
  plain `gte::Event` values), and `GpuMemoryTracker` (takes plain enums + a
  byte count, never a real `VmaAllocation`) for the pattern to copy. Before
  wiring new logic directly into a GPU/SDL-owning class, ask whether it can
  instead be extracted as a small pure function/class that takes
  already-resolved plain values - if it can, do that, then add its test under
  `tests/<Layer>/` (mirroring the folder it lives in under `src/`), not "if
  there's time".
- **Every change to Tier 1 code must come with a matching test change.**
  Adding a new branch/case to `EventTranslator`, `InputState`,
  `GpuMemoryTracker`, `GpuResourceHandle`, `Vertex`, or any future Tier 1
  module must add or update the corresponding file in `tests/` in the same
  change - never leave a new code path with zero coverage in a module that
  already has a test file. Fixing a bug in one of these files must add a
  regression test that fails before the fix and passes after it, not just a
  code change.
- **Run the actual test suite before considering any change to `gte_core`
  done - a successful build is not enough.** Build `GreatTamanaEngineTests`
  and run it (e.g. `ctest` from the build directory, or the built `.exe`
  directly) after any change under `src/` - a change can compile cleanly
  while still silently breaking `InputState`'s held/pressed/released
  semantics, `EventTranslator`'s mappings, or `GpuMemoryTracker`'s
  bookkeeping. Treat any newly-failing test as a real regression to fix, not
  something to work around by loosening the test's expectation without
  understanding why it failed.
- **GPU-dependent ("Tier 2") code - `Buffer`, `RenderTexture`, `Pipeline`,
  `GpuResourceFactory`, `FramePresenter`, `FrameRecorder`, everything under
  `Renderer/Vulkan/` - has no automated test coverage yet** (see the "Tier 2"
  note in `tests/CMakeLists.txt`). A headless-surface `GpuTestFixture`
  (`VK_EXT_headless_surface`) is noted there as a possible future addition,
  but it is a backlog/TODO item only, NOT a prerequisite or gate for
  anything else - the current development machine doesn't support headless
  mode anyway, so this must never be treated as a blocker for adding new
  features or landing changes under `Renderer/Vulkan/` or elsewhere. When
  it's convenient, build and run against a real GPU/window as a sanity
  check for changes here, and extracting more logic into Tier-1-testable
  pure functions (per the point above) is always welcome - but the absence
  of automated Tier 2 coverage should never itself slow down or stop
  feature work.

This document will be extended as more conventions are established.
