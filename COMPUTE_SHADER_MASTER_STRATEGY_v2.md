# COMPUTE_SHADER_MASTER_STRATEGY_v2.md

### Master orchestrator: general-purpose compute shader support, as a first-class `gte::rg::RenderGraph` citizen

> **This is a v2 (2nd-iteration review) revision of `COMPUTE_SHADER_MASTER_STRATEGY_v1.md`.**
> See "Step 0" immediately below for exactly what changed and why — everything
> from "Why this document exists..." onward is otherwise IDENTICAL to v1 except
> where a paragraph is explicitly marked `[v2]`. The five child documents that
> needed real amendments were re-issued as `COMPUTE_PHASE1_..._v2.md`,
> `COMPUTE_PHASE4_..._v2.md`, `COMPUTE_PHASE5_..._v2.md`,
> `COMPUTE_PHASE6_..._v2.md`, and `COMPUTE_PHASE7_..._v2.md` — each one keeps its
> own v1 body completely intact and appends a new "Step 6: V2 Revision Notes"
> section at the end (the same "append, don't silently rewrite" convention this
> codebase already uses for `RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md`
> etc.). Phase 2 (`COMPUTE_PHASE2_PIPELINE_INFRASTRUCTURE_STRATEGY_v1.md`) and
> Phase 3 (`COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md`) were
> checked closely against the real, already-shipped code they build on
> (`Pipeline.cpp`'s private `ReadFile()`/`CreateShaderModule()`,
> `GpuResourceFactory`'s existing `m_materialSetLayout`/
> `m_materialDescriptorPool` precedent) and found to already be accurate and
> consistent — they are UNCHANGED, still at `_v1`.

## Step 0: V2 Revision Notes (2nd-Iteration Review)

This review was done by reading the actual, currently-shipped engine source
(`Renderer.cpp/.h`, `RenderGraph.cpp/.h`, `RenderGraphBuilder.cpp/.h`,
`RenderGraphCompiler.cpp/.h`, `RenderGraphResourcePool.cpp/.h`,
`RenderGraphTypes.cpp/.h`, `Buffer.cpp/.h`, `RenderTexture.cpp/.h`,
`Texture2D.cpp/.h`, `Pipeline.cpp/.h`, `GpuResourceFactory.cpp/.h`,
`Application.cpp`, `RenderPasses.cpp`) side-by-side with all seven v1 phase
documents, specifically hunting for two things: (1) places where a v1
document's plan silently assumes a capability the real, already-landed Render
Graph campaign (Phases 1-8) does NOT actually have, and (2) places where the
real, already-shipped code already establishes a pattern (e.g. how
`Renderer::Submit()` and `PassContext::recordDraw` actually interact) that a
v1 document re-derives differently/more awkwardly than necessary. Five real
findings came out of that; all five are cross-cutting enough that they belong
here at the master-document level too, not just buried in one child doc.

1. **Transient (render-graph-pooled) storage images are NOT possible today,
   and no v1 document says so.** `rg::TextureDesc` (`RenderGraphTypes.h`,
   shipped as part of the original Render Graph campaign) has exactly four
   fields — `width`/`height`/`format`/`hasDepth` — with NO usage-flags field
   at all, and `RenderGraphResourcePool::AcquireTexture()`
   (`RenderGraphResourcePool.cpp`) always creates a fresh entry via
   `m_renderer->CreateRenderTexture(width, height, format, debugName,
   nullptr)`, with no way to opt into `VK_IMAGE_USAGE_STORAGE_BIT`. Phase 1's
   plan to add an `allowStorageImageAccess` bool only to `RenderTexture`'s
   OWN constructor/`Renderer::CreateRenderTexture()` is correct as far as it
   goes, but it means a `RWTexture` can only ever be an EXTERNALLY-OWNED,
   persistent resource (created directly and brought in via
   `RenderGraphBuilder::ImportTexture()`) — never a resource requested
   through `RenderGraphBuilder::CreateTexture()` itself. Phase 7's own blur
   workload happens to sidestep this (its `blurredSceneOutput` is a
   dedicated, persistent `RenderTexture`, imported every frame, exactly like
   the Game/Scene views already are) — but nothing in v1 says this is
   REQUIRED, or explains why. See `COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md`'s
   own Step 6 for the explicit scope note this needed, and
   `COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md`'s Step 6 for
   the resulting Phase 7 clarification.
