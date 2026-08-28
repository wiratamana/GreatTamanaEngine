# GPU Vertex Skinning — Phase 1: Data & Buffer Layout Foundations

Part of the GPU Vertex Skinning campaign — see
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map,
must-have feature list, and cross-phase rules. This phase produces no
visible engine behavior by itself — it defines the byte-for-byte GPU
buffer layouts every later phase (the compute kernel in Phase 2, the
render-graph wiring in Phase 3, the per-model cache in Phase 4) builds
against. Get this wrong and every later phase inherits the bug silently.

## Step 1: The Goal (Where are we going?)

A small, precisely-specified, hand-documented set of GPU buffer layouts —
plain `std430` structured-buffer structs — that together carry everything
a compute shader needs to reproduce `SkinVertexRange()`'s exact per-vertex
blend, for exactly one model, entirely GPU-side:

1. **Bind-pose vertex buffer** — read-only, immutable, uploaded once per
   model at load time (positions + normals, and separately UVs).
2. **Skin-weight buffer** — read-only, immutable, uploaded once per model
   at load time (up to 4 bone indices + 4 weights per vertex).
3. **Bone matrix buffer** — read-only from the shader's perspective, but
   **re-uploaded every frame** for every currently-playing animator (this
   is the one genuinely dynamic input).
4. **Skinned output buffer** — write-only from the shader's perspective,
   read afterward as an ordinary vertex-buffer attribute input by the
   graphics pass. This is the buffer that physically *is* the `Mesh`'s
   vertex buffer for a GPU-skinned model.

By the end of this phase, every one of these four layouts is written down,
with exact field order/types/alignment, plus the C++-side plain structs
that describe them (mirroring `Vertex.h`/`MeshVertex.h`'s existing role: a
plain POD struct describing what a GPU buffer's bytes mean, with zero
Vulkan calls of its own).

## Step 2: The Situation / The Problem (Where are we now?)

Every one of these four data sets already exists — but only as CPU-side,
`std::vector`-backed, C++-native types, never laid out for GPU consumption:

- **Bind pose**: `SkinnedMeshData::bindPositions`/`bindNormals`/`uvs`
  (`SkeletalRigCache.h`, populated from `MeshData` at import time) — plain
  `std::vector<Vec3>`/`std::vector<Vec2>`.
