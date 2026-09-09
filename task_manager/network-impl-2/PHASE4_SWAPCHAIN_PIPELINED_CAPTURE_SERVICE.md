# PHASE4 — Swapchain Pipelined Capture Service

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 1 (encoding
utilities), Phase 2 (`FrameCaptureBridge`), Phase 3 (proves the overall
plumbing end-to-end against the easy synchronous case first). This is the
highest-risk, most novel phase in the whole campaign - read
`PHASE0_MASTER_STRATEGY.md`'s Step 2 findings on the pipelined Present
regime again before starting.

> **Middleman review note (see `PHASE4_MIDDLEMAN_REVIEW_REPORT.md`, same
> folder):** this document was independently re-verified line-by-line
> against the actual, current source tree before this revision. Every
> uncertain/"confirm before assuming" hedge in the original draft has been
> resolved to a concrete, verified fact below. Two real corrections were
> made (both flagged inline where they occur): (1) the GPU-timing precedent
> this phase mirrors is NOT actually exercised anywhere in production code
> today - it is a fully-implemented but dead code path, so this phase's own
> `SwapchainCaptureService` will be the FIRST real, exercised consumer of
> this exact pattern, not a follow-on to a battle-tested one; (2) an
> additional, previously-missing Vulkan buffer barrier (host-read
> visibility) is now spelled out in Step 3.2 - without it, the CPU-side read
> of the readback buffer's mapped memory is not actually guaranteed correct
> by the Vulkan spec, even though the per-slot fence wait alone "usually
> works in practice" on common drivers.

## Step 1: The Goal (Where are we going?)

Add the ability to read back the REAL swapchain image's pixels - the one
going through `FramePresenter`'s pipelined, multi-frame-in-flight Present
regime - with **zero added GPU stall**, by piggy-backing on synchronization
the engine already performs for an unrelated, pre-existing reason (the same
discipline `AGENTS.md`'s own GPU-timestamp-queries section locks in for
`GpuTimingService`: *"no new GPU wait was ever added anywhere purely to
fetch a timing result sooner"* - this phase applies the identical principle
to pixel data instead of timing data). This phase produces raw pixel data
only - the `FrameCaptureBridge`/PNG-encoding/HTTP wiring on top of it is
Phase 5's job (reusing Phase 3's already-proven pattern almost verbatim).

## Step 2: The Situation (Where are we now?)

Re-stating the key facts `PHASE0_MASTER_STRATEGY.md` already established,
since this phase depends on every one of them being exactly right - each
one below has now been independently re-derived directly from the live
source files (not merely re-quoted from Phase 0), and is reported as either
CONFIRMED or CORRECTED:

1. **CONFIRMED, with a sharper root cause.** A render-graph pass that only
   READS resources (declares zero entries in `PassRecord::writes`) is
   *provably, structurally* never kept by `RenderGraphCompiler::Compile()`
   - not merely "in every case anyone tried," but by construction. Tracing
   `Compile()` directly (`src/Renderer/RenderGraph/RenderGraphCompiler.cpp`):
   the "kept" set is seeded ONLY from passes whose `writes` list contains a
   texture present in the caller's `finalOutputs`, and is otherwise grown
   ONLY by walking `edgeExists[predecessor][node]` backwards from an
   already-kept node. An edge only ever originates FROM a pass index that
   is the recorded "last writer" of some resource (`lastTextureWriter`/
   `lastBufferWriter`, both only ever updated inside the `pass.writes`
   loop) - so a pass with an empty `writes` list can *never* be the "from"
   end of any edge, meaning it can never be reached by the backward walk
   either. A read-only pass is therefore unreachable through BOTH of
   `Compile()`'s only two ways to become "kept" - this is a structural
   guarantee of the current algorithm, not an incidental behavior. **This
   phase does NOT add a new render-graph pass for the capture** - it hooks
   in manually, at the exact same graph-external seam `FramePresenter.cpp`
   already uses for the swapchain image's own final `PRESENT_SRC_KHR`
   transition.
   - **One important nuance the original Phase 0/4 drafts omitted:** a
     render-graph-native path is NOT actually impossible in every form -
     `RenderGraphBuilder::ImportBuffer(name, VkBuffer, size)` already exists
     (`RenderGraphBuilder.h`) precisely for wrapping an externally-owned
     buffer (like a per-slot readback `Buffer`) into the graph each frame,
     and a pass could force itself to be "kept" by *also* declaring a
     `WriteTexture(swapchainHandle, ResourceAccess::TransferSrc)` against
     the swapchain handle (which IS already in `finalOutputs` today, since
     the existing "Present" pass writes it) - `Compile()`'s root-detection
     loop does not validate that a declared `write` access is a *true*
     write (`IsWriteAccess()` is never consulted there), so this would, in
     fact, survive culling. **This phase deliberately still does NOT do
     this**, for two independent reasons worth recording explicitly (rather
     than just asserting "no render-graph way exists," which is not quite
     true): (a) it depends on an unvalidated, undocumented quirk of
     `Compile()`'s root-detection loop - a future, entirely reasonable
     tightening of that loop to check `IsWriteAccess(usage.access)` before
     counting a "write" as a real write would silently break it with no
     compile error; (b) even inside a render-graph pass, the render graph's
     own `ResourceAccess` vocabulary has no "make visible to a HOST read"
     concept at all (see point 5 below, new) - the pass's own `execute`
     callback would still have to hand-emit that exact barrier itself, so
     going through the render graph buys no real barrier-authoring savings
     here, only fragility. The manual, graph-external approach below is
     therefore still the right call - just for a more precise, defensible
     reason than "there is no other way."
