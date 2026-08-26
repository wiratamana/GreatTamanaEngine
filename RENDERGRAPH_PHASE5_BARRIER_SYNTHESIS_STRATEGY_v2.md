# RENDERGRAPH_PHASE5_BARRIER_SYNTHESIS_STRATEGY_v2.md
### (Part 5 of 9 - see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md` for the full campaign)

## V2 Revision Notes (2nd iteration review)

Three changes from v1:

1. **Full MRT (multi-color-attachment) support is removed from this
   phase's MVP commitment and moved to Phase 9**, alongside a dependency
   v1 never mentioned at all: `Pipeline`'s own
   `VkPipelineRenderingCreateInfo` (`Pipeline.cpp`) hardcodes
   `colorAttachmentCount = 1`/`pColorAttachmentFormats = &colorFormat`
   today - MRT is unusable without ALSO teaching `Pipeline` to build
   against an array of color formats. Every real pass in this campaign's
   Phase 7 migration (Game view, Scene view, Present) uses exactly one
   color attachment, so v1's Step 3.3 (`RenderTargetSet`, a
   `std::array<VkImageView, kMaxColorAttachments>`, `colorAttachmentCount`
   threaded through from however many `WriteColorAttachment()` calls a
   pass made) was real, non-trivial surface area built for zero exercised,
   tested, shipped consumer - directly contradicting this campaign's own
   "build only what is needed, when the need is real and demonstrated"
   discipline (see `RENDERGRAPH_PHASE9_ADVANCED_FUTURE_SCOPE_STRATEGY_v2.md`'s
   own closing words). This phase now ships against exactly ONE color
   attachment (plus the existing optional depth attachment) for the MVP,
   matching literally every real pass in Phases 1-8, and Phase 9 is where
   MRT is built for real, together with the `Pipeline` change it actually
   needs, once a genuine multi-attachment pass (a G-buffer) is being
   implemented.
2. **Viewport/scissor state ownership is now explicitly assigned to
   `RenderGraph`'s execution harness (Phase 6), not left to each pass
   author to remember.** v1 never mentioned who calls
   `vkCmdSetViewport`/`vkCmdSetScissor` for a pass's own attachment extent
   under the new model - today, `FrameRecorder::RecordFrame()` sets both
   exactly once, right after `vkCmdBeginRendering`, sized to `target.extent`
   (see `FrameRecorder.cpp`). This phase's barrier/attachment-info work is
   the natural place to specify that the SAME must happen per pass in the
   new model - see Step 3.3 below, and Phase 6 v2 for the execution-harness
   side of this.