2. **A compute pass whose only declared write is a BUFFER can be silently
   culled, and no v1 document warns about it.** `RenderGraphCompiler::Compile()`
   (`RenderGraphCompiler.cpp`) takes its reachability root set as
   `std::span<const TextureHandle> finalOutputs` — there is no
   `BufferHandle` equivalent, and `RenderGraphBuilder` has no
   `ImportBuffer()` counterpart to `ImportTexture()` either (confirmed
   against the real, shipped `RenderGraphBuilder.h` — this is a
   pre-existing, already-self-documented limitation of
   `RenderGraphResourcePool.h`'s own class comment, not something this
   compute campaign introduces). A buffer-producing compute pass survives
   culling ONLY when its consumer chain transitively reaches a real texture
   write in `finalOutputs` within the SAME `Execute()` call — never because
   it wrote a buffer at all. This is exactly the kind of thing a future
   compute-pass author could get silently bitten by (their pass "just never
   runs," with the only symptom being an unwritten buffer, no error, no
   crash). See `COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md`'s Step 6.
3. **Phase 4/6's proposed `PassContext::recordDispatch` callback is
   unnecessary extra API surface — the real, shipped code already solves this
   more simply for draws, and the same trick applies to dispatches for free.**
   Reading the ACTUAL shipped `Renderer::Submit()`/`BeginGraphPassRecording()`/
   `RenderPasses.cpp` code (not just `RenderGraph.h`'s doc comments) shows
   that `PassContext::recordDraw` is never called by a pass author directly —
   it's handed to `Renderer::BeginGraphPassRecording()` ONCE per pass, and
   `Renderer::Submit()` invokes it automatically every time it's called from
   deep inside `Game::Render()`. `Renderer::Dispatch()` (Phase 4) should
   follow this exact same already-established pattern rather than asking
   every future compute-pass author to remember to call a new
   `ctx.recordDispatch(...)` themselves. Separately: GPU timing for a
   compute pass is ALREADY fully automatic today, regardless of pass content
   — `RenderGraph::ExecuteCompiledGraph()`'s `WriteBegin()`/`WriteEnd()` calls
   bracket EVERY pass unconditionally (see `RenderGraph.cpp`), so there is no
   new GPU-timing plumbing a compute pass needs at all. See
   `COMPUTE_PHASE4_DISPATCH_EXECUTION_STRATEGY_v2.md`'s and
   `COMPUTE_PHASE6_RENDERGRAPH_INTEGRATION_STRATEGY_v2.md`'s own Step 6.
4. **Phase 7's `blurredSceneOutput` must NOT be created with the default
   format.** `Renderer::CreateRenderTexture()`'s `format` parameter defaults
   to `VK_FORMAT_UNDEFINED`, which resolves to `Renderer::ColorFormat()` —
   whatever the swapchain actually negotiated (commonly
   `VK_FORMAT_B8G8R8A8_UNORM` per `VulkanSwapchain.cpp`'s own
   `ChooseSurfaceFormat`). `VK_FORMAT_B8G8R8A8_UNORM` storage-image support
   is genuinely NOT guaranteed across all Vulkan drivers — Phase 1's own
   plan already flags this general concern, but Phase 7's concrete blur
   workload never actually says what format to pass, leaving the door open
   to inheriting exactly this uncertainty for no reason (this new texture
   has no pipeline-sharing requirement to match `ColorFormat()` at all —
   it's only ever sampled via `ImGui::Image()`, which doesn't care). See
   `COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md`'s Step 6.
5. **`CreateStructuredBuffer()` should accept an optional extra usage-flags
   parameter.** As specified in v1, it always ORs in exactly
   `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` (+`TRANSFER_DST_BIT` for
   `GpuOnly`) with no way for a caller to ALSO request e.g.
   `VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT` — needed by the companion
   GPU-driven-rendering document's own indirect-draw-buffer workload, which
   is explicitly meant to share this same infrastructure (see this
   document's own "Why this document exists" section). Without this, that
   companion document's Phase A would need its own, second, near-duplicate
   structured-buffer factory method instead of reusing this one. See
   `COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md`'s Step 6.

None of these five findings change the overall Goal, phase ordering, or
"What We Will NOT Do" scope of this campaign — they are all
implementation-detail corrections/clarifications caught by reading the real
code the plan will actually sit on top of. Land them as part of whichever
phase actually implements the affected piece; nothing here blocks starting
Phase 1.

---

Everything below this point is IDENTICAL to `COMPUTE_SHADER_MASTER_STRATEGY_v1.md`,
except the reading-order table in Step 3 now points at the `_v2` documents for
Phases 1, 4, 5, 6, 7 (Phases 2 and 3 are unchanged, still `_v1`).

**Read this file first.** It exists to answer three questions before you
open any child document: *why does this need to be its own strategy at
all*, *how does it relate to the GPU-driven-rendering document that already
exists*, and *what order do I build these seven pieces in*.

---

## Why this document exists, and how it relates to `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`

`GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md` already scopes and
plans **one concrete compute workload**: GPU frustum culling that populates
an indirect draw buffer. That document is correct and should still be
built — but it deliberately narrows `ResourceAccess`/the descriptor/pipeline
work to exactly what ONE culling shader needs (two/three storage BUFFERS,
nothing else), because its own stated discipline is "build only what is
needed, when the need is real and demonstrated, never speculatively."

The request behind THIS document is different in kind: **a Unity compute-
shader developer's mental model** — `RWStructuredBuffer`, `StructuredBuffer`,
`RWTexture`, `Texture` as four general building blocks usable by *any*
future compute shader, not just a culling one — plus making compute passes
first-class `RenderGraph` citizens in general, not just for one pass shape.

Concretely, this master strategy:

- Generalizes the GPU-driven document's Phase A (compute pipeline
  infrastructure) and Phase B (`ResourceAccess` extension) into reusable
  infrastructure any future compute shader can use, not infrastructure
  privately shaped around one culling shader's two buffers.
- Adds the ONE resource family the GPU-driven document never needed at all:
  **storage images** (`RWTexture` — `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`,
  `VK_IMAGE_LAYOUT_GENERAL`). The culling workload is buffers-only; a
  general compute-shader story is incomplete without images too (GPU
  skinning, procedural textures, post-process blur/blooms, etc. all need
  `RWTexture`).
- Treats GPU-driven culling (the companion document) as **this campaign's
  first validation workload for the buffer side**, and adds one NEW small
  validation workload of its own (a compute box-blur post-process, Phase 7
  below) specifically to validate the texture side, since nothing in the
  GPU-driven document ever exercises an `RWTexture`.

**Practical consequence for sequencing:** Phases 1-4 of this document and
Phases A-C of the GPU-driven document can be built in either order, or
merged (they overlap almost entirely) — whichever team picks this up first
should treat `ComputePipeline`/`GpuResourceFactory::CreateComputePipeline()`
etc. as ONE shared piece of infrastructure serving BOTH documents, not
duplicate it. Phase 5 of this document (synchronization) explicitly
extends, rather than replaces, the GPU-driven document's own Phase B
`ResourceAccess` additions. See each child document's own "Situation"
section for the exact cross-reference.

---

## Step 1: The Goal

By the end of this whole campaign, any future engineer adding a NEW compute
shader to this engine should be able to:

1. Declare exactly the resources they need using four Unity-familiar
   mental-model building blocks, each mapped onto a real Vulkan mechanism:
   - **`RWStructuredBuffer`** → a `Buffer` created with
     `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`, bound as
     `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, read AND written by the shader
     (GLSL `buffer` block, no `readonly` qualifier).
   - **`StructuredBuffer`** → the exact same underlying Vulkan buffer/
     descriptor type, but declared `readonly` in GLSL and declared as a
     `ComputeShaderRead`-only usage in the render graph, so the barrier
     planner never has to treat it as a write hazard source.
   - **`RWTexture`** → a `RenderTexture`/`Texture2D` created with
     `VK_IMAGE_USAGE_STORAGE_BIT`, bound as
     `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`, sampled via `imageLoad`/`imageStore`
     in `VK_IMAGE_LAYOUT_GENERAL`. **[v2] Only as an externally-owned,
     persistent resource imported per-frame — see Step 0, Finding 1.**
   - **`Texture`** → the engine's existing combined-image-sampler texture
     (`MaterialTexture`'s own convention), sampled read-only via
     `sampler2D`/`texture()` in a compute shader exactly like a fragment
     shader already does.
2. Build a `ComputePipeline` from a `.comp` shader with almost the same
   ceremony as building a graphics `Pipeline` today.
3. Declare a compute pass via `RenderGraphBuilder::AddPass()` (or a thin
   `AddComputePass()` alias) that reads/writes any mix of the four resource
   kinds above, with the render graph automatically synthesizing every
   barrier and pooling every transient resource — exactly as already
   happens for a graphics pass's color/depth attachments today.
4. Trust that this was validated against BOTH families (buffers via the
   GPU-driven culling document's own workload, images via this campaign's
   own blur validation workload — see Phase 7), not merely asserted to
   work.

## Step 2: The Situation

As of the end of the Render Graph campaign (Phase 8) and before either this
document or the GPU-driven document has been implemented:

- Zero compute shaders exist anywhere (`Pipeline.h/.cpp` is graphics-only,
  no `VkComputePipelineCreateInfo` anywhere, no `.comp` files under
  `src/Shaders/`).
- `Buffer`/`RenderTexture`/`Texture2D` all exist and are RAII-solid, but
  none of them ever request `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` or
  `VK_IMAGE_USAGE_STORAGE_BIT` — every existing usage flag combination was
  chosen for graphics-pipeline/transfer/sampling needs only (see `Buffer.cpp`,
  `RenderTexture.cpp`, `Texture2D.cpp`).
- `RenderGraphTypes.h`'s `ResourceAccess` enum has five graphics-only
  values, with the three exhaustive switches (`IsWriteAccess()`/`ToString()`/
  `RequiredStateFor()`) deliberately missing a `default:` case so a future
  addition is forced to touch all three — this is the extension point every
  child document below relies on.
- `RenderGraphBuilder::PassBuilder` has `ReadTexture()`/`WriteColorAttachment()`/
  `WriteDepthStencilAttachment()`/`ReadBuffer()`/`WriteBuffer()` — the last
  two exist but have never been exercised by a real pass (confirmed by
  Phase 2's own completion report).
- `PassContext`'s "a pass with no `ColorAttachmentWrite` gets no
  `vkCmdBeginRendering` bracket at all" behavior already makes a pure
  compute pass representable by the existing executor with **zero changes**
  — this was already identified and is reused, not rediscovered, by this
  campaign. **[v2] Confirmed true against the real, shipped
  `RenderGraph::ExecuteCompiledGraph()` — it only checks for
  `ColorAttachmentWrite`/`DepthStencilAttachmentReadWrite` when deciding
  `hasColorWrite`/`hasDepthWrite`, so a `ComputeShaderWrite`-only pass is
  already correctly excluded, with no code change needed.**
- `GpuResourceFactory::MaterialDescriptorSetLayout()`/`CreateMaterialTexture2D()`
  is the one existing precedent for "a fixed descriptor set layout plus a
  matching descriptor pool, built once, reused by every consumer" — every
  descriptor-related decision below deliberately mirrors this precedent
  rather than inventing a new one.
- **[v2] `rg::TextureDesc` (the render graph's OWN transient-texture
  descriptor) has no usage-flags field at all, and
  `RenderGraphResourcePool::AcquireTexture()` always creates through
  `Renderer::CreateRenderTexture()` with no storage opt-in — see Step 0,
  Finding 1.**
- **[v2] `RenderGraphCompiler::Compile()`'s `finalOutputs` root set is
  `TextureHandle`-only, and there is no `RenderGraphBuilder::ImportBuffer()`
  — see Step 0, Finding 2.**

## Step 3: The Plan — reading order and dependency map

Seven child documents, meant to be read/landed in this order. Each is
independently shippable and independently testable; the whole campaign does
not need to land in one sitting.

| # | Document | Delivers | Depends on |
|---|----------|----------|------------|
| 1 | `COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md` | Vulkan-level plumbing for the four resource kinds (`RWStructuredBuffer`/`StructuredBuffer`/`RWTexture`/`Texture`) | Nothing — pure `Buffer`/`RenderTexture`/`Texture2D` extension |
| 2 | `COMPUTE_PHASE2_PIPELINE_INFRASTRUCTURE_STRATEGY_v1.md` | `ComputePipeline`, shared `ShaderModule` loader, `.comp` build support | Phase 1 (needs storage buffer/image *concepts* to design against, though not strictly the code) |
| 3 | `COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md` | Reusable descriptor-set-layout builder + compute descriptor pool | Phases 1-2 |
| 4 | `COMPUTE_PHASE4_DISPATCH_EXECUTION_STRATEGY_v2.md` | Dispatch math + `Renderer::Dispatch()` | Phases 2-3 |
| 5 | `COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md` | `ResourceAccess` additions for compute+storage-image hazards | Phase 1 (needs the resource kinds to exist conceptually); coordinates with `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s own Phase B |
| 6 | `COMPUTE_PHASE6_RENDERGRAPH_INTEGRATION_STRATEGY_v2.md` | `PassBuilder` texture read/write for compute, `AddComputePass()`, per-frame descriptor re-resolution | Phases 2-5 |
| 7 | `COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md` | End-to-end proof for both resource families + Editor visibility | Phase 6, and (for the buffer half) `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s own Phase D/G |

Dependency shape, visually:

```
Phase 1 (resource vocabulary)
   |
   +--> Phase 2 (ComputePipeline) --> Phase 3 (descriptor binding) --> Phase 4 (dispatch)
   |                                                                        |
   +--> Phase 5 (synchronization) -----------------------------------------+
                                                                            |
                                                                            v
                                                                     Phase 6 (RenderGraph integration)
                                                                            |
                                                                            v
                                                                     Phase 7 (validation: buffers via
                                                                     GPU_DRIVEN doc + textures via blur)
```

## Step 4: What We Will NOT Do (campaign-wide)

- We will **not** duplicate `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s
  own culling-specific work (the actual `FrustumCull.comp` shader, the
  indirect-draw-buffer format, `Renderer::SubmitIndirect()`). That document
  remains the authority for indirect draw + culling; this campaign only
  supplies/generalizes the compute-shader plumbing underneath it.
- We will **not** build async compute / a second (compute) queue — same
  reasoning as the GPU-driven document's own refusal: nothing exists yet to
  parallelize against, and this stays entirely on the single existing
  graphics queue for the whole duration of this campaign.
- We will **not** build bindless/descriptor-indexing infrastructure
  (`VK_EXT_descriptor_indexing`) — every compute pass in this campaign uses
  the existing "one descriptor set per distinct resource combination"
  precedent `MaterialTexture` already established.
- We will **not** build 3D/volume textures, texture arrays, or cubemap
  storage images — `RWTexture`/`Texture` here mean plain 2D only, matching
  `RenderTexture`/`Texture2D`'s own existing scope.
- We will **not** build shader reflection (SPIRV-Cross or otherwise) to
  auto-derive descriptor layouts from GLSL source — binding numbers are a
  documented, hand-maintained convention between C++ and GLSL, exactly like
  `GpuResourceFactory::MaterialDescriptorSetLayout()`'s existing fixed
  layout.
- We will **not** attempt GPU vertex skinning, procedural mesh generation,
  or any other *specific* compute workload beyond this campaign's own two
  validation features (culling, from the companion document; a compute
  blur, from Phase 7 here). Name GPU skinning explicitly as the natural
  next candidate once this campaign closes (see `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`,
  Section C.5) — never fold it into this campaign speculatively.
- **[v2] We will not (yet) extend `rg::TextureDesc`/`RenderGraphResourcePool`
  to support a transient, render-graph-POOLED `RWTexture` — see Step 0,
  Finding 1, and `COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md`'s Step 6
  for the explicit scope boundary this adds. Every `RWTexture` in this
  campaign is an externally-owned, persistent resource, imported per-frame.**
- **[v2] We will not add a `BufferHandle`-based `finalOutputs`/
  `ImportBuffer()` concept to the render graph in this campaign — see Step
  0, Finding 2. A compute pass's buffer output must have an in-graph reader
  that eventually reaches a texture-based final output, or it will be
  silently culled; this campaign only documents that constraint, it does
  not lift it.**

## Step 5: Their Role

- Read this file, then read the seven child documents in the table order
  above — do not skip to Phase 6 "because RenderGraph integration is the
  interesting part." Phases 1-4 are the unglamorous, load-bearing
  foundation; skipping ahead means re-deriving decisions (binding
  conventions, descriptor pool sizing, dispatch math) under time pressure
  instead of deliberately.
- Coordinate explicitly with whoever is building (or has built)
  `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md` — Phases 1-4 here
  and that document's Phases A-C are the SAME underlying infrastructure
  viewed from two different angles. Build it once, reference it from both
  documents, never maintain two competing `ComputePipeline` classes.
- Treat Phase 5's "no `default:` case forces every switch to be revisited"
  guardrail (inherited from Phase 1 of the original Render Graph campaign)
  as a feature, not friction, exactly as the GPU-driven document already
  instructs.
- Do not consider this campaign "done" until Phase 7's TWO validation
  workloads (buffer-side culling, texture-side blur) are both landed,
  manually verified with validation layers enabled, and visually confirmed
  correct. A campaign that only proves the buffer half is not a proof that
  compute shaders are general-purpose in this engine — only that one
  buffer-shaped workload works.
- Once Phase 7 lands, write `COMPUTE_SHADER_CAMPAIGN_COMPLETION_REPORT.md`,
  mirroring `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`'s own shape,
  including an honest "what remains open" section (async compute, bindless,
  GPU skinning, 3D/array textures, AND transient-RWTexture pooling /
  buffer-final-outputs [v2] all belong there, not silently dropped).
