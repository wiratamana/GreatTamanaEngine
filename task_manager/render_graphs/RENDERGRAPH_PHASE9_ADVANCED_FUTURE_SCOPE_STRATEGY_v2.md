# RENDERGRAPH_PHASE9_ADVANCED_FUTURE_SCOPE_STRATEGY_v2.md
### (Part 9 of 9 - see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md` for the full campaign)

## V2 Revision Notes (2nd iteration review)

One addition: **a new backlog entry, 3.10, for full multi-color-attachment
(MRT) support**, moved here from Phases 5/6's v1 MVP commitment - see
`RENDERGRAPH_PHASE5_BARRIER_SYNTHESIS_STRATEGY_v2.md`'s own V2 Revision
Note 1 for the full reasoning: no real pass in Phases 1-8 ever needs more
than one color attachment, v1 committed to building MRT plumbing anyway
(a second `RenderTarget`/`RenderTargetSet` shape, multi-attachment barrier
handling) with zero exercised consumer, AND v1 never even mentioned the
`Pipeline.cpp` change (`VkPipelineRenderingCreateInfo::colorAttachmentCount`/
`pColorAttachmentFormats` are hardcoded to exactly one today) that MRT
support actually depends on to be usable at all. This entry captures both
pieces together, to be built the day a real multi-attachment pass (a
G-buffer) needs them.

Everything else in this phase is unchanged from v1.

---

## Step 1: The Goal (Where are we going?)

Unlike Phases 1-8, this document ships **no code**. Its goal is to be the
one, honest, permanent home for every genuinely valuable render-graph
capability that this campaign deliberately did NOT build - written down
now, in enough detail that a future session can pick any one item up
without having to re-derive from scratch why it was deferred, what it
would take, and what (if anything) should happen first. This mirrors
`TODO.md`'s own role for the rest of the engine exactly: "nothing here is a
forgotten bug; each item below was consciously deferred."

## Step 2: The Situation / The Problem (Where are we now?)

Every one of Phases 1-8's "What We Will NOT Do" sections pointed at this
document for a reason - a render graph is a large enough design space that
drawing an honest line around the MVP required repeatedly saying "not yet"
to real, legitimate capabilities. Left unrecorded, each of those "not yet"
decisions risks being either (a) forgotten entirely, so the same design
question gets re-litigated from zero the next time it comes up, or (b)
silently reinvented differently by whoever hits the need next, producing
an inconsistent design relative to the rest of the graph. This document
exists to make sure neither happens.

## Step 3: The Plan (How will we get there?)

Each item below is deliberately written as a self-contained mini-brief -
what it is, why Phases 1-8 didn't include it, what it would take, and what
(if anything) should land first - not as a flat bullet list, so a future
reader can lift any ONE of them out and start from real context.

### 3.1 - Memory aliasing (transient resources sharing physical memory)