3. **`FrameRecorder::RecordFrame()`'s existing debug-build format-matching
   asserts must have an equivalent in the new system, not just matching
   barrier field VALUES.** `RecordFrame()` asserts `target.format ==
   expectedFormat`/`target.depthFormat == expectedDepthFormat` before ever
   touching a barrier (see `FrameRecorder.cpp`, and AGENTS.md's "Render
   Target Format Matching") - this is a real, load-bearing safety net (a
   pipeline built for one format bound against a target of a different
   format is invalid per the Vulkan spec and can silently misrender). v1's
   own regression test (Step 3.4's last bullet) checks that BARRIER FIELDS
   match `FrameRecorder.cpp` exactly, but never mentions preserving this
   format-mismatch ASSERTION itself. Added to this phase's plan below.

Everything else in this phase is unchanged from v1.

---

## Step 1: The Goal (Where are we going?)

Replace `FrameRecorder::RecordFrame()`'s hand-written, fixed-shape barrier
code (exactly one color image, one optional depth image, `oldLayout`
always `VK_IMAGE_LAYOUT_UNDEFINED`, `finalLayout` always one of exactly two
hardcoded choices) with a small, DATA-DRIVEN barrier planner: given a
resource's PREVIOUS known state (layout, access mask, pipeline stage) and
its NEXT declared `ResourceAccess` (Phase 1), produce the exact
`VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2` needed to transition
between them - correctly, for an arbitrary number of passes touching an
arbitrary number of resources in an arbitrary order, not just the three
fixed passes this engine has today, **for a single color attachment plus an
optional depth attachment per pass in the MVP (see V2 Revision Note 1 -
full MRT is Phase 9 scope)**. Crucially, split this into a PURE decision
half (Tier-1-testable: "given these two states, what barrier fields are
needed") and a THIN Vulkan-call half (Tier-2, simply issuing whatever the
pure half decided) - the exact same split this campaign's own Phase 1
established for `ResourceAccess`, and the exact same split `GpuTiming.h`
(pure) / `GpuTimingService.cpp` (Vulkan calls) already proved out
successfully in this same codebase.

## Step 2: The Situation / The Problem (Where are we now?)

`FrameRecorder::RecordFrame()` (`src/Renderer/FrameRecorder.cpp`) hardcodes
three barrier-relevant assumptions that are only true because there are
only three fixed passes today:

1. **Every image always starts this pass at `VK_IMAGE_LAYOUT_UNDEFINED`.**
   True today because every render target is always fully cleared
   (`loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR`) with no cross-pass history ever
   preserved. The MOMENT a pass needs to READ another pass's un-cleared
   output (a G-buffer feeding a lighting pass, one pass's bloom output
   feeding the next mip level), `oldLayout` must be whatever that
   resource's ACTUAL last layout was left in - `UNDEFINED` would legally
   permit the driver to discard the image's contents, corrupting the read.
2. **Exactly one color attachment, one optional depth attachment.** No
   support for multiple render targets (MRT) at all today - and, per V2
   Revision Note 1, this phase deliberately keeps it that way for the MVP,
   rather than building unused MRT plumbing now. A future G-buffer pass
   (needing, say, albedo + normal + material color attachments
   simultaneously) is exactly the trigger that should bring MRT support
   back into scope, built together with the `Pipeline` change it needs -
   see Phase 9.
3. **The final barrier's destination is always one of exactly two
   hardcoded choices** (`VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` or
   `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`), selected by a `finalLayout`
   parameter that is really just "is this Present or RenderOffscreen,"
   never derived from what the NEXT pass that reads this resource actually
   declared it needs.

This phase removes assumptions 1 and 3 by making every barrier a COMPUTED
FUNCTION of `(previous ResourceState, next ResourceAccess)`, tracked
per-resource across the WHOLE compiled pass list, rather than re-derived
from scratch, wrongly-assumed-`UNDEFINED`, on every single call - while
deliberately leaving assumption 2 (single color attachment) unchanged for
the MVP, per V2 Revision Note 1.

## Step 3: The Plan (How will we get there?)

### 3.1 - New file: `src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h/.cpp`

**Pure half** (`RenderGraphBarrierPlanner.h`, Vulkan-ENUM-typed but
Vulkan-CALL-free, same bucket as `RenderGraphTypes.h` from Phase 1):

```cpp
struct ResourceState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; // buffers use a dummy/ignored value here
    VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    VkAccessFlags2 accessMask = VK_ACCESS_2_NONE;
};

// Pure decision: what state does `access` require a resource to be in?
// A straightforward, EXHAUSTIVE switch over ResourceAccess (Phase 1) - no
// default: case, so a future Phase-9 enumerator forces every call site to
// be revisited, mirroring IsWriteAccess()'s own rule from Phase 1.
ResourceState RequiredStateFor(ResourceAccess access, bool isDepthResource) noexcept;

// Pure decision: is a barrier even NEEDED between these two states? (A
// resource read by two consecutive ShaderRead passes with identical
// layout/stage/access needs no barrier at all between them - just a
// well-known Vulkan optimization this planner must not skip.)
bool RequiresBarrier(const ResourceState& previous, const ResourceState& next) noexcept;
```

**Thin Vulkan-call half** (`RenderGraphBarrierPlanner.cpp`'s
`EmitImageBarrier(VkCommandBuffer, VkImage, subresource, previous, next)`/
`EmitBufferBarrier(...)`) does nothing but populate a
`VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2` from the two
already-decided `ResourceState`s and call `vkCmdPipelineBarrier2` - it
contains no decision logic of its own, mirroring `GpuTimingService`'s own
`RecordOffscreenPassStart()` being "thin" once `ResolveGpuTimingStatus()`
(the pure decision) already exists.

### 3.2 - Per-resource state tracking across the whole compiled pass list

Phase 6's execution engine (not this phase) OWNS a
`std::vector<ResourceState> m_currentTextureStates` (parallel to Phase 3's
`textureLifetimes`, one entry per virtual `TextureHandle`), seeded at the
start of a frame from either: (a) an imported resource's caller-supplied
initial state (Phase 2's `ImportTexture()` - now an explicit, required
`currentLayout` parameter, see Phase 2 v2 - e.g. a freshly-acquired
swapchain image starts at `VK_IMAGE_LAYOUT_UNDEFINED`, a Game-view
`RenderTexture` left over from last frame starts at
`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, matching EXACTLY what
`FrameRecorder::RecordFrame()`'s final barrier already left it in - THIS
phase's design finally makes that fact an explicit, tracked piece of
state instead of an implicit assumption nothing verifies), or (b) a
freshly-pooled transient resource's synthetic starting state,
`{VK_IMAGE_LAYOUT_UNDEFINED, TOP_OF_PIPE, NONE}` (a transient resource has
no meaningful prior content by construction - its very first use this
frame is necessarily also its first-ever use since being claimed from the
pool, or since creation). As `RenderGraphCompiler`'s `executionOrder` is
walked pass by pass, EVERY declared read/write for that pass calls
`RequiredStateFor()` to get its target state, calls `RequiresBarrier()`
against the tracked "current" state, emits a barrier only if needed, and
then OVERWRITES the tracked current state with the new one - a plain,
sequential state machine, one entry per resource, walked once per frame.

### 3.3 - Single color attachment for the MVP, plus per-pass viewport/scissor

`RenderTarget` (`src/Renderer/RenderTarget.h`) is left EXACTLY as it is
today - unchanged - for the MVP (see V2 Revision Note 1: MRT and the
matching `Pipeline` change it needs are both Phase 9 scope, built together
the day a real consumer needs them). Every pass in Phases 1-8 declares AT
MOST one `WriteColorAttachment()` call and at most one
`WriteDepthStencilAttachment()` call - `RenderGraphCompiler`'s Step 3.4
tests (Phase 3) already implicitly assume this, and Phase 6's `PassContext`
resolves a pass's writes directly into a single `RenderTarget`, exactly the
same shape `FrameRecorder::RecordFrame()` already consumes today. **v2,
new: right after `vkCmdBeginRendering` for a given pass, `RenderGraph`'s
execution harness (Phase 6) must call `vkCmdSetViewport`/`vkCmdSetScissor`
sized to that pass's own resolved color attachment's `extent` - exactly
mirroring `FrameRecorder::RecordFrame()`'s own existing behavior (set once,
right after `vkCmdBeginRendering`, from `target.extent`) - BEFORE invoking
that pass's `execute` callback, so a pass author's `execute` lambda can
issue `vkCmdDraw`/`vkCmdBindPipeline` calls immediately without needing to
remember to set viewport/scissor itself first. This phase's own barrier-
planner code doesn't implement this (it belongs to Phase 6's execution
loop), but it is called out here because it is a direct consequence of
retiring `FrameRecorder::RecordFrame()`'s own combined
begin-rendering-and-viewport-setup block, and v1 never assigned this
responsibility to anyone.**

### 3.4 - Tests: `tests/Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp`

Entirely Tier-1 (pure `ResourceState`/`ResourceAccess` values, zero
`VkCommandBuffer`, zero live device):

- `RequiredStateFor(ColorAttachmentWrite, false)` ->
  `{COLOR_ATTACHMENT_OPTIMAL, COLOR_ATTACHMENT_OUTPUT, COLOR_ATTACHMENT_WRITE}`.
- `RequiredStateFor(DepthStencilAttachmentReadWrite, true)` -> the
  depth/early+late-fragment-test triple, matching
  `FrameRecorder.cpp`'s existing `toDepthAttachment` barrier fields exactly
  (this is a REGRESSION-SAFETY test: the new, general code must reproduce
  the OLD, hand-written code's exact behavior for the cases the old code
  already handled correctly).
- `RequiredStateFor(ShaderRead, false)` ->
  `{SHADER_READ_ONLY_OPTIMAL, FRAGMENT_SHADER, SHADER_READ}`.
- `RequiresBarrier()` returns `false` for two IDENTICAL states (the
  "back-to-back reads need no barrier between them" optimization,
  explicitly pinned by its own test) and `true` for any states differing in
  layout, in stage, or in access mask alone.
- A hand-simulated three-pass sequence (A writes color -> B reads as
  shader-read -> C writes color again, e.g. a ping-pong) walked through the
  state-tracking logic by hand in the test (not needing the full compiler/
  executor to exist) confirms exactly the expected NUMBER of barriers is
  emitted (2 - one after A, one after B - not 3, not 1) and each one's
  `srcAccessMask`/`dstAccessMask` matches expectations.
- A resource whose declared access sequence exactly matches
  `FrameRecorder.cpp`'s existing `Present()`/`RenderOffscreen()` shapes
  (`UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR` /
  `... -> SHADER_READ_ONLY_OPTIMAL`) produces barriers with FIELD-FOR-FIELD
  identical `srcStageMask`/`dstStageMask`/`srcAccessMask`/`dstAccessMask`/
  `oldLayout`/`newLayout` values to what `FrameRecorder.cpp` hardcodes today
  - this is the single most important regression test in this whole phase,
  since it is the proof that migrating to the new system (Phase 7) cannot
  silently change rendering behavior for the three passes that already
  work correctly today.
- **v2, new: a test (or an explicit, documented manual-verification note if
  it genuinely can't be Tier-1) confirming the new system preserves
  `FrameRecorder::RecordFrame()`'s existing debug-build format-matching
  assertion behavior** - i.e. that Phase 6's execution harness still fails
  loudly (assert/throw in debug builds) if a resolved `RenderTarget`'s
  actual format doesn't match whatever a bound `Pipeline` was built
  against, rather than only checking that barrier VALUES are correct when
  formats already happen to agree. See AGENTS.md, "Render Target Format
  Matching" - this discipline must not be quietly dropped just because the
  barrier-emission code moved to a new file.

## Step 4: What We Will NOT Do (Focus)

- We will **not** implement any FLUSH/split-barrier optimization (batching
  multiple resources' barriers into one `vkCmdPipelineBarrier2` call with a
  combined `pImageMemoryBarriers` array spanning several resources at once)
  in the MVP - one `vkCmdPipelineBarrier2` call per resource-transition is
  simpler, correct, and exactly matches `FrameRecorder.cpp`'s own existing
  granularity (which already batches the color+depth barrier for ONE pass
  into one call, and this phase preserves that specific batching - see 3.4's
  regression test - without generalizing batching ACROSS passes/resources
  any further). Cross-resource barrier batching is a pure performance
  micro-optimization, explicitly deferred (Phase 9) until profiling ever
  shows it matters.
- We will **not** add queue-family-ownership-transfer barriers (`
  srcQueueFamilyIndex`/`dstQueueFamilyIndex` beyond the existing
  `VK_QUEUE_FAMILY_IGNORED` this engine already uses everywhere) - there is
  only one queue family actually used for rendering work today (see
  `VulkanDevice::GraphicsQueueFamily()`), and multi-queue support is
  explicitly Phase 9 scope.
- We will **not** support buffer barriers with anything beyond the
  `ResourceAccess` values Phase 1 already defined (no storage-buffer
  read-write access) - buffers in the MVP are exclusively vertex/index/
  uniform-shaped, matching this engine's existing `Buffer`/
  `BufferMemoryUsage` scope; a compute pass's storage-buffer access pattern
  is Phase 9 scope.
- We will **not** change `RenderTarget`'s EXISTING single-attachment shape
  in this phase at all, in either direction - no new `RenderTargetSet`
  type, no `std::array<VkImageView, kMaxColorAttachments>`, nothing. See V2
  Revision Note 1: this whole MRT surface moves to Phase 9, to be built
  alongside the `Pipeline` multi-format-attachment change it actually
  requires, once a real consumer exists. Building it now, unused, would be
  exactly the kind of speculative complexity this campaign explicitly
  disavows elsewhere (Phase 9's own closing words).

## Step 5: Their Role (What does this mean for you?)

- If you are implementing this phase, the single test you must get
  bit-for-bit correct before considering this phase done is the
  regression test described in 3.4's next-to-last bullet - open
  `FrameRecorder.cpp` side by side with your new code and confirm every
  single barrier field matches, not just "looks plausible." A subtly wrong
  `srcAccessMask` here is exactly the kind of bug that produces a
  driver-dependent, hard-to-repro visual corruption weeks later, not a
  clean crash today - treat this test with the seriousness AGENTS.md's own
  "Render Target Format Matching" section already asks for around format
  mismatches. **The format-matching ASSERTION itself (3.4's last bullet)
  is just as important - don't let the barrier-field regression test's
  precision distract from also preserving this simpler, cheaper, earlier
  line of defense.**
- Do not be tempted to make `RequiredStateFor()`/`RequiresBarrier()` take
  a `Renderer&` or read anything about the actual device "just to be
  safe" - if you find yourself wanting live-device information inside
  these two functions, that is a sign the information belongs in
  `ResourceState`/`ResourceAccess` themselves (Phase 1), passed in as
  plain data, not fetched by this phase's own code.
- When Phase 6 wires this planner into real execution, and later when
  Phase 7 migrates real passes onto it, keep watching the Editor's
  validation-layer output (`kEnableValidation` is already `true` in every
  non-`NDEBUG` build - see `Renderer.cpp`'s anonymous namespace) for any
  new synchronization-validation warning - that is this engine's existing,
  already-wired-up safety net for exactly the class of bug this phase is
  most likely to introduce if a state gets tracked incorrectly.
- If a real need for MRT shows up before this campaign's Phase 9 backlog
  is ever revisited (e.g. someone starts a G-buffer pass mid-campaign),
  do not quietly bolt MRT support onto this phase's file as a "small
  addition" - come back and amend THIS document (and Phase 9's backlog
  entry) explicitly, including the matching `Pipeline` change, so the
  scope-fence stays honest and reviewable, exactly as Phase 1's own "Their
  Role" section already asks for any similar temptation.
