# PHASE4 Middleman Review Report

Scope: **PHASE4 only** (`PHASE4_SWAPCHAIN_PIPELINED_CAPTURE_SERVICE.md`).
PHASE0/1/2/3/5/6 were read for context (PHASE0 in full) but are explicitly
out of scope for correction here - a separate review pass covers them.
This is a documentation-only review; no engine source under `src/` was
modified, and no code was implemented.

## Method

Read `PHASE0_MASTER_STRATEGY.md` in full, then `PHASE4_SWAPCHAIN_PIPELINED_
CAPTURE_SERVICE.md` in full. Then, independently of both documents' own
quoted line numbers/snippets, opened and traced the actual current source:

- `src/Renderer/FramePresenter.h` / `.cpp` (full read)
- `src/Renderer/RenderGraph/RenderGraphCompiler.cpp` (full read, traced the
  root-seeding + backward-reachability algorithm by hand)
- `src/Renderer/RenderGraph/RenderGraphTypes.h` (full read)
- `src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h` (full read)
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` (full read, to check for
  a legitimate render-graph-native alternative)
- `src/Renderer/RenderGraph/RenderGraph.cpp`/`.h` (targeted read, present-
  path GPU timing preamble)
- `src/Renderer/Buffer.h` / `.cpp` (full read)
- `src/Renderer/GpuTimingService.h` / `.cpp` (full read)
- `src/Renderer/Renderer.h` / `.cpp` (targeted read: constructor, `CreateBuffer`,
  `ImmediateSubmit`, `FramePresenter` construction site)
- `src/Renderer/GpuResourceFactory.cpp` (targeted read: `CreateBuffer`)
- `src/Renderer/RenderTarget.h` (full read)
- `src/Renderer/Vulkan/VulkanFrameSync.h` (targeted read: `InFlightFence`)
- Repo-wide greps for `RequiredStateFor`, `CaptureRenderTexturePixels`,
  `RecordPresentPassStart|RecordPresentPassEnd|ReadPresentResultIfAvailable|
  MarkPresentSlotWritten`, `GpuTimingSlot::SwapchainPresent`, `m_gpuTiming`,
  `CreateBuffer`, `ImmediateSubmit`.

## Verdict

PHASE4 was **fundamentally sound and its core architectural decision
(manual, graph-external barrier+copy, no new render-graph pass) is
correct** - but the original draft contained several hedged/uncertain
claims that needed independent resolution, one materially misleading
claim about precedent, and one genuine, previously-unaddressed Vulkan
correctness gap. `PHASE4_SWAPCHAIN_PIPELINED_CAPTURE_SERVICE.md` was
overwritten in place with a corrected, more concrete revision (same
Goal/Situation/Plan structure, same or greater level of detail - every
"confirm before assuming" hedge was replaced with the actual, now-verified
fact).

## Confirmed-accurate claims (no correction needed)

1. **`vkWaitForFences` location.** Confirmed at the top of
   `FramePresenter::PresentViaRenderGraph()`, waiting on
   `m_frameSync.InFlightFence(m_currentFrame)`, before
   `vkAcquireNextImageKHR` - matches the original draft's "~lines 355-356"
   almost exactly (verified 0-indexed lines 355-356 in the live file).
2. **Manual `PRESENT_SRC_KHR` finalize block location.** Confirmed right
   after `graph.Execute(...)` returns and right before
   `vkEndCommandBuffer(cmd)` - verified at 0-indexed lines 425-432 (`graph.
   Execute` ends at 413, `vkEndCommandBuffer` at 434).
3. **`m_currentFrame` advancement.** Confirmed to happen at exactly one
   place, `m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;`, as the
   last statement before `return true;` (0-indexed line 472) - strictly
   AFTER both hook points this phase uses. The frame-in-flight bookkeeping
   reasoning in the original draft's Step 2 point 4 is correct.
4. **Render-graph culling of pure-read passes.** Independently traced
   `RenderGraphCompiler::Compile()`'s actual algorithm by hand (not merely
   re-confirmed by inspection): the "kept" set is seeded only from passes
   whose `writes` include a `finalOutputs` texture, and grown only by
   walking `edgeExists[predecessor][node]` backwards - and an edge can only
   ever originate from a pass that appears in some resource's `writes` list
   (`lastTextureWriter`/`lastBufferWriter` are only ever updated inside the
   `writes` loop). A pass with an empty `writes` list can therefore be
   reached by neither of `Compile()`'s two "become kept" mechanisms. This is
   a **structural** guarantee, confirmed definitively, not an incidental
   behavior someone merely observed once. PHASE4's decision to avoid a new
   render-graph pass for this reason is justified.
5. **`RenderGraphBarrierPlanner.h`** (not `RenderGraphTypes.h` or any other
   file) is the header declaring `RequiredStateFor()`, `EmitImageBarrier()`,
   `EmitBufferBarrier()`, and `struct ResourceState { VkImageLayout layout;
   VkPipelineStageFlags2 stageMask; VkAccessFlags2 accessMask; }` (this
   exact field order). All are plain free functions in `gte::rg` with zero
   dependency on a live `RenderGraph`/`RenderGraphBuilder` instance -
   confirmed safe to call from a hand-written call site, exactly as PHASE4
   assumed. The original draft's own hedge ("confirm `rg::ResourceState`'s
   exact field names/order... before writing this literal - do not guess
   the struct shape") is now resolved; the corrected doc states the
   confirmed order directly.
6. **`Buffer`'s public constructor.** Confirmed fully public and directly
   constructible with `(VmaAllocator, std::shared_ptr<GpuMemoryTracker>,
   VkDeviceSize, VkBufferUsageFlags, BufferMemoryUsage, const char*)` -
   nothing Renderer/GpuResourceFactory-private about it. `Buffer` has no
   `Resize()` method at all, confirming the "destroy and reconstruct on
   resize" requirement in the original draft's Step 2 point 5.
7. **`FramePresenter`'s real constructor** already receives and stores both
   `VmaAllocator` (`m_allocator`) and `std::shared_ptr<GpuMemoryTracker>`
   (`m_memoryTracker`) as private members - confirming the plan to
   construct the new `SwapchainCaptureService` member directly from
   `FramePresenter`'s own constructor body needs no new constructor
   parameter and no plumbing gap.
8. **`RenderTarget`'s fields** (`image`, `imageView`, `extent`, `format`,
   plus depth counterparts) match exactly what PHASE4's `RecordCaptureIfRequested(cmd,
   target.image, target.extent, target.format, m_currentFrame)` call site
   assumes.

## Corrected claims

### 1. GpuTimingService's "Present" precedent is dead code, not a proven pattern

**Before:** PHASE0/PHASE4 both describe the swapchain capture's
"read-one-round-later, keyed by frame-in-flight index, right after the
existing per-slot fence wait" design as "mirroring `GpuTimingService`'s
existing Present-timing pattern" / "`GpuTimingService::
ReadPresentResultIfAvailable()`'s own established pattern for GPU
timestamps... the precedent to imitate, not reinvent" - strongly implying
this exact mechanism is already proven, working, production code for the
Present path today.

**After (verified):** `GpuTimingService::RecordPresentPassStart()`/
`RecordPresentPassEnd()`/`ReadPresentResultIfAvailable()`/
`MarkPresentSlotWritten()` are fully implemented and correct, but a
repo-wide grep confirms **`GpuTimingSlot::SwapchainPresent` is never passed
as an argument anywhere in production code**, and none of those four
methods are called from `FramePresenter.cpp` or `Renderer.cpp` (only the
OFFSCREEN trio - `RecordOffscreenPassStart`/`RecordOffscreenPassEnd`/
`ReadOffscreenResultNow` - is actually called, from
`FramePresenter::RenderOffscreen()`). These four Present-path methods are
dead code. The mechanism that ACTUALLY drives the pipelined Present path's
own GPU timing today is a different, newer, more general system:
`RenderGraph::ExecuteCompiledGraph()`'s own `m_pipelinedFrameCounter %
kGpuTimingFramesInFlight` preamble, backed by `RenderGraphTimestampPool` -
apparently built to supersede `GpuTimingService`'s original Present-specific
API once render-graph passes gained their own per-pass-name GPU timing,
with the old API simply never removed.

**Why this matters:** it's still fine to copy the *shape* of
`GpuTimingService`'s Present trio (this is a reasonable, well-organized
design template) - but the implementer needs to know that
`SwapchainCaptureService` will be the **first real, exercised production
consumer** of "read one round later, right after the per-slot fence wait,
called directly from inside `FramePresenter.cpp`" - there is no already-
battle-tested call site in this exact file to lean on if some subtle timing
assumption turns out wrong. The corrected PHASE4 document now states this
explicitly and calls for extra care during Step 3.5's manual verification
specifically because of this.

### 2. The render-graph-native alternative was dismissed slightly too absolutely

**Before:** PHASE0/PHASE4 state a pass-based approach "does NOT work here"
and would require "widening `RenderGraphCompiler::Compile()`'s/
`RenderGraph::Execute()`'s root-detection to understand buffer-only
outputs - real, invasive, cross-cutting surgery" - reading as "there is no
render-graph-native way to do this at all."

**After (verified):** the literal claim (a pure-read-only pass is always
culled) is 100% correct (see confirmed-accurate item 4 above). However,
`RenderGraphBuilder::ImportBuffer(name, VkBuffer, size)` **already exists**
specifically for wrapping an externally-owned buffer (exactly what a
per-slot readback `Buffer` is) into the graph each frame, and
`RenderGraphCompiler::Compile()`'s root-detection loop does **not**
validate that a declared `write` access is a genuine write
(`IsWriteAccess()` is never consulted there) - so a pass could force itself
to survive culling by *also* declaring a semantically-bogus
`WriteTexture(swapchainHandle, ResourceAccess::TransferSrc)` against the
swapchain handle (which is already a `finalOutputs` root today, since the
existing "Present" pass writes it). This would, in fact, work mechanically,
with zero core `RenderGraphCompiler`/`RenderGraph` changes.

**Why PHASE4's ultimate decision is still correct, now for a sharper
reason:** this workaround (a) relies on an unvalidated/undocumented quirk
of the culling loop that a future, entirely reasonable tightening (checking
`IsWriteAccess()` before counting a declared write as a real write) would
silently break with no compile error, and (b) the render graph's
`ResourceAccess` vocabulary has no "make visible to a HOST read" concept at
all (see correction 3 below) - so a render-graph pass's own `execute`
callback would still have to hand-emit that exact barrier itself, meaning
the render-graph route buys no real barrier-authoring savings here, only
fragility. The corrected document keeps PHASE4's original decision but now
states this precise, defensible reasoning instead of an over-broad "no
other way exists" claim.

### 3. A missing Vulkan barrier: host-read visibility for the readback buffer

**Before:** PHASE4's plan recorded an image barrier
(`ColorAttachmentWrite -> TransferSrcOptimal`) and the
`vkCmdCopyImageToBuffer` call, then relied on the per-slot
`vkWaitForFences` (two frames later) as the sole synchronization before the
CPU reads `slot.readbackBuffer->MappedData()`. No buffer-side barrier was
mentioned anywhere in the plan.

**After (a genuine correctness gap, now fixed):** a fence wait alone
guarantees the GPU work has finished *executing*, but the Vulkan
specification's memory-visibility rules require an explicit memory
dependency whose destination access/stage includes
`VK_ACCESS_2_HOST_READ_BIT` / `VK_PIPELINE_STAGE_2_HOST_BIT` before a
device write is guaranteed visible to a subsequent host (CPU) read. This
engine has no existing precedent for this specific step (`PHASE0`'s own
observation that nothing currently constructs a `GpuToCpu` buffer anywhere
in the engine means there is genuinely no prior call site to copy). The
corrected PHASE4 document adds one extra `rg::EmitBufferBarrier()` call
(using the already-existing, already-pure buffer-barrier half of
`RenderGraphBarrierPlanner.h` - no new machinery needed) immediately after
`vkCmdCopyImageToBuffer`, transitioning the readback buffer from
`{TRANSFER, TRANSFER_WRITE}` to `{HOST, HOST_READ}`. This is exactly the
kind of "structurally incomplete Vulkan barrier/synchronization logic" the
review was tasked to look for - without it, the code would very likely
still *appear* to work correctly during manual testing on most current
desktop GPU drivers (coherent host-visible memory types and incidental
ordering commonly paper over the missing barrier in practice), making it a
latent, driver-dependent bug rather than an immediately-obvious one. The
corrected Step 3.5 (Tests) now explicitly calls out "verify the PNG is not
garbage/torn" as the concrete symptom to watch for if this barrier were
ever accidentally dropped in a future edit.

### 4. Minor: the `Buffer`/`Renderer::CreateBuffer` wrapper location

**Before:** "`Renderer::CreateBuffer()` is only a convenience wrapper
around the exact same constructor - confirm this in `Renderer.cpp` before
assuming it."

**After:** confirmed true, but the one-line pass-through wrapper
(`return Buffer(m_allocator, m_memoryTracker, size, usage, memoryUsage,
debugName);`) actually lives in `GpuResourceFactory::CreateBuffer()`
(`GpuResourceFactory.cpp`) - `Renderer::CreateBuffer()` itself
(`Renderer.cpp`) just forwards one level deeper to
`m_resources.CreateBuffer(...)`. Cosmetic, but corrected for precision
since the original explicitly asked the implementer to go look in
`Renderer.cpp` for it.

## Real remaining risks flagged for whoever implements PHASE4

- **No production precedent for this exact pattern in `FramePresenter.cpp`
  itself** (see correction 1) - budget real manual-testing time specifically
  for the "capture arrives correct, two real frames later" verification;
  do not assume it "must work" just because `GpuTimingService`'s
  Present-oriented methods exist somewhere in the codebase.
- **The host-read visibility barrier (correction 3) is easy to
  accidentally omit or accidentally delete in a future refactor** since its
  absence will very likely not cause an obvious crash or validation-layer
  error on common desktop drivers/memory types - it is the single most
  "quietly wrong" piece of this whole phase. Whoever implements this should
  specifically pull up the Vulkan validation layers' synchronization
  validation feature (`VK_LAYER_KHRONOS_validation` with
  `synchronization2`/"best practices" enabled) while manually testing, since
  standard validation does not universally catch missing host-visibility
  dependencies.
- **A resize racing an in-flight capture request is an accepted, silent
  timeout**, not a fast-failed error (per `NotifySwapchainRecreated()`'s own
  documented contract) - this is a deliberate, reasonable design choice, not
  a gap, but is worth the implementer double-checking still matches product
  expectations before Phase 5 ships a public HTTP behavior on top of it (a
  caller hitting `/get_swapchain` during a live window resize will simply
  see a `504` after ~3 seconds, with no special-cased message distinguishing
  this from "capture subsystem is just slow").
- **The render-graph-native alternative (correction 2) remains available
  in principle** if a future maintainer ever needs a similar
  externally-owned-buffer-write capability elsewhere and considers
  generalizing `RenderGraphCompiler::Compile()`'s root-detection to
  genuinely support buffer-only final outputs (rather than the
  bogus-texture-write workaround) - that would be legitimate, real
  "invasive, cross-cutting surgery" on a shared module, exactly as PHASE0
  originally described, and remains correctly out of scope for this
  campaign.