**What**: Two (or more) transient resources whose computed lifetimes
(Phase 3's `ResourceLifetime`) never overlap could, in principle, share the
exact same underlying `VkDeviceMemory` allocation, the way Frostbite's/
Unreal RDG's own transient resource systems do, driven by VMA's own
aliasing-aware allocation APIs. **Why deferred**: Phase 4's
`claimedThisFrame` rule already gives correct, safe reuse ACROSS FRAMES
(the same pool entry, the same physical resource, reused frame after
frame) - it simply does not reuse a physical resource for TWO DIFFERENT
LOGICAL RESOURCES within the SAME frame, even when their lifetimes are
provably disjoint. That is a real, measurable memory saving left on the
table (a multi-pass post-process chain with several same-sized scratch
targets is the textbook case this would help most), but it interacts
directly with `GpuMemoryTracker`'s own accounting (AGENTS.md: "the tracked
record must always reflect the CURRENT actual allocation") in a way that
needs its own careful design pass - an aliased resource genuinely has TWO
(or more) logical identities sharing one `GpuResourceHandle`-tracked
allocation, which the tracker's current one-record-per-allocation model
does not represent at all. **What first**: real profiling evidence, from a
real multi-pass feature (once one exists) built on top of the Phase 1-8
MVP, that this is actually costing meaningful memory - do not build this
speculatively.

### 3.2 - Async compute / multi-queue submission

**What**: Submitting independent GPU work (e.g. a compute-shader GPU
skinning pass, once one exists) on a separate compute-capable queue,
running concurrently with graphics-queue work, synchronized via timeline
semaphores instead of a single in-order command-buffer stream. **Why
deferred**: this engine has exactly one queue used for rendering work today
(`VulkanDevice::GraphicsQueue()`) and zero compute shaders anywhere in the
codebase - there is no existing WORK this would even parallelize yet.
Building multi-queue support ahead of a genuine multi-queue WORKLOAD is
speculative complexity with no way to validate it is even correct (nothing
to compare timing against). **What first**: 3.3 (compute passes) landing
and proving useful on the SAME queue first; multi-queue is a scheduling
OPTIMIZATION on top of already-correct single-queue compute support, not a
prerequisite for it.

### 3.3 - Compute passes as first-class graph citizens

**What**: `PassBuilder::ReadBuffer()`/`WriteBuffer()` with
storage-buffer-shaped `ResourceAccess` values (`ShaderReadWrite`), a
`ComputePassContext` (no `VkRenderingInfo`/attachments at all - just
`vkCmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, ...)` +
`vkCmdDispatch`), and Phase 5's barrier planner extended with
`VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT` states. **Why deferred**: no
compute shader exists anywhere in this engine yet (see `TODO.md`'s own "no
GPU skinning... happens anywhere in this engine yet" acknowledgment) - this
campaign's Phase 1 enum (`ResourceAccess`) was deliberately scoped to
exactly what graphics passes need, and extending it is a small, mechanical
addition ONCE a real compute workload exists to design it against (GPU
vertex skinning - replacing today's CPU `VertexSkinning.h`/
`Mesh::UpdateVertexData()` per-frame re-upload path, see `README.md`'s own
animation-runtime section - is the obvious first candidate). **What
first**: identify the first real compute workload, THEN extend Phase 1's
enum/Phase 5's planner together, in one small, focused follow-up phase -
do not extend the enum "just in case" ahead of a real consumer.

### 3.4 - Subpass merging / tile-based-renderer (mobile) optimizations

**What**: On tile-based GPUs (mobile Vulkan implementations), merging
multiple passes that share the exact same attachments into one
`vkCmdBeginRendering`/`vkCmdEndRendering` region (Vulkan's own
`VK_KHR_dynamic_rendering` doesn't have "subpasses" the classic
`VkRenderPass` way, but an equivalent optimization - avoiding a full
attachment store+reload between two passes that could have stayed
resident in tile memory - is still meaningful on those GPUs). **Why
deferred**: this engine targets desktop Vulkan (see `BUILDING.md`'s
prerequisites) with no mobile target on the roadmap at all today - this is
a real optimization for a platform this engine does not currently ship to.
**What first**: a stated requirement to actually target mobile hardware;
revisit this item at that point, not before.

### 3.5 - Temporal/history resources (e.g. for TAA, temporal accumulation)

**What**: A resource that persists ACROSS frames by design (not merely
"happens to be pooled and reused" per Phase 4's incidental reuse, but a
resource whose PREVIOUS frame's content is a DECLARED INPUT to THIS
frame's pass - e.g. a TAA resolve pass reading last frame's accumulated
color buffer). **Why deferred**: Phase 4's pool deliberately treats every
transient resource's content as undefined/fresh at the start of each frame
(Phase 5's synthetic `UNDEFINED` seed state) - there is no "read what I
myself wrote last frame" concept anywhere in Phases 1-8, and no existing
feature in this engine needs one yet (no TAA, no motion blur, no
temporally-accumulated GI). **What first**: `RenderGraphBuilder` would need
a new `ImportTexture`-adjacent concept - "this transient resource's content
should persist frame-to-frame instead of resetting" - designed once a real
temporal-effect feature actually needs it.

### 3.6 - Cross-resource barrier batching

**What**: Combining several resources' barriers that happen to fall at the
exact same point in the pass sequence into one `vkCmdPipelineBarrier2` call
with a multi-entry `pImageMemoryBarriers` array, instead of Phase 5's
current one-call-per-resource-transition granularity. **Why deferred**: a
pure performance micro-optimization with no correctness benefit at all,
explicitly called out in Phase 5's own "What We Will NOT Do." **What
first**: real GPU-timing evidence (via Phase 6/8's own per-pass timing,
ironically making this optimization self-measuring using the very system
it would optimize) that barrier submission overhead is ever actually
significant for this engine's pass COUNT, which is expected to remain small
for a long time yet.

### 3.7 - A stale pooled-resource eviction/trim policy

**What**: Phase 4's resource pool never evicts a stale entry (one whose
`TextureDesc` no longer matches anything any pass declares this session,
e.g. after a one-time Editor-panel resize) - see Phase 4's own Step 3.6/
Step 4 acknowledgment. **Why deferred**: bounded, small, and not yet
observed to matter in practice - the same "revisit if that ever changes"
judgment call `TODO.md` already applies elsewhere in this codebase (e.g.
its own `VkAllocationCallbacks` host-memory-hook entry). **What first**: a
real, observed memory-growth symptom (checked via the "Memory" panel,
Phase 8's own new "Render Graph" panel, or `GetVmaHeapBudgets()`) that
traces back to accumulated stale pool entries specifically.

### 3.8 - Data-driven / scripted pass declaration

**What**: Describing a frame's pass graph from a config file (JSON, a
custom DSL, ...) instead of hand-written C++ `AddPass()` calls, the way a
production engine's shipped render-pipeline-asset system (e.g. Unity's
Scriptable Render Pipeline configuration) might. **Why deferred**: this
engine's entire philosophy today is hand-authored C++ (see `README.md`'s
own "all gameplay is hand-authored C++ entities in `Game.cpp`" - there is
not even a SCENE serialization format yet, per `TODO.md`'s own
highest-priority roadmap item). Building a data-driven pass-authoring
layer ahead of basic scene serialization would be solving a much harder,
lower-priority problem first. **What first**: scene serialization
(`TODO.md`'s own top engine-roadmap item) landing and proving out this
engine's approach to data-driven authoring in general, before applying
that same approach to render passes specifically.

### 3.9 - Widening `Profiling::GpuPass` beyond three fixed named passes

**What**: Letting the Profiler's own data model (`ProfilingTypes.h`,
`FrameGraphData.h`, `ProfilerPanelData.h`, `Panels/ProfilerPanel.cpp`) track
an arbitrary, render-graph-declared set of passes instead of exactly
`GameView`/`SceneView`/`Present`, so a future shadow/post-process pass gets
its own dedicated line in "Profiler" (not just in Phase 8's new "Render
Graph" panel, which already handles an arbitrary pass count from day one).
**Why deferred**: real, separate blast radius across four existing,
already-shipped Profiler files, explicitly flagged as out of scope for this
whole nine-phase campaign in Phase 6's own Step 4. **What first**: Phase
8's "Render Graph" panel shipping and being used for a while - if it turns
out engineers keep wanting a NEW pass's timing to ALSO show up in
"Profiler" specifically (rather than "Render Graph" already showing it
being good enough), that is the signal to invest in widening
`Profiling::GpuPass` for real.

### 3.10 - Full multi-color-attachment (MRT) support, plus the matching
`Pipeline` multi-format-attachment change (new in v2)

**What**: A second `RenderTarget`-adjacent shape (a `RenderTargetSet` -
`std::array<VkImageView, kMaxColorAttachments>` + count, chosen for zero
heap allocation per frame, mirroring this engine's existing
`std::array<VkCommandBuffer, kFramesInFlight>` instinct in `FramePresenter`),
`PassBuilder::WriteColorAttachment()` callable more than once per pass,
Phase 5's barrier planner extended to emit one barrier per declared color
write instead of assuming exactly one, and - the piece v1 of this campaign
never even mentioned needing - a corresponding change to `Pipeline`
(`Pipeline.cpp`'s `VkPipelineRenderingCreateInfo::colorAttachmentCount`/
`pColorAttachmentFormats`, currently hardcoded to `1`/`&colorFormat`) so a
pipeline can actually be built against more than one color attachment
format at once. Without this second piece, MRT support in the graph alone
would be unusable - no `Pipeline` this engine could construct would ever
be valid to bind inside a multi-attachment pass. **Why deferred**: every
real pass this campaign's Phases 1-8 migrate (Game view, Scene view,
Present) uses exactly one color attachment - there is no exercised,
tested, shipped consumer for MRT anywhere in the MVP, and building it
speculatively (as v1 of Phases 5/6 originally proposed) directly
contradicts this campaign's own stated discipline of building only what a
real, demonstrated need justifies. **What first**: an actual G-buffer /
deferred-shading pass (or any other genuinely multi-attachment feature) is
designed and about to be implemented - build this backlog item's BOTH
pieces (the graph-side `RenderTargetSet`/barrier work AND the `Pipeline`
multi-format change) together, in that same effort, rather than landing
either half alone ahead of the other.