- **Skin weights**: `SkinnedMeshData::skinWeights`, a
  `std::vector<VertexSkinWeights>` (`src/Assets/MeshData.h`) — each entry
  holds up to 4 `(boneIndex, weight)` pairs, `int`/`float`, already
  weight-normalized at import time (SDEF/QDEF folded down to a BDEF2/4
  equivalent — see `README.md`'s "A spawned MMD model can now actually be
  ANIMATED" entry).
- **Bone matrices**: `EvaluateAnimatedSkinningPose()`'s return value, a
  freshly-computed `std::vector<Mat4>`, one entry per skeleton bone, every
  frame, for every currently-playing animator.
- **Skinned output**: today this is `AnimatorScratchBuffers::skinnedPositions`/
  `skinnedNormals` (`std::vector<Vec3>`, reused across frames) immediately
  re-packed into `std::vector<MeshVertex>`/`std::vector<MeshVertexUv>` and
  uploaded via `Mesh::UpdateVertexData()`.

None of these four C++ shapes is GPU-buffer-ready as-is:

- `Vec3` (`src/Math/Vec3.h`) is a plain 3-`float` struct with **no padding**
  — perfectly fine for a CPU array, but a `std430` GLSL layout aligns an
  array element containing a `vec3` to 16 bytes (the same alignment as
  `vec4`), meaning a naive `std::vector<Vec3>` uploaded byte-for-byte and
  read back as `vec3 positions[]` in GLSL will **silently read the wrong
  bytes** for every element after the first — a classic, extremely easy to
  get wrong, extremely hard to notice (until you compare against a CPU
  oracle — see Phase 6) source of GPU data corruption. This is the single
  most important pitfall this phase exists to head off.
- `VertexSkinWeights`'s exact field layout/packing needs to be pinned down
  and mirrored exactly in GLSL — get the order of `boneIndex`/`weight`
  pairs wrong and every vertex skins against the wrong bones.
- `Mat4` (`src/Math/Mat4.h`) is already column-major, matching GLSL's
  native `mat4` layout exactly (this is explicitly documented in
  `README.md`'s "Math" section — "matches GLSL's `mat4` layout exactly, so
  `Mat4::Data()` uploads to a push constant/uniform with zero transpose")
  — this is the one input that needs **no reshaping at all**, just a
  buffer to live in.
- There is no existing precedent anywhere in this codebase yet for a
  *vertex* buffer that is simultaneously a `std430` storage buffer (for
  the compute shader to write) and a real vertex-input-state buffer (for
  the graphics pipeline to read via `vkCmdBindVertexBuffers`) — every
  existing render-graph `BufferDesc`/`Renderer::CreateStructuredBuffer()`
  caller so far only ever needed `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`
  alone (e.g. a future GPU-driven-rendering indirect-draw buffer). This
  phase must explicitly work out — and document — the combined usage-flag
  requirement.

## Step 3: The Plan (How will we get there?)

### 3.1 — Fix the padding problem once, in one place

Define GPU-side structs using **`Vec4`-padded** fields wherever a `Vec3`
would otherwise be read by GLSL — never upload a raw `std::vector<Vec3>`
and hope for the best. Concretely:

```cpp
// New file: src/Renderer/GpuSkinning/GpuSkinningTypes.h
// (Vulkan-header-free — see this phase's own Step 3.4 for why this file
// deliberately has ZERO <volk.h> dependency, mirroring GpuTiming.h's own
// precedent of "the pure data/math half has no Vulkan dependency at all".)

struct GpuBindPoseVertex {
    float positionX, positionY, positionZ, _padPosition;
    float normalX, normalY, normalZ, _padNormal;
};
static_assert(sizeof(GpuBindPoseVertex) == 32, "must match std430 vec4+vec4 layout exactly");
```

A conversion function, `PackBindPoseVertices(const std::vector<Vec3>& positions,
const std::vector<Vec3>& normals) -> std::vector<GpuBindPoseVertex>`, is the
one and only place this padding/repacking ever happens — it lives in this
same new file, is pure (no Vulkan/no Renderer dependency), and gets its own
Tier-1 test (`tests/Renderer/GpuSkinning/GpuSkinningTypesTests.cpp`)
verifying the exact byte offsets a hand-computed example produces. This
mirrors `MeshVertexPacking.h`'s own existing "one shared, tested packing
function, never duplicated inline" precedent exactly (see `AGENTS.md`'s
Job System section, which already documents this exact convention for the
CPU path's own packing step).

UVs are a `Vec2` — two 4-byte floats, no padding concern at `std430`'s own
8-byte alignment rule for `vec2` (a `vec2` aligns to 8 bytes, matching two
tightly-packed floats exactly) — `GpuUv { float u, v; }` needs no padding.

### 3.2 — Skin weights: mirror `VertexSkinWeights` exactly, GLSL-friendly

```cpp
struct GpuSkinWeights {
    std::uint32_t boneIndices[4]; // unused slots = 0 (bone 0), weight 0 makes this harmless
    float weights[4];             // unused slots = 0.0
};
static_assert(sizeof(GpuSkinWeights) == 32);
```

`boneIndices` as `uint32_t` (not `int32_t`) matches GLSL's `uvec4`/`uint[]`
convention and sidesteps any sign-extension ambiguity. A vertex with fewer
than 4 real influences (BDEF1/BDEF2) has its unused slots' `weight` already
zeroed by the existing CPU-side extraction (`PmxLoader.cpp`) — the GPU
kernel (Phase 2) must honor "weight 0 contributes nothing" exactly like
`SkinVertexRange()` already does, never assume all 4 slots are meaningful.
`PackSkinWeights()` (same new file) is the pure conversion function, tested
the same way as 3.1.

### 3.3 — Bone matrices: no repacking needed, just a buffer

`Mat4::Data()` already returns a column-major `float[16]` matching GLSL's
`mat4` exactly. The bone-matrix buffer is simply `elementStride =
sizeof(Mat4)` (64 bytes), `elementCount = skeleton.bones.size()`, uploaded
via `Renderer::CreateStructuredBuffer(sizeof(Mat4), boneCount,
BufferMemoryUsage::CpuToGpu, ...)` (host-visible, persistently mapped,
sequential-write — the exact `BufferMemoryUsage` category this buffer
needs, since it's rewritten wholesale, every frame, from the CPU side via
`Buffer::Upload()`). Document explicitly in this phase's own file: **this
is the one buffer among the four that is genuinely re-uploaded every
frame** — the other three are write-once-at-load-time, immutable for the
model's whole lifetime (until the model itself is destroyed).

### 3.4 — Skinned output buffer: the combined usage-flag requirement

The single most novel factory requirement this campaign introduces. Define
the exact combined creation parameters here (implemented in Phase 4, not
this phase — this phase only pins down and documents the *requirement*):

- `BufferMemoryUsage::GpuOnly` (device-local, not CPU-mappable — this
  buffer's contents come **only** from the compute shader, never the CPU,
  once the model is loaded).
- `usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT`.
  Unlike an image's `VK_IMAGE_USAGE_STORAGE_BIT` (which is genuinely
  format-dependent — see `Vulkan/FormatCapabilities.h`'s
  `SupportsStorageImageUsage()`, needed because not every `VkFormat`
  supports storage-image access), a **buffer's** usage flags have no such
  format-feature gate in the Vulkan spec — `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`
  and `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT` are core, universally-available
  buffer usage bits that may always be combined on the same
  `VkBuffer`/`VmaAllocation` on any conformant implementation. Document
  this explicitly so a future reader doesn't go looking for a
  `FormatCapabilities`-style runtime check that isn't actually needed here
  — the one thing that *is* worth an explicit, loud runtime check (per
  this campaign's Must-Have #9) is querying `VkPhysicalDeviceLimits::
  maxStorageBufferRange`/general storage-buffer support, which every
  Vulkan 1.0 implementation already guarantees a usable minimum for; a
  defensive `assert`/log is enough, not a hard capability probe.
- The struct written into this buffer must match whatever `VertexLayout`
  the consuming `Pipeline` expects **exactly**, field-for-field — this
  phase defines two matching output layouts (mirroring the CPU path's own
  `MeshVertex`/`MeshVertexUv` split exactly, never unified into one, per
  the master strategy's "no shader permutation" refusal):

  ```cpp
  // Matches VertexLayout::PositionNormal (Mesh.vert/.frag consumers).
  struct GpuSkinnedVertexPositionNormal {
      float positionX, positionY, positionZ;
      float normalX, normalY, normalZ;
  };
  static_assert(sizeof(GpuSkinnedVertexPositionNormal) == sizeof(MeshVertex),
      "must stay byte-identical to MeshVertex - the graphics pipeline reads this via the SAME vertex input state");

  // Matches VertexLayout::PositionNormalUv (TexturedMesh.vert/.frag consumers).
  struct GpuSkinnedVertexPositionNormalUv {
      float positionX, positionY, positionZ;
      float normalX, normalY, normalZ;
      float u, v;
  };
  static_assert(sizeof(GpuSkinnedVertexPositionNormalUv) == sizeof(MeshVertexUv),
      "must stay byte-identical to MeshVertexUv");
  ```

  Crucially: **this output buffer is NOT `std430`-padded like the input
  bind-pose buffer is.** It is read by the graphics pipeline's fixed-function
  vertex input assembler (`VkVertexInputAttributeDescription`, exactly the
  same as `MeshVertex`/`MeshVertexUv` are read today), not by a shader's
  `std430` block — so it must be *tightly packed*, matching `MeshVertex`/
  `MeshVertexUv`'s own existing (unpadded) layout byte-for-byte. The
  `static_assert`s above are the load-bearing guard against a future edit
  to `MeshVertex.h` silently breaking this agreement — add a matching
  `static_assert` any time either struct changes.

  On the GLSL side, this same buffer is declared as a **tightly-packed
  `buffer` block using `layout(buffer_reference)`-free plain arrays of
  scalars** rather than `vec3`/`vec2` fields (to avoid `std430`'s own
  padding rules applying to an output layout that must NOT be padded) —
  Phase 2 documents the exact GLSL declaration that achieves this
  (essentially: declare the output as `float outputFloats[]` and compute
  manual byte/float offsets, OR declare the buffer with
  `layout(std430, ...)` but pack the *struct itself* using only `float`
  scalars in the exact tightly-packed order needed, relying on the fact
  that `std430` only inserts padding around `vec2`/`vec3`/`vec4`-typed
  *fields*, never around a struct built purely from scalar `float`
  members declared in sequence with no vector types at all. Phase 2 spells
  this out with a worked example and a compile-time size check on the
  C++ side, mirroring this phase's `static_assert`s.

### 3.5 — Binding-number convention

Per `Vulkan/DescriptorSetLayoutBuilder.h`'s already-documented convention
(declaration order: read-only buffers first, read-write buffers next,
read-only textures next, storage images last), this phase fixes the
skinning descriptor set's binding numbers for Phase 2/3 to use verbatim:

| Binding | Resource | GLSL qualifier |
|---|---|---|
| 0 | Bind-pose vertex buffer | `readonly buffer` |
| 1 | Skin-weight buffer | `readonly buffer` |
| 2 | Bone matrix buffer | `readonly buffer` |
| 3 | Skinned output buffer | `buffer` (read-write not required, but declared as plain `buffer` since GLSL has no write-only storage-buffer qualifier the way it does for `image2D`'s `writeonly`) |

Document this table in this phase's own header comment
(`GpuSkinningTypes.h`) as the single source of truth — Phase 2's `.comp`
files and Phase 4's descriptor-set-building code must both cite it rather
than re-deriving binding numbers independently.

## Step 4: What We Will NOT Do (Focus)

- **No generic/templated GPU buffer-layout system.** Four concrete
  structs, four concrete pack functions — no attempt to build a reusable
  "C++ struct -> std430 layout" reflection/codegen mechanism. This mirrors
  the master strategy's standing refusal of shader reflection.
- **No compression of skin weights/bone indices** (e.g. packing 4 weights
  into a single `uint32_t` via 8-bit quantization). This campaign uploads
  plain, full-precision `float`/`uint32_t` data, exactly matching the CPU
  path's own precision, specifically so Phase 6's parity check has the
  best possible chance of matching within a tight epsilon. Bandwidth
  optimization is a legitimate future follow-up, explicitly not here.
- **No dynamic/variable bone-influence-count support** (e.g. BDEF-N for
  N > 4). The CPU path is hard-capped at 4 influences; the GPU path
  mirrors that cap exactly, no more, no less.
- **No buffer creation, descriptor set building, or Vulkan calls in this
  phase.** This phase is pure data-shape/layout definition plus pure C++
  packing functions — Phase 2 builds the shader that reads these layouts,
  Phase 4 builds the actual `Buffer`s.

## Step 5: Their Role (What does this mean for you?)

- Land `src/Renderer/GpuSkinning/GpuSkinningTypes.h` (+ a matching `.cpp`
  only if the pack functions need one — likely header-only, given their
  size) with every struct above, every `static_assert`, and the binding
  table as a header comment.
- Land `tests/Renderer/GpuSkinning/GpuSkinningTypesTests.cpp` covering:
  a hand-built `Vec3`/`Vec2` input producing exactly the expected padded
  byte layout; a `VertexSkinWeights` with fewer than 4 real influences
  packing its unused slots to zero-weight/bone-0, never garbage; and the
  two `static_assert`s above actually compiling (a compile-time test is
  itself the test — no runtime assertion needed for those two).
- Do **not** touch `Renderer.h`/`RenderGraph*`/`AnimationSystem*` in this
  phase at all — this phase's deliverable is exactly one new header (plus
  its test), nothing else. If you find yourself editing `Renderer.cpp`,
  you have started Phase 4's work early; stop and confirm Phases 2-3 are
  actually done first.
