# GPU Vertex Skinning — Phase 3: RenderGraph Synchronization — v2

Part of the GPU Vertex Skinning campaign — see
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md` for the campaign map. Supersedes
`GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v1.md`. Depends on
Phase 2's compute kernel already compiling and being dispatchable in
isolation.

## V2 Revision Notes (read this first)

This revision was produced by re-reading v1 directly against the real,
current `RenderGraph.h`/`RenderGraph.cpp` source (attached in full to this
review). Two things changed:

- **V2 Revision Note 1 (the important one) — a genuine, unverified
  write-after-write (WAW) hazard.** v1's Step 3.3 asserted, without
  verifying it against the actual barrier-planning code, that "the
  compiler/barrier planner will then correctly serialize [two passes both
  writing the same buffer] with a `ComputeShaderWrite -> ComputeShaderWrite`
  barrier, exactly like it already does for any other resource written by
  two passes in sequence." Reading the REAL `RenderGraph.cpp` directly
  shows this is not obviously true. This document's new Step 3.6 works
  through the concrete risk and the required mitigation in full.
- **V2 Revision Note 2 — confirmed accurate, unchanged.** Every other v1
  claim in this document was checked directly against the real code and
  found correct: `RenderGraphBuilder::ImportTexture()`'s real shape
  (`TextureImportInfo`/`isImported`/`externalTarget`/`currentLayout`,
  confirmed via `RenderGraph.cpp`'s `EnsureTextureResolved()`),
  `EnsureBufferResolved()`'s real, confirmed lack of an import branch (its
  own comment says so verbatim), and `PassContext::resolveBuffer`
  ALREADY EXISTING and already wired up exactly as v1 assumed (confirmed
  directly in `RenderGraph.cpp`'s `ExecuteCompiledGraph()`, which already
  builds `ctx.resolveBuffer` from `physicalBuffers`). Sections 3.1-3.5 below
  are therefore carried over from v1 essentially unchanged (light wording
  fixes only) — the real, substantive new content is Step 3.6.

## Step 1: The Goal (Where are we going?)

Unchanged from v1: a GPU-skinned model's vertex buffer is written by a
compute pass and read by a graphics pass, both inside the same
`RenderGraph::Execute()` call, with the dependency expressed the same way
every other resource dependency in this render graph already is — a
declared read/write against a handle, automatically ordered and barriered
by the existing `RenderGraphCompiler`/`RenderGraphBarrierPlanner`.

Two small, general-purpose primitives are added to the shared render-graph
vocabulary:

1. `ResourceAccess::VertexBufferRead`.
2. `RenderGraphBuilder::ImportBuffer()`.

**A third thing this phase must now also deliver, per the v2 audit:**
provable, correct ordering between TWO writer passes that target the SAME
buffer handle in the SAME frame (the two-animators-sharing-one-Mesh
scenario) — not merely "a barrier exists between a writer and a reader",
which is all v1's design actually guaranteed.

## Step 2: The Situation / The Problem (Where are we now?)

Everything from v1's Step 2 remains accurate and is not repeated in full
here (see v1 for the complete `ResourceAccess`/`ImportTexture()`/
`EnsureBufferResolved()` gap analysis, all independently re-confirmed
against the real source for this revision). The one new problem this
revision adds:

**`RenderGraph.cpp`'s `ApplyUsageBarrierIfNeeded()` (real code, quoted in
full in this campaign's shared context) computes a barrier purely from a
per-resource `state` field that is compared against a freshly-computed
`next` value:**

```cpp
// Buffer branch, real code:
EnsureBufferResolved(usage.buffer.index, input, physicalBuffers);
PhysicalBuffer& buf = physicalBuffers[usage.buffer.index];
const ResourceState next = RequiredStateFor(usage.access, false);
if (RequiresBarrier(buf.state, next)) {
    EmitBufferBarrier(cmd, buf.buffer, 0, buf.size, buf.state, next);
}
buf.state = next;
```

`RequiredStateFor(access, isDepthResource)` is a **pure function of the
`ResourceAccess` enum value alone** — it has no notion of "which pass wrote
this last" or "has anything actually happened to this resource's contents
since I last computed this same value". This means: if Pass A (skinning
animator #1) writes `BufferHandle X` with access `ComputeShaderWrite`, and
later in the same frame Pass B (skinning animator #2, targeting the SAME
shared Mesh's output buffer, hence the SAME `BufferHandle X`) also writes it
with access `ComputeShaderWrite`, then:

- After Pass A's write, `buf.state` is set to `RequiredStateFor(ComputeShaderWrite, false)`.
- When Pass B's write is processed, `next` is computed as the exact SAME
  value, `RequiredStateFor(ComputeShaderWrite, false)`.
- `RequiresBarrier(buf.state, next)` is called with `buf.state == next`
  (bit-for-bit identical `ResourceState` values, since `RequiredStateFor()`
  is deterministic and pure).

**If `RequiresBarrier()` is implemented the ordinary, common way a
transition-barrier planner is implemented — "a barrier is needed only when
the required state actually differs from the current one" — then this
scenario produces `RequiresBarrier() == false`, and Pass B's write is
recorded with ZERO barrier separating it from Pass A's write.** On the GPU,
two `vkCmdDispatch` calls into the same command buffer with no memory
barrier declaring a dependency between them are **not guaranteed to
execute in program order at the memory-visibility level** — Vulkan's
execution model allows an implementation to overlap/reorder compute work
absent an explicit synchronization point. Two dispatches racing to write
the same buffer with no barrier between them is a textbook, spec-defined
WRITE-AFTER-WRITE hazard: undefined which write's bytes end up where,
potentially torn/interleaved results, and something the Vulkan validation
layers (`VK_LAYER_KHRONOS_synchronization2`) will flag loudly and
correctly as a real bug the moment anyone runs this scenario with
validation enabled.

This is **strictly worse** than today's CPU-path behavior for the exact
same two-instances-share-one-mesh scenario. Today's CPU path is fully
sequential (the "STRICTLY SEQUENTIAL" rule already enforced in
`AnimationSystem.cpp`) — whichever animator's `Mesh::UpdateVertexData()`
call happens to run later in the frame simply, deterministically,
harmlessly overwrites the earlier one's upload with well-defined bytes
("last write wins", a real but benign, already-documented visual
limitation). A GPU-side WAW hazard with no barrier is not "last write
wins" — it's **undefined**, and could in principle corrupt vertex data in
ways that don't even look like either animator's intended pose, or trip
validation errors / crash on some drivers.

**This must be verified, not assumed**, against the ACTUAL
`RenderGraphBarrierPlanner.cpp`/`RenderGraphTypes.cpp` source before this
phase is considered complete — that source was not available to this
review (only its header-level comments and consuming code in
`RenderGraph.cpp` were). It is entirely possible the real
`RequiresBarrier()` implementation already does something smarter (e.g.
always requiring a barrier for any write-vs-write pair regardless of
whether the two states are equal, since two writes are never safely
reorderable purely because they happen to target the "same access kind").
**Step 3.6 below gives the concrete verification procedure AND a
guaranteed-correct mitigation to apply regardless of what that
verification finds** — so this phase has a safe path forward either way.

## Step 3: The Plan (How will we get there?)

### 3.1 — Add `ResourceAccess::VertexBufferRead`

Unchanged from v1 — see that document's Step 3.1 in full for the exact
enumerator, its `IsWriteAccess()`/`ToString()`/`RequiredStateFor()`
placement, and the required Tier-1 test additions.

### 3.2 — Add `RenderGraphBuilder::ImportBuffer()`

Unchanged from v1 — see that document's Step 3.2 in full for the exact
`BufferImportInfo` shape, the `EnsureBufferResolved()` import branch, and
the required Tier-1 test additions.

### 3.3 — The real skinning pass(es)

Unchanged from v1 — see that document's Step 3.3 for the
`AddComputePass()`/`ReadBuffer()`/`WriteBuffer()` shape and the "phantom
read" declaration the consuming graphics pass needs.

### 3.4 — Where does the skinning pass actually run in the frame?

Unchanged from v1 — the `SynchronousImmediateReadback` regime, declared
into the same builder as Game View/Scene View, before Phase 5 wires the
runtime switch on top.

### 3.5 — Real render-graph pass ordering is by declared dependency, not call order

Restated from v1 (this remains true and is directly relevant to Step 3.6
below): `RenderGraph.cpp`'s own comment confirms "the compiler
topologically sorts by declared dependency, not by call order (see
RenderGraphCompiler)". This is exactly why Step 3.6's mitigation below
must work by adding a REAL declared dependency edge between the two writer
passes — reordering which `AddComputePass()` call happens first in the
`build` lambda changes nothing about the actual compiled execution order
or the barriers emitted.

### 3.6 — NEW: closing the WAW hazard for two writers sharing one output buffer

**Step 1 — verify, in code, before relying on anything else in this
section.** Before writing any skinning-pass code, open the real
`RenderGraphBarrierPlanner.cpp`/`.h` and answer, in writing, in this
phase's own completion report:

> "Given two passes A and B, both declaring `WriteBuffer(handle,
> ResourceAccess::ComputeShaderWrite)` against the SAME `BufferHandle`,
> with no other pass reading or writing that handle in between, does
> `RenderGraphBarrierPlanner::RequiresBarrier()` return `true` for B's
> write (i.e. does a real `vkCmdPipelineBarrier2`/`VkBufferMemoryBarrier2`
> get emitted between A's dispatch and B's dispatch)? Quote the exact
> function body reasoning that makes this true, or confirm it is false."

**Step 2 — apply this mitigation regardless of what Step 1 finds**, since
it is strictly correct either way and costs nothing extra when a barrier
would have been emitted anyway: change each GPU-skinning pass's own
`WriteBuffer()` declaration for the OUTPUT buffer from a plain write into a
**read-modify-write pair**:

```cpp
graphBuilder.AddComputePass("SkinModel:<debug name>",
    [&](RenderGraphBuilder::PassBuilder& pass) {
        pass.ReadBuffer(bindPoseHandle, ResourceAccess::ComputeShaderRead);
        pass.ReadBuffer(skinWeightsHandle, ResourceAccess::ComputeShaderRead);
        pass.ReadBuffer(boneMatricesHandle, ResourceAccess::ComputeShaderRead);
        // NEW in v2: declare a READ of this pass's own output buffer FIRST,
        // then the WRITE - even though this pass's own compute shader never
        // actually reads the prior contents of its output buffer. This is a
        // deliberate, documented "fake" read whose ONLY purpose is to force
        // a REAL dependency edge into the compiler's graph between this
        // pass and whichever pass most recently wrote this same handle -
        // see this document's own Step 3.6 for why a plain WriteBuffer()
        // pair between two same-access writers may not be barriered at all
        // by a state-diffing barrier planner. A ComputeShaderRead ->
        // ComputeShaderWrite transition on the SAME resource, by
        // construction, is NEVER a no-op transition (read and write are
        // different ResourceAccess values, so RequiredStateFor() produces
        // two DIFFERENT ResourceState values no matter how RequiresBarrier()
        // is implemented) - this makes the fix correct independent of
        // Step 1's verification result, not merely "probably fine".
        pass.ReadBuffer(outputHandle, ResourceAccess::ComputeShaderRead);
        pass.WriteBuffer(outputHandle, ResourceAccess::ComputeShaderWrite);
    },
    [&](rg::PassContext& ctx) { /* ... dispatch ... */ });
