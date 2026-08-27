# COMPUTE_PHASE6_RENDERGRAPH_INTEGRATION_STRATEGY_v2.md

### Child document 6 of 7 — see `COMPUTE_SHADER_MASTER_STRATEGY_v2.md` for the full campaign map.

> **v2 (2nd-iteration review):** this document's Step 1-5 body is IDENTICAL to
> `COMPUTE_PHASE6_RENDERGRAPH_INTEGRATION_STRATEGY_v1.md`. New material was
> appended as **Step 6** below. Read it before implementing this phase — it
> removes one proposed `PassContext` field (superseded by Phase 4 v2's
> finding) and cross-references Phase 5 v2's buffer-reachability caveat,
> which applies directly to the `WriteTexture()`/`ReadBuffer()`/
> `WriteBuffer()` usages this phase introduces real call sites for.

## Step 1: The Goal

Make a compute pass a genuine **first-class citizen** of
`gte::rg::RenderGraph` — declarable via `RenderGraphBuilder::AddPass()`
(or a thin naming convenience over it), able to declare reads/writes across
ALL FOUR resource kinds from Phase 1, with automatic barrier synthesis
(Phase 5) and automatic transient-resource pooling (the existing
`RenderGraphResourcePool`, unmodified) — exactly as already true for a
graphics pass today, with the smallest possible amount of genuinely NEW
`PassContext`/`RenderGraphBuilder` surface area.

## Step 2: The Situation

