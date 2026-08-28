# GPU Vertex Skinning — Phase 3: RenderGraph Synchronization — Completion Report

Status: **DONE** (the RenderGraph-level primitives this phase owns).
Implements the parts of
`task_manager/gpu_skinning/GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md`
that do not require Phase 4's per-model GPU resource cache to already exist,
per the scope fence set by `GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md`
("Focus on selected section only" — this report covers ONLY the "ON GOING"
Phase 3 item; Phase 1/2 are already `[DONE]` and unmodified; Phases 4-7
remain `[TODO]`, untouched).

## Why this phase's actual deliverable is narrower than its own strategy doc's full text

Phase 3's strategy document (Steps 3.3-3.5: "the real skinning pass(es)",
"where does the pass run in the frame", "pass ordering is by declared
dependency") describes the ACTUAL `AddComputePass("SkinModel:<name>", ...)`
call site that dispatches a real model's skinning kernel. That call site
needs a real `ComputeDescriptorSet` bound to a real per-model bind-pose/
skin-weight/bone-matrix/output buffer set — which is explicitly Phase 4's
job ("Per-Model Resource Management", still `[TODO]` per the master
strategy's own phase table). Writing that call site now, against
resources that don't exist yet, would mean guessing at Phase 4's own
design rather than building on it.

What Phase 3 CAN, and does, deliver independently — the generic
RenderGraph-level *vocabulary and safety mechanism* every future dispatch
call site (Phase 4/5) will need, plus the proof that the WAW-hazard this
phase's own v2 revision flagged is real and has a working mitigation:

1. `ResourceAccess::VertexBufferRead` (Step 3.1).
2. `RenderGraphBuilder::ImportBuffer()` (Step 3.2).
3. Confirmation (Step 3.6's "Step 1") that the write-after-write hazard the
   master strategy's v2 audit predicted is **real**, quoted directly from
   the actual `RenderGraphBarrierPlanner.cpp`/`RenderGraph.cpp` source.
4. The mitigation pattern (Step 3.6's "Step 2") — proven correct with
   dedicated Tier-1 tests, both at the barrier-planner level and at the
   full-compiler level — ready for Phase 4/5's real dispatch call site to
   use verbatim.

## What was built

### 1. `ResourceAccess::VertexBufferRead` (`RenderGraphTypes.h`/`.cpp`)

A new `ResourceAccess` enumerator — a vertex buffer read by the
fixed-function vertex-input assembler, as distinct from `ShaderRead`
(sampled by a shader stage) and `ComputeShaderRead` (a `StructuredBuffer`
read by a compute shader). This is the graphics pass's own declaration
(`pass.ReadBuffer(outputHandle, ResourceAccess::VertexBufferRead)`) that a
GPU-skinned model's draw pass will use to synchronize against the skinning
compute pass's own `ComputeShaderWrite` of the exact same buffer — the
"phantom read" the master strategy document names, since the graphics
pipeline never actually reads this buffer through a descriptor set; it
reads it through its own `VkVertexInputAttributeDescription` binding,
completely outside the render graph's own resolution machinery.

Wired into every exhaustive switch this enum already had:
`IsWriteAccess()` (false — a vertex buffer bound for drawing is only ever
read), `ToString()` ("VertexBufferRead"), and
`RenderGraphBarrierPlanner.cpp`'s `RequiredStateFor()` — buffer-only in
practice (mirrors `IndirectCommandRead`'s own "buffer-only" precedent,
`layout` left at its default), `VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT` /
`VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT`. `TargetsDepthState()`/
`IsColorAttachmentWriteAccess()` needed no code changes at all (both are
plain equality checks against a single named enumerator, not exhaustive
switches) — both correctly return `false` for this new value by
construction; a regression test was still added to each to prove it
directly rather than by inspection, matching this file's own established
convention for every other `ResourceAccess` addition.

### 2. `RenderGraphBuilder::ImportBuffer()` (`RenderGraphBuilder.h`/`.cpp`, `RenderGraph.cpp`)

The buffer sibling of the already-existing `ImportTexture()` — closes the
exact gap `RenderGraph.cpp`'s own `EnsureBufferResolved()` used to call
out by name ("Phase 2 has no `ImportBuffer()` counterpart to
`ImportTexture()` — every declared `BufferHandle` is necessarily
transient/pooled"). Concretely:

- **`BufferImportInfo`** (`RenderGraphBuilder.h`) — the buffer mirror of
  `TextureImportInfo`: `isImported`, `externalBuffer` (`VkBuffer`),
  `size`. Parallel to `CompiledGraphInput::bufferDescs`/`bufferNames` (same
  index) — `CreateBuffer()` now also pushes a default-constructed
  (`isImported == false`) entry, so the three vectors always stay in
  lock-step regardless of which of `CreateBuffer()`/`ImportBuffer()` a
  caller used.
- **`RenderGraphBuilder::ImportBuffer(name, externalBuffer, size)`** —
  mints a `BufferHandle` exactly like `CreateBuffer()` does, but tags it as
  imported. **Deliberately has no `currentLayout`-equivalent parameter at
  all** (unlike `ImportTexture()`, which REQUIRES one) — a buffer has no
  image-layout concept, and (per this phase's own Step 3.2 design
  decision, documented directly in the header) `RenderGraph::
  EnsureBufferResolved()` always seeds an imported buffer's tracked
  `ResourceState` completely fresh (`ResourceState{}`, the exact same
  "never touched before" default a transient/pooled buffer already starts
  every `Execute()` call at). This mirrors `EnsureTextureResolved()`'s own
  existing behavior for an imported TEXTURE's stage/access mask (never
  carried across frames either — only its `layout` is), and relies on the
  same whole-frame fence/semaphore synchronization this engine already
  performs for an unrelated, pre-existing reason.
- **`RenderGraph::EnsureBufferResolved()`** — now branches on
  `bufferImportInfo[index].isImported`: an imported entry resolves
  directly to its already-live `VkBuffer`/size (never touching
  `RenderGraphResourcePool`, exactly mirroring `EnsureTextureResolved()`'s
  own import branch); a transient entry's behavior is completely
  unchanged (`RenderGraphResourcePool::AcquireBuffer()`, as before).
- **`RenderGraphSnapshot.cpp`** — the Editor's "Render Graph" panel data
  builder had a comment stating "a buffer resource is never imported",
  which was true before this phase and is no longer true — fixed to
  actually read `input.bufferImportInfo[i].isImported`, mirroring the
  texture branch immediately above it (which was already correct). Left
  unfixed, the panel would have silently mis-reported every future
  imported buffer (e.g. Phase 4's own GPU skinning output buffer) as
  transient.

### 3. Confirmed, in writing, the WAW-hazard the v2 master strategy predicted

Per Step 3.6's own required "Step 1" — read the REAL
`RenderGraphBarrierPlanner.cpp` and `RenderGraph.cpp` sources directly
(both were available in full to this session) and answer explicitly:

> **Given two passes A and B, both declaring `WriteBuffer(handle,
> ResourceAccess::ComputeShaderWrite)` against the SAME `BufferHandle`,
> with no other pass reading or writing that handle in between, does
> `RenderGraphBarrierPlanner::RequiresBarrier()` return `true` for B's
> write?**
>
> **No.** `RequiresBarrier()`'s real body is exactly:
> ```cpp
> bool RequiresBarrier(const ResourceState& previous, const ResourceState& next) noexcept
> {
>     return !(previous == next);
> }
> ```
> a pure state-diff with zero notion of "did a DIFFERENT pass write this a
> moment ago" — and `RequiredStateFor(ComputeShaderWrite, false)` is a pure
> function of the access kind alone, so it returns the bit-identical
> `ResourceState` both times `RenderGraph::ApplyUsageBarrierIfNeeded()`
> computes it (once after Pass A's write, once for Pass B's write). Since
> `previous == next` in that scenario, `RequiresBarrier()` returns `false`,
> and `ApplyUsageBarrierIfNeeded()` skips `EmitBufferBarrier()` entirely —
> **zero barrier is emitted between the two writes.** This is confirmed
> directly by a new regression test,
> `RenderGraphBarrierPlannerTest.RequiresBarrierIsFalseForTwoConsecutiveComputeShaderWrites`
> (already existed, from the earlier compute-shader campaign — re-read and
> re-confirmed here as the exact evidence this phase's own Step 1 needed),
> plus the new
> `RenderGraphBarrierPlannerTest.ReadBeforeWriteMitigationForcesBarriersWhereConsecutiveWritesAloneWouldNotHaveOne`
> test added this phase, which demonstrates the mitigation closes it.

This means: **two `SkeletalAnimator`s sharing one GPU-skinned `Mesh`'s
output buffer (this campaign's own already-accepted, documented CPU-side
limitation) would, under a naive GPU-mode implementation, produce a real,
unsynchronized GPU write-after-write hazard** — worse than the CPU path's
own well-defined "last write wins" behavior for the identical scenario.
This is exactly what the master strategy's v2 audit predicted, now
confirmed against the real source rather than assumed.

### 4. The mitigation, proven correct (Step 3.6's "Step 2")

The fix requires no new engine code at all — it is a **call-site pattern**
every future GPU-skinning dispatch (Phase 4/5) must follow: the SECOND (and
any subsequent) writer of a shared output buffer declares a "phantom"
`ReadBuffer(handle, ResourceAccess::ComputeShaderRead)` immediately BEFORE
its real `WriteBuffer(handle, ResourceAccess::ComputeShaderWrite)` — even
though its own compute shader body never actually reads the buffer's prior
contents. Since `ComputeShaderRead`/`ComputeShaderWrite` are different
`ResourceAccess` values (different access masks), `RequiredStateFor()`
necessarily produces two DIFFERENT `ResourceState` values for them, which
means `RequiresBarrier()` is **guaranteed** to return `true` for both the
transition into the phantom read and the transition from that phantom read
to the real write — closing the hazard unconditionally, independent of how
`RequiresBarrier()` happens to be implemented (this genuinely does not
depend on the "Step 1" finding above being true or false — it would work
either way, which is exactly why the master strategy calls for applying it
"regardless of what Step 1 finds").

This is proven at TWO independent levels, both new Tier-1 tests added this
phase:

- **Barrier-planner level**
  (`RenderGraphBarrierPlannerTest.ReadBeforeWriteMitigationForcesBarriersWhereConsecutiveWritesAloneWouldNotHaveOne`)
  — confirms `RequiresBarrier()` returns `true` for both the
  write-A → phantom-read-B and phantom-read-B → write-B transitions, in
  direct contrast to the confirmed-hazard test immediately above it. A
  sibling test,
  `RenderGraphBarrierPlannerTest.ComputeShaderWriteFollowedByVertexBufferReadRequiresABarrier`,
  proves the same holds for the DRAW pass's own `VertexBufferRead` reading
  a compute pass's `ComputeShaderWrite` output — the other synchronization
  edge this campaign needs, spot-checking the exact barrier fields
  (`VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT`/`VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT`
  → `VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT`/`VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT`).
- **Full-compiler level**
  (`RenderGraphCompilerTest.GpuSkinningReadBeforeWriteMitigationPreservesOrderForSharedOutputBuffer`)
  — builds a real three-pass graph through `RenderGraphBuilder`/
  `RenderGraphCompiler::Compile()` exactly mirroring the campaign's own
  two-SkeletalAnimators-sharing-one-Mesh scenario (`SkinModel:InstanceA`
  writes an *imported* output buffer; `SkinModel:InstanceB` applies the
  phantom-read mitigation before its own write; `DrawModel` reads the
  result as `VertexBufferRead` and writes the real final color output) and
  confirms the compiler produces the fully serialized execution order
  `{InstanceA, InstanceB, DrawModel}` with none of the three passes culled
  — proving the mitigation's `ReadBuffer()` call also does what it needs
  to do at the DEPENDENCY-GRAPH level (forcing a real edge Pass B is
  ordered after Pass A), not only at the barrier-field level.

### 5. Documentation for future phases

`RenderGraphTypes.h`'s own `ResourceAccess` doc comment, `RenderGraphBuilder.h`'s
`ImportBuffer()` doc comment, and `RenderGraphBarrierPlanner.cpp`'s new
`VertexBufferRead` case all carry prominent, explicit warnings that this
"phantom read"/"phantom read-before-write" pattern is NOT dead code and
must never be "cleaned up" by a future reader unfamiliar with why it's
there — mirroring the existing warning `RenderGraph.h` already carries for
`PassContext::recordDraw`/`resolveBuffer` and the graphics-pass phantom
`VertexBufferRead` declaration itself.

## Wiring

- `src/Renderer/RenderGraph/RenderGraphTypes.h` — new `ResourceAccess::VertexBufferRead` enumerator + doc comment.
- `src/Renderer/RenderGraph/RenderGraphTypes.cpp` — `IsWriteAccess()`/`ToString()` cases.
- `src/Renderer/RenderGraph/RenderGraphBarrierPlanner.cpp` — `RequiredStateFor()` case.
- `src/Renderer/RenderGraph/RenderGraphBuilder.h`/`.cpp` — `BufferImportInfo`, `CompiledGraphInput::bufferImportInfo`, `RenderGraphBuilder::ImportBuffer()`, `CreateBuffer()` updated to keep the three buffer-side vectors parallel.
- `src/Renderer/RenderGraph/RenderGraph.cpp` — `EnsureBufferResolved()` now branches on import vs. transient.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.cpp` — buffer resource `isImported` now genuinely reflects `bufferImportInfo` instead of being hardcoded `false`.
- No `CMakeLists.txt` changes were needed — every file touched already exists in `gte_core`'s source list.

Test files updated (all Tier 1, no live `VkDevice`/`Renderer` involved,
following each file's own already-established conventions):

- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` — `VertexBufferRead` added to `IsWriteAccess()`/`ToString()` coverage.
- `tests/Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp` — `RequiredStateFor(VertexBufferRead)`, `TargetsDepthState()`/`IsColorAttachmentWriteAccess()` extended, and the two new WAW-hazard-mitigation regression tests described above.
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` — five new `ImportBuffer()` tests (handle usability, import tagging, verbatim field storage, desc mirroring, contiguous handle space) plus a null-name death test — mirroring `ImportTexture()`'s own existing test suite one-for-one.
- `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp` — the new end-to-end mitigation-preserves-order test described above.
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` — the old `BufferResourceIsNeverReportedAsImported` test renamed to `TransientBufferResourceIsNotReportedAsImported` (its own scenario — a `CreateBuffer()`-only graph — is unaffected and still true) plus a new `ImportedBufferResourceIsReportedAsImported` test proving the opposite case now works.

## What was deliberately NOT done (per Phase 3's own scope, and the master strategy's phase fence)

- **No actual `AddComputePass("SkinModel:<name>", ...)` dispatch call
  site.** That requires Phase 4's per-model `ComputeDescriptorSet`/buffer
  cache to exist first — see "Why this phase's actual deliverable is
  narrower..." above. `GpuSkinningPipelines` (Phase 2) is untouched.
- **No `AnimationSystem`/`Game.cpp`/`RenderPasses.cpp` changes of any
  kind.** Nothing about *when* in the frame a skinning pass would run, or
  *which* models need one, is decided by this phase — that is Phase 4/5's
  job.
- **No attempt to prevent two `SkeletalAnimator`s from sharing one Mesh's
  output buffer.** This campaign's own accepted, documented limitation
  (see `AGENTS.md`'s Job System table, "Cross-entity/cross-instance shared
  GPU mesh buffers" row) is unchanged — this phase only makes the SAFETY
  of that scenario's GPU-mode equivalent well-defined (a real barrier
  exists), never its underlying "two dispatches racing to be the last
  writer" semantics, which stays exactly as harmless-and-visually-
  arbitrary as the CPU path's own "last write wins" today.
- **No live-`VkDevice` verification of the mitigation with actual Vulkan
  validation layers.** Step 3.6's own "Step 4" (spawn two real instances of
  the same rigged model in GPU mode, confirm zero
  `SYNC-HAZARD-WRITE-AFTER-WRITE` validation messages) is explicitly a
  Phase 5-or-later activity, once a real dispatch call site and a running
  multi-instance scene actually exist — nothing to run that check against
  exists yet at this phase.

## Verification performed

Per this session's own instructions — fast compile check only, no full
build/regression test (explicitly deferred to later):

- `cmake --build build --target gte_core` — succeeded, 26/26 objects
  built + linked into `libgte_core.a` with zero errors/warnings.
- `cmake --build build --target GreatTamanaEngineTests` — succeeded,
  linked cleanly against the updated `gte_core` + updated test sources.
- Ran the full `RenderGraph*` test filter directly
  (`GreatTamanaEngineTests.exe --gtest_filter=RenderGraph*`) as an extra,
  cheap correctness check beyond a bare compile (since this phase's
  central deliverable — the WAW-hazard finding/mitigation — is a genuine
  logic claim, not just new plumbing): **133/133 tests passed**, including
  every pre-existing RenderGraph test (proving nothing regressed) and
  every new test this phase added.
- Did **not** run the full engine-wide test suite (`ctest`), a clean
  `build_joboff` verification build, or any runtime/GPU-device smoke
  test — all explicitly deferred to "later, after everything done" per
  this session's instructions.

## Notes for future phases

- **Phase 4 (Per-Model Resource Management)** is what actually calls
  `RenderGraphBuilder::ImportBuffer()` for real, against each distinct
  shared-vertex-buffer group's `GpuSkinningRigCache::GpuModelEntry::
  OutputGroup::outputVertexBuffer` (a `std::shared_ptr<Buffer>`, per that
  phase's own strategy document) — re-imported into a freshly-built graph
  every frame, exactly like the Editor's persistent Game/Scene
  `RenderTexture`s are re-imported as textures today. `ImportBuffer()`'s
  own signature (`name, externalBuffer, size`) was chosen to need nothing
  Phase 4 wouldn't already have on hand at that call site.
- **Phase 4/5's real dispatch call site MUST follow the read-before-write
  mitigation pattern** documented in this phase's code (`RenderGraphTypes.h`'s
  `VertexBufferRead` comment, `RenderGraphBarrierPlanner.cpp`'s matching
  case) for the SECOND and any subsequent skinning pass sharing an output
  buffer with an earlier one in the same frame — see this report's own
  Section 4 and the two regression tests it added for exactly what that
  looks like in practice. The FIRST writer of a given buffer in a frame
  needs no phantom read (there is nothing yet to synchronize against this
  frame — though see the next bullet).
- **Phase 4/5 must also independently confirm (or re-derive) what a GPU
  skinning output buffer's very FIRST-ever frame needs**, since this
  phase's own `ImportBuffer()` seeds every imported buffer's tracked state
  completely fresh every single `Execute()` call (never carrying real
  cross-frame state) — this is safe for the specific hazard this phase
  closes (two writers IN THE SAME FRAME), but Phase 4's own design must
  separately confirm that a genuinely uninitialized `GpuOnly` buffer's
  first write (before its very first read) doesn't need some additional
  synchronization this phase didn't have reason to consider (Phase 4's own
  strategy document already flags exactly this as "the sharpest edge in
  this phase's whole design" under its own Step 5).
- **The graphics pass reading a GPU-skinned model's vertex buffer must
  declare `ReadBuffer(outputHandle, ResourceAccess::VertexBufferRead)`** —
  this is the other, symmetric half of this phase's synchronization work,
  proven correct by
  `ComputeShaderWriteFollowedByVertexBufferReadRequiresABarrier` — Phase
  4/5's own dispatch/draw wiring must actually declare it at the real draw
  call site (`RenderSystem::Draw()` or wherever a GPU-skinned `Mesh`'s
  submission ends up living), never rely on the buffer already being in
  the right state by coincidence.