2. **CONFIRMED, with exact current line numbers.** The existing manual seam
   lives in `FramePresenter::PresentViaRenderGraph()` (`FramePresenter.cpp`),
   right after `graph.Execute(...)` returns and right before
   `vkEndCommandBuffer(cmd)` - as of this review, that is the block at
   (0-indexed) lines 425-432, with `graph.Execute(...)` ending at line 413
   and `vkEndCommandBuffer(cmd)` at line 434 (re-read the live file before
   editing - Phases 1-3 changes elsewhere don't touch this file at all, so
   these should still be exact, but always re-verify). Today it
   unconditionally assumes the swapchain image's current state is
   `RequiredStateFor(ResourceAccess::ColorAttachmentWrite, false)` (the
   state the "Present" render-graph pass itself left it in) and transitions
   straight to `PRESENT_SRC_KHR`. **This phase inserts an optional extra
   step BETWEEN those two** - if a capture was requested this call, copy the
   image to a readback buffer FIRST (transitioning
   `ColorAttachmentWrite -> TransferSrcOptimal`), and then the existing
   finalize block's own "previous state" must become `TransferSrcOptimal`
   instead of `ColorAttachmentWrite` for that one call only.
3. **CONFIRMED, with an important correction about which precedent is
   actually "live."** The one place this whole pipelined regime already
   blocks on a specific frame-in-flight slot's PRIOR use being fully
   GPU-complete is `vkWaitForFences(m_device, 1, &fence, ...)` where
   `fence = m_frameSync.InFlightFence(m_currentFrame)`, near the TOP of
   `FramePresenter::PresentViaRenderGraph()` - as of this review, at
   (0-indexed) lines 355-356, BEFORE `vkAcquireNextImageKHR`. By the time
   this call returns, whatever GPU work was submitted the LAST time
   `m_currentFrame` had this same value (`kFramesInFlight == 2` frames ago)
   is guaranteed complete - including any readback copy this phase recorded
   into that slot's buffer back then. This is the exact point to safely
   READ a previous capture's results with zero extra wait.
   - **Correction:** the original draft (and `PHASE0_MASTER_STRATEGY.md`)
     describe this as "mirroring `GpuTimingService::
     ReadPresentResultIfAvailable()`'s own established pattern for GPU
     timestamps." `GpuTimingService::RecordPresentPassStart()`/
     `RecordPresentPassEnd()`/`ReadPresentResultIfAvailable()`/
     `MarkPresentSlotWritten()` are indeed fully implemented, correct, and
     a genuinely good design template to copy the SHAPE of - but a direct
     grep across `src/` confirms `GpuTimingSlot::SwapchainPresent` is never
     passed as an argument anywhere in production code, and none of those
     four methods are called from `FramePresenter.cpp` or `Renderer.cpp`
     (only `RecordOffscreenPassStart`/`RecordOffscreenPassEnd`/
     `ReadOffscreenResultNow` - the OFFSCREEN trio - are actually called,
     from `FramePresenter::RenderOffscreen()`). **These four Present-path
     methods are dead code today** - the ACTUAL, currently-live mechanism
     that gives the pipelined Present path its own "read one round later"
     GPU timing today is a *different, newer, more general* system:
     `RenderGraph::ExecuteCompiledGraph()`'s own
     `m_pipelinedFrameCounter % kGpuTimingFramesInFlight` preamble, backed
     by `RenderGraphTimestampPool` (see `RenderGraph.cpp`, the "pipelined-
     regime GPU timing readback PREAMBLE" comment) - it was evidently
     built to supersede `GpuTimingService`'s original Present-specific API
     once render-graph passes gained their own per-pass-name timing, and
     nothing ever removed the now-unused old API from `GpuTimingService`.
     **Practical consequence for this phase:** `SwapchainCaptureService`'s
     own `RecordCaptureIfRequested()`/`TryTakeCompletedCapture()` pairing
     will be the FIRST real, exercised production consumer of the
     "read-one-round-later, keyed by frame-in-flight index, right after the
     existing per-slot fence wait" pattern applied directly inside
     `FramePresenter.cpp` itself. Treat it with the extra care that implies
     (thorough manual testing per Step 3.5 - there is no already-proven-in-
     the-wild call site in THIS file to lean on if something about the
     timing assumption turns out subtly wrong). The frame-in-flight
     bookkeeping reasoning in point 4 below was independently re-derived
     directly from `FramePresenter.cpp` itself for exactly this reason, not
     borrowed from `GpuTimingService`.
4. **CONFIRMED.** `m_currentFrame` itself (an index in `[0, kFramesInFlight)`,
   `kFramesInFlight == 2`) is advanced at exactly one place,
   `m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;`, at
   (0-indexed) line 472 - the LAST statement before `return true;`, i.e.
   strictly AFTER `vkQueuePresentKHR` and after both hook points this phase
   uses (the fence-wait read point near the top, and the post-
   `graph.Execute()` write point in the middle). A captured pixel result
   therefore belongs to whatever `m_currentFrame` value was CURRENT at the
   moment the copy was recorded, and must be looked up/consumed using that
   SAME value the next time it comes back around - which, by construction,
   is exactly what `m_currentFrame` still equals when the read runs (the
   read always happens before this call's own advance).
5. **CONFIRMED.** `Buffer` (`src/Renderer/Buffer.h`) has no `Resize()` -
   unlike `RenderTexture`. A window resize (`FramePresenter::OnResize()` ->
   `RecreateSwapchain()`) changes swapchain extent, so this phase's own
   per-frame-in-flight readback buffers must be DESTROYED AND RECONSTRUCTED
   (not resized in place) whenever the swapchain itself is recreated -
   mirror `m_depthBuffers`' own "rebuilt alongside the swapchain in
   `RecreateSwapchain()`, but `RecreateSwapchain()` itself only rebuilds them
   if they already existed" lazy-rebuild precedent (`FramePresenter.h`'s own
   `m_depthBuffers` doc comment) exactly. `RecreateSwapchain()`'s real body
   (as of this review): early-returns if minimized; otherwise
   `vkDeviceWaitIdle`, `m_swapchain.Recreate(...)`,
   `m_frameSync.RecreateRenderFinishedSemaphores(...)`, then conditionally
   `CreateDepthBuffers()` if `!m_depthBuffers.empty()`, then clears
   `m_resizeRequested`. This phase's `NotifySwapchainRecreated()` call must
   be added inside this same function, after the early-minimized-return
   (so it never fires for a resize that never actually happened), e.g.
   right after `m_frameSync.RecreateRenderFinishedSemaphores(...)`.
6. **NEW (not in the original draft) - a real Vulkan synchronization gap.**
   `vkCmdCopyImageToBuffer` writes GPU memory; the per-slot
   `vkWaitForFences` (point 3) guarantees that write has finished
   EXECUTING, but the Vulkan spec's memory-visibility rules do not
   automatically make a device write visible to a subsequent HOST read
   through a fence wait alone - visibility to the host domain requires a
   memory dependency whose destination access/stage explicitly includes
   `VK_ACCESS_2_HOST_READ_BIT` / `VK_PIPELINE_STAGE_2_HOST_BIT`. Nothing in
   this engine has ever needed this before (`PHASE0_MASTER_STRATEGY.md`'s
   own observation that "nothing currently constructs a `GpuToCpu` buffer
   anywhere in the engine" means there is no existing precedent to copy
   for this specific step - it must be added fresh here). See Step 3.2 for
   the exact fix (an extra `rg::EmitBufferBarrier()` call, using the
   already-existing, already-pure `RenderGraphBarrierPlanner.h` buffer-
   barrier half - no new machinery needed, just one more call).
7. **CONFIRMED.** `Buffer`'s constructor
   (`Buffer(VmaAllocator, std::shared_ptr<GpuMemoryTracker>, VkDeviceSize,
   VkBufferUsageFlags, BufferMemoryUsage, const char*)`) is fully public and
   directly constructible with nothing beyond those five/six primitive-ish
   arguments - verified in `Buffer.h`/`Buffer.cpp`. `Renderer::CreateBuffer()`
   is confirmed to be a thin, one-line pass-through wrapper around exactly
   this constructor - **note the wrapper actually lives in
   `GpuResourceFactory::CreateBuffer()` (`GpuResourceFactory.cpp`, not
   `Renderer.cpp` as the original draft guessed)**: `Renderer::CreateBuffer()`
   itself (`Renderer.cpp`) just forwards one level deeper to
   `m_resources.CreateBuffer(...)`. This confirms `SwapchainCaptureService`
   can construct a `Buffer` directly, with zero dependency on `Renderer`/
   `GpuResourceFactory`, exactly as planned below.
8. **CONFIRMED.** The header declaring `RequiredStateFor()`/
   `EmitImageBarrier()`/`ResourceState` (and, needed fresh by this phase,
   `EmitBufferBarrier()`) is `src/Renderer/RenderGraph/
   RenderGraphBarrierPlanner.h`, namespace `gte::rg`. `ResourceState`'s
   exact field order is `{ VkImageLayout layout; VkPipelineStageFlags2
   stageMask; VkAccessFlags2 accessMask; }` (aggregate-initializable in
   that order, as already used in Step 3.3 below). Both `RequiredStateFor()`
   and `EmitImageBarrier()`/`EmitBufferBarrier()` are plain, free functions
   in `gte::rg` with no dependency on any live `RenderGraph`/
   `RenderGraphBuilder` instance - `EmitImageBarrier()`/`EmitBufferBarrier()`
   only need a `VkCommandBuffer` plus plain POD arguments, and are safe to
   call from a hand-written call site exactly like `FramePresenter.cpp`'s
   existing manual finalize block already does for the image case.
9. **CONFIRMED.** `FramePresenter`'s real, current constructor
   (`FramePresenter.h`) is:
   `FramePresenter(VkPhysicalDevice, VkDevice, VkSurfaceKHR, std::uint32_t
   graphicsQueueFamily, std::uint32_t presentQueueFamily, VkQueue
   graphicsQueue, VkQueue presentQueue, int width, int height, VmaAllocator
   allocator, VkFormat depthFormat, std::shared_ptr<GpuMemoryTracker>
   memoryTracker, std::shared_ptr<GpuTimingService> gpuTiming)`, and it
   already stores both `m_allocator` (`VmaAllocator`) and `m_memoryTracker`
   (`std::shared_ptr<GpuMemoryTracker>`) as private members for its own
   `CreateDepthBuffers()` use. This means `FramePresenter`'s own
   constructor body can construct its new `SwapchainCaptureService` member
   by simply passing `m_allocator`, `m_memoryTracker`, and
   `FramesInFlight()` straight through - no new constructor parameter, no
   plumbing gap, exactly as planned in Step 3.3 below.

## Step 3: The Plan

### 3.1 - `src/Renderer/SwapchainCaptureService.h/.cpp`

A new class, structurally modeled directly on `GpuTimingService` (same
file's neighbor, same "FramePresenter only ever calls INTO this, never
issues the raw Vulkan calls itself" division of labor) - see Step 2, point
3's correction above for why this is a *design-shape* precedent to copy,
not a "this exact pattern is already proven working for Present" claim.

```cpp
// SwapchainCaptureService.h
#pragma once
#include <volk.h>
#include "Buffer.h"
#include "Memory/GpuMemoryTracker.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace gte {

