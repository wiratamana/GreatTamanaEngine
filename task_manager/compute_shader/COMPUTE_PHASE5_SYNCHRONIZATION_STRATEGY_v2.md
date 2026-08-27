# COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md

### Child document 5 of 7 — see `COMPUTE_SHADER_MASTER_STRATEGY_v2.md` for the full campaign map.
### Corresponds to the user's requested **"Module 3: Synchronization"**.

> **v2 (2nd-iteration review):** this document's Step 1-5 body is IDENTICAL to
> `COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v1.md`. New material was appended
> as **Step 6** below, after re-reading this plan directly against the real,
> currently-shipped `RenderGraphCompiler.cpp/.h` and `RenderGraphBuilder.h`.
> Read Step 6 before writing this phase's regression tests — it identifies a
> real, silent-failure-mode gap in the render graph's culling logic that this
> campaign is the first to actually be able to trigger.

## Step 1: The Goal

Extend the render graph's existing, already-proven automatic barrier
machinery (`RenderGraphTypes.h`'s `ResourceAccess`, `RenderGraphBarrierPlanner.h/.cpp`'s
`RequiredStateFor()`/`RequiresBarrier()`) so it correctly understands every
hazard a compute shader can create — against BOTH buffers (already partly
scoped by the companion GPU-driven document's own Phase B) AND, newly,
**storage images** (`RWTexture`), which no existing or planned document
covers yet. By the end of this phase, a compute pass that writes an
`RWTexture` and a later graphics pass that samples it as a plain `Texture`
gets its barrier synthesized automatically, with the exact same "zero
manual barrier code in the pass author's own `execute` callback" guarantee
graphics passes already enjoy today.

## Step 2: The Situation

- `RenderGraphTypes.h`'s `ResourceAccess` enum has five graphics-only
  values today (`ColorAttachmentWrite`/`DepthStencilAttachmentReadWrite`/
  `ShaderRead`/`TransferSrc`/`TransferDst`). `IsWriteAccess()`/`ToString()`/
  `RequiredStateFor()` are all written as exhaustive switches with **no
  `default:` case**, specifically so adding a new enumerator forces every
  one of these call sites to be revisited — this is the extension point
  this phase (and the companion document's own Phase B) both use.
- `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s own Phase B
  already plans to add `ComputeShaderRead`, `ComputeShaderWrite`, and
  `IndirectCommandRead` — all three scoped, in that document, **purely
  against buffers** (its own culling workload never touches a texture).
  This phase does not re-add those three values a second time; it instead
  extends `RequiredStateFor()`'s handling of `ComputeShaderRead`/
  `ComputeShaderWrite` so they are ALSO valid when the resource in question
  is a `TextureHandle` (a storage image), not only a `BufferHandle`.
  Whichever document's implementation lands FIRST is where the enum values
  are actually added — the other should treat them as already-satisfied
  prerequisites (see the master document's own dependency notes).
- `RenderGraph.cpp`'s `ApplyUsageBarrierIfNeeded()` already branches on
  `usage.kind == ResourceKind::Texture` vs. buffer, and for textures
  further special-cases exactly one access value
  (`DepthStencilAttachmentReadWrite`) as targeting the texture's DEPTH half
  (`tex.depthState`) — every other texture access targets the COLOR half
  (`tex.colorState`). A storage-image compute access must be added to this
  same "targets the color half" bucket, NOT invent a third tracked state —
  this is a deliberate, minimal, mechanical addition, not a redesign.
- Storage-image read/write in core Vulkan (without extensions like
  `VK_KHR_shader_read_only_optimal_layout` or similar) requires
  `VK_IMAGE_LAYOUT_GENERAL` — a DIFFERENT layout from a plain sampled
  `Texture`'s `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`. This means a
  single `TextureHandle` transitioning from "written as an `RWTexture` by a
  compute pass" to "read as a plain `Texture` by a later graphics pass"
  needs a REAL layout transition (`GENERAL` → `SHADER_READ_ONLY_OPTIMAL`),
  which is exactly the kind of barrier this engine's existing machinery is
  built to synthesize automatically — this phase is what teaches it the
  new layout value.
- `RenderGraph.cpp`'s per-pass barrier loop operates at **pass granularity**
  — each declared read/write on a `PassRecord` gets exactly one barrier
  applied before that pass's `execute` callback runs. If a single compute
  pass's `execute` callback issues MULTIPLE dispatches with a real
  ordering dependency between them (e.g. dispatch A writes an `RWTexture`,
  dispatch B — in the SAME pass — reads what A just wrote), the render
  graph has no visibility into that intra-pass sequencing at all; this is
  a genuinely new consideration no existing graphics pass has ever needed
  to think about (a graphics pass's internal draws never had a write-then-
  read hazard against each other in this engine, since nothing ever reads
  back what it just wrote within one pass).

## Step 3: The Plan

- **Confirm/extend `RequiredStateFor(ResourceAccess access, bool
  isDepthAccess)`** (`RenderGraphBarrierPlanner.cpp`) so that
  `ComputeShaderRead`/`ComputeShaderWrite` — regardless of whether they
  were added by this document or by the companion GPU-driven document's own
  Phase B — return a valid `ResourceState` for the TEXTURE case too:
  `VK_IMAGE_LAYOUT_GENERAL`, `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT`, and
  `VK_ACCESS_2_SHADER_STORAGE_READ_BIT` (read) or
  `VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT` (write). This is almost certainly
  the exact same `ResourceState` value the buffer case already returns for
  the same `ResourceAccess` value (Vulkan's stage/access flags are the
  same regardless of whether the resource is a buffer or image — only the
  LAYOUT concept is image-specific, and buffers have no notion of "layout"
  at all) — confirm this symmetry explicitly rather than assuming it; if
  `RequiredStateFor()`'s current signature returns a struct that conflates
  "layout" with "buffer state" in a way that doesn't cleanly support both,
  this is the moment to look at `ResourceState`'s own definition
  (`RenderGraphTypes.h`) and confirm it already generalizes (a buffer's
  "layout" field, if the struct has one, is simply unused/ignored for
  buffer barriers — mirror however `TransferSrc`/`TransferDst` already
  handle this dual buffer/texture applicability today, since those two
  enumerators already apply to both kinds).
- **`RenderGraph.cpp`'s `ApplyUsageBarrierIfNeeded()`**: confirm the
  existing `isDepthAccess = (usage.access ==
  ResourceAccess::DepthStencilAttachmentReadWrite)` check correctly leaves
  `ComputeShaderRead`/`ComputeShaderWrite` on a texture routed to
  `tex.colorState` (the `else` branch, already the default) — this should
  require literally zero code change if the enum/switch extension above is
  done correctly; write a targeted unit test proving it rather than
  trusting it by inspection alone.
- **Regression tests**: extend `RenderGraphTypesTests.cpp` (one case per
  new enumerator, matching the existing per-enumerator pattern) and
  `RenderGraphBarrierPlannerTests.cpp` with a NEW, texture-specific
  hand-simulated sequence: `ComputeShaderWrite` (texture, as an
  `RWTexture`) → `ShaderRead` (same texture, sampled normally by a later
  graphics pass) — confirming exactly one barrier is emitted with
  `VK_IMAGE_LAYOUT_GENERAL → VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` and
  the matching stage/access masks. This is the texture-side sibling of the
  companion GPU-driven document's own buffer-side `ComputeShaderWrite` →
  `IndirectCommandRead` regression test — together the two prove the whole
  `ResourceAccess` extension generalizes correctly across both resource
  kinds.
- **Document the intra-pass hazard caveat explicitly**, in this file and
  again as a comment at `PassContext`'s own definition once Phase 6 adds
  any compute-specific fields to it: a pass whose `execute` callback issues
  more than one dispatch with a real read-after-write dependency between
  them is responsible for its OWN manual `vkCmdPipelineBarrier2` call
  between those two dispatches — the render graph's declared-resource
  model only sees a pass's AGGREGATE reads/writes, not the internal
  ordering of multiple dispatches within one pass's callback. Recommend, as
  a design guideline (not a hard rule), that a genuinely sequential
  compute-to-compute dependency be expressed as TWO SEPARATE passes instead
  whenever practical, specifically so the existing automatic barrier
  synthesis handles it for free — this is exactly what Phase 7's own blur
  validation workload should do (read source texture in one pass/dispatch,
  write destination texture, with any further blur pass a SEPARATE
  declared pass) rather than hand-rolling an intra-pass barrier to prove a
  point.

## Step 4: What We Will NOT Do

- No automatic detection or warning for a missed intra-pass barrier — this
  is entirely the pass author's responsibility, undetected by any tooling
  in this engine.
- No cross-resource barrier batching (combining several resources' barriers
  into one `vkCmdPipelineBarrier2` call) — this remains explicitly deferred
  per the original Render Graph campaign's own Phase 9 backlog, unaffected
  by this phase.
- No new `ResourceAccess` value for "read-write in one" (e.g. a combined
  `ComputeShaderReadWrite`) — mirror this engine's existing deliberate
  two-value split (`DepthStencilAttachmentReadWrite` is the one accepted
  exception, and it stays that way specifically for depth) by declaring
  BOTH a `ReadTexture(handle, ComputeShaderRead)` and a
  `WriteTexture(handle, ComputeShaderWrite)` usage for a true read-modify-
  write `RWTexture`, exactly as `ReadBuffer()`/`WriteBuffer()` already work
  today for buffers.

## Step 5: Their Role

- Treat this phase as small and mechanical BY DESIGN — if implementing it
  requires touching more than `RequiredStateFor()` plus its two sibling
  switches (and possibly zero lines of `RenderGraph.cpp` itself), something
  has gone wrong; re-read `RenderGraphTypes.h`'s own "no default case"
  design comment before reaching for a bigger change.
- Coordinate directly with whoever lands `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s
  own Phase B — the three buffer-side enum values are shared, single-
  source-of-truth additions; this phase's own job is purely to confirm/
  extend their TEXTURE-side behavior, never to re-declare them.
- Do not proceed to Phase 6 until both the buffer-side (companion
  document) and texture-side (this phase) regression tests pass — Phase 6
  assumes barrier correctness is already proven for both resource kinds
  before it wires up the render-graph-facing API surface.

---

## Step 6: V2 Revision Notes (2nd-Iteration Review)

Checked directly against the real, currently-shipped
`src/Renderer/RenderGraph/RenderGraphCompiler.cpp/.h` and
`src/Renderer/RenderGraph/RenderGraphBuilder.h` — one significant finding
that is not this phase's job to FIX, but very much this phase's job to
DOCUMENT and TEST, since it's the first time this campaign creates a pass
shape (a compute pass whose only meaningful output is a buffer) that can
actually trigger it:

1. **A compute pass whose ONLY declared write is a `BufferHandle` can be
   silently culled by `RenderGraphCompiler::Compile()`, with no error and no
   obvious symptom besides "the buffer looks unwritten."** `Compile()`'s
   real signature (`RenderGraphCompiler.h`) is:
   ```cpp
   CompiledGraph Compile(CompiledGraphInput& input, std::span<const TextureHandle> finalOutputs);
   ```
   `finalOutputs` is `TextureHandle`-only — there is no `BufferHandle`
   equivalent anywhere in this type, and `RenderGraphBuilder.h` has no
   `ImportBuffer()` counterpart to `ImportTexture()` at all (already
   self-documented as a real, pre-existing limitation in
   `RenderGraphResourcePool.h`'s own class comment: *"Phase 2 has no
   ImportBuffer() counterpart to ImportTexture() - every declared
   BufferHandle is necessarily transient/pooled"* — this campaign does not
   introduce the gap, it is simply the first campaign that can actually be
   bitten by it). Concretely: `Compile()`'s Step 2 (backward reachability)
   only ever seeds its root set from a pass's `writes` that are
   `ResourceKind::Texture` AND appear in `finalOutputs` — a pass whose
   writes are 100% buffers can NEVER be a root by itself. It only survives
   culling if some OTHER, already-reachable pass reads that buffer (a
   `ResourceUsage` in some later pass's `reads`, creating a RAW edge back
   to it), and that reader pass's own chain eventually reaches a real
   texture write in `finalOutputs`.
   - For the companion GPU-driven document's own culling→indirect-draw
     workload, this is almost certainly fine in practice: the graphics pass
     that reads the indirect-draw buffer ALSO writes a color attachment
     (the thing that's actually on screen), so it is trivially reachable
     from `finalOutputs`, which transitively keeps the compute culling pass
     upstream of it alive too. But this is an EMERGENT property of that
     specific pass shape, not something either document currently states
     or tests for.
   - **Action for this phase:** add this exact scenario as a NEW regression
     test in `RenderGraphCompilerTests.cpp` (the render graph's own,
     already-existing test file — this is a core `RenderGraphCompiler`
     behavior, not something new introduced by a compute-specific type, so
     it belongs there rather than in a new compute-only test file): a
     three-pass hand-built graph — `PassA` writes `BufferHandle` B (a
     `ComputeShaderWrite`) with NO texture write of its own, `PassB` reads B
     and writes a `TextureHandle` T that IS a `finalOutputs` root, and a
     THIRD pass `PassC` also writes buffer B but has NO reader and NO path
     to `finalOutputs` at all — confirm `PassA`/`PassB` both survive (in
     that execution order) and `PassC` is correctly culled. This is the
     buffer-side sibling of every existing texture-culling test
     `RenderGraphCompilerTests.cpp` already has, and it is the concrete,
     executable version of the "a buffer write alone never keeps a pass
     alive" rule this document must state in prose regardless.
   - **Explicitly NOT this phase's job to fix.** Do not add a
     `BufferHandle`-flavored `finalOutputs`/`ImportBuffer()` in this phase —
     that is new `RenderGraphBuilder`/`RenderGraphCompiler` surface area,
     which contradicts this phase's own "small and mechanical by design"
     mandate (Step 5) and this campaign's own preference for the smallest
     change that proves the point (see the master document's "What We Will
     NOT Do"). If a future workload genuinely needs a buffer-only pass kept
     alive with no in-graph texture-reaching consumer, that is a follow-up
     for whoever owns the Render Graph campaign's own backlog
     (`RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`), not this
     compute campaign.
   - **Document this constraint plainly for future compute-pass authors**,
     alongside this phase's existing intra-pass-hazard caveat above: *"A
     compute pass's buffer write is only guaranteed to survive
     `RenderGraphCompiler::Compile()`'s culling if some other, ALSO-kept
     pass reads it — which itself must have a path (direct or transitive)
     to a real texture write in that call's `finalOutputs`. A buffer write
     with no in-graph reader, or whose only reader's own chain never
     reaches a texture final output, is dead code and will be silently
     dropped — exactly like an unused texture write already is today."*