- `RenderGraph::ExecuteCompiledGraph()`'s existing rule — "a pass with no
  `ColorAttachmentWrite` write gets no `vkCmdBeginRendering` bracket at
  all; its `execute` callback is invoked with a zero-extent `PassContext`
  and is expected to record whatever non-rendering Vulkan work it needs
  directly against `cmd`" — **already makes a pure compute pass fully
  representable by the existing executor with zero changes.** This was
  already identified by `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s
  own Phase D and is reused here verbatim, not rediscovered.
- `RenderGraphBuilder::PassBuilder` (`RenderGraphBuilder.h`) has
  `ReadTexture()`, `WriteColorAttachment()`, `WriteDepthStencilAttachment()`,
  `ReadBuffer()`, `WriteBuffer()` — there is **no general, non-attachment
  `WriteTexture()`** for a texture written by a compute shader (as opposed
  to a color/depth attachment write, both of which additionally flag the
  pass as needing a rendering bracket via `RenderGraph::Execute()`'s
  `hasColorWrite`/`hasDepthWrite` detection). This is the one genuinely
  missing builder method this phase must add.
- `PassContext::resolveReadTexture` already exists and already resolves a
  declared `ReadTexture()` handle into a live `VkImageView`/`VkSampler`
  pair at the exact moment a pass's `execute` callback runs — this is the
  precedent Phase 3's own descriptor-set-rewriting need should follow, but
  there is NO equivalent for a declared `BufferHandle`/`WriteTexture()`
  handle today; `Renderer::Submit()`/`Renderer::Dispatch()` (Phase 4) both
  need a way to obtain the CURRENT frame's real `VkBuffer`/`VkImageView` for
  a handle a pass declared, which `PassContext` does not yet expose for
  anything but texture READS.
- Descriptor sets (Phase 3) must be re-written (`vkUpdateDescriptorSets()`)
  whenever the physical resource behind a declared handle changes identity
  frame-to-frame (`RenderGraphResourcePool` may legitimately hand back a
  different underlying `VkBuffer`/`VkImageView` across frames) — there is
  no existing hook in `PassContext` for "give me the current physical
  resource for this handle so I can rewrite my descriptor set", beyond the
  texture-read-only `resolveReadTexture`.

## Step 3: The Plan

- **Add `PassBuilder::WriteTexture(TextureHandle handle, ResourceAccess
  access)`** — a general, non-attachment write declaration, distinct from
  `WriteColorAttachment()`/`WriteDepthStencilAttachment()` (both of which
  ALSO implicitly mark the pass as needing a `vkCmdBeginRendering` bracket
  via `RenderGraph::Execute()`'s existing `hasColorWrite`/`hasDepthWrite`
  scan). Confirm — and add a regression test proving — that a
  `WriteTexture(handle, ComputeShaderWrite)` usage is correctly EXCLUDED
  from that scan (it only checks for `ColorAttachmentWrite`/
  `DepthStencilAttachmentReadWrite` specifically today, so this should
  already be true with zero further code changes; verify rather than
  assume, per this campaign's own recurring discipline).
- **A true read-modify-write `RWTexture`** (a compute shader that both
  `imageLoad`s and `imageStore`s the same image) declares BOTH
  `pass.ReadTexture(handle, ComputeShaderRead)` AND `pass.WriteTexture(handle,
  ComputeShaderWrite)` on the SAME handle — mirroring how `ReadBuffer()`/
  `WriteBuffer()` are already two separate calls a caller combines for
  buffers today. Do not add a combined "ReadWriteTexture" convenience
  method unless real call sites in Phase 7 end up needing it awkwardly
  duplicated — evaluate during implementation, per the master document's
  "what we will not do" guidance on keeping `PassContext`'s surface area
  minimal.
- **`AddComputePass(name, setup, execute)`** — a thin, purely-cosmetic
  alias of `AddPass()`, exactly as `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s
  own Phase D already scoped it: zero behavioral difference, added ONLY if
  it measurably improves readability at real call sites once a couple of
  compute passes actually exist (Phase 7's blur pass, the companion
  document's culling pass). If it doesn't clearly earn its keep, skip it —
  `AddPass()` alone is already sufficient, since a pass's behavior is
  entirely determined by what it declares in `reads`/`writes`, never by
  which entry point created it.
- **The one genuinely new `PassContext` capability**: a way to resolve a
  declared `BufferHandle`/write-only `TextureHandle` into its current
  physical resource, for a compute pass's `execute` callback to rewrite its
  own descriptor set against. Two candidate shapes — pick based on what
  reads more naturally once real call sites exist in Phase 7:
  1. Extend `resolveReadTexture`'s existing pattern with a sibling
     `ctx.resolveBuffer(BufferHandle) -> VkBuffer` and widen
     `resolveReadTexture` itself (or add `ctx.resolveTexture(TextureHandle)`)
     to also work for a texture declared via `WriteTexture()`, not only
     `ReadTexture()` — i.e. generalize "resolve a declared handle to its
     physical resource" into one concept usable regardless of read/write
     direction, since the PHYSICAL resource behind a handle doesn't care
     which direction it was declared for; only the barrier planner cares
     about that distinction.
  2. Alternatively, have the compute pass's `setup` callback capture the
     `ComputeDescriptorSet` (Phase 3) by reference/pointer, and have the
     `execute` callback call `descriptorSet.Rewrite(ctx.resolveBuffer(...),
     ctx.resolveTexture(...))` right before `Renderer::Dispatch()` — this
     keeps `PassContext` itself minimal (one new resolve method per
     resource kind) while pushing the actual rewrite orchestration into the
     pass author's own `execute` callback, mirroring how a graphics pass's
     `execute` callback already owns its own `Renderer::Submit()` call
     sequencing today. **Prefer this shape** — it keeps `PassContext`'s
     surface area smaller and matches the existing "PassContext exposes
     primitives, the pass's own callback orchestrates them" precedent.
- **Confirm the barrier/pooling story requires zero `RenderGraph::Execute()`
  changes** beyond what Phase 5 already covers — the per-pass loop
  (`ApplyUsageBarrierIfNeeded()` for every declared read then every
  declared write, in that order) already treats a `WriteTexture()`/
  `ReadBuffer()`/`WriteBuffer()` usage identically to any other declared
  usage; this phase's job is ensuring the NEW `WriteTexture()` builder
  method produces exactly the same shape of `ResourceUsage` entry the
  existing loop already knows how to consume, not writing a new execution
  path.
- **Reference the companion GPU-driven document's own Phase D** as the
  concrete first real BUFFER-side consumer of this phase's API surface
  (its culling pass declares `ReadBuffer()`/`WriteBuffer()` exactly as this
  phase makes fully functional); reference THIS document's own Phase 7 as
  the concrete first real TEXTURE-side consumer (`WriteTexture()` for an
  `RWTexture`).

## Step 4: What We Will NOT Do

- No generic, scripted/JSON-driven pass declaration — every compute pass is
  still hand-authored C++ via `AddPass()`/`AddComputePass()`, matching this
  engine's stated philosophy (see `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`,
  Section A.8).