struct CapturedSwapchainPixels {
    std::vector<std::uint8_t> pixels; // tightly packed, width*height*4 bytes
    int width = 0;
    int height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

// Owns a small, fixed (kFramesInFlight-sized) pool of host-visible readback
// Buffers, and the actual vkCmdCopyImageToBuffer/barrier call sites for
// capturing the real swapchain image - mirrors GpuTimingService's own
// per-frame-in-flight design SHAPE (see that class's own header comment),
// applied to pixel data instead of GPU timestamps - NOTE: unlike
// GpuTimingService's Offscreen trio, its Present-specific trio is NOT
// actually exercised by any production code today (see PHASE4's own Step 2,
// point 3) - this class is the first real, exercised consumer of this
// pattern for the pipelined Present regime. FramePresenter only ever calls
// INTO this class, at the two specific points documented on each method
// below - it never issues vkCmdCopyImageToBuffer/vkCmdPipelineBarrier2
// itself for this purpose.
//
// Buffers are allocated LAZILY - only once RequestCapture() is called for
// the very first time (see PHASE0_MASTER_STRATEGY.md's own Locked Design
// Decision #7) - and destroyed/recreated (never resized in place, Buffer
// has no Resize()) whenever the swapchain's own extent changes (see
// NotifySwapchainRecreated() below).
//
// Does NOT own the VmaAllocator/VkDevice passed in - both must outlive this
// object, same convention as every other Vulkan/*-adjacent class here.
class SwapchainCaptureService {
public:
    SwapchainCaptureService(VmaAllocator allocator, std::shared_ptr<GpuMemoryTracker> tracker,
        std::uint32_t framesInFlight);
    ~SwapchainCaptureService() = default;

