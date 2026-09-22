# PHASE3 Strategy-Document Double-Check Report (`render-pass-5`)

## Scope

This was a **strategy-document-only** pre-implementation review of
`PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md`, requested by the
project owner as a dedicated pre-check on one of this campaign's two
named highest-risk phases (PHASE0's Locked Design Decision 10), performed
BEFORE the whole 7-phase strategy set gets its general second-iteration
review. No source code was written or modified; no build/compile was run.

Read in full: `readme.md`, `AGENTS.md`, `PHASE0_MASTER_STRATEGY.md`,
`PHASE1_FOUNDATIONS_BOUNDS_INDIRECT_TYPES_AND_VOCABULARY.md`,
`PHASE2_INSTANCED_DRAW_PRIMITIVE_AND_INDIRECT_SUBMIT.md`, and
`PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md` (the focus of this
review). PHASE3's plan was then cross-checked directly against the actual,
current repository source rather than trusted at face value:

- `src/Renderer/GpuSkinning/GpuSkinningPipelines.h/.cpp` (the precedent
  PHASE3 says to mirror for `CullingPipelines`).
- `src/Renderer/GpuSkinning/GpuSkinningTypes.h` and
  `src/Shaders/SkinVerticesPositionNormal.comp` (the precedent for
  GPU-buffer-layout structs and how a "must stay tightly packed" output
  buffer is actually declared in real, shipped GLSL).
- `src/Renderer/ComputePipeline.h`, `src/Renderer/ComputeDescriptorSet.h`,
  `src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h`,
  `src/Renderer/ComputeDispatch.h`.
- `src/Renderer/RenderGraph/RenderGraphTypes.h` and
  `RenderGraphBarrierPlanner.h/.cpp` (for the `ResourceAccess`/barrier
  claims PHASE0/PHASE1 make).
- `src/Renderer/GpuResourceFactory.cpp` (`CreateStructuredBuffer()`'s real
  `extraUsage`/automatic-`TRANSFER_DST_BIT` behavior).
- `src/Application/RenderPasses.cpp`'s `AddGpuSkinningPasses()` (the real,
  shipped shape of a compute-pass-writes/graphics-pass-reads render-graph
  wiring, for context on what PHASE5 will eventually do with PHASE3's
  shader).
- `src/Shaders/AtmosphereAerialPerspectiveComposite.comp` (an existing,
  shipped compute push-constant block with a `mat4` + `vec4` combination,
  used to confirm this codebase's real, already-respected 128-byte
  push-constant budget convention).
- `src/Math/Mat4.h`/`Mat4.cpp` (confirming column-major storage, resolving
  an ambiguity PHASE1 itself flagged as needing verification).