- No descriptor-set caching/deduplication across passes — each compute
  pass still owns and rewrites its own descriptor set(s), per Phase 3's own
  refusal of a shared cache.
- No compute-to-compute pipeline "fusion"/optimization (merging two
  dependent compute passes into one dispatch) — two declared passes with a
  real dependency between them get two separate dispatches and one
  synthesized barrier, exactly as two dependent graphics passes do today.
- No change to `RenderGraphResourcePool`'s memory-aliasing behavior or
  pooling policy — a transient `RWTexture`/`RWStructuredBuffer` is pooled
  exactly like any other transient resource today, with no special-casing
  for "this one is used by a compute pass." **[v2] — and, per Phase 1 v2's
  own scope note, a transient `RWTexture` cannot be requested at all yet;
  only a transient `RWStructuredBuffer` (a plain `BufferHandle` with
  `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` set in its `BufferDesc::usage`) is
  actually poolable today. See Step 6.**
- **[v2] No `PassContext::recordDispatch` callback — see Phase 4 v2's own
  Step 6; `Renderer::Dispatch()` fuses into a pass's stats via the same
  `BeginGraphPassRecording()`-stored-callback mechanism `Renderer::Submit()`
  already uses, with no new `PassContext` field needed for that purpose.**

## Step 5: Their Role

