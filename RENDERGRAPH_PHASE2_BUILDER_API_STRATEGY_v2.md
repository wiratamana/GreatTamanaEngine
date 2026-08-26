# RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md
### (Part 2 of 9 - see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md` for the full campaign)

## V2 Revision Notes (2nd iteration review)

Two fixes, both small but real:

1. **`ImportTexture()`'s signature was missing its own documented
   parameter.** v1's Step 3.3 prose is explicit that an imported resource
   needs "a CALLER-SUPPLIED initial `VkImageLayout` (what layout this image
   is ACTUALLY in right now...)" - correctly identifying that this is
   exactly what lets Phase 5's barrier synthesis stop always assuming
   `UNDEFINED`. But the actual `RenderGraphBuilder` API sketch in Step 3.1
   showed `TextureHandle ImportTexture(const char* name, RenderTarget externalTarget);`
   - no layout parameter at all. This is now fixed below: the initial
   layout is a required, explicit third parameter, with no default (an
   imported resource's real current layout is exactly the kind of thing
   that must never be silently guessed - a wrong guess here would produce
   a barrier with the wrong `oldLayout`/`srcAccessMask`, a genuine
   correctness bug that would only surface as validation-layer warnings or
   silent visual corruption, not a compile error).
2. **Naming/`debugName` clarified now that `RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md`
   removed `debugName` from `TextureDesc`/`BufferDesc`.** v1 had THREE
   places a resource's name could apparently live: `TextureDesc::debugName`
   (Phase 1), `CreateTexture(name, desc)`'s own `name` parameter (this
   phase), and `RenderGraphResourcePool::AcquireTexture(desc, debugName)`'s
   own `debugName` parameter (Phase 4) - with no stated relationship
   between the three. Phase 1 v2 removes the first one entirely. This
   phase's builder is now the SINGLE place a resource's name is captured at
   all: `CreateTexture()`/`CreateBuffer()`/`ImportTexture()`'s `name`
   parameter is stored in a resource-table-parallel array (indexed exactly
   like the desc table itself - see Step 3.1 below), completely separate
   from the comparable `desc`, and is what eventually reaches Phase 4's
   `AcquireTexture(desc, debugName)` (Phase 6 passes this same stored name
   straight through when it resolves a virtual resource to a physical one)
   and Phase 8's snapshot/panel display. There is now exactly one name per
   declared resource, captured exactly once, at exactly one call site
   shape (`CreateTexture(name, desc)`/`ImportTexture(name, target, layout)`),
   with a single, explicit, single-directional flow to every later
   consumer.

Nothing else in this phase changes from v1.

---

## Step 1: The Goal (Where are we going?)

Give a future pass author (Phase 7's `Game`/`Scene`/`Present`/ImGui-overlay
migration, and every rendering feature after it) a small, ergonomic,
DECLARATIVE way to say "I want a texture like THIS, here is a pass that
writes to it, here is another pass that reads it" - and have that produce
nothing more than an inert, in-memory description of a frame's intended
work. By the end of this phase, `RenderGraphBuilder` can build up a
complete `PassRecord`/resource-description list for a hypothetical frame,
entirely in memory, with **no Vulkan call issued and no physical resource
allocated** - compilation (Phase 3) and execution (Phase 6) both come
later. This is the "setup phase" half of the classic two-phase
setup/execute render-graph pattern (Frostbite's Frame Graph, Unreal's
`RDG_EVENT_SCOPE`/`FRDGBuilder`), adapted to this engine's own naming and
handle conventions from Phase 1.

## Step 2: The Situation / The Problem (Where are we now?)

Today, "declaring a pass" and "recording a pass" are the same call, at the
same time, with no separation: `Renderer::Submit()`
(`src/Renderer/Renderer.cpp`) is called once per draw, directly appending
into `FrameRecorder::m_drawQueue`, and `Application::Run()` decides which
target each `RenderSystem::Draw()` call's output lands in by manually
calling `RenderOffscreen(target, slot)` immediately afterward - the
"what does this pass need" and "what does this pass do" are inseparably
fused into one imperative call sequence, hand-ordered by whoever wrote
`Application::Run()`.

A render graph fundamentally requires SEPARATING those two things, because
the whole point is that the ENGINE decides execution order/culling/barriers
from the declared dependencies - it cannot do that if "declare" and
"execute" happen atomically, in caller-decided order, with no chance to
look at the whole frame's shape first. This phase is where that separation
is introduced, on top of Phase 1's now-existing vocabulary
(`TextureHandle`/`BufferHandle`/`TextureDesc`/`ResourceAccess`/
`PassRecord`).

