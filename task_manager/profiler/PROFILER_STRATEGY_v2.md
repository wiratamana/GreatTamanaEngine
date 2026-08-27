# GreatTamanaEngine — Profiler Grand Strategy (v2)

Status: PROPOSAL / PLANNING DOCUMENT — no implementation yet.
Scope: how to design and roll out an in-engine profiler covering:
  1. CPU scope timers
  2. Vulkan GPU timestamp queries
  3. Frame time graphs
  4. Per-system timings
  5. Draw-call and triangle counts
  6. GPU memory usage over time
  7. A benchmark mode that runs without the Editor

This document is deliberately code-free. It exists to answer "what do we
build, in what order, why that order, and what do we deliberately refuse to
build" — before a single line of implementation is written. It is written in
the same spirit as `TODO.md`/`AGENTS.md` already in this repo: a living
planning document, not a spec frozen in stone. Whoever picks this up (human
or AI agent) should update it as reality diverges from the plan, exactly like
this codebase already treats `TODO.md`.

--------------------------------------------------------------------------
## Changelog: v1 -> v2 (read this first)
--------------------------------------------------------------------------

This is a second-pass review of `PROFILER_STRATEGY.md` (v1). v1's overall
shape, phase ordering, and "reuse the Memory panel's proven pattern"
philosophy are sound and are kept unchanged below. This revision's job was
to find (a) missing context v1 didn't account for, and (b) places where v1
proposed something workable but not the best available option — then fold
concrete fixes back into the plan, in place, rather than bolting a separate
"issues" list onto the end. Every change below is folded into the relevant
Step/Phase section; this list is only a map of *where* to look and *why*.

1. **v1's own success criteria promised something v1's phases never actually
   built.** Step 1.3 says "turning the whole profiler off... measurably
   returns to today's baseline," but none of the eight phases in Step 3
   designed an actual enable/disable switch (compile-time OR runtime) for
   the CPU-timer instrumentation itself — only the Editor *panel* was ever
   gated. Fixed by adding an explicit "Phase 0b: the on/off switch" design
   (folded into Phase 0, see below) plus a concrete numeric overhead budget
   in Step 1.3, so "negligible"/"small and bounded" are no longer
   unverifiable adjectives.
2. **v1 never named a specific clock, and never addressed the profiler's
   own overhead on the very numbers it collects (the observer effect).**
   Fixed with a new "Cross-Cutting Concerns" step (new Step 3a) covering
   clock choice and a hard "no heap allocation in the per-frame hot path"
   rule for the instrumentation itself.
3. **v1's Phase 4 (GPU timestamp queries) was written without first
   confirming what Vulkan version/features this engine's `VulkanDevice`
   actually already requires.** Having now actually read
   `src/Renderer/Vulkan/VulkanInstance.cpp`/`VulkanDevice.cpp`, this is
   good news, not a gap to route around: the engine already requests
   `VK_API_VERSION_1_3` and already enables
   `VkPhysicalDeviceVulkan13Features::synchronization2` unconditionally
   (alongside `dynamicRendering`) in `CreateLogicalDevice()`. That means
   `vkCmdWriteTimestamp2`/`VkPipelineStageFlagBits2` — the modern,
   sync2-flavored timestamp API — are guaranteed available on every device
   this engine can already run on today; there is no separate
   extension/version negotiation to add for Phase 4, and no reason to fall
   back to the legacy `vkCmdWriteTimestamp`/`VkPipelineStageFlags` pair.
   This is now stated explicitly in Phase 4 instead of left as an
   unstated assumption — the kind of "read the actual code before
   proposing" diligence v1's own Step 2 opening paragraph claims for
   itself, now actually backed by a source read.
4. **v1's Phase 4 never mentioned query pool reset, or how pool
   lifetime/sizing differs between the synchronous offscreen path and the
   double-buffered swapchain path** — both real, easy-to-get-wrong Vulkan
   details for whoever implements this. Fixed with an expanded, more
   concrete Phase 4 subsection.
5. **v1 never addressed what a GPU pass sample means on a frame where that
   pass didn't run at all** (e.g. "Scene" tab hidden behind "Game" — see
   `IEditorLayer::GameViewTarget()`/`SceneViewTarget()` returning
   `nullptr` and skipping `RenderOffscreen()` entirely, per `README.md`'s
   own "Visibility-driven rendering" section). v1's graph/history design
   (Phase 2) had no concept of "no data this frame" distinct from "0 ms
   this frame" — the two mean very different things and must not be
   conflated in a graph. Fixed in Phase 2 and Phase 7.
6. **v1's benchmark mode (Phase 6) had no warm-up-frame discard.** The
   first few frames of any run pay one-time costs (shader/pipeline
   creation, first-touch page faults, swapchain image transitions) that
   would skew min/max/percentile statistics on an otherwise steady-state
   workload. Fixed by adding an explicit warm-up window to Phase 6.
7. **v1 never called out that a new, always-compiled engine module gets a
   documented convention section in `AGENTS.md`, the way every other
   cross-cutting engine convention in this codebase does** (see
   `AGENTS.md`'s existing "GPU Resource Memory Tracking"/"CPU Dependency
   Memory Tracking"/"Skeletal Animation Pose Resolution" sections — each
   one written specifically so a *future* contributor doesn't reintroduce
   a solved problem). Fixed by adding this as an explicit Phase 1
   deliverable and a checklist item in Step 5.
8. **v1's flat, per-frame, name-keyed CPU scope model was the right call,
   but v1 never explicitly flagged what it silently gives up: correct
   handling of a scope that (directly or indirectly) calls itself.** Not a
   real concern for today's call sites (this engine's per-frame systems
   don't recurse into themselves), but worth stating as a known,
   deliberate v1 limitation rather than an unconsidered one — see the
   updated Phase 0 design-decision log.
9. **The "pause" control mentioned in Phase 7 was ambiguous about what it
   actually freezes** — the live ring buffer capture, or just the panel's
   own redraw — and that ambiguity matters because benchmark mode (Phase
   6) reuses the exact same underlying capture path. Clarified explicitly
   in Phase 7.
10. **v1 didn't flag secondary ImGui platform windows (multi-viewport,
    `ImGuiConfigFlags_ViewportsEnable`, `RenderPlatformWindows()`) as
    out of scope for GPU-pass attribution.** A panel dragged outside the
    main OS window gets its own swapchain/present call under the hood;
    v1's Phase 4 implicitly assumed a single swapchain present per frame.
    Called out explicitly as a v1-scope refusal in Step 4.
11. **No risk/rollback framing existed for the two technically riskiest
    phases (4 and 6).** Added short "if this goes wrong" notes to both,
    matching this codebase's existing habit of stating *why* something
    was deferred/descoped rather than leaving that as an implicit judgment
    call for whoever reads it later.

Everything else below is v1's content, reorganized only where a fix from
the list above needed to be woven into the surrounding text rather than
appended after it.

--------------------------------------------------------------------------
## Step 1: The Goal (Where are we going?)
--------------------------------------------------------------------------

### 1.1 Vision

GreatTamanaEngine already has a Unity-Memory-Profiler-style **"Memory"**
panel (`src/Editor/Panels/MemoryPanel.cpp`, `src/Editor/MemoryPanelData.h`)
that answers "how much GPU memory is live, of what kind, right now." The
Profiler this document plans is the natural sibling to it, answering the
other half of Unity's own Profiler window: "how much TIME is being spent,
by what, on the CPU and the GPU, right now and over the recent past" — plus
a headless mode that lets that same data be captured and compared
run-to-run without a human watching a window.

Concretely, when this work is done, GreatTamanaEngine will be able to answer
every one of these questions, live, inside the Editor, AND from a script/CI
log with no Editor compiled in at all:

  - "Which CPU-side system (animation, mesh instantiation, rendering
    submission, Editor UI construction, ...) is costing the most time this
    frame, and how has that shifted over the last few hundred frames?"
  - "How much of my frame time is actually GPU-bound vs. CPU-bound, and
    where in the GPU's own command stream is that time going (clearing,
    drawing the Game view, drawing the Scene view, Dear ImGui's own
    overlay)?"
  - "Is my frame time trending up/down/spiky over time, at a glance, the
    same way Unity's Profiler window's timeline strip answers this?"
  - "How many draw calls and triangles is this frame actually submitting,
    and is that number reasonable for the scene currently loaded?"
  - "Is GPU memory usage climbing over the session (a leak), or flat/
    sawtoothing as expected (load/unload churn)?"
  - "Can I get all of the above as a single command-line run with no
    window interaction, so a human or a script can compare 'before' and
    'after' numbers for a given change, without the Editor UI at all?"

### 1.2 Concrete deliverables (mapped 1:1 to the requested feature list)

| # | Feature                              | "Done" means...                                                                                     |
|---|---------------------------------------|------------------------------------------------------------------------------------------------------|
| 1 | CPU scope timers                     | Any function/block in engine code can be wrapped in a named, nestable RAII timer with negligible overhead when unused, and its duration is captured every frame. |
| 2 | Vulkan GPU timestamp queries          | The GPU's own execution timeline (not just CPU-side "time spent recording/submitting") is measured via `vkCmdWriteTimestamp2` (confirmed safe to use unconditionally — see Phase 4), broken down into a small set of named GPU passes. |
| 3 | Frame time graphs                     | The Editor shows a scrolling CPU-vs-GPU frame time graph over the last N frames, Unity-Profiler-timeline-style, correctly distinguishing "this pass didn't run this frame" from "this pass ran in ~0ms." |
| 4 | Per-system timings                    | The Editor shows a sorted, biggest-first breakdown of CPU time by named system (AnimationSystem, MeshInstantiationSystem, RenderSystem, Editor UI build, Present, ...) for the current frame, mirroring the Memory panel's own "sorted, biggest first" convention. |
| 5 | Draw-call and triangle counts         | Every frame's actual `vkCmdDraw`/`vkCmdDrawIndexed` call count and total triangle count is captured and displayed, without recomputing it by re-walking the ECS. |
| 6 | GPU memory usage over time            | The existing, already-O(1) `Renderer::GetMemoryTotals()` is sampled once per frame into the same historical ring buffer the frame-time graph uses, and plotted the same way. |
| 7 | Benchmark mode without the Editor     | A CLI-invocable mode that runs a defined, reproducible workload for a fixed number of frames (after discarding an explicit warm-up window) with no ImGui/Editor UI cost at all, and dumps a CSV + plain-text summary (min/max/avg/p50/p95/p99) of every metric above, exit-code-friendly for scripting/CI. |

### 1.3 Success criteria (how we will know this actually worked)

- A developer can open the Editor, do nothing else, and immediately see a
  live, updating frame-time graph and per-system breakdown — no extra
  configuration step.