    SwapchainCaptureService(const SwapchainCaptureService&) = delete;
    SwapchainCaptureService& operator=(const SwapchainCaptureService&) = delete;

    // Called from Application::Run() (indirectly, via Renderer - see 3.3
    // below) when FrameCaptureBridge::IsCaptureRequested(Swapchain) is
    // true. A safe no-op if a capture is ALREADY pending (this service only
    // ever tracks ONE in-flight request at a time - FrameCaptureBridge's own
    // "already pending -> 503, never queued" rule, Phase 2, is what
    // guarantees RequestCapture() is never called again before the
    // previous request has been fully resolved).
    void RequestCapture();

    // Called from FramePresenter::PresentViaRenderGraph(), ONLY right after
    // graph.Execute() returns and BEFORE the existing manual PRESENT_SRC_KHR
    // finalize block (see PHASE4's own Step 2, point 2). If a capture is
    // currently requested, (re)creates this frame-in-flight slot's readback
    // buffer if needed (matching `extent`), records the
    // ColorAttachmentWrite -> TransferSrcOptimal barrier + vkCmdCopyImageToBuffer
    // + the buffer's own transfer-write -> host-read visibility barrier
    // (see Step 3.2, point 4 - REQUIRED, not optional), and marks this slot
    // "captured, pending read". Returns true if it recorded anything
    // (meaning the caller's own subsequent finalize step must transition
    // FROM TransferSrcOptimal, not ColorAttachmentWrite) - false otherwise
    // (nothing recorded, caller's existing behavior is unchanged).
    bool RecordCaptureIfRequested(
        VkCommandBuffer cmd, VkImage swapchainImage, VkExtent2D extent, VkFormat format, std::uint32_t frameInFlightIndex);

