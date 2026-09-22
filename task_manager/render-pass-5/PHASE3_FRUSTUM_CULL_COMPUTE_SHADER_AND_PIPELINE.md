# PHASE3 — The Real GPU Frustum-Culling Compute Shader ⚠️ HIGH-RISK / DEDICATED DOUBLE-CHECK PHASE

## Parent
`PHASE0_MASTER_STRATEGY.md` — read it first. Also re-read
`PHASE1_COMPLETION_REPORT.md` and `PHASE2_COMPLETION_REPORT.md` before
starting.

**This phase is one of the two dedicated-double-check phases named in
PHASE0's Locked Design Decision 10.** Once this phase's code compiles and its
own manual verification passes, the orchestrator delegates a SEPARATE,
DEDICATED `delegate_task` review of ONLY this phase's diff/output before
moving on to PHASE4 — do not skip that step, and do not fold it into the
later whole-campaign second-iteration review.

> **Pre-implementation strategy-document double-check (2026-09-22).** This
> document was cross-checked against the actual, current repository source
> (`GpuSkinningPipelines.h/.cpp`, `ComputePipeline.h`, `ComputeDescriptorSet.h`,
> `DescriptorSetLayoutBuilder.h`, `ComputeDispatch.h`, `RenderGraphTypes.h`/
> `RenderGraphBarrierPlanner.h/.cpp`, `GpuSkinningTypes.h`,
> `SkinVerticesPositionNormal.comp`, `AtmosphereAerialPerspectiveComposite.comp`,
> `Math/Mat4.h`) **before any of this phase's own code was written**, per the
> project owner's request for a dedicated pre-check on this campaign's two
> highest-risk phases. Three real, load-bearing corrections came out of that
> pass and are now folded directly into the plan below (search for "⚠️
> CORRECTED" to find each one): (1) the output indirect-command buffer's GLSL
> layout was under-specified in a way that would have silently produced the
> WRONG byte stride under `std430` rules; (2) the atomic visible-count
> buffer's required per-frame reset was never mentioned anywhere in this
> campaign's docs at all; (3) the degenerate-padding branch's CPU-side draw
> count was never pinned to the exact value it must equal, which — combined
> with PHASE4's own documented "buffer capacity only grows, never shrinks"
> policy — is a concrete, reachable ghost-geometry bug once a batch's instance
> count decreases frame-to-frame. See `PHASE3_STRATEGY_DOUBLE_CHECK_REPORT.md`
> (this same folder) for the full writeup of everything checked.

## Step 1: The Goal (Where are we going?)

Write the actual culling feature's core: a compute shader that reads one
`GpuCullingInstanceInput` per instance (PHASE1), tests its world-space AABB
against the active camera's 6 frustum planes, and emits either:
- a COMPACTED array of `IndirectDrawCommand`s (only surviving instances,
  atomic-appended, plus a real visible count) — for a device where
  `Renderer::SupportsDrawIndirectCount()` is true, or
- a FULL-WIDTH, ORIGINAL-ORDER array of `IndirectDrawCommand`s (every input
  instance gets a slot; a culled instance's slot has `indexCount = 0`, a
  valid, harmless degenerate draw) — for a device where it's false.

Both branches are real, both are built, and — per PHASE0's Locked Design
Decision 4 — neither this document nor the shader's own source may mention
or depend on which specific development machine supports which branch; the
shader picks its own behavior via ONE push-constant flag, set once per batch
by the CPU based on the capability query PHASE2 already added.

**Two correctness invariants this shader's callers (PHASE4/PHASE5) MUST
uphold, spelled out explicitly here because nothing else in this campaign's
docs currently does (see 3.1/3.2 below for the full reasoning):**
1. The atomic visible-count buffer (binding 2) must contain exactly `0`
   before every dispatch of this shader — this shader never resets it itself
   (it cannot, safely — see 3.1).
2. Whatever `drawCount`/`maxDrawCount` value the CPU passes to
   `Renderer::SubmitIndirect()` for the degenerate-padding
   (`useCompaction == 0`) branch must be EXACTLY this same dispatch's
   `pc.instanceCount` — never a larger, previously-grown buffer capacity
   (see 3.2).

## Step 2: The Situation (Where are we now?)

- `ComputePipeline`/`GpuResourceFactory::CreateComputePipeline()`/
  `DescriptorSetLayoutBuilder`/`ComputeDescriptorSet`/`Renderer::Dispatch()`
  are all real, shipped, and directly reusable — see
  `src/Renderer/GpuSkinning/GpuSkinningPipelines.h/.cpp` for the EXACT class
  shape this phase's own `CullingPipelines` class mirrors (lazy
  `EnsureInitialized()`, one shared `VkDescriptorSetLayout` + one shared
  `ComputePipeline`, a named `kCullingLocalSizeX` constant mirroring
  `kSkinningLocalSizeX`'s own doc comment about the "hand-maintained, never
  tool-enforced" local-size-vs-C++-constant pairing convention) — confirmed
  by direct read of both files during this pre-check.
- `GpuResourceFactory::CreateStructuredBuffer()`/
  `Renderer::CreateStructuredBuffer()` already accept `extraUsage` —
  the indirect-command output buffer must be created with
  `extraUsage = VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT` (it is ALSO written as a
  plain storage buffer by this compute shader, so
  `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` — already always-ORed-in by
  `CreateStructuredBuffer()` — plus this `extraUsage` flag together, exactly
  as that method's own doc comment anticipates). Confirmed by direct read of
  `GpuResourceFactory.cpp`: for `BufferMemoryUsage::GpuOnly` (what every
  buffer this shader touches will use), `VK_BUFFER_USAGE_TRANSFER_DST_BIT` is
  ALSO always ORed in automatically — this is directly relevant to the
  count-buffer-reset requirement below (3.1): a `vkCmdFillBuffer`-based reset
  needs exactly that usage bit, and PHASE4/5 get it for free with zero extra
  buffer-creation change.
- The atomic count buffer's own required Vulkan usage flags are pinned down
  here rather than left open (PHASE4's own draft left this as an open
  question — "confirm against the Vulkan spec's own requirement for the
  count buffer's usage flags before finalizing"): per the Vulkan spec,
  `vkCmdDrawIndexedIndirectCount`'s `countBuffer` argument **must** have been
  created with `VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT` set, in addition to
  `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` (needed for this shader's own
  `atomicAdd`/write access to it as a plain SSBO) — both flags together, on
  the SAME buffer, is completely legal Vulkan (usage flags are additive/
  independent of how a specific pipeline stage binds the buffer).
- PHASE1 already produced `GpuCullingInstanceInput` (the shader's read-only
  input), `IndirectDrawCommand` (the shader's write-only output element
  shape), and a pure C++ reference implementation of the exact math
  (`ExtractFrustumPlanes()`/`AABBIntersectsFrustum()`) this shader's GLSL must
  match — this is this phase's PRIMARY correctness anchor: whenever in doubt
  about the shader's own math, re-derive it from the PHASE1 C++ function it
  mirrors, never from first principles again.
- `Math/Mat4.h` is confirmed COLUMN-MAJOR storage (`Mat4.h` line ~10's own
  class comment, `Mat4.cpp`'s adjugate/determinant code, and
  `SkinVerticesPositionNormal.comp`'s own comment — "`Mat4::Data()` is already
  column-major, matching GLSL's `mat4` layout exactly, so this needs zero
  repacking") — confirmed directly during this pre-check, removing the
  ambiguity PHASE1's own doc flagged ("VERIFY... if Math/Mat4.h is row-major
  internally, must transpose"). **No transpose is needed anywhere in this
  phase**, whether `viewProj`/`worldMatrix` is uploaded as a push constant or
  read from `GpuCullingInstanceInput`.
- PHASE2 already produced the device capability probe
  (`Renderer::SupportsDrawIndirectCount()`) this shader's dispatch-time push
  constant is set from.
- `SkinVerticesPositionNormal.comp` (this campaign's own cited GLSL
  precedent) establishes a load-bearing convention this phase's own output
  buffer MUST copy: its own skinned-output buffer is **deliberately declared
  as a flat, manually-indexed `float values[]` array — NOT a GLSL `struct`
  array** — specifically because a `std430` SSBO array-of-structs rounds each
  element's stride up to a multiple of 16 bytes even when every member of the
  struct is a plain 4-byte scalar (the GLSL/Vulkan `std430` spec relaxes this
  16-byte rounding ONLY for arrays of bare scalars/vectors, never for arrays
  of structs — structs keep the `std140`-style "round up to `vec4`
  alignment" rule even under `std430`). This exact hazard applies to this
  phase's own output buffer too (⚠️ CORRECTED — see 3.1/3.2 below): a naive
  `struct IndirectCommandGlsl { uint indexCount; uint instanceCount; uint
  firstIndex; int vertexOffset; uint firstInstance; }` (20 bytes of scalar
  members) declared as an SSBO array-of-structs would silently get a 32-byte
  stride, not the 20-byte stride `IndirectDrawCommand`'s own
  `static_assert(sizeof(IndirectDrawCommand) == 20)` (PHASE1) and this
  buffer's CPU-side sizing/`vkCmdDrawIndexedIndirect(Count)` stride argument
  both assume.

## Step 3: The Plan

### 3.1 — Binding table (document this table verbatim inside the shader's own header comment, mirroring `GpuSkinningTypes.h`'s own binding-table precedent)

| Binding | Resource | GLSL qualifier |
|---|---|---|
| 0 | Per-instance input (`GpuCullingInstanceInput[]`) | `readonly buffer` (std430, array-of-structs — SAFE here, see note below) |
| 1 | Output indirect-command array (`IndirectDrawCommand[]`, sized to at least `instanceCount` entries) | `buffer` (std430) — **⚠️ CORRECTED: MUST be declared as a flat, manually-indexed array of scalars, mirroring `SkinVerticesPositionNormal.comp`'s own `OutputBuffer.values[]` precedent — see the exact GLSL below. A plain GLSL `struct` array here would silently misalign (32-byte stride instead of the required tightly-packed 20-byte stride) and corrupt every element after the first — this is NOT a stylistic choice, it is load-bearing correctness.** |
| 2 | Atomic visible-count buffer (single `uint`) | `buffer` (std430) — **must contain `0` when this dispatch begins; see the reset requirement below.** |

Binding 0 is safe as a plain GLSL struct array because `GpuCullingInstanceInput`
(PHASE1) was deliberately designed as a `mat4` + `vec4` + `vec4` + `uvec4`
shape (112 bytes, already a multiple of 16) specifically so its natural
`std430` struct alignment/stride (16, per the "round up to `vec4`" structure
rule) produces exactly 112 bytes with zero implicit padding — confirmed by
re-deriving the `std430` structure-alignment rule against this exact struct
during this pre-check. Binding 1's element (20 bytes, NOT a multiple of 16)
gets no such free pass, which is exactly why it needs the flat-array
treatment instead.

```glsl
// Binding 0 - matches GpuCullingInstanceInput (CullingTypes.h) exactly.
// Safe as a plain struct-array under std430 (see this file's own header
// comment): every member is already 16-byte-aligned/sized, so the struct's
// own std430 stride (112 bytes) needs no implicit padding.
struct CullingInstance {
    mat4 worldMatrix;
    vec4 aabbMinAndPad;   // .xyz = local-to-world-transformed AABB min, .w unused
    vec4 aabbMaxAndPad;   // .xyz = ...AABB max, .w unused
    uvec4 meshOffsets;    // .x = firstIndex, .y = indexCount, .z = uint(vertexOffset), .w unused
} ;
layout(std430, binding = 0) readonly buffer InstanceInputBuffer {
    CullingInstance instances[];
} instanceInput;

// Binding 1 - matches IndirectDrawCommand (IndirectDrawTypes.h) EXACTLY:
// 5 x 4-byte fields, tightly packed, 20 bytes/element, stride == 20 - the
// SAME byte-for-byte shape vkCmdDrawIndexedIndirect(Count) expects. NEVER
// declare this as `struct IndirectCommandGlsl { uint...; } commands[]` - see
// this file's own header comment for why that silently produces a 32-byte
// stride instead. Field order per element: [indexCount, instanceCount,
// firstIndex, vertexOffset (bit-reinterpreted through uint), firstInstance].
layout(std430, binding = 1) buffer IndirectCommandOutputBuffer {
    uint values[]; // values[i*5 + 0..4], see the worked example in 3.2
} indirectCommands;

// Binding 2 - a single atomic uint. MUST be 0 when this shader is dispatched
// (see 3.2's "count buffer reset" note) - this shader only ever increments
// it, never resets it.
layout(std430, binding = 2) buffer VisibleCountBuffer {
    uint value;
} visibleCount;
```

Push constants (document exactly, mirroring every other `.comp` file's own
convention — total size and the exact field choice below are BOTH now
pinned down, not left to the implementer, per this pre-check's own finding):

```glsl
layout(push_constant) uniform PushConstants {
    // RECOMMENDED (see 3.2's own reasoning): 6 ALREADY-EXTRACTED frustum
    // planes, computed ONCE per frame on the CPU via PHASE1's own tested
    // ExtractFrustumPlanes(), never re-derived in GLSL. Each Plane
    // (Vec3 normal + float distance) is already a tightly-packed 16 bytes
    // with zero implicit padding (3 floats + 1 float), so `vec4
    // planes[6]` (.xyz = normal, .w = distance) matches PHASE1's C++ Plane
    // array byte-for-byte, no repacking needed on the CPU side either.
    vec4 planes[6];    // 96 bytes - see budget table below
    uint instanceCount; // total instances in this batch's input array (this dispatch's own thread-count bound AND, for useCompaction==0, the EXACT value the CPU must also pass as SubmitIndirect()'s drawCount - see 3.2).
    uint useCompaction;  // 1 = atomic-append/compacted mode (real drawIndirectCount path); 0 = degenerate-padding mode (fallback path). Set ONCE per batch by the CPU from Renderer::SupportsDrawIndirectCount() - never re-derived in-shader.
} pc; // 96 + 4 + 4 = 104 bytes total.
```

**Push-constant byte-budget — pinned down explicitly, not left open:**
Vulkan only GUARANTEES `VkPhysicalDeviceLimits::maxPushConstantsSize >= 128`
bytes; this engine must never assume more than that anywhere (this campaign's
own graphics-side convention is already a fixed 128-byte block, `Pipeline.h`,
and `AtmosphereAerialPerspectiveComposite.comp` — confirmed by direct read —
already uses a 96-byte compute push-constant block, i.e. this codebase
already lives inside the 128-byte guarantee everywhere. Do not break that
here). The two options this phase considered:

| Option | Fields | Total size |
|---|---|---|
| **Recommended**: pre-extracted planes | `vec4 planes[6]` + 2 `uint` | **104 bytes** ✅ within the 128-byte guarantee |
| Rejected: in-shader plane extraction | `mat4 viewProj` + 2 `uint` | 72 bytes alone ✅, but see reasoning below for why this is still the WORSE choice despite fitting |
| Never do this | `mat4 viewProj` **AND** `vec4 planes[6]` (in case a future edit tries to keep both "just in case") | 64+96+8 = **168 bytes** ❌ exceeds the guaranteed minimum on some real devices — would only fail `vkCreatePipelineLayout` on a device with a small `maxPushConstantsSize`, i.e. **exactly the kind of device-dependent bug PHASE0's Locked Design Decision 4 exists to prevent** |

**Recommendation (upgraded from the original "whichever the implementer finds
cleaner" to a firm choice, per this pre-check): precompute the 6 frustum
planes on the CPU via PHASE1's own already-tested `ExtractFrustumPlanes()`
and pass them as data, INSTEAD of re-deriving the row/column plane-extraction
algorithm a second time in GLSL.** This is a strictly better choice than
in-shader extraction for two independent reasons: (1) it removes an entire,
genuinely tricky piece of NEW GLSL math (matrix-row/column plane extraction,
which PHASE0 itself names as this campaign's single biggest named risk —
"a wrong culling test... no compiler catches this") from the shader
entirely, leaving only the much more mechanical, easier-to-get-right p-vertex
AABB test as new GLSL (see 3.2); (2) it is cheaper at runtime — 6 planes are
identical for every instance in a dispatch, so extracting them once on the
CPU beats every one of up to hundreds of GPU threads (`kCullingLocalSizeX
== 256` per workgroup) redundantly re-deriving the exact same 6 planes from
the exact same `viewProj` matrix.

### 3.2 — `Shaders/FrustumCull.comp`

One thread per instance (`gl_GlobalInvocationID.x`, bounds-checked against
`pc.instanceCount`, local size mirrors `kCullingLocalSizeX` — pick 256,
matching `kSkinningLocalSizeX`'s own precedent, unless a concrete reason
argues otherwise):

1. Read this thread's own `GpuCullingInstanceInput` entry (`instanceInput.instances[i]`).
2. Test its world-space AABB (`aabbMinAndPad.xyz`/`aabbMaxAndPad.xyz` —
   ALREADY world-space, transformed by PHASE4's per-frame CPU packing step,
   never re-transformed here) against `pc.planes[0..5]`, using the SAME
   "positive vertex" (p-vertex) test `PHASE1`'s `AABBIntersectsFrustum()`
   implements. **Exact GLSL** (spelled out here rather than left implicit,
   per this pre-check's own "insufficiently specified" finding):
   ```glsl
   bool AabbIntersectsFrustum(vec3 worldMin, vec3 worldMax)
   {
       for (int p = 0; p < 6; ++p) {
           vec3 normal = pc.planes[p].xyz;
           float distance = pc.planes[p].w;
           // The p-vertex: the AABB corner furthest ALONG this plane's
           // normal - if even THIS corner is outside, the whole box is
           // outside (PHASE1's AABBIntersectsFrustum() doc comment).
           vec3 pVertex = vec3(
               normal.x >= 0.0 ? worldMax.x : worldMin.x,
               normal.y >= 0.0 ? worldMax.y : worldMin.y,
               normal.z >= 0.0 ? worldMax.z : worldMin.z);
           if (dot(normal, pVertex) + distance < 0.0) {
               return false; // fully outside this one plane -> fully outside the frustum
           }
       }
       return true; // inside (or straddling) all 6 planes - PHASE1's own documented conservative accept
   }
   ```
3. If NOT visible:
   - `useCompaction == 1`: do nothing (this instance simply never gets a
     command written — the compacted array only ever grows via step 4 below).
   - `useCompaction == 0`: write a DEGENERATE command directly to THIS
     thread's own original index in the output array (see the worked
     example below for the exact `indirectCommands.values[]` offsets) —
     `indexCount = 0, instanceCount = 0, firstIndex = 0, vertexOffset = 0,
     firstInstance = i` — a harmless, zero-work draw the GPU still iterates
     over but does no real work for.
4. If visible:
   - `useCompaction == 1`: `atomicAdd(visibleCount.value, 1)` to reserve a
     COMPACTED output slot (the return value of `atomicAdd` is the PRE-
     increment value — this thread's own reserved slot index — standard
     GLSL atomic-counter idiom), then write the real command
     (`indexCount = instanceInput.instances[i].meshOffsets.y`, `instanceCount
     = 1`, `firstIndex = instanceInput.instances[i].meshOffsets.x`,
     `vertexOffset = int(instanceInput.instances[i].meshOffsets.z)`,
     `firstInstance = i` — this instance's own ORIGINAL index into the
     never-compacted input array) into that reserved slot.
   - `useCompaction == 0`: write the SAME real command directly to THIS
     thread's own original index `i` (no atomic needed at all in this branch
     — every thread writes exactly its own, uncontended slot).

   **The `firstInstance` field is the load-bearing trick that makes this
   whole feature work**: it is ALWAYS the instance's ORIGINAL row index in
   the (never-compacted) input buffer, regardless of which branch runs or
   where the command itself ends up in the output array — this is exactly
   what lets `Shaders/MeshInstanced.vert` (PHASE2) read the correct model
   matrix for THIS draw via `gl_InstanceIndex`. **Confirmed correct against
   the Vulkan spec during this pre-check**: `gl_InstanceIndex` is defined as
   `firstInstance + <this draw's own instance-loop index, 0..instanceCount-1>`;
   since every emitted `IndirectDrawCommand` here always has `instanceCount
   == 1`, that loop index is always exactly `0`, so `gl_InstanceIndex ==
   firstInstance` always, for every single draw this mechanism ever issues.

   **Worked numeric example** (added per this pre-check — the plan
   previously had no concrete walkthrough): a batch of **5** instances,
   where the frustum test (step 2) finds instances **1** and **3** are NOT
   visible (culled), the rest ARE visible:

   | Instance `i` | Visible? | `useCompaction == 1` (compacted) output | `useCompaction == 0` (degenerate) output |
   |---|---|---|---|
   | 0 | yes | slot 0: `{idx>0, inst=1, first=0}` | slot 0: `{idx>0, inst=1, first=0}` |
   | 1 | **no** | *(no slot written at all)* | slot 1: `{idx=0, inst=0, first=1}` (degenerate) |
   | 2 | yes | slot 1: `{idx>0, inst=1, first=2}` | slot 2: `{idx>0, inst=1, first=2}` |
   | 3 | **no** | *(no slot written at all)* | slot 3: `{idx=0, inst=0, first=3}` (degenerate) |
   | 4 | yes | slot 2: `{idx>0, inst=1, first=4}` | slot 4: `{idx>0, inst=1, first=4}` |

   Compacted mode: `visibleCount.value` ends at **3**; the CPU-issued
   `vkCmdDrawIndexedIndirectCount` reads `min(3, maxDrawCount)` commands from
   slots `0..2` — every one of them real, in `firstInstance` order `0, 2, 4`
   (NOT original array order beyond that — compaction order is whatever
   order threads happen to complete their `atomicAdd`s in, which is fine:
   nothing about this mechanism depends on compacted-array ORDER, only on
   each slot's own `firstInstance` value being correct). Degenerate mode:
   the CPU issues `vkCmdDrawIndexedIndirect` with `drawCount = 5` (== this
   dispatch's own `pc.instanceCount`, see the invariant below) covering all 5
   slots — 3 real draws, 2 zero-work degenerate ones.

   **⚠️ CORRECTED — the atomic count buffer MUST be reset to exactly `0`
   before every dispatch of this shader.** This was never mentioned anywhere
   in PHASE0-4's own drafts (confirmed by search during this pre-check).
   This shader cannot safely reset it itself: GLSL compute has no ordering
   guarantee whatsoever BETWEEN workgroups within one dispatch (only
   `barrier()`/`memoryBarrier()` WITHIN a single workgroup), so a "have
   invocation 0 of workgroup 0 zero it first" scheme is a genuine race — some
   OTHER workgroup's `atomicAdd` could easily execute before that "reset"
   runs, corrupting the whole compaction. **This is squarely PHASE5's job**
   (the render-graph pass-wiring phase): declare a small reset step — a
   `vkCmdFillBuffer(count buffer, 0)` is the simplest correct choice, and
   needs no new buffer-creation change at all, since `CreateStructuredBuffer()`
   already ORs in `VK_BUFFER_USAGE_TRANSFER_DST_BIT` automatically for any
   `BufferMemoryUsage::GpuOnly` buffer (confirmed in 3.1's Situation notes
   above) — declared as a `ResourceAccess::TransferDst` WRITE against the
   SAME `BufferHandle`, ordered (via the render graph's own existing
   dependency-edge machinery) strictly BEFORE this shader's own
   `ComputeShaderWrite`/`atomicAdd` pass every single frame. **PHASE4/5 must
   not skip this** — this document flags it here, at the point the
   dependency is introduced, specifically so it cannot be silently missed by
   the time PHASE5 wires the actual render-graph pass.

   **⚠️ CORRECTED — for the degenerate-padding (`useCompaction == 0`)
   branch, the CPU-side `drawCount`/`maxDrawCount` argument passed to
   `Renderer::SubmitIndirect()` MUST be EXACTLY this same dispatch's
   `pc.instanceCount` — never the underlying buffer's ALLOCATED CAPACITY.**
   This matters because PHASE4's own per-batch buffer cache explicitly only
   GROWS capacity, never shrinks it ("`EnsureCapacity(key, instanceCount)` —
   reallocates ONLY when `instanceCount` GREW past what's currently
   allocated"). Concretely: if a batch had 10 live instances last frame
   (buffer capacity == 10) and shrinks to 6 this frame (4 entities
   destroyed/moved out of the batch), this dispatch only writes slots
   `0..5` (`pc.instanceCount == 6`) — slots `6..9` retain WHATEVER REAL,
   non-degenerate `IndirectDrawCommand`s were written into them LAST frame,
   since nothing this frame ever touches them. If the CPU then issued
   `vkCmdDrawIndexedIndirect` with `drawCount == 10` (the buffer's capacity)
   instead of `6` (this frame's real instance count), it would silently
   redraw 4 GHOST instances that no longer exist in the scene this frame —
   a real, reachable, visible rendering bug, not a theoretical one. PHASE4/5
   must thread THIS frame's own `instanceCount` (a value the CPU always
   already knows — it built this frame's own input array) through as the
   fixed-path draw count, completely independent of whatever the buffer's
   own physical capacity happens to be.

### 3.3 — `src/Renderer/Culling/CullingPipelines.h`/`.cpp` (new class, mirrors `GpuSkinningPipelines` exactly)

- One shared `VkDescriptorSetLayout` (3 storage buffers, all
  `VK_SHADER_STAGE_COMPUTE_BIT`, per the binding table in 3.1).
- One shared `ComputePipeline` (`FrustumCull.comp.spv`), push-constant range
  sized to the `PushConstants` block in 3.1 (104 bytes, per the pinned-down
  choice above).
- `EnsureInitialized(Renderer&)` — lazy, idempotent, mirrors
  `GpuSkinningPipelines::EnsureInitialized()` byte-for-byte in shape
  (confirmed directly against the real `GpuSkinningPipelines.cpp` source
  during this pre-check — the shape described here is accurate to copy).
- Register the new shader via `gte_add_shader()` in `CMakeLists.txt`.

### 3.4 — Correctness verification BEFORE trusting this against real camera data

Per PHASE0's own testability discipline: hand-build a small, throwaway test
scene (2-4 known instances at known world positions, a known camera
transform) where the EXPECTED visible/culled outcome for each instance can be
computed by hand from `PHASE1`'s own pure C++ reference
(`AABBIntersectsFrustum()`), run it through the real GPU shader (via the
throwaway harness pattern PHASE2.3.6 already established, extended to call
`CullingPipelines`), and read back the resulting `IndirectDrawCommand`
array/count buffer (mirror `Renderer.cpp`'s own existing GPU-to-CPU readback
pattern — a `BufferMemoryUsage::GpuToCpu` staging `Buffer` + `vkCmdCopyBuffer`
+ `Buffer::MappedData()`, exactly like `Renderer::CapturePixels()`'s own
image-readback precedent, just for a buffer instead of an image) to confirm
they match the C++ oracle's own predicted outcome EXACTLY, for BOTH
`useCompaction` values (a single readback per device is fine here — this is
manual/throwaway verification code, not a per-frame production read). This is
a genuine GPU-vs-CPU-oracle cross-check, mirroring this codebase's own "the
CPU oracle is right by definition, the GPU shader is checked against it"
discipline (`AtmosphereMath.h`'s own established precedent) applied to a
brand-new domain.

**This harness must ALSO exercise, explicitly, the two invariants called out
in 3.2 above, since nothing else in this campaign will until PHASE5 — do not
skip these two just because they're "PHASE5's job" to wire into production**:
- **Dispatch the shader TWICE in a row without resetting the count buffer
  between calls**, and confirm the second dispatch's `visibleCount.value`
  is WRONG (double-counted) relative to the C++ oracle — this is the
  intended way to PROVE, empirically, that the reset requirement is real and
  load-bearing, not a theoretical worry. Then re-run with an explicit reset
  (even a simple host-side `Buffer::Upload()` of a zero for this throwaway
  harness's own purposes — PHASE5 uses `vkCmdFillBuffer` in the real
  render-graph pass, see 3.2) between the two dispatches and confirm it now
  matches the oracle both times.
- For the degenerate-padding branch specifically, dispatch once with
  `pc.instanceCount == N` (a real command written to every one of `0..N-1`),
  then dispatch AGAIN with a SMALLER `pc.instanceCount == M < N` reusing the
  SAME output buffer, and confirm slots `M..N-1` still contain the FIRST
  dispatch's real (non-degenerate) commands — this is the exact shape of the
  ghost-geometry bug 3.2 describes, and confirming it happens here (with a
  full understanding of why) is what makes the corresponding CPU-side
  `drawCount == pc.instanceCount` invariant an informed, verified requirement
  for PHASE4/5, not a guess.

### What We Will NOT Do (this phase)

- No occlusion culling, no hierarchical/two-phase culling, no LOD selection
  in this same shader (PHASE0 Non-Goals).
- No render-graph pass declaration yet (`AddComputePass()`/`WriteBuffer()`
  calls) — that is PHASE5's job. This phase proves the SHADER ITSELF works,
  driven by a throwaway harness, exactly like PHASE2 proved
  `SubmitIndirect()` itself before any render-graph involvement.
- No `RenderSystem`/ECS involvement — the throwaway harness's input data is
  still hand-authored, not sourced from a live scene (PHASE4's job).
- **No actual implementation of the count-buffer reset or the
  drawCount/instanceCount coupling described in 3.2 as production
  render-graph wiring** — this phase only PROVES (3.4) that both are real
  requirements and documents them precisely enough for PHASE5 to wire
  correctly; the real `vkCmdFillBuffer` reset pass and the real
  `SubmitIndirect()` call site threading through this frame's own
  `instanceCount` both remain PHASE5's own deliverable.

### Compile check + dedicated double-check

Fast, targeted incremental compile check only. Manually verify 3.4's
cross-check for BOTH `useCompaction` branches, INCLUDING the two additional
invariant-proving sub-checks added above, before considering this phase done
(validation layers enabled, zero new warnings/errors). Write
`PHASE3_COMPLETION_REPORT.md` (include the exact hand-computed test scene
used in 3.4 and its confirmed matching GPU output, for both branches, AND the
confirmed results of the count-buffer-reset and drawCount/instanceCount
sub-checks — this record is what the dedicated double-check phase below will
re-verify against the actual shader source). Commit.

**Immediately after this phase's own completion report is committed, delegate
a dedicated, standalone `delegate_task` whose ONLY job is to re-derive the
expected visible/culled outcome for `PHASE3_COMPLETION_REPORT.md`'s own test
scene independently (from `Shaders/FrustumCull.comp`'s actual committed
source and `PHASE1`'s own `AABBIntersectsFrustum()`), confirm it matches what
was recorded, and flag ANY discrepancy, ANY untested code path (the
un-exercised `useCompaction` branch in particular), or ANY GLSL/C++ math
mismatch it finds — including explicitly re-checking that the output buffer
was declared as a flat scalar array (never a GLSL struct array, per 3.1's own
corrected guidance) and that the shader genuinely never attempts to reset the
count buffer itself — reporting back with its own findings before PHASE4
starts. That delegated task must also use `ask_questions` for any genuine
ambiguity it hits, and must repeat this same instruction to anything it
further delegates.**