- `task_manager/render-pass-5/PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md`
  (read to understand the per-batch buffer cache's real lifecycle
  policy — its "capacity only grows" rule is what makes one of this
  report's findings concrete and reachable, not theoretical).

## What was found

PHASE3's overall shape, its choice of precedent to mirror
(`GpuSkinningPipelines`), its binding-table concept, and its central claim
about `firstInstance`/`gl_InstanceIndex` semantics were all confirmed
**correct** against the real source and the Vulkan spec. Three concrete,
load-bearing gaps were found and fixed directly in
`PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md` (now overwritten in
place — see that file's own new "Pre-implementation strategy-document
double-check" preface note and the "⚠️ CORRECTED" markers throughout):

1. **Incorrectness (would have produced a real GPU memory-layout bug):**
   the original plan described the output indirect-command buffer only as
   `IndirectDrawCommand[]` / `buffer` (std430), with no GLSL declaration
   shown. `IndirectDrawCommand` (PHASE1) is 20 bytes of plain scalar
   members (5 x `uint`/`int32`) — NOT a multiple of 16. Declared as an
   ordinary GLSL `struct` array under `std430`, the GLSL/Vulkan spec's own
   structure-alignment rule (structs are rounded up to `vec4`/16-byte
   alignment EVEN under `std430`, which only relaxes this rounding for
   arrays of bare scalars/vectors, never arrays of structs) would silently
   produce a 32-byte stride instead of the required tightly-packed 20
   bytes — corrupting every element after the first and breaking direct
   consumption by `vkCmdDrawIndexedIndirect(Count)`. This is the exact
   same hazard `GpuSkinningTypes.h`/`SkinVerticesPositionNormal.comp`
   already had to solve for their own tightly-packed output buffer (a flat
   `float values[]` array with manually computed offsets, explicitly
   documented as "deliberately NOT std430-vec-padded"). The corrected
   PHASE3 now mandates the identical flat-array treatment for this buffer,
   with the exact GLSL shown, and calls this out explicitly as
   load-bearing, not stylistic.
2. **Missing piece (never mentioned anywhere in PHASE0-4's own drafts —
   confirmed via `search_in_dir` for "reset"/"FillBuffer", zero hits):**
   the atomic visible-count buffer (binding 2) must be reset to exactly
   `0` before every dispatch of this shader, and the shader cannot safely
   do this itself (GLSL compute has no cross-workgroup ordering guarantee
   within one dispatch, so a "have one invocation reset it first" scheme
   is a genuine data race against every other workgroup's own
   `atomicAdd`). The corrected PHASE3 now states this as an explicit
   precondition/invariant up front, explains why it can't be the shader's
   own job, names the exact mechanism PHASE5 should use
   (`vkCmdFillBuffer` + a `ResourceAccess::TransferDst` write ordered
   before the culling pass's own `ComputeShaderWrite` — confirmed
   buildable with zero new buffer-creation change, since
   `CreateStructuredBuffer()`'s `GpuOnly` path already ORs in
   `VK_BUFFER_USAGE_TRANSFER_DST_BIT` automatically), and adds a concrete
   sub-check to PHASE3's own manual-verification harness (3.4) that
   empirically PROVES the bug this reset prevents (dispatch twice without
   resetting, confirm the count is wrong) rather than leaving it as an
   unverified theoretical claim.
3. **Missing piece / reachable bug, confirmed concrete by cross-referencing
   PHASE4's own documented buffer-lifecycle policy:** the plan never pinned
   down what value the CPU must pass as `Renderer::SubmitIndirect()`'s
   `drawCount`/`maxDrawCount` argument for the degenerate-padding
   (`useCompaction == 0`) branch. PHASE4 documents that a batch's own
   buffer capacity only GROWS across frames, never shrinks
   (`EnsureCapacity()` "reallocates ONLY when instanceCount GREW"). If a
   batch shrinks (e.g. 10 live instances last frame, 6 this frame), this
   shader's dispatch only ever touches slots `0..5` this frame — slots
   `6..9` still contain REAL, non-degenerate commands left over from the
   previous, larger frame. If the CPU then issued the fixed-count draw
   using the buffer's CAPACITY (10) instead of THIS FRAME'S actual
   instance count (6), it would silently redraw 4 ghost instances that no
   longer exist in the scene — a real, reachable, visible rendering bug.
   The corrected PHASE3 now states this invariant explicitly (the CPU-side
   draw count must always equal this exact dispatch's own
   `pc.instanceCount`, never the buffer's physical capacity) and adds a
   matching sub-check to 3.4's verification harness that reproduces the
   bug on purpose (dispatch once at N instances, then again at a smaller
   M, confirm stale real commands remain in slots `M..N-1`) so the
   corresponding requirement is empirically informed, not a guess.

Two smaller, load-bearing ambiguities were also resolved (not bugs, but the
task's own "insufficiency" criterion — too vague to implement without
guessing):

4. The push-constant block's exact shape and total byte size were left
   genuinely open ("mat4 viewProj... OR pass 6 already-extracted planes
   instead, whichever the implementer finds cleaner"). This is now a firm
   recommendation (precompute planes on the CPU via PHASE1's own
   already-tested `ExtractFrustumPlanes()`, pass as `vec4 planes[6]`),
   with an explicit byte-budget table proving the recommended choice (104
   bytes) and the rejected alternative (72 bytes alone) both fit inside
   this engine's real, already-respected 128-byte push-constant
   convention (confirmed against the real, shipped
   `AtmosphereAerialPerspectiveComposite.comp`, which already uses 96
   bytes for a compute push-constant block) — and explicitly flags the
   one combination that would NOT fit (mat4 + planes together, 168 bytes)
   as something a future edit must not accidentally do. This recommendation
   also directly reduces PHASE0's own single biggest named risk for this
   phase ("a wrong culling test... no compiler catches this") by removing
   an entire piece of new, tricky GLSL math (matrix-based plane extraction)
   from the shader altogether, leaving only the simpler, more mechanical
   AABB-vs-plane test as genuinely new GLSL.
5. The AABB-vs-frustum p-vertex test itself, and a full worked numeric
   example (a 5-instance batch with 2 culled instances, showing the exact
   resulting buffer contents for both `useCompaction` branches side by
   side), were both missing from the original plan and are now spelled out
   verbatim in the corrected document.

## What was confirmed already correct (no change needed)

- The `firstInstance`/`gl_InstanceIndex` semantics claim in the original
  plan is correct per the Vulkan spec (`gl_InstanceIndex = firstInstance +
  <instance-loop index>`; since every emitted command here always has
  `instanceCount == 1`, this always reduces to `gl_InstanceIndex ==
  firstInstance`).
- The overall `CullingPipelines` class shape (one shared descriptor-set
  layout, one shared `ComputePipeline`, lazy `EnsureInitialized()`) matches
  `GpuSkinningPipelines` byte-for-byte, confirmed by direct read of the
  real file.
- `GpuCullingInstanceInput`'s own `std430` layout (PHASE1) needs no special
  flat-array treatment the way the output buffer does — it was already
  deliberately sized as vec4-multiples (112 bytes total) specifically to
  avoid the same hazard, confirmed by re-deriving the `std430`
  structure-alignment rule against it directly.
- `Math/Mat4.h` is confirmed column-major, matching GLSL's own `mat4`
  layout — no transpose needed anywhere in this phase's packing/shader
  code, resolving an ambiguity PHASE1's own draft explicitly flagged as
  needing verification.
- `ComputePipeline`/`ComputeDescriptorSet`/`DescriptorSetLayoutBuilder`/
  `ComputeDispatch.h` are all real, shipped, and require zero changes to
  support this phase, as PHASE3 already claimed.

## Flagged for the upcoming whole-campaign second-iteration review (NOT fixed here, per this task's own scope)

These two points involve PHASE4/PHASE5, not PHASE3, so — per this task's
explicit instructions — they were **not** edited into either of those
files. They are called out here so the whole-campaign review does not miss
them:

- **PHASE4 and PHASE5 currently contain no mention at all of the atomic
  count-buffer reset requirement** (confirmed by `search_in_dir` across the
  whole `render-pass-5` folder for "reset"/"FillBuffer" — zero hits
  anywhere except this now-corrected PHASE3 file). PHASE5, which is the
  phase that actually wires the real render-graph pass, needs an explicit
  `vkCmdFillBuffer`-based (or equivalent) reset step declared and ordered
  before the culling compute pass every frame — PHASE3 now documents
  exactly what's needed and why, but PHASE5's own document should be
  updated during the general review to actually include this step in its
  own pass-wiring plan, not just rely on a forward-reference into PHASE3.
- **PHASE4's own per-batch buffer cache document should explicitly note**
  that whatever value ends up threaded through to
  `Renderer::SubmitIndirect()`'s draw-count argument for the
  degenerate-padding branch must be the CURRENT frame's `instanceCount`,
  never the cache's own (monotonically-growing) buffer capacity — PHASE4's
  current draft doesn't surface this distinction at all, since it never
  discusses the fallback path's CPU-side draw call in the first place
  (that's arguably PHASE5's job, but PHASE4 is where the "capacity only
  grows" policy that makes this dangerous actually lives, so a one-line
  cross-reference there would help whoever writes PHASE5 avoid missing
  it).
- Independently, `GpuCullingInstanceInput` (PHASE1) stores `firstIndex`/
  `indexCount`/`vertexOffset` PER INSTANCE, even though every instance in
  one batch necessarily shares the exact same `Mesh` (and therefore the
  exact same values for all three fields) — a minor, harmless data
  redundancy (112 bytes/instance instead of a smaller shared value), not a
  correctness bug, and not touched here since it's PHASE1's own struct
  design, not PHASE3's. Worth a one-line mention in the general review in
  case a future bandwidth-sensitive revision wants to hoist those three
  fields to a single per-batch push constant/uniform instead of
  duplicating them per instance — not urgent, callable out as a "nice to
  have" only.
- The atomic count buffer's required Vulkan usage flags
  (`VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT` in addition to
  `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`) were left as an open question in
  PHASE4's own draft ("confirm against the Vulkan spec... before
  finalizing"). This report confirms the answer directly (yes, both flags,
  simultaneously, on the same buffer — legal Vulkan) and the corrected
  PHASE3 document now states this plainly in its own Situation section, so
  PHASE4 no longer needs to re-derive it — but PHASE4's own file itself
  still has the old open-question phrasing and could be tightened during
  the general review.

## Outcome

`PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md` was overwritten in
place with a corrected, more fully-specified version (exact GLSL for all
three SSBO bindings, a pinned-down push-constant shape/byte-budget, explicit
AABB-vs-frustum pseudocode, a worked numeric example, and the three
correctness invariants above spelled out as explicit preconditions with
matching verification sub-checks added to section 3.4). No other `.md` file
in this campaign was modified, per this task's scope. No source code was
written or changed. No build/compile was run (this was a document-only
review).