```

Document this pattern prominently, right at this call site, with a comment
pointing back at this document — a future reader who doesn't understand
why a compute pass declares a read of a buffer its own shader body never
actually samples from could easily "clean it up" as dead/redundant code,
which would silently reintroduce the exact hazard this fix exists to
close. This mirrors the exact same "this looks like dead code but is not"
concern v1 already flagged for the graphics pass's own phantom
`VertexBufferRead` declaration (Step 3.3) — now applied a second time, to
the writer side.

**Step 3 — an even simpler, complementary safety net: never actually
declare two GPU skinning passes against the same output buffer handle in
the same frame in the first place**, mirroring the exact de-duplication
Phase 4 already performs for MULTIPLE PARTS of the SAME model instance
(`PendingGroup`/`Mesh::VertexBufferIdentity()`). If, when collecting
"models needing a GPU skinning pass this frame"
(`AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()`, Phase 5),
TWO DIFFERENT SkeletalAnimator entities are both found to target the same
underlying shared output buffer (the two-instances-of-the-same-`*.gta`-file
scenario), this is a signal worth surfacing loudly (e.g. a one-time logged
warning the first time it's observed) even after Step 2's fix makes it
GPU-safe — because it still means one of the two animators' skinning work
is being silently discarded/overwritten every frame, the same pre-existing,
accepted visual limitation the CPU path has always had. Step 2 makes this
GPU-safe; it does not make it meaningful to run twice. This document does
NOT propose actually preventing the second dispatch (that would require
per-instance GPU mesh buffers, explicitly out of scope per the master
strategy) — only that the barrier fix in Step 2 is what keeps this
existing, accepted limitation from silently becoming a NEW, worse
(GPU-race) problem under GPU mode.

**Step 4 — validate with real tooling, not just visual inspection.** Once
Step 2's fix is in place, spawn two entities from the same rigged `*.gta`
file, each playing a different animation, in GPU mode, with the Vulkan
validation layers enabled (`VK_LAYER_KHRONOS_validation`, already this
engine's standard local development configuration per its Vulkan-heavy
codebase) and confirm ZERO `SYNC-HAZARD-WRITE-AFTER-WRITE` (or equivalent
synchronization-validation) messages appear in the validation output for a
full play session. This is the actual, trustworthy evidence Phase 0 v2's
new Must-Have #11 requires — not merely "the mesh rendered without an
obvious visual artifact".

## Step 4: What We Will NOT Do (Focus)

Unchanged from v1, plus one addition:

- **No attempt to give two same-model instances their own private GPU
  output buffers as part of closing the WAW hazard above.** Step 3.6's
  fix (a fake read-before-write dependency edge) closes the GPU-safety gap
  without touching the campaign's already-accepted "two instances share and
  fight over one Mesh" limitation at all — that limitation stays exactly as
  documented and out of scope, per the master strategy. Only the SAFETY of
  the resulting race (well-defined vs. undefined) changes, not whether the
  race's VISUAL outcome ("last write wins") happens at all.

## Step 5: Their Role (What does this mean for you?)

Same as v1 (land the enumerator + tests, land `ImportBuffer()` + tests,
then the real pass-declaration code, then manually confirm the graph via
the Editor's "Render Graph" panel), with Step 3.6 inserted as a new,
mandatory gate between "the skinning pass dispatches correctly for one
model" and "this is considered done": do not sign off on this phase, and
do not let Phase 5 proceed to shipping the runtime switch, until you have
either the written confirmation from Step 3.6's Step 1, or the applied fix
from Step 3.6's Step 2 plus the validation-layer-clean evidence from Step
3.6's Step 4, in your own completion report.