- Build `DescriptorSetLayoutBuilder` as pure, isolated infrastructure first
  — it has no dependency on `ComputePipeline` (Phase 2) beyond needing a
  `VkDevice`, so it can be written and manually smoke-tested (Tier 2, per
  `AGENTS.md`) in parallel with Phase 2 if convenient.

  *(Note: this bullet describes Phase 3's own deliverable and is retained
  here verbatim from v1's Step 5 for continuity with the original
  document's own text — treat it as informational context for sequencing,
  not this phase's own task list.)*
- Treat this as the single riskiest phase in this whole campaign — it is
  the one place genuinely NEW `PassContext` surface area is added, unlike
  every other phase, which is either pure addition-behind-existing-seams
  (Phases 1-3) or pure math (Phase 4) or a mechanical enum extension
  (Phase 5). Build the SIMPLEST possible throwaway test pass first — a
  compute pass that `WriteTexture()`s a single `RWTexture` with a constant
  debug color pattern, displayed via a temporary Inspector-style viewer or
  simply visually diffed by eye — before attempting either the culling
  workload (companion document) or the blur workload (Phase 7) for real.
  **[v2] Since a transient `RWTexture` cannot be requested via
  `CreateTexture()` yet (Phase 1 v2), this throwaway test pass's texture
  must be a small, dedicated, EXTERNALLY-created `RenderTexture`
  (`allowStorageImageAccess = true`), imported once via `ImportTexture()` —
  the same shape Phase 7's real blur workload ends up using, so this
  throwaway test doubles as a rehearsal for it.**
- Once the throwaway test pass is validated (validation layers clean,
  correct visual output, correct barrier sequence confirmed via the
  Editor's existing "Render Graph" panel — Phase 8 of the original
  campaign, already pass/resource-shape-agnostic and needing no changes
  here), delete it and move to Phase 7's real validation workloads.
- Confirm with whoever is building `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s
  own Phase D that `AddComputePass()` (if built) and `WriteTexture()` are
  shared, single-source-of-truth additions — that document's own culling
  pass and this campaign's own blur pass should call the exact same
  builder methods, never two independently-invented variants.

---

## Step 6: V2 Revision Notes (2nd-Iteration Review)

Two amendments, both direct consequences of findings already made in
Phase 1 v2/Phase 4 v2/Phase 5 v2 — recorded here too since this is the
phase where their concrete, real-code consequences actually surface:

1. **No `ctx.recordDispatch()` — superseded by Phase 4 v2.** v1's Step 3
   left the door open for a `PassContext::recordDispatch(...)` callback
   "mirroring `PassContext::recordDraw`'s own shape." Per
   `COMPUTE_PHASE4_DISPATCH_EXECUTION_STRATEGY_v2.md`'s own Step 6
   (checked against the real, shipped `Renderer::Submit()`/
   `BeginGraphPassRecording()` code): a compute pass's `execute` callback
   should call `renderer.BeginGraphPassRecording(ctx.cmd, ...)` /
   `Renderer::Dispatch()` / `renderer.EndGraphPassRecording()` in exactly
   the same shape a graphics pass already calls
   `BeginGraphPassRecording`/`Submit()`/`EndGraphPassRecording` today — no
   new `PassContext` field is needed for stats-fusion purposes, because (a)
   `Renderer::Dispatch()` can fuse into the pass's own stats via the exact
   same stored-callback mechanism `Submit()` already uses, and (b) there is
   no meaningful compute equivalent of `DrawStats`'s triangle count worth
   adding a field for yet. This phase's actually-needed new `PassContext`
   surface area is therefore narrower than v1 implied: `resolveBuffer()`/
   `resolveTexture()` (for descriptor-set rewriting, per Step 3's
   "candidate shapes" above) — real, load-bearing additions — but NOT a
   `recordDispatch` callback.
2. **`WriteTexture()`/`ReadBuffer()`/`WriteBuffer()` real call sites are
   exactly where Phase 5 v2's buffer-reachability caveat first bites in
   practice — restate it here for whoever writes this phase's own
   `AddComputePass()`/first real pass.** Per
   `COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md`'s own Step 6:
   `RenderGraphCompiler::Compile()`'s `finalOutputs` root set is
   `TextureHandle`-only, and there is no `ImportBuffer()`. A compute pass
   declared through THIS phase's own `AddComputePass()`/`WriteBuffer()`
   whose only meaningful output is a buffer (e.g. an early, standalone
   experiment before the companion document's real culling→indirect-draw
   consumer exists) will be silently culled unless something else, kept
   alive by a real path to a texture `finalOutputs` entry, reads that
   buffer. When building this phase's own throwaway validation pass (Step
   5 above), prefer the `WriteTexture()` (texture) shape specifically
   BECAUSE it sidesteps this exact culling gotcha for a texture-final-
   output-driven test — a throwaway pass that instead only writes a buffer
   would need an artificial downstream reader just to avoid being culled,
   which is a confusing, avoidable complication for a "simplest possible
   test" per Step 5's own stated goal.
3. **Also note, for completeness, the Phase 1 v2 scope boundary already
   listed in this document's own "What We Will NOT Do" above**: any
   `WriteTexture()` usage declared in this phase must target an
   EXTERNALLY-OWNED, `ImportTexture()`-brought-in `RenderTexture`/
   `Texture2D` — never a `RenderGraphBuilder::CreateTexture()`-declared
   transient handle, since `rg::TextureDesc` has no storage-usage opt-in
   today. `ReadBuffer()`/`WriteBuffer()`, by contrast, work fine against
   either an externally-created OR a transient, graph-pooled
   `BufferHandle` — `BufferDesc::usage` is already a full
   `VkBufferUsageFlags`, so `RenderGraphBuilder::CreateBuffer()` can
   already request `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` today with zero
   further changes.