## Step 3: The Plan (How will we get there?)

### 3.1 - New file: `src/Renderer/RenderGraph/RenderGraphBuilder.h/.cpp`

**`RenderGraphBuilder`** owns the whole in-progress description of one
frame: a `std::vector<PassRecord>`, a `std::vector<TextureDesc>`/
`std::vector<BufferDesc>` (one entry per handle ever minted this frame,
`ResourcePool`-style dense indexing - reusing `src/Renderer/ResourcePool.h`
itself where it fits, rather than a fifth hand-rolled slot-array), a small
"imported resource" side-table (see 3.3 below), **and, new in v2, a
`std::vector<const char*> m_textureNames`/`m_bufferNames` PARALLEL to the
desc tables, one name per handle - the single place a resource's
human-readable name lives now that `TextureDesc`/`BufferDesc` themselves
never carry one (see this document's own Revision Notes and Phase 1 v2).**
It exposes:

```cpp
class RenderGraphBuilder {
public:
    // `name` must be a string literal (or otherwise static-storage-duration)
    // const char* - stored by pointer, never copied, mirroring
    // GTE_PROFILE_SCOPE's own rule (AGENTS.md, "Profiling"). This is now
    // the ONE place a texture/buffer's name is captured - see this
    // document's own Revision Notes for why it no longer also lives
    // inside TextureDesc/BufferDesc.
    TextureHandle CreateTexture(const char* name, const TextureDesc& desc);
    BufferHandle  CreateBuffer(const char* name, const BufferDesc& desc);

    // Wraps an ALREADY-LIVE, externally-owned resource (the swapchain
    // image this frame, or the Editor's own long-lived Game/Scene
    // RenderTexture) as a graph resource, so existing call sites keep
    // working - see Phase 4's own "imported vs. transient" split.
    // `currentLayout` is REQUIRED (no default) - the caller must state
    // exactly what VkImageLayout this image is ACTUALLY in right now
    // (VK_IMAGE_LAYOUT_UNDEFINED for a freshly-acquired swapchain image;
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL for a Game/Scene
    // RenderTexture left in that state by last frame's graph - see Phase
    // 5/6 for how this seeds that resource's tracked ResourceState). v2:
    // this parameter was documented but MISSING from v1's own code sketch
    // - now made explicit and required, since guessing it wrong is a
    // silent correctness bug, not a compile error.
    TextureHandle ImportTexture(const char* name, RenderTarget externalTarget, VkImageLayout currentLayout);

    class PassBuilder {
    public:
        void ReadTexture(TextureHandle, ResourceAccess = ResourceAccess::ShaderRead);
        void WriteColorAttachment(TextureHandle);
        void WriteDepthStencilAttachment(TextureHandle);
        // ... ReadBuffer/WriteBuffer, symmetric, for a future compute pass (Phase 9)
    };

    // `name` must be a string literal (mirrors GTE_PROFILE_SCOPE's own
    // static-storage-duration requirement - see AGENTS.md, "Profiling").
    // `setup` runs IMMEDIATELY (this call), declares reads/writes via the
    // PassBuilder& it's handed, and returns whatever captured state
    // `execute` will need. `execute` is stored, NOT run, until Phase 6's
    // RenderGraph::Execute() actually reaches this pass post-compilation -
    // and only ever runs at all if the pass survives Phase 3's culling.
    template <typename SetupFn, typename ExecuteFn>
    void AddPass(const char* name, SetupFn&& setup, ExecuteFn&& execute);

    // Consumes this builder, handing its whole in-progress description
    // over to Phase 3's compiler - see RenderGraphCompiler.h. Carries the
    // name tables described above alongside the desc tables, so Phase 4/6/8
    // can resolve a handle back to a human-readable name without needing a
    // second lookup mechanism.
    CompiledGraphInput Finish();
};
```

The `AddPass` two-callback shape is deliberate and mirrors Unreal's
`FRDGBuilder::AddPass` almost exactly, for a good reason: `setup` needs
access to a `PassBuilder&` (to declare reads/writes) but must NOT be handed
a live `VkCommandBuffer` (nothing is being recorded yet); `execute` is the
reverse - it needs a live `VkCommandBuffer` (via Phase 6's `PassContext`)
but has no further use for a `PassBuilder&` (every declaration already
happened). Fusing them into one callback would force either an unused
parameter or, worse, tempt a future pass author into recording draws
INSIDE `setup`, defeating the entire "declare first, decide, then execute"
model. Two distinct callback types make the illegal state (recording during
setup) simply impossible to express.

`ExecuteFn` is stored as `std::function<void(PassContext&)>` -
`PassContext` (a small struct: the pass's own `VkCommandBuffer`, its
resolved `RenderTarget` for any declared color/depth writes, and read-only
access to resolve any OTHER declared-read texture back into something
bindable) is fully specified in Phase 6, since it needs Phase 4's physical-
resource-realization result to even have a shape - this phase only needs to
know it exists as an opaque forward-declared type its `std::function`
closes over.

### 3.2 - `CompiledGraphInput` - the hand-off shape to Phase 3

A plain struct bundling everything `RenderGraphBuilder` accumulated
(`std::vector<PassRecord>` plus the texture/buffer desc tables **and their
parallel name tables, new in v2**) - NOT yet "compiled" in any real sense
(no ordering/culling has happened) despite the name; named this way so
Phase 3's `RenderGraphCompiler::Compile(CompiledGraphInput&&)` reads
naturally as "the raw material a compiler consumes." This hand-off
boundary is itself a natural Tier-1 test seam: a test can build a
`RenderGraphBuilder`, call `Finish()`, and assert on the resulting
`CompiledGraphInput`'s shape directly, without Phase 3 needing to exist
yet.

### 3.3 - `ImportTexture()` and the "imported resource" concept

An imported resource is a texture/buffer the render graph does NOT own the
lifetime of - it already exists (the swapchain image acquired this frame by
`FramePresenter`, or `ImGuiEditorLayer`'s own persistent `m_gameView`/
`m_sceneView` `RenderTexture`s) and the graph must treat reads/writes to it
exactly like any transient resource for barrier-synthesis purposes (Phase
5), while never trying to allocate OR free it (Phase 4). `ImportTexture()`
therefore stores the caller-supplied `RenderTarget` (already-resolved
`VkImage`/`VkImageView`/format/extent - see `src/Renderer/RenderTarget.h`)
directly in the resource table, tagged `IsImported = true`, alongside the
now-explicit, required `currentLayout` parameter (see 3.1 above) - this is
what lets Phase 5's barrier synthesis stop always assuming `UNDEFINED` as
`FrameRecorder` does today.

### 3.4 - Tests: `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`

Entirely Tier-1, since nothing in `RenderGraphBuilder` touches a live
device - `setup`/`execute` lambdas in every test are trivial (`setup`
declares a read/write; `execute` is typically an empty lambda or one that
increments a captured counter, to prove it is NOT invoked during `Finish()`
or during `AddPass()` itself):

- `CreateTexture()`/`CreateBuffer()` mint distinct, valid handles; calling
  either twice with the SAME desc still mints two DIFFERENT handles (two
  separate declared resources this frame, even if they end up POOLED to the
  same physical resource in Phase 4 - handle identity and physical-resource
  identity are deliberately different concepts, pinned by this test).
- **v2, new: `CreateTexture()`/`CreateBuffer()` called with two DIFFERENT
  `name` arguments but an otherwise IDENTICAL `desc` still mints two
  handles whose underlying `desc` values compare EQUAL (`operator==`) -
  proving that a resource's name genuinely has no bearing on Phase 4's
  pool-matching logic, the direct regression test for the bug fixed in
  Phase 1 v2.**
- `AddPass()`'s `setup` callback runs synchronously, exactly once, at the
  call site - proven by a captured counter.
- `AddPass()`'s `execute` callback is captured but NEVER invoked by
  `AddPass()`/`Finish()` themselves - proven by a captured counter staying
  zero all the way through `Finish()`.
- `PassBuilder::ReadTexture()`/`WriteColorAttachment()`/
  `WriteDepthStencilAttachment()` correctly append to that pass's
  `reads`/`writes` list with the right `ResourceAccess` tag.
- `ImportTexture()` produces a handle indistinguishable, from the pass
  author's point of view, from a `CreateTexture()`-minted one (same handle
  type, usable in `ReadTexture`/`WriteColorAttachment` identically) - but
  is tagged `IsImported` internally, verified via a `Finish()`-exposed
  accessor. **v2, new: also verify the `currentLayout` argument passed to
  `ImportTexture()` is exactly what's recorded and later retrievable
  (Phase 4/6 depends on this being threaded through correctly, not
  silently dropped or defaulted) - since this parameter did not exist in
  v1's code sketch at all, this test did not previously exist either.**
- A pass name that is `nullptr`/empty is rejected (an assertion in debug
  builds, mirroring `GTE_PROFILE_SCOPE`'s own static-storage-duration
  discipline) - add the regression test for this exact guard. The same
  guard/test now applies to `CreateTexture()`/`CreateBuffer()`/
  `ImportTexture()`'s own `name` parameter, since it is now the SOLE
  place a resource's name is captured (v1 arguably had this covered
  implicitly via `TextureDesc::debugName`'s own similar rule - now that
  field is gone, this test must explicitly cover the `name` parameter
  itself).