- **Turning the whole profiler off is a real, designed switch, not an
  aspiration**: with `GTE_ENABLE_PROFILER=OFF` (compile-time, see Phase 0),
  none of the instrumentation call sites compile to anything beyond an
  empty inline no-op, and measured frame time returns to today's baseline
  within noise. With the profiler compiled IN but the runtime capture
  toggle turned off, per-frame CPU overhead of the scope-timer bookkeeping
  itself must stay under **~0.05 ms per frame** on the reference
  development machine (a concrete, checkable number, not just "small") —
  and with the runtime toggle ON (including GPU timestamp queries), total
  added overhead must stay under **~5% of an otherwise-identical frame's
  time**. These numbers are a starting budget to validate against once
  real measurements exist, not handed-down physical constants — revise
  them in this document once Phase 1/4 produce real data, the same way
  every numeric claim elsewhere in this codebase gets revised against
  reality (see `AGENTS.md`'s own "living document" framing).
- `--benchmark` produces byte-identical *simulation* behavior (same entity
  positions/poses every run) across repeated runs on the same machine, so
  two CSVs are diffable/comparable — only the measured timings differ.
- The full existing test suite (438+ tests as of this writing) still passes
  after every phase below, and every new piece of pure logic ships with its
  own Tier-1 test in the same change, per `AGENTS.md`'s existing testing
  discipline.
- Nothing about `Game`, `RenderSystem`, `AnimationSystem`, or
  `MeshInstantiationSystem`'s existing public API/behavior changes — the
  profiler is purely additive instrumentation, never a rewrite of the
  rendering or ECS pipeline.
- **`AGENTS.md` gains its own "Profiling" conventions section** (see Phase
  1), the same way every other cross-cutting engine concern in this
  codebase (GPU memory tracking, CPU dependency memory tracking, skeletal
  animation pose resolution) already has one — so a future contributor
  adding a new instrumented call site has one place to learn the pattern
  from, instead of reverse-engineering it from example call sites.

--------------------------------------------------------------------------
## Step 2: The Situation / The Problem (Where are we now?)
--------------------------------------------------------------------------

This section is the result of actually reading the current source tree
(`src/Application`, `src/Renderer`, `src/Game`, `src/Editor`,
`src/Renderer/Memory`, `CMakeLists.txt`, `tests/CMakeLists.txt`) before
proposing anything — not a generic profiler design pasted in from nowhere.
This revision additionally re-read `VulkanInstance.cpp`/`VulkanDevice.cpp`/
`FramePresenter.h`/`FrameRecorder.h` specifically to firm up Phase 4's
Vulkan-version assumptions (see the changelog above) rather than leaving
them as something Phase 4's implementer has to discover from scratch.

### 2.1 What already exists that we get to reuse (the good news)

- **A precedent for exactly this shape of feature already exists**: the
  Memory panel. `GpuMemoryTracker` (`src/Renderer/Memory/GpuMemoryTracker.h`)
  is a live, O(1)-queryable, handle-addressed registry that
  `Renderer::GetMemoryTotals()`/`GetMemoryResources()` expose, and
  `MemoryPanelData.h/.cpp` reshapes into plain, ImGui-free, Tier-1-tested
  rows that `Panels/MemoryPanel.cpp` renders. This is *the* template to copy
  for the Profiler: pure data collection in an always-compiled engine
  module → pure, Tier-1-tested reshaping logic → a thin, untested (Tier 2,
  accepted-gap) ImGui panel on top. We are not inventing a new
  architectural pattern here; we are extending an existing, proven one.
- **Renderer is already a thin façade over three collaborators**
  (`FramePresenter` — swapchain + sync + the actual `vkQueueSubmit`/
  `vkAcquireNextImageKHR`/`vkQueuePresentKHR` calls; `GpuResourceFactory` —
  Buffer/RenderTexture/Pipeline/Mesh creation + `ImmediateSubmit()`;
  `FrameRecorder` — this frame's clear color + queued `Submit()` draw list
  + the actual `vkCmdBeginRendering`/draw-loop/`vkCmdEndRendering` recording
  shared by `Present()` and `RenderOffscreen()`). This split is exactly
  where GPU timestamp writes and draw/triangle counters need to be
  inserted, and it is already factored cleanly enough that doing so does
  not require touching unrelated code.
- **`FrameRecorder::DrawItem` already carries `vertexCount`/`indexCount`
  per queued draw** (`src/Renderer/FrameRecorder.h`) — the raw numbers a
  draw-call/triangle counter needs are already sitting right there in the
  one loop (`RecordFrame()`) that issues every `vkCmdDraw`/
  `vkCmdDrawIndexed` call. No new bookkeeping elsewhere in the engine is
  needed to get these numbers; we only need to accumulate what is already
  being iterated. Confirmed directly against the current header: the
  queue is cleared both at `BeginFrame()` and immediately after
  `RecordFrame()` consumes it, so a pure counting pass over the queue
  right before it's cleared sees exactly, and only, what this specific
  `RecordFrame()` call is about to submit — no double counting across the
  Game-view/Scene-view split.
- **`Application::Run()` is a single, linear, well-commented loop**
  (`src/Application/Application.cpp`) with clearly separated phases: poll
  events → `Game::Update()` → resolve Game/Scene render targets →
  `Game::Render()` (×1 or ×2, once per visible view) →
  `Renderer::RenderOffscreen()` (×1 or ×2) → `IEditorLayer::BuildUI()` →
  `Renderer::Present()` → `IEditorLayer::RenderPlatformWindows()`. Every
  phase we want a CPU scope timer around already has an obvious, single
  call site.
- **`Game` is already decomposed into three named systems**
  (`RenderSystem`, `MeshInstantiationSystem`, `AnimationSystem` — see
  `AGENTS.md`, "Entity-Component-System", and `README.md`'s own account of
  the `Game.cpp` "god object" refactor) with clear entry points
  (`AnimationSystem::Update()`, `RenderSystem::CollectRenderables()`/
  `Draw()`). "Per-system timings" is not a hypothetical requirement we have
  to invent boundaries for — the boundaries (and their names) already
  exist in the code, freshly refactored specifically to be individually
  addressable.
- **The Editor module boundary (`IEditorLayer`) already fully solves "run
  without the Editor"** at the *compile-time* level: `NullEditorLayer`
  (`GTE_ENABLE_EDITOR=OFF`) makes every Editor call a true no-op with zero
  ImGui linked in at all. `main.cpp` already has a precedent for a
  *headless, non-`Application::Run()`* code path too: the `--reimport` CLI
  mode constructs no `Window`/`Renderer` at all and just calls the asset
  importer directly, returning a process exit code. Benchmark mode is a
  variation on this same idea, one level up (it does need a `Window`/
  `Renderer`, since Vulkan requires a real `VkSurfaceKHR` today — see 2.3 —
  but the "special CLI branch in `main()`, before the normal `Application`
  construction" shape is already an established, working pattern).
- **CMake's build-flag plumbing pattern is established and easy to copy**:
  `GTE_ENABLE_EDITOR`, `GTE_ENABLE_PROJECT_PANEL`, and `GTE_BUILD_TESTS` are
  each a CMake `option()`, forwarded into C++ as a real `#if`-usable macro
  via `target_compile_definitions(gte_core PUBLIC ...=$<BOOL:...>)`.
  Confirmed directly against `CMakeLists.txt`: this is a `PUBLIC` compile
  definition specifically so the same macro value is visible to
  `GreatTamanaEngineTests` too, not just `gte_core` itself — a new
  `GTE_ENABLE_PROFILER` option (see Phase 0) must follow this exact same
  `PUBLIC` pattern for the same reason, or Tier-1 tests for the profiler's
  own data model would silently see a different macro value than the
  library they're testing.
- **The testing tier discipline is already defined and documented**
  (`TESTING.md`): "Tier 1" = pure logic, no live Vulkan/SDL, always tested;
  "Tier 2" = needs a real `VkDevice`, currently accepted as untested
  (`Buffer`/`RenderTexture`/`Pipeline`/`GpuResourceFactory`, and the
  Editor's `AssetPreviewMesh`/`AssetPreviewTexture`). We already know,
  before writing a single profiler file, exactly which parts of this new
  work will be Tier 1 (nearly all of the *data model* and *math*) and which
  will be Tier 2 (the actual `VkQueryPool` timestamp plumbing) — see Step 3
  and the Testing Strategy subsection.
- **The engine already targets Vulkan 1.3 with `synchronization2` enabled
  unconditionally** (`VulkanInstance.cpp`'s `appInfo.apiVersion =
  VK_API_VERSION_1_3`; `VulkanDevice.cpp`'s `CreateLogicalDevice()` setting
  `features13.synchronization2 = VK_TRUE` alongside `dynamicRendering`,
  the same feature struct device selection already gates on via
  `SupportsDynamicRendering()`). This was not previously verified before
  proposing Phase 4's Vulkan API choice — it now has been, and it means
  Phase 4 can commit to the modern `vkCmdWriteTimestamp2`/
  `VkPipelineStageFlagBits2` timestamp API with no additional version or
  extension negotiation, on every device this engine can already run on.

### 2.2 What is genuinely missing today (the actual gap)

- **There is no timing instrumentation anywhere in the engine at all.**
  Not a single `std::chrono`/`SDL_GetTicksNS()` call exists outside of
  `Application::Run()`'s own frame-delta computation (used only to feed
  `Game::Update(deltaSeconds, ...)`, not measured/reported anywhere).
  Nothing records how long `Game::Update()`, `RenderSystem::Draw()`,
  `IEditorLayer::BuildUI()`, or `Renderer::Present()` themselves take.
  **Nor is there any existing convention for which clock to use** — a
  decision this plan now makes explicitly (see Step 3a) rather than
  leaving to whoever writes the first scope timer.
- **`FrameRecorder`'s draw queue has no attached metadata for
  attribution.** It knows a draw's vertex/index count (needed for the
  triangle counter, see 2.1), but has no idea which *system* queued it,
  and — more importantly for this feature set — nothing anywhere sums
  those counts into a per-frame total today; the numbers are used and
  discarded per-draw, never accumulated.
- **No GPU-side timing exists at all.** Every current cost measurement
  this engine could produce today (if we added CPU timers right now) would
  only be *CPU* time — "how long did it take to record and submit the
  command buffer" — never *GPU* time — "how long did the GPU actually take
  to execute what we submitted." These are routinely very different
  numbers (a CPU-bound frame can have near-zero GPU cost and vice versa),
  and distinguishing them is the actual reason Vulkan timestamp queries
  were explicitly requested as their own feature.
- **`VulkanDevice` does not currently query timestamp support at all**
  (`src/Renderer/Vulkan/VulkanDevice.h/.cpp`) — no
  `VkPhysicalDeviceLimits::timestampPeriod`/`timestampComputeAndGraphics`
  read, no `VK_QUERY_TYPE_TIMESTAMP` pool anywhere in the codebase. This is
  new plumbing, not an extension of something half-built. **Note this is
  an orthogonal capability check from the `dynamicRendering`/
  `synchronization2` features device selection already requires** (2.1) —
  a device can conform to Vulkan 1.3 and still (in principle, e.g. some
  software rasterizers) report `timestampPeriod == 0`/no valid bits on the
  graphics queue family, so Phase 4 must still query and gracefully
  degrade on this specifically, not assume it's implied by the version
  check already happening elsewhere.
- **There is no historical/ring-buffer data structure anywhere in the
  engine.** `GpuMemoryTracker` is deliberately a *live snapshot* (see its
  own class comment: "the tracked record must always reflect the CURRENT
  actual allocation"), not a time series — there is nothing today that
  remembers "what was frame N-100's value," which "frame time graphs" and
  "GPU memory usage over time" both fundamentally require.
- **The Editor's docked-panel set (`Hierarchy`/`Inspector`/`Scene`/`Game`/
  `Memory`/`Project`) has no "Profiler" panel, and `DockLayout.cpp`'s
  one-shot default layout / `kAllPanelNames` list would need to learn about
  a new one** — a small, well-understood, but real piece of work (see
  `DockLayout.cpp`'s own comments about why this one-shot logic is
  order-sensitive and must never re-trigger after the user has
  rearranged panels).
- **There is no headless/benchmark execution mode beyond `--reimport`**,
  and `--reimport` deliberately never constructs a `Window`/`Renderer` at
  all (it doesn't need to — it's a pure file-conversion CLI). A profiling
  benchmark mode is a fundamentally different kind of headless: it *does*
  need a real window/swapchain/GPU device (see 2.3 below), it *does* need
  `Game`'s ECS/systems running for a sustained number of frames, and it
  needs a defined, reproducible workload to drive them with — none of
  which exists today. `Game`'s only existing "scene" is
  `EnsureDemoSceneBuilt()`'s three hardcoded triangles — not remotely
  representative of a real profiling workload, and explicitly flagged in
  its own doc comment as a temporary placeholder to be replaced once scene
  authoring exists.
- **`Application::Run()` has exactly one execution mode** — there is no
  existing seam for "run N frames then stop and report" vs. "run forever
  until the window closes." Introducing benchmark mode means adding a new,
  clearly-scoped alternate entry point, not bolting a frame counter onto
  the existing infinite `while (running)` loop in a way that risks
  affecting normal interactive behavior.
- **There is no compile-time OR runtime switch to disable profiler
  instrumentation itself.** This is distinct from the Editor's own
  `GTE_ENABLE_EDITOR` switch (which controls the ImGui *panel*, not
  whether CPU scope timers execute at all) — v1 of this document promised
  such a switch in its success criteria without ever designing one; this
  is fixed in Phase 0 below (see Changelog item 1).

### 2.3 Constraints discovered while reading the code (must be respected)

- **`VulkanDevice::PickPhysicalDevice()` requires a real, non-headless
  `VkSurfaceKHR` today** (confirmed by `TODO.md`'s own "Tier 2 (GPU-backed)
  integration test fixture" entry, which calls this out explicitly as the
  reason no headless GPU test fixture exists yet). This means **benchmark
  mode cannot be a truly headless, surface-less process** the way
  `--reimport` is — it still needs a real `Window` (and therefore SDL video
  init) and a real `Renderer`/swapchain, exactly like the normal
  interactive path. "Runs without the Editor" must be read precisely as
  "runs without the ImGui/Editor UI layer and its cost," not "runs without
  a window at all." This is an important, easy-to-get-wrong distinction
  that the plan below is careful about (see Step 3, Phase 6).
- **Two-frames-in-flight double buffering (`FramePresenter::kFramesInFlight
  = 2`) plus a *separate*, synchronous offscreen submission path
  (`m_offscreenCommandBuffer`/`m_frameSync.OffscreenFence()`) both already
  exist and must not be disturbed.** Confirmed directly against
  `FramePresenter.h`: the offscreen path already fully blocks on its own
  dedicated fence before `RenderOffscreen()` returns (see its class
  comment — off-screen rendering never contends with the swapchain's own
  per-frame-in-flight objects). Any GPU timestamp query pool design must
  account for both paths independently — a query pool sized only for the
  2 in-flight swapchain frames would silently corrupt/overwrite in-flight
  offscreen (Editor "Game"/"Scene" view) timestamps, or vice versa. This
  is the single trickiest synchronization detail in the whole plan (see
  Step 3, Phase 4, which now spells out concrete pool sizing per path
  rather than leaving it implicit).
- **`Game::Render()` (and therefore `RenderSystem::Draw()`/
  `Renderer::Submit()`/`FrameRecorder::Submit()`) can run TWICE in the same
  frame** — once for the Editor's "Game" view, once for its "Scene" view,
  each into its own `RenderTexture`, each immediately consumed by its own
  `RenderOffscreen()` call before the next one re-queues (see
  `FrameRecorder.h`'s own comment on why a target must consume the queue
  before the next `Render()` call). Any per-frame draw-call/triangle
  counter or GPU timing must decide, explicitly, whether "this frame's
  numbers" means "the Game view's pass" (the gameplay-representative
  number an end user actually cares about) or "everything submitted this
  frame including the Editor-only Scene view" (a superset, useful for
  Editor-overhead-awareness but not what a shipped build will ever do,
  since a release build never renders a Scene view at all). Silently
  conflating the two would produce numbers that mean different things in
  Editor vs. release builds without anyone noticing.
- **Either or both of the Game/Scene `RenderOffscreen()` passes can be
  entirely SKIPPED on a given frame**, not merely resized to zero — see
  `README.md`'s "Visibility-driven rendering": a tabbed-and-hidden panel
  makes `GameViewTarget()`/`SceneViewTarget()` return `nullptr` outright,
  and `Application::Run()` skips calling `RenderOffscreen()` for that view
  entirely that frame, at genuine zero GPU cost. A GPU-pass sample or a
  draw-call/triangle count for a pass that didn't run must be recorded as
  **"no data this frame,"** never silently defaulted to `0`, which would
  read on a graph as "this pass ran and cost nothing" — a materially
  different, misleading claim (see Phase 2/7 below).
- **`GpuResourceHandle`'s generational-slot-map pattern is for
  *dynamically created, potentially-thousands-of* resources** (buffers,
  textures) — it is the wrong tool for the Profiler's own "named pass"/
  "named CPU scope" identifiers, which are a small, statically-known,
  enumerable set (a few dozen at most: `AnimationSystem::Update`,
  `RenderSystem::Draw`, `Present`, ...). Reusing `GpuResourceHandle`/
  `ResourcePool<T,HandleT>` here would be over-engineering copied from the
  wrong precedent; a much simpler fixed-name/fixed-index model is the
  right fit (spelled out in Step 3).
- **`AGENTS.md`'s "GPU Resource Memory Tracking" debug-name convention
  (Editor-only, `#if GTE_ENABLE_EDITOR`, `const char*` never `std::string`,
  static-storage-duration string literals only) is explicitly about
  *optional, cosmetic* names on top of an always-present, name-free hot
  record.** The Profiler's scope/pass *names* are different in kind: they
  are the PRIMARY payload (a benchmark CSV's whole point is a
  human-readable "AnimationSystem::Update took 2.1ms" row), not a
  cosmetic extra — and they must be available with `GTE_ENABLE_EDITOR=OFF`
  too, since benchmark mode's CSV/summary output needs them regardless of
  whether the Editor is compiled in. This means the Profiler's naming
  strategy must NOT simply copy the debug-name-is-Editor-only pattern
  verbatim — it needs its own, always-compiled, zero-allocation
  convention (`const char*` string literals passed by the caller at each
  instrumentation site, stored as a raw pointer, never Editor-gated). This
  is called out explicitly so a future implementer does not "fix" this by
  wrapping scope names in `#if GTE_ENABLE_EDITOR`, which would silently
  break benchmark mode.
- **The engine is explicitly single-threaded today**
  (`GpuMemoryTracker`'s own class comment: "Not thread-safe (matches the
  rest of this single-threaded engine)"). The CPU scope timer design must
  not build in thread-id-aware infrastructure "just in case" — that would
  be speculative complexity with no current user, contrary to this
  engine's stated philosophy of building only what's needed now (see
  `TODO.md`'s own recurring "deliberately NOT done yet" framing applied to
  its own backlog items).
- **The profiler's own instrumentation must not itself perturb the
  numbers it's trying to measure (the observer effect).** This is new
  context v1 didn't call out explicitly: a per-frame hot path that pushes
  onto a growable container, formats a string, or otherwise heap-allocates
  on every scope enter/exit would add real, variable-latency cost that
  then shows up baked into the very durations being reported — most
  dangerously, in a way that scales with how much profiling data is being
  collected, silently defeating the "negligible when small" success
  criterion. See Step 3a for the concrete rule this drives.
- **Every existing Editor panel is a stateless free function taking
  `EditorContext&`** (`AGENTS.md`, "Editor Module Structure": "not classes,
  and NOT implementations of any common `IEditorPanel` interface... don't
  introduce an `IEditorPanel` abstraction preemptively"). A new "Profiler"
  panel must follow this exact convention (a `BuildProfilerPanel(...)` free
  function called explicitly by name from `ImGuiEditorLayer::BuildUI()`),
  not a new class hierarchy.
- **`DockLayout.cpp`'s default-layout logic is one-shot and
  order-sensitive** — adding a panel means updating `kAllPanelNames` AND
  `BuildDefaultDockLayout()`'s `DockBuilderDockWindow()` calls together, in
  the same change, or the one-shot "wait until every panel is accounted
  for" latch (`ctx.dockLayoutEnsured`) will never trigger correctly for a
  fresh `imgui.ini`.

--------------------------------------------------------------------------
## Step 3: The Plan (How will we get there?)
--------------------------------------------------------------------------

The plan is organized as **eight phases**, each one independently
buildable, independently testable, and independently mergeable — i.e. the
engine builds and passes its full test suite at the end of every phase, not
just at the very end of the whole effort. This mirrors how every other
recent feature in this codebase's own `README.md` "Status" section was
delivered (small, sequential, test-suite-green steps), and lets whoever
picks this up stop after any phase with something genuinely useful already
shipped, rather than a big-bang all-or-nothing change.

Phases are ordered so that **later phases only ever consume data
structures earlier phases already defined** — nothing is built twice.

A new subsection, **Step 3a ("Cross-Cutting Concerns")**, follows the phase
list — read it before implementing ANY phase, since it captures rules
(clock choice, allocation policy, the on/off switch) that apply across
every phase rather than belonging to exactly one of them; v1 lacked this
section entirely and scattered a couple of these concerns as asides instead.

### Phase 0 — Foundation: the `Profiling` module, its data model, and the on/off switch

**Goal:** define, once, the core always-compiled data model everything
else plugs into, AND the mechanism to turn it off entirely — both at
compile time and at runtime — since this is a promise Step 1.3 makes that
must actually be designed, not deferred. No Vulkan, no ImGui, no Editor
dependency at all — pure engine-level code, the same "always compiled, no
`GTE_ENABLE_EDITOR` dependency" tier `src/Assets/` and `src/Animation/`
already occupy (per `AGENTS.md`'s own precedent of "engine-level modules
that don't depend on the Editor").

Proposed location: a new `src/Profiling/` folder (its own top-level
sibling to `src/Animation/`, `src/Assets/`, `src/ECS/`, etc. — not nested
under `src/Editor/`, precisely because benchmark mode needs it with the
Editor compiled OUT).

**Phase 0a — the data model** (unchanged in spirit from v1):

- **A named CPU scope stack**, tracked per-frame, forming a simple call
  tree (parent/child relationship by nesting order — a scope opened while
  another is already open becomes its child). Because the engine is
  single-threaded (see 2.3), this can be a single flat stack with no
  thread-local storage at all — this is a deliberate simplification, not
  an oversight, and should be called out again in code comments when
  implemented so a future contributor doesn't "fix" it into something
  thread-aware speculatively.
- **A named GPU pass list**, deliberately much simpler than the CPU
  scope stack: a small, fixed enumeration of well-known GPU passes (see
  Phase 4) rather than an arbitrarily-nestable tree — GPU timestamp
  queries are comparatively expensive to multiply out arbitrarily, and a
  flat, small, fixed set of passes is enough to answer "where did GPU time
  go" at the granularity this engine actually has today (a handful of
  `RenderOffscreen()`/`Present()` calls per frame, not thousands of
  individual draws).
- **A single per-frame "sample" record** aggregating: total CPU frame
  time, the CPU scope tree for that frame (or at minimum a flattened,
  named-duration list — see the "hierarchy vs. flat list" design decision
  below), total GPU frame time plus its own named-pass breakdown (each
  pass explicitly tagged **present / absent / unsupported**, never
  defaulted to a bare `0` — see 2.3's "no data this frame" constraint),
  draw call count, triangle count (same present/absent tagging per named
  pass), and a copy of `GpuMemoryTracker::Totals` for that frame (already
  O(1) to fetch — see 2.1).
- **A fixed-capacity ring buffer of that per-frame sample**, sized to
  hold a few hundred frames of history (Unity's own Profiler window
  defaults to a similar order of magnitude) — old frames simply age out;
  there is no unbounded growth risk, and no persistence-to-disk
  requirement for the live/interactive case (only the benchmark-mode CSV
  dump, Phase 6, needs durable storage, and that's a distinct, simpler,
  write-once-at-the-end operation). Per Step 3a, this buffer's storage is
  allocated ONCE, up front, at a fixed capacity — never grown/reallocated
  per frame.

**Design decision to make explicitly, and document once decided:
hierarchy vs. flat list for CPU scopes.** Unity's own Profiler supports a
fully nested call tree; a flat "name → duration, summed across every time
that name appeared this frame" list is dramatically simpler to implement,
store, and render (a sorted table, exactly like `MemoryPanelData`'s own
biggest-first row list), and is sufficient for "per-system timings"
(feature 4) as literally requested. Recommendation: **start flat** (a
`{name, totalDurationThisFrame, callCount}` row per distinct scope name,
same shape as `MemoryRow`), explicitly deferring true nested-tree
visualization as a documented follow-up (see Step 4) — this is the same
"ship the simpler thing that answers the actual question first" judgment
call already visible throughout this codebase's own `TODO.md` (e.g. VMA
per-block detail explicitly deferred in favor of the simpler aggregate
view that already answers the real question). **Documented limitation of
the flat model, decided here rather than discovered later**: a scope name
that (directly or indirectly) calls itself within the same frame would
have its self-time double-counted across the nested invocations under
today's engine — a non-issue for every current call site (nothing in
`Game`'s per-frame systems recurses into itself), and deliberately not
guarded against now; flag this explicitly in code comments so it's a
known, chosen simplification rather than an undiscovered bug if a future
system ever does recurse.

**Phase 0b — the on/off switch (new in v2, fixes Changelog item 1):**

Two independent layers, mirroring how `GTE_ENABLE_EDITOR` and (say) a
future runtime "hide this panel" checkbox would compose, but applied here
to instrumentation cost instead of UI cost:

- **Compile-time: `GTE_ENABLE_PROFILER` CMake option, `ON` by default**,
  plumbed exactly like `GTE_ENABLE_EDITOR` (`target_compile_definitions
  (gte_core PUBLIC GTE_ENABLE_PROFILER=$<BOOL:${GTE_ENABLE_PROFILER}>)` —
  `PUBLIC`, not `PRIVATE`, for the exact reason spelled out in 2.1's
  updated bullet above: `GreatTamanaEngineTests` must see the same macro
  value `gte_core` was actually built with). With this `OFF`, every scope-
  timer call site (see Phase 1) must compile down to a literal no-op —
  concretely, the RAII scope-timer type itself becomes an empty struct
  with no constructor/destructor body under `#if !GTE_ENABLE_PROFILER`,
  so the compiler has nothing left to even inline away; this is the
  "genuinely zero cost, not just small" branch, for a true minimal-size
  release build that wants to shed even the ring-buffer memory and the
  per-call timestamp reads.
- **Runtime: a single `bool` capture-enabled flag** (owned alongside the
  ring buffer in the `src/Profiling/` module, defaulting to `true`),
  checked ONCE per scope-timer construction (a single branch, not a
  virtual call or anything heavier) — this is the switch a developer
  flips from the Editor's new "Profiler" panel (Phase 7) or from
  benchmark mode's own CLI flag without needing a second, disabled-
  profiler build lying around. When this flag is `false`, a scope timer
  still exists (compiled in, since `GTE_ENABLE_PROFILER` is `ON`) but
  skips reading the clock and skips touching the ring buffer entirely —
  this is the "small and bounded, not zero" branch the Step 1.3 budget
  numbers are written against.
- Both layers are designed to compose the same way `GTE_ENABLE_EDITOR`
  (compile-time) and `gameViewVisible`/`sceneViewVisible` (runtime,
  per-panel) already compose for the Editor's rendering cost today — this
  is not a new pattern, it's the same two-layer shape already proven
  elsewhere in this codebase, applied to a new cross-cutting concern.

**Testing:** every one of the above (ring buffer wraparound behavior,
scope stack push/pop/mismatched-pop-safety, flat aggregation-by-name
correctness, present/absent/unsupported tagging round-tripping correctly,
and — once percentile summaries are needed in Phase 6 — min/max/average/
percentile math) is textbook Tier-1 material: pure data structures and
pure math, no Vulkan/SDL/ImGui, straightforward to hand-feed synthetic
timings to and assert exact results against. This phase should ship with a
`tests/Profiling/` folder mirroring `tests/Animation/`'s own "new engine
module gets its own test folder" convention from day one, not retrofitted
later. The runtime capture-enabled flag's "skip the clock and the ring
buffer entirely when off" behavior is itself Tier-1-testable (assert the
ring buffer's frame count doesn't advance while disabled, then does once
re-enabled) and must ship with a test proving it, since this is the exact
behavior Step 1.3's overhead budget depends on.

### Phase 1 — CPU scope timers wired into the existing per-frame call sites

**Goal:** deliver features 1 and 4 (CPU scope timers, per-system timings)
end-to-end, with no GPU/Editor dependency yet, proving the Phase 0 data
model against real call sites before anything else builds on top of it.

Instrumentation points, chosen because they are each already a single,
well-isolated call in `Application::Run()`/`Game.cpp` (no refactor needed
to "make room" for a timer — see 2.1):

- Event polling/translation loop (`Application::Run()`'s `SDL_PollEvent`
  loop)
- `Game::Update()` as a whole, AND, one level deeper, its one real
  sub-call today, `AnimationSystem::Update()` — proving the design
  actually supports *nesting* (Game::Update contains AnimationSystem::
  Update) from the very first real use, not just flat top-level timings.
- `RenderSystem::CollectRenderables()` and `RenderSystem::Draw()`
  separately (the pure ECS-walk step vs. the actual Renderer-submission
  step — already two distinct functions today, see `RenderSystem.h`'s own
  comment on why they're split for testability; the Profiler should
  respect and expose that same seam rather than only timing `Draw()` as
  one opaque blob).
- `Renderer::RenderOffscreen()` (×1 or ×2 per frame — Game view / Scene
  view, named distinctly so their CPU cost is never conflated — see 2.3).
- `IEditorLayer::BuildUI()` (the cost of constructing every Editor panel
  this frame — Hierarchy/Inspector/Scene/Game/Memory/Project — deliberately
  measured as ONE scope to start, since `BuildUI()` is already one call
  site; splitting it per-panel is a natural, easy follow-up once this
  lands, not a blocker for it).
- `Renderer::Present()`.

Each instrumentation site is a single RAII scope-timer object constructed
at the top of the block/function being measured and destroyed at its
natural end (exactly the "acquire in constructor, release in destructor"
discipline `AGENTS.md`'s "RAII" section already mandates for every other
resource in this engine — a CPU timer's "resource" is simply
"time elapsed between construction and destruction"). Per Step 3a, the
clock read is `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()`
— see Step 3a for why, over `std::chrono`.

**New in v2 — document the convention as it's built, not after.** In the
same change that introduces the first scope-timer call site,
`AGENTS.md` gains a new "Profiling" section (mirroring the structure of
its existing "GPU Resource Memory Tracking"/"CPU Dependency Memory
Tracking" sections exactly): what clock is used and why, the "scope names
are `const char*` literals, never `std::string`, never Editor-gated" rule
(2.3), the "no heap allocation in the per-frame hot path" rule (Step 3a),
and the two-layer on/off switch (Phase 0b). This closes the gap v1 left
open (Changelog item 7) and gives every subsequent phase/instrumentation
site a single place to point back to instead of re-explaining the
convention inline at each call site.

**Where this data becomes visible:** nowhere yet, visually — Phase 1's
job is only to prove the collection pipeline (instrumentation point → CPU
scope stack → per-frame flat aggregation → ring buffer) is correct and
its overhead is negligible, verified via the Tier-1 tests from Phase 0
plus a manual sanity check (e.g. a temporary `printf`/log line, removed
before this phase is considered done) that the numbers look sane against a
stopwatch-level gut check, AND against the Step 1.3 overhead budget
specifically (measure frame time with `GTE_ENABLE_PROFILER=OFF` vs. `ON`-
but-runtime-disabled vs. `ON`-and-enabled, and record the actual deltas in
this document's own "design decision log" once known — see Step 5.3). The
actual UI (Phase 7) and CSV/benchmark consumer (Phase 6) both come later
and reuse this unchanged.

### Phase 2 — Frame time graph data model (history + the pure "what to draw" step)

**Goal:** deliver the *data* half of feature 3 (frame time graphs) — the
ring buffer already exists (Phase 0) and is already being filled with real
CPU numbers (Phase 1); this phase is about shaping that history into
exactly what a graph widget needs: an ordered array of
`(frameIndex, cpuMs, gpuMsOrAbsent)` points ready to hand to a plotting
call, following the exact same "pure reshape function, Tier-1-tested,
consumed by an untested ImGui-drawing function" split `MemoryPanelData.h`'s
`BuildMemoryRows()`/`BuildHeapBudgetRows()` already establish. `gpuMs` is
allowed to be all-"absent" at this point in the plan (Phase 4 fills it in
later) — the graph's *shape*/data pipeline does not need to wait for GPU
timestamps to exist first, and building it now, cross-checked against
known CPU numbers only, is a smaller, easier-to-verify step than building
graph plumbing and GPU timestamp plumbing simultaneously. **New in v2**:
this reshape step must preserve the present/absent/unsupported distinction
from Phase 0 all the way through to the plot points it produces (2.3) —
e.g. by emitting `NaN`/a sentinel the plotting call is told to skip, or by
simply omitting that point from the GPU line for that frame — rather than
collapsing "the Scene view was hidden this frame" into a data point that
looks identical to "the Scene view rendered in 0.0ms," which would be a
silent, misleading simplification a developer could easily misread as "my
Scene view got free" instead of "my Scene view didn't run."

### Phase 3 — Draw-call and triangle counts

**Goal:** deliver feature 5, by extracting a small, pure counting step out
of `FrameRecorder::RecordFrame()`'s existing per-draw-item loop (see 2.1 —
`vertexCount`/`indexCount` are already present on every `DrawItem`).

Design approach: rather than scattering counter increments inline inside
`RecordFrame()`'s Vulkan-calling loop (which would make that function
harder to unit-test in isolation, and would entangle counting logic with
real `vkCmd*` calls), pull the *counting* itself out as its own small,
pure function that takes the same draw-item list `RecordFrame()` already
iterates and returns a plain `{drawCallCount, triangleCount}` result —
exactly the same "extract the pure part, keep it separate from the
Vulkan-calling part" discipline this codebase already applied when it
pulled `MeshVertexPacking`/`MeshMaterialPartitioner` out of `Game.cpp`
(see `AGENTS.md`'s ECS section, and `README.md`'s own account of that
refactor). `RecordFrame()` itself then calls this pure function once
(no behavior change to the actual Vulkan recording), and the result is
handed to the same per-frame sample record Phase 0 defined.

Respecting the "Game view vs. Scene view" ambiguity flagged in 2.3: the
counts are captured **per `RecordFrame()` call**, tagged with which pass
produced them (mirroring the same distinct naming Phase 1 already applies
to `RenderOffscreen()`'s two call sites) — and, per the "no data this
frame" fix above, a pass that didn't run this frame contributes an
explicitly absent count for that pass, not a `0` — the Profiler UI (Phase
7) decides which one(s) to show/label as "the" gameplay-representative
number (the Game view's own pass) versus which to show only as
Editor-overhead-awareness context (the Scene view's pass, and — in the
rare all-hidden-panels edge case, or a release build — the direct-to-
swapchain `Present()` pass).

**Testing:** the pure counting function is textbook Tier 1 — hand-built
draw-item lists in, exact expected counts out, no Vulkan device needed at
all, same shape as `MeshMaterialPartitionerTests.cpp`.

### Phase 4 — Vulkan GPU timestamp queries (the substantial technical phase)

**Goal:** deliver feature 2, and fill in the `gpuMs` half of the frame-time
graph (Phase 2) with real, driver-measured GPU execution time instead of
"absent."

This is the phase requiring genuinely new Vulkan plumbing, so it is
sequenced deliberately late — every earlier phase's data model, ring
buffer, and UI-facing shape already exist and are already proven against
real CPU data by the time this phase touches anything GPU-side, so this
phase only has to get the *GPU-specific* part right, not the whole
pipeline at once.

**Confirmed up front (new in v2, resolves an assumption v1 left implicit
— see 2.1/Changelog item 3): this engine already requires Vulkan 1.3 and
already enables `synchronization2` unconditionally.** `vkCmdWriteTimestamp2`
and `VkPipelineStageFlagBits2` are therefore safe to use directly, with no
additional extension/feature negotiation needed in this phase — device
selection already fails outright for anything that can't provide
`dynamicRendering`, and `synchronization2` is requested in the very same
feature struct. The ONE thing that genuinely still needs its own runtime
query (see below) is timestamp support specifically — a materially
different capability from dynamic rendering/sync2, not implied by them.

Key design points, each directly informed by the constraints in 2.3:

- **Device capability query, added to `VulkanDevice`.** Read
  `VkPhysicalDeviceLimits::timestampPeriod` (nanoseconds per tick — needed
  to convert raw timestamp deltas into milliseconds) and confirm
  `timestampComputeAndGraphics`/a non-zero `timestampValidBits` on the
  graphics queue family, exposed as a small, explicit capability query
  (mirroring how `PickDepthFormat()` already surfaces a
  queried-not-assumed device capability). **A device that doesn't support
  timestamps (rare on desktop, but not impossible, e.g. some software
  rasterizers) must degrade gracefully — GPU timing simply reports
  unavailable/"absent" for every frame, never crashes or throws** — the
  same "degrade gracefully" convention this codebase already applies
  everywhere else (corrupt asset files, missing textures, unmatched
  animation bones, ...).
- **A dedicated RAII wrapper around `VkQueryPool`** (`VK_QUERY_TYPE_
  TIMESTAMP`), following the exact same construction/destruction contract
  every other Vulkan wrapper in `src/Renderer/Vulkan/`/`src/Renderer/`
  already uses (created in the constructor, destroyed in the destructor,
  non-copyable, move-safe) — this is not a new pattern, just one more
  instance of an established one.
- **New in v2 — query pool reset is a real, easy-to-miss requirement, not
  an afterthought.** A `VkQueryPool` slot must be reset
  (`vkCmdResetQueryPool`, core Vulkan, no extension needed) before it is
  ever written to again — reusing a slot without resetting it first is
  invalid per the spec and can produce garbage/validation errors on some
  drivers, not just "stale data." Reset calls belong right before the
  FIRST `vkCmdWriteTimestamp2` of the pair for a given slot each time that
  slot's pass runs, recorded into the same command buffer as the pass
  itself (not a separate one-off command buffer) so there is no additional
  submission/synchronization to reason about.
- **New in v2 — pool lifetime/sizing is genuinely DIFFERENT between the
  two existing submission paths, and this must be designed explicitly
  rather than sized "generically":**
  - The **offscreen path** (`RenderOffscreen()`, Game/Scene views) is
    already fully synchronous — it blocks on its own dedicated fence
    before returning (2.3). This means its query pool needs only ONE set
    of slots per named pass (no multi-buffering at all): write the
    timestamps, wait on the existing fence exactly as the code already
    does today, then read the result back immediately afterward — there
    is no risk of reading an in-flight query, because the call has
    already fully blocked by the time the read happens.
  - The **swapchain `Present()` path** is double-buffered
    (`kFramesInFlight = 2`) and must NOT gain a new stall to accommodate
    this — its query pool needs `kFramesInFlight` sets of slots (one per
    in-flight frame), written this frame, and READ BACK from
    `kFramesInFlight` frames ago, at the exact point `Present()` already
    calls `vkWaitForFences()` to confirm that older frame is done (2.3
    again) — never a new, additional wait purely to fetch a query result
    sooner than the engine otherwise would have.
  - Since the Game/Scene views can each independently be present, absent,
    or (rare edge case) both hidden on any given frame, the query pool(s)
    backing the offscreen path must be sized for the MAXIMUM number of
    named offscreen passes that can occur in one frame (today: 2 — Game
    and Scene), not assumed to always be exactly 2 in a way that would
    break if a third offscreen view is ever added later.
- **A small, fixed set of named GPU passes to start** (per the "flat, not
  arbitrarily nested" call made in Phase 0): at minimum, one pair of
  timestamps (start/end) around the color+depth dynamic-rendering scope
  `FrameRecorder::RecordFrame()` already delineates via its own
  `vkCmdBeginRendering`/`vkCmdEndRendering` pair, recorded separately for
  each of: the Game view's `RenderOffscreen()` pass, the Scene view's
  `RenderOffscreen()` pass, and the swapchain `Present()` pass (which, in
  the common Editor case, is mostly-or-entirely Dear ImGui's own overlay
  cost — see `FrameRecorder::HasQueuedDraws()`'s own comment on why the
  swapchain pass draws real engine geometry only in a release build or the
  rare all-panels-hidden case). This directly gives "how much GPU time is
  Dear ImGui's own chrome costing vs. actual scene rendering" as a
  practically free byproduct of passes that already exist as distinct
  recorded scopes today. **Explicitly out of scope for v1 (new in v2, see
  Step 4's refusal list): a secondary ImGui platform window (dragged
  outside the main OS window, `ImGuiConfigFlags_ViewportsEnable`) gets its
  own swapchain/present call under the hood that this phase does not
  attribute a named GPU pass to** — only the main viewport's `Present()`
  is measured.
- **Insertion mechanism that does not entangle `FrameRecorder` with
  Vulkan-timestamp specifics it shouldn't need to know about**: the exact
  same shape as the already-existing `recordExtra` callback parameter
  threaded through `RecordFrame()`/`Present()`/`RenderOffscreen()` today —
  i.e. GPU timestamp writes are inserted via a small, optional
  "before/after" hook around the existing recording sequence (conceptually
  identical to how ImGui's own overlay is recorded via `recordExtra`
  without `Renderer`/`FrameRecorder` needing to know ImGui exists), rather
  than hardcoding `vkCmdWriteTimestamp2` calls directly inside
  `FrameRecorder::RecordFrame()`'s body. This keeps the option to compile
  the whole GPU-timing feature out with zero cost (see Phase 0b) without
  touching `FrameRecorder`'s own recording logic at all.
- **Ownership**: mirrors `GpuMemoryTracker`'s own established ownership
  shape exactly (see `AGENTS.md`, "GPU Resource Memory Tracking" — "Own the
  tracker via `std::shared_ptr`, never a raw pointer/reference"): `Renderer`
  constructs one shared GPU-timing collaborator alongside its existing
  `m_memoryTracker`, and hands a `shared_ptr` copy to `FramePresenter` (which
  needs to write timestamps at the points it already records frames) — not
  a second, independently-invented ownership convention.

**Risk/rollback note (new in v2):** this is the phase most likely to
surface a genuinely driver-specific quirk (query availability edge cases,
`bufferImageGranularity`-adjacent oddities, a particular vendor's timestamp
period rounding). If GPU timestamp queries prove unreliable on the actual
development machine/GPU in a way that costs disproportionate time to
debug, the fallback is NOT to block the rest of this plan — CPU-only
profiling (Phases 0-3, 5-7 minus the `gpuMs` line) is already a complete,
useful deliverable on its own, and Phase 4 can be descoped to "landed but
reports 'unsupported' on this specific driver, revisit later" without
blocking anything downstream, since every downstream phase already treats
"absent"/"unsupported" GPU data as a first-class, expected case (2.3),
not an error path bolted on afterward.

**Testing:** the actual `VkQueryPool` read-back correctness is
unavoidably Tier 2 (needs a real `VkDevice`), explicitly, deliberately
accepted into the exact same "Tier 2, no automated coverage yet" bucket
`TESTING.md` already documents for `Buffer`/`RenderTexture`/`Pipeline`, not
a new gap this plan invents. What IS Tier-1-testable and must be tested as
such: the nanosecond-to-millisecond conversion math given a
`timestampPeriod` and a raw tick delta (pure arithmetic), and the "which
`kFramesInFlight`-ago slot do I read back this frame" indexing logic (pure,
off-by-one-prone integer math, exactly the kind of thing this codebase
already carefully unit-tests elsewhere — e.g.
`ECS/TransformHierarchyTests.cpp`'s cycle-detection edge cases). Manual
verification against a real GPU, cross-checked with an external tool
(RenderDoc or Nsight Graphics attached to a debug build) before considering
this phase done — the same "build and run against a real GPU/window as a
sanity check" convention `AGENTS.md`'s own Testability section already
prescribes for Tier 2 work.

### Phase 5 — GPU memory usage over time

**Goal:** deliver feature 6. This is, by a wide margin, the cheapest phase
in the whole plan, specifically because `Renderer::GetMemoryTotals()`
already exists and is already O(1) (per `GpuMemoryTracker`'s own doc
comment — "maintained incrementally by `Track()`/`Untrack()`, never
recomputed by summing every live record"). The only new work is: call it
once per frame (from the same place `Application::Run()` already updates
the Phase 0 per-frame sample), and store the result in that same sample
record (already designed in Phase 0 to have room for it) — no new
tracking infrastructure, no new Vulkan calls, nothing GPU-side beyond what
already exists. This phase exists mainly to be explicit that it is
this cheap, so nobody re-derives a parallel memory-history mechanism next
to the existing Memory panel by accident.

Presentation-wise, this phase's data can be shown two ways, and the actual
Editor implementation (Phase 7) should do both, since they answer
slightly different questions: as its own sparkline inside the new
Profiler panel (consistent with every other metric living together, for
the "one place to look" experience the whole Profiler exists to provide),
AND as a small addition to the EXISTING Memory panel (which today only
ever shows an instantaneous snapshot — see `MemoryPanel.cpp`'s "GPU
(Tracked by Engine)" section) so a developer who already has the Memory
panel open, out of habit, doesn't need to also open a second panel to spot
a slow climb. Both simply read the same one ring buffer Phase 0 defined —
there must be exactly one history, never two independently-sampled copies
drifting apart.

### Phase 6 — Benchmark mode (headless-of-the-Editor CLI run)

**Goal:** deliver feature 7, consuming every data model defined in Phases
0–5 as-is — this phase is explicitly a *consumer*, not a parallel
reimplementation. If a phase before this one needs to change to make
benchmark mode work, that is a sign an earlier phase's design was wrong,
not a reason to fork a second code path.

Key design decisions:

- **What "without the Editor" precisely means here** (see the constraint
  flagged in 2.3): benchmark mode still constructs a real `Window` and
  `Renderer` (Vulkan needs a real surface today — no headless surface
  support exists in this engine yet, and building that is explicitly out
  of scope for this plan, see Step 4). What it *skips* is the ImGui/
  `IEditorLayer` cost: no `BuildUI()`, no `Render()`
  (ImGui draw-data recording), no `RenderPlatformWindows()`. Two ways to
  achieve this, both worth naming so the choice is made deliberately
  rather than by default:
    (a) **Compile-time**: build with `-DGTE_ENABLE_EDITOR=OFF` (already
        works today, zero new code needed) — the "true release benchmark,"
        matching exactly what a shipped game would actually run.
    (b) **Runtime**: a new CLI switch on the SAME Editor-enabled binary a
        developer already has built, which still constructs the real
        `ImGuiEditorLayer` (so nothing about `Application`'s construction
        needs an alternate path) but has `Application::Run()` skip calling
        into it for the UI-cost methods specifically, for lower day-to-day
        friction (a developer profiling a change doesn't need a second,
        Editor-disabled build lying around just to benchmark).
  **Recommendation: build (b) as the primary/first-class path, and
  document (a) as the option to reach for when a true, zero-ImGui-linked-
  in release number is specifically needed** (e.g. right before a
  milestone/release). (b) requires threading one new boolean through
  `Application`'s constructor/`Run()` — a small, explicit, easy-to-review
  change, not a structural one. **New in v2 — an honest caveat on (b)**:
  even with UI methods skipped, (b) still pays for `ImGuiEditorLayer`'s
  own construction cost (ImGui context creation, its SDL3/Vulkan backend
  setup, two extra `RenderTexture`s for Game/Scene views) — this is an
  accepted approximation, not a true zero-Editor-linked-in number; anyone
  needing the latter should reach for (a) instead, and this distinction
  should be stated plainly in the benchmark mode's own `--help` text so
  it's never a silent surprise when comparing an (a) run against a (b) run.
- **A defined, reproducible workload.** `Game`'s only existing scene
  (`EnsureDemoSceneBuilt()`) is three flat triangles — explicitly
  documented in its own code as a temporary placeholder. Benchmark mode
  needs something that actually exercises the systems the Profiler
  measures (animation, mesh instantiation, rendering a non-trivial vertex/
  triangle count). Rather than inventing a new scene-description file
  format (which would duplicate effort that rightfully belongs to the
  already-planned, separately-tracked "Scene serialization" engine-roadmap
  item — see `TODO.md` — and this plan explicitly does NOT want to
  scope-creep into that), benchmark mode's workload should be described
  entirely via **simple, explicit CLI arguments driving `Game`'s existing
  public spawn API** (`Game::CreatePrimitiveEntity()`,
  `Game::CreateMeshEntityFromGtaFile()`, `Game::PlayAnimationOnEntity()`) —
  e.g. "spawn 500 cubes," or "load this specific `.gta` mesh + this
  specific `.gta` animation and play it" — resolved once at benchmark
  startup, before the timed frame loop begins.
- **Fixed frame count, not wall-clock duration**, for reproducibility —
  e.g. "run exactly 1800 frames" rather than "run for 30 seconds," so a
  slower machine doesn't simply produce fewer, differently-distributed
  samples than a faster one; both should produce the same NUMBER of
  samples, just with different timing values, which is what actually makes
  two benchmark runs comparable.
- **New in v2 — an explicit warm-up window, discarded from every
  statistic.** The first N frames (a reasonable starting point: 30-60
  frames, i.e. roughly the first half-second-to-second at 60 FPS) of any
  run pay one-time costs this plan's own earlier phases already surface
  elsewhere in this codebase — first-touch shader/pipeline creation, the
  swapchain's own first-resize/first-acquire behavior, page faults on
  freshly-allocated buffers — none of which are representative of
  steady-state performance. These frames are still RECORDED into the ring
  buffer/CSV (so the full run is visible if needed for debugging a
  startup-specific regression), but are explicitly excluded from the
  min/max/average/percentile summary the benchmark reports, and the CSV/
  summary output must clearly label which rows were warm-up so a
  before/after comparison never accidentally includes them on one side but
  not the other.
- **A fixed, simulated `deltaSeconds`** fed to `Game::Update()` (e.g. a
  constant 1/60s) rather than real measured elapsed time between
  iterations, decoupling "what the simulation believes happened between
  frames" (must be identical every run, for reproducible entity positions/
  animation poses/frame-to-frame content) from "how long this run actually
  took to compute it" (the thing being measured, which SHOULD vary
  run-to-run — that's the whole point). Conflating these two would make
  a slower machine also simulate a *different* scene state than a faster
  one at frame N, which would silently invalidate any attempt to compare
  their profiler numbers against each other.
- **Output**: a CSV dump of the full per-frame sample history (same ring
  buffer/record shape as the interactive Profiler, Phase 0 — one shared
  exporter function, not two independently-written CSV writers that could
  drift apart in column meaning, and the exact same present/absent/
  unsupported tagging from Phase 0/2 preserved as its own explicit column
  value — e.g. a literal `N/A`, never a bare `0` — so a spreadsheet/script
  consuming the CSV can't accidentally average "didn't run" in with real
  zeros), plus a plain-text summary to stdout (min/max/average/p50/p95/p99
  for CPU frame time, GPU frame time, draw calls, triangle count, and GPU
  memory, computed only over the post-warm-up frames per the point above)
  — process exit code 0 on a clean run, non-zero on any setup failure (e.g.
  the requested mesh/animation file doesn't resolve), following the exact
  same "return the process exit code directly from `main()`" convention
  `--reimport` already established.
- **CLI surface**: extend `main.cpp`'s existing special-cased-argument
  dispatch (the same `if (argc >= N && argv[1] == "--something")` shape
  `--reimport` already uses) with a new `--benchmark` branch, rather than
  introducing a general-purpose argument-parsing library/framework this
  engine has no other use for yet (consistent with Step 4's "no new
  scripting/config language" refusal below).

**Risk/rollback note (new in v2):** if the (b) runtime-skip approach
above turns out to leak more Editor-construction cost into the numbers
than expected (see its own caveat), the fallback is simply to lead with
(a) as the documented, authoritative benchmark path and demote (b) to a
"quick, approximate, day-to-day" convenience note — a presentation/
documentation change only, since both paths reuse the exact same
underlying Phase 0-5 data model and CSV exporter; no data-model rework
would be needed either way.

**Testing:** the percentile/summary-statistics math and CSV row formatting
(including the present/absent/unsupported column tagging and warm-up-row
exclusion) are Tier 1 (pure functions over a hand-built sample array, exact
expected output). The actual end-to-end CLI behavior (does `--benchmark`
really skip ImGui cost, does it really produce a file) is inherently an
integration-level concern most naturally verified by actually running the
built executable with the flag and inspecting the output — the same
"Tier 2, verified manually" acceptance already extended to
`AssetPreviewMesh`/`AssetPreviewTexture` and the real-Vulkan-device smoke
tests (`PmxLoaderRealModelSmokeTest`, `VmdLoaderRealMotionSmokeTest`)
elsewhere in this codebase's own test suite.

### Phase 7 — The Editor "Profiler" panel (ties every feature together visually)

**Goal:** give a human, inside the Editor, one place to see everything
Phases 0–5 already collect — the last phase, deliberately, since it has
nothing of its own to build except presentation on top of an
already-proven data pipeline.

Following the Memory panel's own established split exactly:

- **`ProfilerPanelData.h/.cpp`** (new, under `src/Editor/`, gated behind
  `GTE_ENABLE_EDITOR` the same as `MemoryPanelData.h/.cpp` — NOT
  additionally behind `GTE_ENABLE_PROJECT_PANEL`, mirroring how the Memory
  panel itself is not gated behind that switch either): pure, ImGui-free
  reshaping functions — building the graph's plottable point array
  (Phase 2, including its "gap" handling for absent-pass frames), sorting
  the per-system breakdown biggest-first (matching `BuildMemoryRows()`'s
  own "biggest contributor first" convention exactly, same rationale:
  "what's contributing most" answered at a glance), and formatting a
  duration as a human-readable string (a `FormatDuration()` sibling to
  `MemoryPanelData.h`'s existing `FormatBytes()` — same "shared pure
  formatter so the panel and its test can never drift apart" reasoning).
  Tier-1-tested in `tests/Editor/ProfilerPanelDataTests.cpp`, exactly like
  `tests/Editor/MemoryPanelDataTests.cpp`.
- **`Panels/ProfilerPanel.cpp`** (new, alongside `Panels/MemoryPanel.cpp`):
  a stateless free function, `BuildProfilerPanel(EditorContext&,
  Renderer&, /* profiler data source */)`, called explicitly by name from
  `ImGuiEditorLayer::BuildUI()` in the same fixed, explicit order every
  other panel already is (per `AGENTS.md`'s "no `IEditorPanel`
  abstraction preemptively" rule). Sections, in the same spirit as the
  Memory panel's own three-section layout: a frame-time graph (CPU vs GPU
  line plot over recent history, with a visible gap/marker rather than a
  misleading dip to zero on a frame where a pass didn't run), a per-system
  CPU breakdown table (name/total-this-frame/call-count, sorted biggest
  first), a draw-call/triangle-count readout (current frame + short
  rolling average, again distinguishing "didn't run" from "ran and drew
  nothing"), and a GPU-memory-over-time sparkline (Phase 5). A small
  toggle to pause/resume live updates, and a button to dump the current
  history to CSV via the SAME exporter function Phase 6's benchmark mode
  uses. **New in v2 — the pause toggle's exact semantics, stated
  explicitly to remove an ambiguity v1 left open:** pausing freezes only
  the PANEL's own redraw (it keeps showing whatever the ring buffer looked
  like at the moment of pausing) — it must NOT stop the underlying Phase
  0 ring buffer from continuing to record real frames underneath, since
  that same ring buffer is the one thing benchmark mode (Phase 6) also
  reads from, and a "pause" flag that accidentally also gated capture
  would silently break benchmark mode the moment the two features share
  code. Resuming simply lets the panel start reading the (never-stopped)
  ring buffer's latest contents again.
- **Docking wiring**: add `"Profiler"` to `DockLayout.cpp`'s
  `kAllPanelNames` and to `BuildDefaultDockLayout()`'s bottom-docked group
  (tabbed alongside "Memory"/"Project" — Unity's own default layout groups
  "Profiler" and "Console" together along the bottom too, so this matches
  a familiar convention), in the SAME change that introduces the panel —
  per the constraint flagged in 2.3, these two must never be updated
  independently of each other.
- **CMake wiring**: `ProfilerPanelData.h/.cpp` and `Panels/ProfilerPanel.h/
  .cpp` added to `gte_core`'s `target_sources()` inside the existing
  `if(GTE_ENABLE_EDITOR)` block in `CMakeLists.txt`, exactly where
  `MemoryPanelData.h/.cpp`/`Panels/MemoryPanel.h/.cpp` already live —  no
  new CMake option needed for the panel itself (the always-compiled
  `src/Profiling/` module from Phase 0, gated by its OWN new
  `GTE_ENABLE_PROFILER` option, is what needs to be reachable with the
  Editor OFF, for benchmark mode — the PANEL specifically is correctly
  Editor-only, same as the Memory panel).

### Design decision log (things explicitly decided so they are not re-litigated later)

- **Scope/pass names are `const char*` string literals passed at each call
  site, never `std::string`, and never Editor-gated** — see 2.3's
  explanation of why this differs from the `GpuMemoryTracker` debug-name
  convention.
- **CPU scopes are a flat, per-frame, name-keyed aggregation, not a full
  nested tree, for v1** — see Phase 0's reasoning; explicitly revisitable
  later (see Step 4) once the flat version is in use and its limits are
  actually felt. **A recursive/self-referential scope's self-time can be
  double-counted under this model** — a known, accepted v1 limitation, not
  an oversight (see Phase 0's own design-decision paragraph).
- **GPU passes are a small, fixed, named set (Game view / Scene view /
  Present), not a general-purpose nested GPU zone system** — see Phase 4's
  reasoning. **Secondary ImGui platform windows (multi-viewport) are not
  attributed a named GPU pass at all in v1** — see Phase 4/Step 4.
- **The single ring buffer defined in Phase 0 is the ONE history every
  consumer (frame graph, GPU-memory-over-time sparkline, CSV export) reads
  from** — never a second, independently-sampled copy.
- **Benchmark mode still opens a real window/swapchain** — it is not, and
  cannot currently be, a truly surface-less headless process (see 2.3).
- **A GPU/draw-call/triangle sample that didn't run this frame is tagged
  "absent," never defaulted to a bare `0`** — new in v2, see 2.3/Phase 2.
- **The profiler has a genuine two-layer off switch**
  (`GTE_ENABLE_PROFILER` compile-time + a runtime capture-enabled flag) —
  new in v2, see Phase 0b; this is what makes Step 1.3's overhead claims
  checkable rather than aspirational.
- **`SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()` is the
  one clock used everywhere in this module** — new in v2, see Step 3a.

--------------------------------------------------------------------------
## Step 3a: Cross-Cutting Concerns (new in v2 — apply to every phase above)
--------------------------------------------------------------------------

v1 folded a few concerns that genuinely apply across every phase into
asides inside individual phases (or omitted them entirely). Pulling them
out here, once, is meant to stop a future implementer from having to
re-derive the same answer independently in Phase 1, then again
differently in Phase 4.

- **Clock choice: `SDL_GetPerformanceCounter()`/
  `SDL_GetPerformanceFrequency()`, not `std::chrono`.** SDL is already the
  one platform-abstraction layer this engine depends on for everything
  else timing-adjacent (`Application::Run()`'s own frame-delta computation
  already uses SDL's timing facilities), and using the same clock the rest
  of the engine already uses avoids a second, independent notion of "what
  time is it" with its own potential clock-skew/resolution differences
  from the one driving `deltaSeconds`. This also keeps the CPU scope timer
  usable identically whether `GTE_ENABLE_EDITOR` is `ON` or `OFF` (SDL is
  always linked, per `BUILDING.md`), unlike, say, a Windows-specific
  `QueryPerformanceCounter()` call that would need its own portability
  shim for no real benefit on this already-SDL-based engine.
- **No heap allocation in the per-frame CPU-scope-timer hot path.** The
  scope stack, the flat per-frame aggregation map, and the ring buffer's
  own slot storage must all be pre-sized/fixed-capacity, populated via
  plain array/POD writes, not `std::vector::push_back` past a reserved
  capacity or `std::string` construction on every scope enter/exit. This
  directly protects the "negligible when small" success-criterion number
  in Step 1.3 from the observer effect described in 2.3 — an allocator
  call has real, variable latency that would otherwise get baked into the
  very durations being measured, and would scale unpredictably with how
  much is currently being profiled, which is exactly the failure mode a
  profiler must never itself exhibit.
- **The two-layer on/off switch (Phase 0b) is the mechanism every
  numeric claim in Step 1.3 is validated against** — once Phase 1 lands,
  actually measure all three states (`GTE_ENABLE_PROFILER=OFF`; `ON` but
  runtime-disabled; `ON` and enabled) on the reference development machine
  and record the real numbers back into this document's Step 1.3 (replacing
  the starting-budget placeholders with measured reality) — the same
  "revise the plan against what actually happened" discipline this
  document's own closing section (5.3/5.4) already asks for.
- **Every "is GPU timing available" / "did this pass run this frame"
  question is answered with an explicit tri-state (present / absent /
  unsupported), never a bare numeric zero standing in for one of the other
  two.** This single rule is what Phase 2/3/4/6/7 all lean on to avoid the
  "hidden panel reads as free" and "unsupported GPU reads as instant"
  misreadings flagged in 2.3 — implement the tri-state ONCE in Phase 0's
  data model and thread it through unchanged, rather than letting each
  downstream phase invent its own ad hoc "is this a real value" convention.

--------------------------------------------------------------------------
## Step 4: What We Will NOT Do (Focus)
--------------------------------------------------------------------------

Being explicit about scope refusals is as important as the plan itself —
this mirrors `TODO.md`'s own recurring "deliberately NOT done yet, and
here is exactly why" discipline, which is what has kept this codebase from
sprawling so far.

- **No fully nested, arbitrary-depth GPU profiling zones in v1.** Phase 4
  ships a small, fixed set of named passes (Game view / Scene view /
  Present). A future, more granular breakdown (e.g. separate timestamps
  around opaque-vs-transparent sub-passes, once such a distinction even
  exists in this renderer) is a natural, additive follow-up once there is
  an actual rendering feature that would benefit from it — not built
  speculatively now.
- **No multi-threaded/job-system-aware profiling infrastructure.** This
  engine is explicitly single-threaded today (`GpuMemoryTracker`'s own
  class comment). Building thread-id-tagged scope stacks now would be
  unused complexity with no current caller, contrary to this codebase's
  established "build only what's needed now" discipline. Revisit only if
  and when the engine itself grows a real job system.
- **No remote/networked profiling client.** Everything stays in-process:
  visualized live via the existing ImGui Editor, or dumped to a local CSV/
  stdout summary for headless/benchmark use. A separate always-on
  streaming capture protocol (à la Tracy's remote-client model, or
  RenderDoc's capture-and-load-in-a-separate-app workflow) is explicitly
  out of scope — nothing here prevents wiring one in later, but it is not
  a goal of this plan.
- **No re-opening of the already-explicitly-deferred `VkAllocationCallbacks`
  CPU-side Vulkan driver memory hook.** `TODO.md`'s Memory Profiler section
  already investigated and deliberately deferred this (no evidence of an
  unaccounted-for CPU memory gap large enough to justify the multi-file
  refactor it would need). This plan's GPU-memory-over-time feature
  (Phase 5) reuses the existing, already-sufficient `GpuMemoryTracker`
  totals and does not reopen that question.
- **No automated performance-regression CI gate in this pass** (e.g.
  automatically failing a build if frame time regresses beyond some
  threshold). This plan delivers the *measurement and visualization*
  infrastructure only; turning that into an automated pass/fail gate is a
  reasonable, natural follow-up, but needs its own baseline-establishment
  and tolerance-tuning work that would only distract from getting the core
  data pipeline right first.
- **No new headless-Vulkan (`VK_EXT_headless_surface`) test fixture
  bootstrapped as part of this effort.** `TODO.md` already documents this
  as an accepted, deliberately-deferred backlog item, explicitly not a
  blocker for other feature work — this plan's own Phase 4/6 Tier 2 gaps
  are folded into that exact same already-accepted bucket, not a new one
  invented for this occasion.
- **No new scene-description/scenario file format (JSON/YAML/etc.) for
  benchmark workloads.** Benchmark scenarios are driven by simple, explicit
  CLI arguments against `Game`'s existing public spawn API (Phase 6). A
  richer, data-driven scenario format is only worth building once real
  Scene Serialization exists (already a separately tracked, higher-level
  engine-roadmap item in `TODO.md`) — building a bespoke, profiler-specific
  scene format now would be throwaway work duplicating that eventual,
  more general capability.
- **No rewrite of `FrameRecorder`/`FramePresenter`/the draw-submission
  pipeline.** Every phase above is additive instrumentation (new counters,
  new optional timestamp-write hooks reusing the existing `recordExtra`-
  style seam) layered onto the existing, working recording/submission
  code — the `AGENTS.md` "Render Target Format Matching" assertions, the
  lazy swapchain-depth-buffer allocation, the two-frames-in-flight
  double-buffering, and every other piece of carefully-reasoned-through
  existing behavior in that code stays completely untouched.
- **No per-draw-call GPU timing.** A timestamp per individual
  `vkCmdDraw`/`vkCmdDrawIndexed` call would require a query slot per draw
  (an unbounded, scene-dependent number) and would materially perturb GPU
  scheduling/pipelining just by existing in such volume. Only coarse,
  whole-pass GPU timing (Phase 4) is in scope.
- **No change to `Game`, `RenderSystem`, `AnimationSystem`, or
  `MeshInstantiationSystem`'s existing public APIs or behavior.** The
  Profiler observes these systems (wrapping their existing call sites in
  timers) — it never needs, and must never be given, a reason to change
  what they do or how they're called.
- **No GPU-pass attribution for secondary ImGui platform windows
  (multi-viewport, `ImGuiConfigFlags_ViewportsEnable`/
  `RenderPlatformWindows()`).** (New in v2, see Phase 4.) Only the main
  viewport's `Present()` pass is measured; a panel dragged out into its own
  OS window is not represented in the GPU timeline in v1.
- **No automatic correction for recursive/self-referential CPU scopes.**
  (New in v2, see Phase 0's design-decision log.) The flat aggregation
  model can double-count a recursive scope's self-time; this is accepted
  because no current call site recurses, not because the limitation was
  overlooked.

--------------------------------------------------------------------------
## Step 5: Their Role (What does this mean for you?)
--------------------------------------------------------------------------

This section is written for whoever actually picks up implementation next
— a human contributor or an AI coding agent working from this document —
since that is who needs to turn "Step 3" into real commits.

### 5.1 How to start

1. Read this document fully, then re-read `AGENTS.md` in full (not just
   skim it) — every phase above was deliberately designed to slot into
   conventions `AGENTS.md` already mandates (RAII, `gte` namespace,
   handle-based identification where it actually fits, the Tier 1/Tier 2
   testing split). Violating one of those conventions here would be a
   regression in project consistency, not just a stylistic nit.
2. Implement phases **in order**. Do not start Phase 4 (GPU timestamps)
   or Phase 6 (benchmark mode) before Phases 0–3 exist and their own tests
   pass — both later phases are explicitly designed as *consumers* of the
   earlier phases' data model, and implementing them first would either
   force a second, throwaway data model or silently paper over a
   Phase-0-level design mistake that would otherwise have been caught
   early and cheaply.
3. Treat each phase as its own reviewable, buildable, testable unit —
   build (`cmake --build build --config Debug`) and run the FULL test
   suite (`ctest --test-dir build -C Debug --output-on-failure`) after
   every phase, per `AGENTS.md`'s own "Run the actual test suite before
   considering any change to `gte_core` done" rule. A phase is not
   "done" if it merely compiles.

### 5.2 Non-negotiable checklist per phase (copy this into every PR/commit description)

- [ ] Every new piece of pure logic has a matching Tier-1 test added in the
      SAME change (per `AGENTS.md`'s "Every change to Tier 1 code must come
      with a matching test change").
- [ ] Every new engine-level type lives inside `namespace gte` (nested
      sub-namespaces, e.g. a hypothetical `gte::Profiling`, are fine and
      encouraged for a cohesive new module — see how `Animation/` and
      `Assets/` are organized).
- [ ] Every new resource-owning type (a `VkQueryPool` wrapper, in
      particular) follows RAII exactly — acquired in its constructor,
      released in its destructor, no manual cleanup calls scattered
      through calling code.
- [ ] Nothing outside `src/Editor/` (specifically: nothing in
      `src/Profiling/`, `src/Renderer/`, `src/Game/`) includes an ImGui
      header, directly or transitively — the Profiler's DATA model must
      stay usable with `GTE_ENABLE_EDITOR=OFF` for benchmark mode to work
      at all.
- [ ] **New in v2**: nothing in the profiler's per-frame hot path
      (scope-timer enter/exit, ring-buffer write, per-frame flat
      aggregation) allocates on the heap — verified by inspection, and
      ideally by a debug-build assertion/counter during development (per
      Step 3a).
- [ ] **New in v2**: with `GTE_ENABLE_PROFILER=OFF`, confirm (by actually
      building that configuration) that no `src/Profiling/` translation
      unit compiles in anything beyond the empty no-op form of its public
      API — not just "the feature looks disabled at runtime."
- [ ] `CMakeLists.txt`'s `gte_core` source list and any new
      `GTE_ENABLE_EDITOR`-gated (or, new in v2, `GTE_ENABLE_PROFILER`-gated)
      block are updated together with the panel/module code that needs
      them, never left mismatched — and any new option's compile
      definition is `PUBLIC`, matching `GTE_ENABLE_EDITOR`'s own existing
      `PUBLIC` definition, so `GreatTamanaEngineTests` sees the same macro
      value `gte_core` was built with (see 2.1).
- [ ] `DockLayout.cpp`'s `kAllPanelNames` and `BuildDefaultDockLayout()`
      are updated together, in the same change, when the "Profiler" panel
      is introduced (Phase 7) — never one without the other.
- [ ] **New in v2**: every place that reports a GPU pass's timing/draw/
      triangle data (ring buffer, CSV export, the graph/table widgets)
      correctly distinguishes "absent this frame" from "measured as zero" —
      spot-check this specifically by hiding the Scene panel for a few
      frames and confirming the Profiler shows a gap, not a dip to zero.
- [ ] The full test suite is built and run (not just "it compiles") before
      the phase is considered complete.
- [ ] **New in v2**: `AGENTS.md` gains (Phase 1) or is updated to reference
      (every later phase touching a new call site) its "Profiling"
      conventions section, the same way every other cross-cutting engine
      concern already has one.
- [ ] `README.md`'s "Status" section, `TESTING.md`, and (if a new
      deliberate scope refusal is discovered mid-implementation that isn't
      already listed in Step 4 above) `TODO.md` are updated in the same
      spirit/level of detail this codebase's own history already
      demonstrates for every other feature — documentation-as-you-go, not
      a separate catch-up pass at the end.

### 5.3 What "done" looks like for the whole effort, and what happens after

When Phase 7 lands, this document's Step 1 "success criteria" should be
re-checked literally, one by one, against the real, running engine — not
assumed. This specifically includes replacing Step 1.3's starting-budget
overhead numbers (~0.05 ms/frame disabled-but-compiled-in, ~5%
enabled-with-GPU-timestamps) with the actual measured numbers from the
reference development machine, per Step 3a. At that point, this document
itself should be updated (or retired/merged into `TODO.md`/`README.md`,
matching how this codebase already treats its own planning documents as
living artifacts, not one-time specs) to record: which of Step 4's
deliberate refusals still hold, which (if any) turned out to be worth
revisiting sooner than expected (e.g. if the flat CPU-scope model from
Phase 0 turns out to feel genuinely limiting once real per-system data is
being stared at daily, or if the recursive-scope double-counting
limitation actually gets hit by a future system), and what the natural
next follow-ups are (nested GPU zones, an automated regression gate, a
data-driven benchmark scenario format once Scene Serialization exists) —
exactly the same way `TODO.md`'s own entries routinely get a "DONE" or
"UPDATE" note appended rather than being silently deleted once superseded.

### 5.4 A note on judgment calls left deliberately open

A few decisions in Step 3 are flagged as "recommendations" rather than
hard requirements (the flat-vs-nested CPU scope model, the runtime-vs-
compile-time benchmark-mode split). This is deliberate: they are the kind
of call best confirmed once the simpler option is actually in front of a
developer using it, not theorized about further in a planning document.
If you are the one implementing this and a recommendation above turns out
to be wrong once real usage shows it, that is an expected, healthy outcome
of this plan's own "ship the simpler thing first" philosophy — change it,
and record why in this document's Step 3 "design decision log," the same
way this codebase's own `TODO.md` already documents every "UPDATE (this
session)" course-correction in its history.