    // Called from FramePresenter::PresentViaRenderGraph(), ONLY right after
    // its own vkWaitForFences() call for `frameInFlightIndex` returns (see
    // PHASE4's own Step 2, point 3) - i.e. BEFORE this same slot's buffer
    // might be reused/destroyed by a resize this same call. If this slot
    // has a completed pending capture, copies it out of the buffer's mapped
    // memory into a plain std::vector and returns it (clearing the
    // pending flag) - std::nullopt if nothing was pending for this slot.
    std::optional<CapturedSwapchainPixels> TryTakeCompletedCapture(std::uint32_t frameInFlightIndex);

    // Called from FramePresenter::RecreateSwapchain() whenever the
    // swapchain is actually recreated (a real resize, not merely a resize
    // REQUEST that's still pending due to a minimized window) - destroys
    // every existing readback buffer (they're the wrong size now) and
    // discards ANY currently-pending capture (there is no safe way to
    // finish a copy whose source image no longer exists at that size/
    // identity) - the corresponding FrameCaptureBridge request, if any,
    // will simply time out and the caller can retry; this is an accepted,
    // rare edge case (a resize racing an in-flight screenshot request),
    // not one this service tries to paper over with a synthetic
    // "just-resized, please retry immediately" fast-fail path.
    void NotifySwapchainRecreated();

private:
    struct Slot {
        std::optional<Buffer> readbackBuffer;
        bool pendingCapture = false;
        int capturedWidth = 0;
        int capturedHeight = 0;
        VkFormat capturedFormat = VK_FORMAT_UNDEFINED;
    };

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::shared_ptr<GpuMemoryTracker> m_memoryTracker;
    bool m_captureRequested = false;
    std::vector<Slot> m_slots; // sized to framesInFlight
};

} // namespace gte
```

### 3.2 - Implementation notes (`SwapchainCaptureService.cpp`)

Must `#include "RenderGraph/RenderGraphBarrierPlanner.h"` (for
`rg::RequiredStateFor`/`rg::EmitImageBarrier`/`rg::EmitBufferBarrier`/
`rg::ResourceState`) - confirmed the right header in Step 2, point 8.