## Step 4: What We Will NOT Do (Focus)

- We will **not** implement `RenderGraphCompiler`, `RenderGraphResourcePool`,
  or `RenderGraph` itself in this phase - `Finish()` returns a plain,
  uncompiled `CompiledGraphInput` value and stops there.
- We will **not** allow a pass's `setup` callback to receive a live
  `VkCommandBuffer`, a `Renderer&`, or a `GpuResourceFactory&` - if a
  future requirement seems to need this, that is a sign the requirement
  belongs in `execute`, not `setup`; do not weaken this separation to make
  a single awkward call site more convenient.
- We will **not** support re-declaring/overwriting an existing pass by name
  (no "upsert" semantics, unlike e.g.
  `pptx_shape_elbow_connector`'s own "upserted by shape_name" convention
  elsewhere in this toolset) - every `AddPass()` call this frame adds a
  brand new pass; passes are NOT persistent, addressable-by-name objects
  across frames. A graph is rebuilt, in full, from scratch, every single
  frame (see Phase 6's own reasoning for why this is the correct,
  deliberate simplification for this engine's current scale, not a
  shortcut to fix later).
- We will **not** add any form of automatic resource-usage validation yet
  (e.g. "this pass reads a texture nothing ever writes" producing an error)
  - that class of validation is Phase 3's job, once the whole graph's
    shape is visible to compile against; `RenderGraphBuilder` itself stays a
    "dumb," unopinionated recorder of what it's told.