## Step 4: What We Will NOT Do (Focus)

- We will **not** assign a phase number or a committed timeline to any item
  above - this document is explicitly a BACKLOG, not a roadmap with dates.
  Nothing here is scheduled; everything here is scoped and ready to be
  scheduled the day a real need for it shows up.
- We will **not** let this document grow into a dumping ground for vague
  ideas with no "why deferred"/"what first" reasoning - every future
  addition to this file must follow the exact same mini-brief shape every
  existing entry above already uses, mirroring `TODO.md`'s own discipline.
- We will **not** treat any item here as blocking Phases 1-8 shipping and
  being considered DONE - the MVP campaign (Phases 1-8, plus Phase 7 v2's
  own Step 3.7 validation follow-up) is complete and useful entirely
  without any of this document's contents; this document's entire purpose
  is to make that boundary explicit and defensible, not to imply the work
  is somehow unfinished without it.

## Step 5: Their Role (What does this mean for you?)

- If you are a future developer (or agent) with a genuine need that
  matches one of the items above, START by re-reading its "why deferred"/
  "what first" reasoning in full, and confirm the "what first" precondition
  has actually been met, before writing any code - if it hasn't, either
  satisfy it first or explicitly, consciously decide (and document, right
  here, updating this entry) why it's being pulled forward anyway.
- If you have a genuine need that does NOT match anything above, that is a
  sign this campaign's Phase 0-8 scope fence had a real gap - add a NEW
  entry here, in this same mini-brief shape, rather than silently building
  around the gap inside one of the earlier phases' own files. Keep this
  document as the single source of truth for "what the render graph does
  not do yet, and why."
- **If you are the one who eventually builds 3.10 (MRT), do not build only
  the graph-side half and leave `Pipeline` hardcoded to one color
  attachment "for later" - the two halves are only useful together, and
  landing one without the other creates a dead, untestable feature exactly
  like the one this v2 revision removed from the MVP in the first place.**
- Above all: resist the urge to treat this list as a checklist to clear out
  for its own sake. Every item here is exactly as valuable as the real
  feature need that eventually justifies it, and exactly that
  premature otherwise - this whole campaign's discipline, from Phase 1
  through Phase 9, has been to build only what is needed, when the need is
  real and demonstrated, never speculatively. Extend that discipline to
  whatever you build on top of it next.