- `RequestCapture()`: `if (m_captureRequested) return; m_captureRequested = true;`
  (Idle/no-op if already requested - defensive, matches the class comment.)
- `RecordCaptureIfRequested()`:
  1. `if (!m_captureRequested) return false;`
  2. `Slot& slot = m_slots[frameInFlightIndex];`
  3. `const VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * 4;`
     If `!slot.readbackBuffer.has_value() || slot.readbackBuffer->Size() !=
     size`, destroy the old one (if any - just `slot.readbackBuffer.reset()`)
     and construct a fresh one: `Buffer(m_allocator, m_memoryTracker, size,
     VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferMemoryUsage::GpuToCpu,
     "SwapchainCaptureReadback")` - confirmed a directly, publicly
     constructible `Buffer` (Step 2, point 7), no `Renderer&`/
     `GpuResourceFactory&` needed. Note: in practice, by the time this
     branch's size-mismatch case could ever trigger, `NotifySwapchainRecreated()`
     will already have reset `slot.readbackBuffer` to `std::nullopt` for
     every slot (see below) - the size-mismatch check is retained purely as
     cheap defensive belt-and-braces, not the primary rebuild path.
  4. Record, in order, into `cmd`:
     - An image barrier from `rg::RequiredStateFor(rg::ResourceAccess::
       ColorAttachmentWrite, false)` to a `TransferSrcOptimal` state
       (`{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
       VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT }`) on
       `swapchainImage`'s color subresource, via `rg::EmitImageBarrier()` -
       the "previous" state is `ColorAttachmentWrite` (not `ShaderRead` -
       the swapchain image is coming out of the "Present" pass's own
       color-attachment write, not a sampled texture), mirroring `PHASE3`'s
       `Renderer::CaptureRenderTexturePixels()` but with this one
       previous-state difference.
     - `VkBufferImageCopy region{}; region.imageSubresource = {
       VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; region.imageExtent = {
       extent.width, extent.height, 1 };` then
       `vkCmdCopyImageToBuffer(cmd, swapchainImage,
       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.readbackBuffer->Native(),
       1, &region);`
     - **REQUIRED (Step 2, point 6 - new in this revision): a buffer memory
       barrier making the copy's write visible to a later HOST read.**
       `vkCmdCopyImageToBuffer` finishing execution (guaranteed by the
       per-slot fence wait in `TryTakeCompletedCapture()`'s caller) is NOT,
       by itself, a Vulkan-spec guarantee that the CPU can safely read
       `slot.readbackBuffer->MappedData()` afterwards - a memory dependency
       whose destination access/stage includes `VK_ACCESS_2_HOST_READ_BIT`/
       `VK_PIPELINE_STAGE_2_HOST_BIT` is required to make a device write
       visible to the host domain. Immediately after the
       `vkCmdCopyImageToBuffer` call above:
       ```cpp
       rg::EmitBufferBarrier(cmd, slot.readbackBuffer->Native(), 0, size,
           rg::ResourceState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT },
           rg::ResourceState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT });
       ```
       (`layout` is ignored for buffer barriers - see `ResourceState`'s own
       doc comment in `RenderGraphBarrierPlanner.h` - `VK_IMAGE_LAYOUT_UNDEFINED`
       is simply the required placeholder value.) There is NO "transition
       back" step for the image afterward - the caller's own subsequent
       manual finalize block (see 3.3) is what takes the image the rest of
       the way to `PRESENT_SRC_KHR`.
  5. `slot.pendingCapture = true; slot.capturedWidth =
     extent.width; slot.capturedHeight = extent.height; slot.capturedFormat
     = format; m_captureRequested = false;` (clear the REQUEST flag - the
     copy is now IN FLIGHT, not "still wanted"). Return `true`.
- `TryTakeCompletedCapture()`:
  1. `Slot& slot = m_slots[frameInFlightIndex]; if (!slot.pendingCapture)
     return std::nullopt;`
  2. `slot.pendingCapture = false;`
  3. Build a `CapturedSwapchainPixels` by `memcpy`-ing
     `slot.capturedWidth * slot.capturedHeight * 4` bytes out of
     `slot.readbackBuffer->MappedData()`, using the CAPTURED
     width/height/format (not whatever the swapchain's CURRENT extent
     happens to be right now, in case of an intervening resize - though see
     `NotifySwapchainRecreated()` below for why that specific race is
     actually foreclosed rather than silently mishandled: any resize between
     recording and reading already cleared `pendingCapture` for every slot,
     so this function would have returned `std::nullopt` in step 1 instead).
  4. Return it.
- `NotifySwapchainRecreated()`: for every slot, `slot.readbackBuffer.reset();
  slot.pendingCapture = false;`. Also clear `m_captureRequested = false;` -
  a resize-in-progress capture request is simply dropped (see the class's
  own doc comment above); the network thread's own fixed timeout in
  `FrameCaptureBridge` (Phase 2) is what ultimately surfaces this to the
  caller as a timeout, without this service needing its own separate
  "failed" signal path back to `Application`.

### 3.3 - `FramePresenter` integration

- `FramePresenter` gains a new member,
  `std::unique_ptr<SwapchainCaptureService> m_swapchainCapture;` (or a plain
  value member if `SwapchainCaptureService` doesn't need Pimpl - it has no
  httplib/forward-declare requirement, so a plain value member is simpler;
  construct it in `FramePresenter`'s own constructor, passing `m_allocator`/
  `m_memoryTracker`/`FramesInFlight()` - all three are already available as
  either existing private members or a `static constexpr` method, confirmed
  in Step 2, point 9, so this needs no new constructor parameter).
- `FramePresenter` gains two new public methods that simply forward:
  `void RequestSwapchainCapture();` and
  `std::optional<CapturedSwapchainPixels> TakeLastCompletedSwapchainCapture();`
  - the SECOND of these does NOT call `m_swapchainCapture->
  TryTakeCompletedCapture()` directly (that needs a specific
  `frameInFlightIndex` argument tied to a specific moment in
  `PresentViaRenderGraph()`'s own execution, not "whatever `m_currentFrame`
  happens to be when some caller outside this class asks") - instead, add a
  member `std::optional<CapturedSwapchainPixels> m_lastCompletedCapture;`,
  populated ONLY from inside `PresentViaRenderGraph()` itself (see below),
  and `TakeLastCompletedSwapchainCapture()` just moves it out
  (`std::exchange(m_lastCompletedCapture, std::nullopt)`) - this is what
  lets `Application::Run()` retrieve a same-frame result via a simple,
  argument-free call AFTER `Renderer::PresentViaRenderGraph()` returns,
  without needing to know anything about frame-in-flight indices itself.
- Inside `PresentViaRenderGraph()`:
  - Right after the EXISTING `vkWaitForFences(m_device, 1, &fence, ...)`
    call (0-indexed lines 355-356, per Step 2 point 3): `m_lastCompletedCapture =
    m_swapchainCapture->TryTakeCompletedCapture(m_currentFrame);` (BEFORE
    `RecreateSwapchain()` could ever be called later in this same function
    for a DIFFERENT reason - an acquire-time `VK_ERROR_OUT_OF_DATE_KHR`,
    line 364 - reading the completed capture first, before any chance of
    this same call recreating the swapchain out from under it, is correct.
    Note there is also an EARLIER possible `RecreateSwapchain()` call, at
    the very top of the function, line 349, if `m_resizeRequested` was
    already set from a PRIOR call - this one runs BEFORE the fence wait, so
    it is not a race either way: if it fires, it already clears
    `pendingCapture` for every slot via `NotifySwapchainRecreated()` before
    `TryTakeCompletedCapture()` is even reached, so the read simply, safely,
    correctly returns `std::nullopt` for a slot whose in-flight capture was
    just invalidated by the resize.).
  - Right after `graph.Execute(...)` returns (Step 2, point 2 above), BEFORE
    the existing manual finalize block:
    ```cpp
    const bool capturedThisFrame = m_swapchainCapture->RecordCaptureIfRequested(
        cmd, target.image, target.extent, target.format, m_currentFrame);
    ```
  - Change the existing manual finalize block's `previous` state
    computation from the current hardcoded
    `rg::RequiredStateFor(rg::ResourceAccess::ColorAttachmentWrite, false)`
    to:
    ```cpp
    const rg::ResourceState previous = capturedThisFrame
        ? rg::ResourceState{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT }
        : rg::RequiredStateFor(rg::ResourceAccess::ColorAttachmentWrite, false);
    ```
    (`rg::ResourceState`'s confirmed field order is `{ layout, stageMask,
    accessMask }` - Step 2, point 8 - matching the literal above exactly.)
  - `RecreateSwapchain()` must ALSO call
    `m_swapchainCapture->NotifySwapchainRecreated();` - add this call right
    after `m_frameSync.RecreateRenderFinishedSemaphores(m_swapchain.ImageCount());`
    (after the early minimized-window return, so it only fires for a real
    recreate - see Step 2, point 5's exact function body walk-through).
- `Renderer` gains two matching pass-through public methods,
  `RequestSwapchainCapture()` and `TakeLastCompletedSwapchainCapture()`,
  simply forwarding to `m_presenter`'s own two methods above - the same
  "thin façade, every public method forwards to whichever collaborator
  actually implements it" shape `Renderer.h`'s own class comment already
  documents for everything else.

### 3.4 - What this phase deliberately does NOT wire up yet

- `Application::Run()` calling `RequestSwapchainCapture()`/
  `TakeLastCompletedSwapchainCapture()`, encoding the result to PNG, and
  feeding `FrameCaptureBridge` - that, plus the actual `GET /get_swapchain`
  route, is Phase 5. This phase's own deliverable is complete once
  `SwapchainCaptureService` + its `FramePresenter`/`Renderer` plumbing
  compiles and is provably correct in isolation (see Tests below) - Phase 5
  is then a small, mechanical wiring phase mirroring Phase 3's own
  `Application::Run()` pattern almost verbatim.

### 3.5 - Tests

`SwapchainCaptureService` is Tier 2 (needs a real `VmaAllocator`/`VkDevice`
to construct a real `Buffer`) - same accepted "no automated coverage yet"
bucket `Buffer`/`RenderTexture`/`Pipeline` already sit in (see `AGENTS.md`'s
"Testability & Regression Safety"). This phase's own verification is
therefore primarily MANUAL, but still do the following:

- Extract the pure "does this frame-in-flight index's slot need a
  buffer resize" size-comparison logic (step 3.2, point 3's `size`
  mismatch check) into a small, standalone, Tier-1-testable free function if
  it ends up non-trivial enough to be worth isolating (e.g. if it needs to
  account for more than a simple `!=` comparison) - otherwise it's fine
  inline, per this codebase's own judgment calls elsewhere (`AspectRatioOf()`
  in `Application.cpp` is inline despite technically being "pure logic",
  since it's trivial).
- Manual verification: build, run `GreatTamanaEngine.exe`, and confirm (via
  a temporary, throwaway debug call - e.g. a keybind or an existing debug
  panel button wired to `RequestSwapchainCapture()` + dumping
  `TakeLastCompletedSwapchainCapture()`'s pixels to a raw `.ppm`/`.bmp` file
  via a five-line throwaway helper, DELETED again before this phase is
  considered done) that a requested capture actually arrives with sane,
  non-garbage pixel data, TWO real frames later (proving the "read one
  round of frames-in-flight later" plumbing genuinely works before Phase 5
  builds the full HTTP-facing feature on top of it - this is doubly
  important given Step 2 point 3's finding that no other production call
  site already proves this exact pattern for the Present path). Also
  manually verify resizing the window WHILE a capture is pending doesn't
  crash/hang (per `NotifySwapchainRecreated()`'s own accepted "the request
  simply never completes, no crash" contract), and specifically verify the
  captured PNG is NOT garbage/all-zero/torn (the concrete, observable
  symptom that would show up if the Step 2 point 6 host-visibility barrier
  were ever accidentally dropped - on most current desktop GPU drivers this
  bug would likely still "happen to work" due to coherent memory types and
  incidental ordering, making it exactly the kind of latent, driver-
  dependent bug that manual testing on the actual target hardware, not just
  "it built," must positively rule out).