- We will **not** re-add a name/label field to `TextureDesc`/`BufferDesc`
  themselves to "simplify" this phase's API - see Phase 1 v2's own
  standing rule. A resource's name lives in this builder's own parallel
  name table, full stop.

## Step 5: Their Role (What does this mean for you?)

- If you are implementing this phase, resist adding "just one more"
  convenience overload before Phase 7 actually needs it - e.g. do not add
  `ReadTexture()` overloads that also bind a sampler/descriptor set right
  now; that is Phase 6's `PassContext` concern, once real physical
  resources exist to bind. Keep this phase's surface exactly as wide as
  "declare a resource, declare a pass's reads/writes, capture an execute
  callback," and no wider.
- When you eventually write Phase 7's real passes (a `GameViewPass`, a
  `SceneViewPass`, a `PresentPass`), you will be the first real consumer of
  this API - if anything about it feels awkward to use once you get there,
  that is useful signal to bring back and adjust THIS phase's design
  (amend this document, note the change, update its tests) rather than
  working around an awkward API silently inside Phase 7's own code.
- Keep `PassRecord`'s `reads`/`writes` vectors as `std::vector` for now
  (this phase is setup-time bookkeeping, not a per-frame hot path) - do not
  preemptively chase a fixed-size/no-allocation design here the way
  `Profiling::FrameProfiler`'s ring buffer had to (AGENTS.md's "nothing in
  the per-frame hot path may allocate" rule applies to code that runs EVERY
  frame at STEADY STATE measuring itself; a render graph's `setup` phase,
  by contrast, is exactly the kind of "happens once per frame, building a
  plan" code every other engine with this pattern accepts ordinary
  allocation in - this is a considered, not a lazy, choice, documented
  here so nobody "optimizes" it prematurely).
- Double-check every call site you write against `ImportTexture()`'s now-
  explicit, required `currentLayout` parameter - there is no default to
  silently fall back on, by design (see this document's own Revision
  Notes). If you're not sure what layout a resource is ACTUALLY in at the
  point you're importing it, that uncertainty is itself a sign something
  upstream (probably Phase 5/6's resource-state tracking) needs to be
  consulted first, not guessed at here.
