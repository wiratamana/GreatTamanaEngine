# RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md

## V2 Revision Notes (2nd iteration review)

This is a second-pass review of `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v1.md`
and its eight companion phase documents. The overall shape of the campaign
(nine independently-landable, phased-strategy-doc-driven chunks, pure data
-> builder -> compiler -> physical realization -> barriers -> execution ->
migration -> tooling -> backlog) is sound and is kept unchanged. This pass
found five concrete issues worth fixing before Phase 1 implementation
starts, in decreasing order of severity:

1. **A real correctness bug in Phase 1's `TextureDesc::operator==`.**
   `TextureDesc` carried a `const char* debugName` field, and its
   `operator==` was declared `= default`, which means debugName's POINTER
   VALUE participates in equality. Phase 4's whole resource-pooling
   mechanism ("does an existing pooled resource have a desc that equals
   this one?") depends on `operator==` being purely structural
   (width/height/format/hasDepth). Two logically-identical requests for the
   same conceptual resource issued from two different call sites (or even
   the same call site across two frames, if the name string ever came from
   a non-identical-but-equal-content literal/buffer) would almost always
   compare unequal purely because of the debugName pointer, silently
   defeating pooling entirely and reintroducing the exact "a new
   `RenderTexture`/`VkImage` every single frame" regression Phase 4 exists
   to prevent - with no test ever catching it, since a naive test would
   likely reuse the identical string-literal pointer for both sides of the
   comparison and never notice. **Fix (see Phase 1 v2/Phase 2 v2/Phase 4
   v2): `debugName` is removed from `TextureDesc` entirely.** A resource's
   human-readable name is now threaded as its own, separate parameter
   throughout (`RenderGraphBuilder::CreateTexture(name, desc)` ->
   internally kept in a parallel, handle-indexed name table, never inside
   the comparable `desc` itself -> `RenderGraphResourcePool::AcquireTexture(desc,
   debugName)` - which already had `debugName` as a separate parameter in
   v1, it just no longer redundantly ALSO lived inside `desc`).

2. **An unresolved contradiction between Phase 6 and Phase 7 about how many
   `RenderGraph::Execute()` calls happen per frame, and whether that's even
   safe.** Phase 6's prose says "one `Execute()` call... per logical frame's
   worth of GPU work... once per `RenderOffscreen()`-equivalent call PLUS
   once for the swapchain Present-equivalent call" (implying at least TWO
   calls per frame, matching today's two independent submission regimes),
   but Phase 7's own `Application::Run()` code sample shows exactly ONE
   `Execute()` call wrapping Game view, Scene view, AND Present all
   together. These are not compatible, and the difference is not cosmetic:
   today's engine genuinely has TWO DIFFERENT submission regimes sharing no
   command buffer - `RenderOffscreen()` (Game/Scene) records into its own
   dedicated command buffer and blocks synchronously on its own fence
   before returning (see `FramePresenter::RenderOffscreen()`), while
   `Present()` records into one of `kFramesInFlight` (2) per-frame command
   buffers and is deliberately NON-blocking/pipelined (acquire -> submit ->
   present, fenced by frame-in-flight index, never waited on synchronously
   within the same call). Silently merging these into one shared
   `VkCommandBuffer`/one `Execute()` call, as Phase 7's sample literally
   shows, would force either (a) Present to become synchronous too (a real,
   measurable frame-pacing regression nobody asked for and Phase 7
   explicitly promises NOT to introduce - "zero observable behavior
   difference to a user"), or (b) some hidden multi-command-buffer submission
   splitting inside `RenderGraph::Execute()` that neither Phase 6 nor 7
   ever describes. This also collides with the minimized-window case: today
   `FramePresenter::Present()` can independently return `std::nullopt` (skip
   the swapchain acquire/present) while Game/Scene `RenderOffscreen()` calls
   keep running normally (a minimized OS window doesn't pause the Editor's
   own off-screen Game/Scene views) - a single, atomic "compile once, run
   once" `Execute()` covering all three has no obvious place to skip only
   the Present portion. **This needed to be resolved, not glossed over -
   see Phase 6 v2/Phase 7 v2 for the actual decision:** `RenderGraph::Execute()`
   is called TWICE per frame, matching today's two real submission regimes
   exactly (one call for the synchronous offscreen regime covering
   Game+Scene together, one call for the async/pipelined swapchain regime
   covering Present alone) - Phase 7's code sample is corrected accordingly.

3. **Full MRT (multi-color-attachment) support in the MVP is speculative
   and has zero real consumer anywhere in Phases 1-8.** Every real pass
   this campaign actually migrates (Game view, Scene view, Present) uses
   exactly one color attachment. Building full MRT plumbing now (a second
   `RenderTarget`/`RenderTargetSet` shape, `PassBuilder::WriteColorAttachment()`
   called N times, a fixed-size `std::array<VkImageView, kMaxColorAttachments>`
   in the barrier planner, etc.) is real, non-trivial surface area with
   no exercised, tested, shipped consumer - and it was ALSO missing a
   dependency the v1 docs never mention: `Pipeline`'s own
   `VkPipelineRenderingCreateInfo` today hardcodes
   `colorAttachmentCount = 1`/`pColorAttachmentFormats = &colorFormat`
   (`Pipeline.cpp`) - MRT is unusable without ALSO teaching `Pipeline` to
   build against an array of color formats, which none of Phases 1-8 ever
   proposed touching. This directly contradicts the campaign's own,
   repeatedly-stated discipline ("build only what is needed, when the need
   is real and demonstrated, never speculatively" - Phase 9's own closing
   words). **Fix (see Phase 5 v2/Phase 6 v2): MRT is explicitly moved out of
   the Phases 1-8 MVP commitment and into Phase 9's backlog, alongside the
   matching `Pipeline` multi-format-attachment change it actually requires**
   - both to be built together, for real, the day a genuine multi-attachment
   pass (a G-buffer) is actually being implemented, not before. Phase 5/6's
   barrier-synthesis/execution design keeps a single, generously-sized
   fixed-attachment-count constant in mind (so this isn't a hard rewrite
   later) but ships against exactly one color attachment for the MVP,
   matching literally every real pass in this campaign.

4. **The campaign's single most novel/risky capability - resolving a
   declared cross-pass READ (one pass sampling another pass's rendered
   output through the graph) - is never exercised by any of the THREE real
   passes Phase 7 actually migrates.** `AddGameViewPass`/`AddSceneViewPass`/
   `AddPresentPass` all only ever WRITE; Dear ImGui samples the Game/Scene
   `RenderTexture`s entirely on its own, outside the graph's resource model
   (see point 5 below). The only place a cross-pass read is ever built or
   tested is Phase 6's own explicitly-throwaway two-pass demo scene (Step
   5: "build it with a THROWAWAY two-pass test scene FIRST... before
   touching anything Phase 7 will eventually need"), which is explicitly
   never shipped/kept. That means after all eight MVP phases land, the
   entire justification given in this document's own Step 1 for why this
   campaign matters at all - "shadow maps... a G-buffer... post-processing...
   chained by texture reads" - remains COMPLETELY UNVALIDATED in production.
   **Recommendation (see Phase 7 v2's own new Step 3.7):** immediately after
   Phase 8 lands, before calling the campaign "proven," implement ONE small,
   real, already-in-`TODO.md` follow-on feature with a genuine cross-pass
   read dependency - the Scene-view outline-highlight post-process
   (`TODO.md`, "Editor / Debug UI": "a Scene-view outline highlight... a new
   render pass") is the natural, already-scoped candidate: an outline pass
   that reads the Scene view's own rendered color (or a small silhouette
   mask) and composites an outline on top. This is deliberately NOT added as
   a tenth phase with its own number - it's a small, focused validation
   step, explicitly called out so nobody mistakes "Phases 1-8 shipped" for
   "the investment is proven."
5. **Minor robustness note, not a blocker:** Phase 3's cycle-detection
   `throw std::runtime_error(...)` is exactly correct as a design decision
   (a cycle can only come from the engine's own pass declarations, never
   user data), but because the WHOLE graph is rebuilt and recompiled from
   scratch every single frame (Phase 3/6's own explicit, deliberate design),
   a cycle bug introduced during development would throw on EVERY frame,
   crashing the process repeatedly rather than once. Phase 6/7 v2 add a
   short note recommending `Application::Run()`'s new
   `RenderGraph::Execute()` call sites catch this specific exception, log it
   loudly (including in a release build, via whatever this engine's stderr/
   assert convention already is), and - in a debug build - re-throw/assert
   so it's impossible to miss during development, rather than leaving this
   unaddressed and discovering the failure mode by surprise.

None of the above change the overall nine-phase shape, the phase boundaries,
or the "must-have" list's intent - they tighten specific technical
decisions that the v1 documents left either wrong (item 1), self-
contradictory (item 2), speculative (item 3), or silently unproven (item
4). Everything below this notice is the original Phase 0 content, updated
in place where these five items touch it.

---

## The Grand Campaign: Bringing a Render Graph to GreatTamanaEngine

*"He who knows the terrain and knows himself will not be imperiled in a
hundred campaigns."* This document is the terrain survey. It defines what a
Render Graph must actually accomplish for THIS engine, why the current
architecture cannot grow past three hardcoded passes without one, and how the
whole campaign is broken into nine independently shippable, independently
testable chunks (`RENDERGRAPH_PHASE1_*` through `RENDERGRAPH_PHASE9_*`),
mirroring the exact phased-strategy-doc + completion-report discipline this
codebase already uses for `PROFILER_STRATEGY_v2.md` and
`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`.

Every phase document that follows uses the same five-step structure:

1. **The Goal** - where that one chunk is going, in isolation.
2. **The Situation / The Problem** - what exists today that makes it
   necessary, cited against real files in this repo.
3. **The Plan** - concretely, what gets built, in what order, with what
   signatures, and how it is tested.
4. **What We Will NOT Do** - the scope fence for that chunk specifically.
5. **Their Role** - what a developer picking up that one document needs to
   internalize before touching code.

---

## Step 1: The Goal (Where are we going?)

The engine ships with exactly **three hardcoded GPU passes**, wired by hand
in `Application::Run()` (`src/Application/Application.cpp`): "Game View"
(`RenderOffscreen(gameTarget, GpuTimingSlot::Offscreen0)`), "Scene View"
(`RenderOffscreen(sceneTarget, GpuTimingSlot::Offscreen1)`), and "Present"
(the swapchain, carrying Dear ImGui's chrome). All three funnel through one
shared recording routine, `FrameRecorder::RecordFrame()`
(`src/Renderer/FrameRecorder.cpp`), which hardcodes: exactly one color
attachment, exactly one optional depth attachment, a fixed
`UNDEFINED -> ATTACHMENT_OPTIMAL -> {PRESENT_SRC_KHR |
SHADER_READ_ONLY_OPTIMAL}` barrier sequence, and a single flat draw-item
queue with no notion of "this pass depends on that pass's output."

The goal of this whole campaign is to replace that hand-wired, three-pass,
single-attachment pipeline with a genuine **Render Graph** (a "Frame Graph,"
in Frostbite/Unreal RDG terminology): a system where every GPU pass for a
frame is *declared* - "I read this texture, I write that texture, here is
the code that records my draws" - and the engine itself computes execution
order, culls anything unreferenced, allocates/reuses the physical GPU memory
behind every declared resource, and synthesizes every barrier/layout
transition automatically, correctly, and only where actually needed.

This is the single most valuable rendering-architecture investment available
right now, because **every future rendering feature this engine will ever
want depends on it**: shadow maps (a pass that writes a depth texture another
pass reads), a G-buffer / deferred lighting pipeline (multiple color
attachments feeding a lighting pass), post-processing (bloom, tonemapping,
FXAA - passes chained by texture reads), even something as simple as a
Scene-view outline-highlight post-process already flagged as a deferred
follow-up in `TODO.md` ("Click-to-select via ray casting + a Scene-view
outline highlight... a new render pass"). Every one of these today would
require hand-extending `FrameRecorder`'s single-attachment, single-barrier-
shape assumption over and over, each time getting more fragile. A render
graph solves this class of problem exactly once. **(See V2 Revision Note 4
above: this campaign's eight MVP phases do not themselves prove this out
end-to-end in production - that is a deliberate, explicitly-tracked
follow-up, not an oversight, per Phase 7 v2's new Step 3.7.)**

## Step 2: The Situation / The Problem (Where are we now?)

Read literally, here is what `FrameRecorder`/`FramePresenter`/`Renderer` do
today, and exactly where each one runs out of road:

- **`FrameRecorder::RecordFrame()`** (`src/Renderer/FrameRecorder.cpp`) takes
  exactly one `RenderTarget` (one color image, one optional depth image),
  performs exactly one `UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL` barrier (plus
  an optional matching depth barrier), one `vkCmdBeginRendering`, drains the
  ENTIRE flat `m_drawQueue` in one uninterrupted loop, invokes one
  caller-supplied `recordExtra` overlay callback, `vkCmdEndRendering`s, and
  performs one final barrier to a caller-supplied `finalLayout`. There is no
  way, today, to record a SECOND pass into a SECOND target that consumes the
  first pass's output as a shader-sampled input, without hand-adding a
  second, bespoke `RecordFrame()`-like function.
- **`m_drawQueue` is genuinely flat**: every `Submit()` call lands in the
  same vector regardless of which logical pass it conceptually belongs to.
  `RenderSystem::Draw()` (`src/Game/RenderSystem.cpp`) is called once per
  visible render target precisely BECAUSE `Renderer::BeginFrame()`/
  `FrameRecorder::Clear()`+`RecordFrame()` consumes and clears the WHOLE
  queue as one atomic unit per call - there is no way to tag "these draws
  belong to pass A, those to pass B" and have them scheduled/barrier'd
  independently within the same queue.
- **Barriers are hand-written, not derived from declared resource usage.**
  `FrameRecorder::RecordFrame()`'s `toColorAttachment`/`toDepthAttachment`/
  `toFinal` `VkImageMemoryBarrier2` structs are written assuming exactly two
  images (color + optional depth), exactly one producer (this call) and
  exactly one known-in-advance consumer (either the presentation engine or a
  fragment shader sampling it - selected by the `finalLayout` parameter,
  itself just a bool-shaped enum in practice). Every image's `oldLayout` is
  hardcoded to `VK_IMAGE_LAYOUT_UNDEFINED`, which is only correct because
  every render target this engine has today is fully re-cleared every single
  pass with no cross-pass history. The instant a future pass needs to READ
  another pass's un-cleared output (e.g. a lighting pass sampling a G-buffer
  that a geometry pass just wrote, without re-clearing it), this
  `UNDEFINED`-always assumption breaks, and nothing today tracks "what
  layout is this image currently actually in."
- **GPU resources for a pass's inputs/outputs are all manually
  pre-allocated by the caller and handed in by reference/value.**
  `RenderTexture` objects (`m_gameView`/`m_sceneView` inside
  `ImGuiEditorLayer`) are constructed once and resized on demand
  (`Renderer::CreateRenderTexture()`) - there is no concept of a
  "transient" resource that only needs to exist for the lifetime of a
  handful of passes within one frame and can be pooled/reused/aliased
  across frames the way a real render graph's scratch G-buffer/shadow-map/
  bloom-mip-chain resources are expected to be.
- **Per-pass GPU timing is a fixed, hardcoded 3-slot enum**
  (`GpuTimingSlot::Offscreen0/Offscreen1/SwapchainPresent`, see
  `src/Renderer/GpuTiming.h`/`GpuTimingService.h`), backed by an 8-slot
  `VkQueryPool` whose slot layout is asymmetric between passes: the two
  offscreen slots are read back SYNCHRONOUSLY, right after a blocking fence
  wait in the very same call; the Present slots are frame-in-flight-indexed
  and read back on a LATER frame, once that slot's own fence has separately
  proven complete (see `FramePresenter::Present()`'s
  `ReadPresentResultIfAvailable()` call, and `RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md`'s
  V2 revision notes for why this asymmetry matters to the render graph's
  own design, not just to `GpuTimingService` internally). This was the
  right call for exactly three, permanently-fixed passes; it cannot scale
  to an arbitrary, frame-to-frame-varying set of declared passes without
  becoming its own bottleneck.
- **`DrawStats` accumulation** (`src/Renderer/DrawStats.h`, fused inline
  into `FrameRecorder::RecordFrame()`'s draw loop per AGENTS.md's own
  correctness rule) is per-`RecordFrame()`-call, i.e. per render TARGET,
  not per logical PASS - fine when "one target == one pass" is always true,
  which stops being true the moment two passes render into the SAME target
  in sequence (e.g. an opaque pass followed by a transparent pass into the
  same color+depth attachment).
- **Nothing here is a design mistake** - it is the CORRECT amount of
  machinery for "three fixed passes, one attachment each, no cross-pass
  resource dependency," which is genuinely all this engine has needed so
  far (see `README.md`'s own "Status" - the engine is still "tech demo with
  a great editor"). The problem is purely that every rendering feature on
  the horizon (shadow maps, deferred shading, post-processing, the
  already-flagged outline-highlight pass) needs MORE than this shape can
  give without a real graph underneath it.
- **Two genuinely different submission/synchronization regimes already
  coexist today, and any render-graph execution model must respect both,
  not quietly merge them (see V2 Revision Note 2 above).**
  `FramePresenter::RenderOffscreen()` (Game/Scene) uses its own dedicated
  command buffer and its own dedicated fence, and blocks synchronously
  (`vkWaitForFences(...)`) before returning - simplest-correct, and cheap
  enough at "two off-screen views, once a frame" scale. `FramePresenter::Present()`
  (the swapchain) is deliberately non-blocking/pipelined across
  `kFramesInFlight == 2` frames' worth of command buffers/fences, and can
  independently skip a frame entirely (minimized window, pending resize) -
  see its own `std::optional<DrawStats>` return. These are not
  interchangeable, and a render graph execution model that pretends they
  are (recording both into one shared command buffer/one `Execute()` call)
  would either force Present to become synchronous too (a real frame-pacing
  regression) or need machinery neither v1 phase document actually
  specified.

## Step 3: The Plan (How will we get there?)

Nine independently landable phases, each with its own strategy document,
each fully buildable/testable on its own without breaking anything upstream
of it (the existing `Game`/`RenderSystem`/Editor call sites never change
shape until the very last integration phase, and even then their public
signatures are untouched):

| # | Document | One-line scope |
|---|----------|-----------------|
| 1 | `RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md` | Pure, Vulkan-free handles/enums/descriptors - the graph's vocabulary. |
| 2 | `RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md` | The declarative `AddPass()`/`CreateTexture()`/`ImportTexture()` authoring API. |
| 3 | `RENDERGRAPH_PHASE3_COMPILATION_STRATEGY_v1.md` | Dependency resolution, culling, topological ordering, resource lifetimes - pure algorithm. (Unchanged in this iteration - see V2 Revision Note 5 for a small, non-blocking robustness follow-up owned by Phase 6/7 instead.) |
| 4 | `RENDERGRAPH_PHASE4_PHYSICAL_REALIZATION_STRATEGY_v2.md` | Turning virtual resource handles into real, pooled/reused `RenderTexture`/`Buffer` objects. |
| 5 | `RENDERGRAPH_PHASE5_BARRIER_SYNTHESIS_STRATEGY_v2.md` | Automatic per-resource, per-pass barrier/layout-transition generation, replacing `FrameRecorder`'s hardcoded barriers. MRT narrowed to a single color attachment for the MVP (see V2 Revision Note 3). |
| 6 | `RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md` | `RenderGraph::Compile()`/`Execute()` - tying 1-5 together into real Vulkan recording, with per-pass GPU timing/`DrawStats`. Execute() cardinality/sync-vs-async resolved (see V2 Revision Note 2). |
| 7 | `RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md` | The strangler-fig migration of Game/Scene/Present + ImGui overlay onto real graph passes, with a new Step 3.7 validation follow-up. |
| 8 | `RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md` | A "Render Graph" Editor panel - visualize pass order/culling/resource lifetimes/timing. (Unchanged in this iteration.) |
| 9 | `RENDERGRAPH_PHASE9_ADVANCED_FUTURE_SCOPE_STRATEGY_v2.md` | The explicit backlog: memory aliasing, async compute, compute passes, temporal resources, and (newly) full MRT + the matching `Pipeline` multi-attachment change. |

**Must-have features for the MVP (Phases 1-8), decided now, non-negotiable:**

1. Declarative resource handles (`RgTextureHandle`/`RgBufferHandle`) - cheap,
   generational, index+generation PODs, exactly like `Entity`/
   `GpuResourceHandle`/`MeshHandle` already are in this codebase (see
   AGENTS.md's "Identify resources by handle, never by pointer or string"
   rule, applied a fourth time).
2. Declarative pass registration with a setup/execute split (Frostbite/RDG
   convention): a `Setup` callback declares reads/writes and returns
   immediately; an `Execute` callback (captured for later) does the actual
   `vkCmdDraw`-issuing work, called only once the graph is compiled and only
   if the pass survived culling.
3. Automatic topological ordering of passes from their declared read/write
   dependencies - no more hand-sequencing "Game, then Scene, then Present"
   in `Application::Run()`.
4. Automatic pass culling - a pass whose outputs are provably never read
   (directly or transitively) by anything is skipped, at zero cost to
   whoever authored it.
5. Automatic per-resource, per-pass barrier synthesis - `oldLayout` is
   whatever this resource's ACTUAL last-known layout was (not always
   `UNDEFINED`), covering both images and buffers, for a SINGLE color
   attachment per pass in the MVP (see V2 Revision Note 3 - full MRT is
   deferred to Phase 9, together with the `Pipeline` change it needs).
6. Transient resource pooling/reuse across frames (same `RgTextureDesc` this
   frame as last frame -> the SAME underlying `RenderTexture`/`VkImage`,
   never a fresh `vmaCreateImage` every single frame) - generalizing the
   lazy-depth-buffer-creation precedent already in `FramePresenter.cpp`.
   Pool matching is PURELY STRUCTURAL (width/height/format/hasDepth) - see
   V2 Revision Note 1; a resource's debug name never participates in this
   comparison.
7. Resource IMPORT - wrapping an already-existing, externally-owned
   resource (the swapchain image, the Editor's own long-lived Game/Scene
   `RenderTexture`s) as a graph resource, so existing Editor code is
   untouched, together with that resource's actual current
   `VkImageLayout` (see Phase 2 v2's corrected `ImportTexture()` signature).
8. Integration with the existing Profiler: per-pass GPU timing
   (generalizing `GpuTimingSlot`'s fixed 3-value enum into an
   arbitrary-cardinality, name-keyed table, while preserving the
   synchronous-offscreen-vs-pipelined-present asymmetry - see Phase 6 v2)
   and per-pass `DrawStats`.
9. ZERO signature changes to `Game::Render()`/`RenderSystem::Draw()`/
   `Renderer::Submit()`/every Editor panel - the graph is an internal
   `Renderer`-layer implementation detail end to end, per this codebase's
   own Clean Architecture rule.
10. Tier-1 testability everywhere the underlying problem allows it (Phases
    1, 2, 3, and the pure half of 5 are ALL genuinely Vulkan-free and
    testable with zero live `VkDevice` - only Phase 4's/6's actual resource
    realization/command recording falls into the accepted, uncovered
    "Tier 2" bucket alongside `Buffer`/`RenderTexture`/`Pipeline` itself).
11. A debuggable, inspectable compiled graph (Phase 8) - the same instinct
    that gave this engine a "Memory" panel and a "Profiler" panel applied to
    its own render-pass scheduling.
12. **(New)** A concrete, real, production cross-pass-read validation step
    immediately following Phase 8 - see V2 Revision Note 4 and Phase 7 v2's
    new Step 3.7. Not a numbered phase of its own, but treated as
    non-optional follow-through, not a "someday."

## Step 4: What We Will NOT Do (Focus)

- We will **not** touch `VulkanAllocator`/VMA, `volk`, or dynamic rendering
  itself - the render graph is built strictly ON TOP of the existing
  `GpuResourceFactory`/`RenderTexture`/`Buffer`/`Pipeline` abstractions. It
  is a new ORCHESTRATION layer, not a new GPU-memory or GPU-API layer.
- We will **not** implement memory aliasing (multiple transient resources
  sharing the same physical `VkDeviceMemory` because their lifetimes never
  overlap) in the MVP - genuinely valuable, genuinely complex (interacts
  with VMA's own sub-allocation and this engine's `GpuMemoryTracker`), and
  explicitly deferred to Phase 9.
- We will **not** add multi-queue/async-compute submission - this engine
  submits everything through one graphics queue today (see
  `VulkanDevice::GraphicsQueue()`, used for both graphics AND the one
  present queue distinction that already exists); that stays true through
  the whole MVP. Deferred to Phase 9.
- We will **not** add compute-shader passes in the MVP - there is not a
  single compute shader anywhere in this engine yet. The graph's pass
  abstraction will be DESIGNED to make adding compute passes later a
  natural extension (Phase 9 sketches this), but no compute path ships in
  Phases 1-8.
- We will **not** build a visual node-graph editor/authoring tool. Passes
  are declared in C++, exactly like every other piece of this engine's
  gameplay/rendering code today (see `README.md`'s own "all gameplay is
  hand-authored C++" acknowledgment) - a data-driven/scripted pass
  description format is explicitly out of scope, indefinitely.
- We will **not** change any Editor panel's USER-FACING behavior as a side
  effect of this migration - "Game"/"Scene"/"Present" must look and behave
  identically to a user before and after Phase 7 lands. Any NEW capability
  (e.g. an outline-highlight pass) is a follow-up project that merely
  BENEFITS from the graph existing - it is not itself part of this
  campaign, except for the validation step explicitly called out in Step
  3's item 12/V2 Revision Note 4, whose scope is deliberately tiny (one
  pass, one already-planned feature) and exists purely to prove the
  campaign's investment, not to ship a finished outline-selection feature
  end to end.
- We will **not** retrofit `AssetPreviewMesh`/`BoneViewerWindow`'s own
  bespoke, self-contained Vulkan pipelines (Inspector mesh preview, Bone
  Viewer) onto the graph as part of this campaign - they are legitimate,
  narrow, already-working "external Vulkan-based rendering backend" call
  sites (per AGENTS.md's own "Editor Module Structure" carve-out) and
  migrating them is an optional future cleanup, not a requirement here.
- We will **not** ship full multi-color-attachment (MRT) support in the MVP
  - see V2 Revision Note 3. It moves to Phase 9, to be built together with
  the `Pipeline` multi-format-attachment change it actually requires, the
  day a real G-buffer-shaped pass needs both.
- We will **not** merge the offscreen (Game/Scene) and swapchain (Present)
  submission regimes into a single shared command buffer/single
  `RenderGraph::Execute()` call - see V2 Revision Note 2. Two calls per
  frame, matching today's two real regimes, is a permanent design decision
  for this campaign, not a stepping stone toward unifying them later.

## Step 5: Their Role (What does this mean for you?)

- **Read the phase documents in order, and land them in order.** Phase 2
  depends on Phase 1's types existing; Phase 3 depends on Phase 2's builder
  producing a well-formed intermediate graph to compile; and so on. Do not
  skip ahead to Phase 6 "because it's the interesting part" - the earlier,
  pure-data phases are individually small, individually low-risk, and are
  what make the later phases tractable and testable at all.
- **Every phase document below is self-sufficient** - it restates enough of
  this master document's context that you should never need to re-read
  Phase 0 mid-implementation, but you SHOULD skim this document once,
  fully, before starting Phase 1, so the whole shape of the destination is
  in your head before you lay the first stone.
- **Follow this codebase's existing conventions without exception**: every
  new type lives in `namespace gte`; every resource is identified by a
  cheap generational handle, never a pointer/string; every class that owns
  a GPU/OS resource is RAII; every phase that CAN be pure/Tier-1-testable
  MUST ship with tests in the same change (AGENTS.md, "Testability &
  Regression Safety" - "adding a new branch/case... must add or update the
  corresponding file in `tests/`... never leave a new code path with zero
  coverage"); and every phase gets its own `RENDERGRAPH_PHASEn_COMPLETION_
  REPORT.md` once landed, mirroring `PHASE4A_COMPLETION_REPORT.md` etc.
- **Never let a phase leave the engine in a worse state than it found it.**
  Phases 1-6 add entirely new, additively-compiled files under
  `src/Renderer/RenderGraph/` that NOTHING existing calls yet - the build
  must stay green and every existing test must keep passing after every
  single phase, including all of 1-6, precisely because nothing outside
  `src/Renderer/RenderGraph/` references them until Phase 7. Only Phase 7
  actually rewires `Application::Run()`/`Renderer`/`FramePresenter`, and it
  does so as a single, carefully bracketed cut-over - never a half-migrated
  intermediate state left in the tree between sessions.
- **When in doubt about a design fork, prefer whatever this codebase already
  does elsewhere.** This campaign invents a genuinely new subsystem, but it
  must feel, to someone reading it in six months, like it was always part
  of this engine - not like a foreign library was dropped in. Concretely:
  handles are generational PODs (like `Entity`), pure logic is pulled out
  of Vulkan-touching classes into small testable functions/files (like
  `DrawStats.h`/`GpuTiming.h`), and every public surface documents its
  "why," not just its "what," in the same dense inline-comment style every
  header in this repository already uses.
- **Do not treat "Phase 8 shipped" as "the campaign is proven."** Read
  V2 Revision Note 4 and Phase 7 v2's Step 3.7 before declaring this
  campaign complete - a render graph that has never resolved a real
  cross-pass texture read in shipped code is an unproven abstraction, no
  matter how many unit tests its compiler has.
