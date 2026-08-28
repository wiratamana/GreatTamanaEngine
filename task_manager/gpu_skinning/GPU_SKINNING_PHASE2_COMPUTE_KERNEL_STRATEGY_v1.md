# GPU Vertex Skinning — Phase 2: The Compute Kernel(s)

Part of the GPU Vertex Skinning campaign — see
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v1.md` for the campaign map. Depends
on Phase 1's buffer layouts (`GpuSkinningTypes.h`) already being landed and
agreed upon.

## Step 1: The Goal (Where are we going?)

Two compiled `.comp` shaders — `SkinVerticesPositionNormal.comp` and
`SkinVerticesPositionNormalUv.comp` — plus the `ComputePipeline`/
descriptor-set-layout C++ scaffolding to build and bind them, that
together perform, entirely on the GPU, the exact same per-vertex bone
blend `Animation/VertexSkinning.cpp`'s `SkinVertexRange()` performs on the
CPU today. By the end of this phase, given the same bind pose, skin
weights, and bone matrices as input, the GPU kernel's output is
*intended* to numerically match the CPU path's output (Phase 6 is where
that intent is actually verified against a live device — this phase gets
the shader compiling, dispatchable, and self-consistent).

## Step 2: The Situation / The Problem (Where are we now?)

`SkinVertexRange()` (`src/Animation/VertexSkinning.cpp`) is the exact
algorithm to mirror. Read it directly rather than paraphrase it from
memory — but the shape, restated for a GLSL audience, is:

```
for each vertex i in [beginIndex, endIndex):
    accumulatedPosition = vec3(0)
    accumulatedNormal   = vec3(0)
    for each of the 4 (boneIndex, weight) slots on vertex i:
        if weight == 0: continue        // unused slot - contributes nothing
        M = skinningMatrices[boneIndex]  // a model-space skinning matrix,
                                         // inverse-bind-pose already folded in
        accumulatedPosition += weight * (M * vec4(bindPosition, 1.0)).xyz
        accumulatedNormal   += weight * (mat3(M) * bindNormal)
    outPosition[i] = accumulatedPosition
    outNormal[i]   = normalize(accumulatedNormal)
```

A few details that are easy to get subtly wrong porting this to GLSL, and
must be checked against the real C++ source, not assumed:

- **Whether weights are already guaranteed to sum to 1.0** at import time,
  or whether the CPU code re-normalizes/doesn't. If the CPU code does *not*
  renormalize and simply trusts PMX's own already-normalized weights, the
  GPU kernel must do the exact same (no defensive renormalization the CPU
  path doesn't also do — an extra renormalization step would be a
  correctness *mismatch* against the oracle, even if it looks "more
  correct" in isolation).
- **Whether the accumulated normal is normalized *after* blending, or
  each per-bone-contribution normal is normalized *before* accumulating.**
  These produce different results. Match whichever the CPU code actually
  does, exactly.
- **The exact meaning of "unused slot."** The CPU path treats a weight of
  exactly `0.0f` as "skip this influence" (see `AGENTS.md`'s own
  `VertexSkinningTests` bullet: "unused influence slots are ignored
  regardless of weight type"). The GPU kernel must use the exact same
  `weight == 0.0` (or `weight <= 0.0`, whichever the CPU source actually
  checks) test, not e.g. `boneIndex == 0` (bone index 0 is a perfectly
  valid *real* bone index and must never be treated as "unused").
- **No-valid-influence-at-all fallback.** `AGENTS.md` documents the CPU
  test `NoValidInfluenceAtAllDegradesToBindPoseRatherThanCollapsingToOrigin`
  — a vertex with zero real influences must end up AT ITS BIND POSE, not
  at the origin. The naive accumulation loop above would produce
  `vec3(0)` for such a vertex (nothing ever added). The GPU kernel must
  replicate the CPU's actual fallback behavior exactly — most likely by
  initializing `accumulatedPosition`/`accumulatedNormal` to the bind pose
  values and only overwriting them if at least one non-zero-weight
  influence was found, mirroring whatever branch structure the real C++
  source uses. Do not guess this — read the source and copy its exact
  control flow.

## Step 3: The Plan (How will we get there?)

### 3.1 — Two `.comp` files, not one — matching Phase 1's two output layouts

`src/Shaders/SkinVerticesPositionNormal.comp` and
`src/Shaders/SkinVerticesPositionNormalUv.comp`. Both share almost
identical bodies (the blend math is identical); the only difference is
whether a UV pass-through happens (UVs never change under skinning — they
are copied from the bind-pose UV buffer straight to the output buffer
unmodified, exactly like the CPU path's own `PackMeshVertexUvRange()`
copies `skinData->uvs` verbatim into the packed output — see
`MeshVertexPacking.cpp`). Two files, not one `#ifdef`-guarded file and
not one shader with a push-constant "hasUv" branch, per the master
strategy's explicit refusal of shader permutation. A short, honest amount
of duplication between the two files is the correct, simple choice here —
add a prominent comment at the top of each pointing at its sibling and at
`VertexSkinning.cpp` as the shared oracle both must independently, exactly
mirror.

### 3.2 — Bindings, per Phase 1's fixed table

```glsl
#version 450
layout(local_size_x = 256) in; // matches kMinVerticesPerBatch's own CPU-side
                                // per-batch floor - NOT a required match,
                                // but a sensible, documented starting point;
                                // tune after Phase 6's own perf comparison.

// Binding 0: read-only bind-pose vertex buffer (std430-padded, per Phase 1).
layout(std430, binding = 0) readonly buffer BindPoseBuffer {
    vec4 positionAndPad[]; // .xyz = position, .w unused
    // NOTE: normals live in a SEPARATE binding-0-adjacent region OR a
    // second buffer, depending on Phase 1's final struct-of-arrays vs.
    // array-of-structs decision - see this file's own worked layout below.
} bindPose;

// Binding 1: read-only skin weights.
layout(std430, binding = 1) readonly buffer SkinWeightsBuffer {
    uvec4 boneIndices[];
    // ... weights[] similarly, per Phase 1's exact struct.
} skinWeights;

// Binding 2: read-only bone matrices - plain mat4[], zero repacking needed
// (Mat4::Data() is already GLSL-column-major - see Phase 1, Step 3.3).
layout(std430, binding = 2) readonly buffer BoneMatricesBuffer {
    mat4 boneMatrices[];
};

// Binding 3: the tightly-packed (NOT std430-vec-padded) output - see
// Phase 1, Step 3.4 for exactly how this avoids std430 padding while still
// living inside a `buffer` block: declared as a flat float array with
// manually computed offsets.
layout(std430, binding = 3) buffer OutputBuffer {
    float outputFloats[]; // stride 6 floats/vertex (PositionNormal) or 8 (PositionNormalUv)
};

layout(push_constant) uniform PushConstants {
    uint vertexCount;
} pc;
```

Whether the bind-pose buffer is struct-of-arrays (separate
position/normal arrays) or array-of-structs (`GpuBindPoseVertex[]`, one
struct per vertex, per Phase 1's actual C++ struct) is Phase 1's decision
to make explicit and Phase 2's job to match exactly — **do not let Phase
2 quietly redefine Phase 1's layout**; if the GLSL side turns out to want
a different physical layout than what Phase 1 specified, that is a Phase
1 revision, not a Phase 2 workaround. Write this dependency down loudly at
the top of both `.comp` files.

### 3.3 — The kernel body, faithfully mirroring `SkinVertexRange()`

```glsl
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.vertexCount) return; // ComputeGroupCount()'s own documented
                                      // ceiling-division overshoot - see
                                      // ComputeDispatch.h.

    vec3 bindPos = /* read from bindPose at index i */;
    vec3 bindNormal = /* read from bindPose at index i */;

    vec3 accumulatedPosition = vec3(0.0);
    vec3 accumulatedNormal = vec3(0.0);
    bool anyInfluence = false;

    for (int slot = 0; slot < 4; ++slot) {
        float weight = /* skinWeights.weights[i][slot] */;
        if (weight == 0.0) continue;
        anyInfluence = true;
        uint boneIndex = /* skinWeights.boneIndices[i][slot] */;
        mat4 m = boneMatrices[boneIndex];
        accumulatedPosition += weight * (m * vec4(bindPos, 1.0)).xyz;
        accumulatedNormal   += weight * (mat3(m) * bindNormal);
        // ^ EXACT accumulate-then-normalize-once-at-the-end order/timing
        //   must be verified against VertexSkinning.cpp, not assumed - see
        //   this document's own Step 2.
    }

    vec3 finalPosition = anyInfluence ? accumulatedPosition : bindPos;
    vec3 finalNormal   = anyInfluence ? normalize(accumulatedNormal) : bindNormal;

    // Write to the tightly-packed output - manual offset math, stride
    // depends on which of the two .comp files this is.
    uint base = i * 6u; // PositionNormal variant; 8u for the Uv variant
    outputFloats[base + 0] = finalPosition.x;
    outputFloats[base + 1] = finalPosition.y;
    outputFloats[base + 2] = finalPosition.z;
    outputFloats[base + 3] = finalNormal.x;
    outputFloats[base + 4] = finalNormal.y;
    outputFloats[base + 5] = finalNormal.z;
    // Uv variant additionally copies the bind-pose UV straight through,
    // unmodified, at offsets base+6/base+7.
}
```

This is illustrative, not final GLSL — the actual implementer must
translate Phase 1's real, final struct layout into real GLSL field
accesses, and must diff the accumulate/normalize order against the real
`VertexSkinning.cpp` source line by line before considering this done.

### 3.4 — C++-side pipeline/descriptor-layout scaffolding

Following `ComputeBlurValidation.cpp`'s already-shipped pattern exactly:

```cpp
// One DescriptorSetLayoutBuilder per kernel variant (both share the same
// 4-binding shape from Phase 1's table - a single shared layout is fine,
// reused by both ComputePipelines, since binding numbers/types are
// identical between the two .comp files):
VkDescriptorSetLayout skinningLayout = DescriptorSetLayoutBuilder(device)
    .AddStorageBuffer(0) // bind pose
    .AddStorageBuffer(1) // skin weights
    .AddStorageBuffer(2) // bone matrices
    .AddStorageBuffer(3) // output
    .Build();

ComputePipeline skinPositionNormalPipeline = renderer.CreateComputePipeline(
    "shaders/SkinVerticesPositionNormal.comp.spv", { skinningLayout },
    VkPushConstantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(std::uint32_t) });

ComputePipeline skinPositionNormalUvPipeline = renderer.CreateComputePipeline(
    "shaders/SkinVerticesPositionNormalUv.comp.spv", { skinningLayout },
    VkPushConstantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(std::uint32_t) });
```

Both pipelines are built **once**, at engine start (or lazily on first use,
mirroring `AssetPreviewMesh`'s "build the pipeline once, reuse across every
mesh asset selected afterwards" precedent) and owned by whatever Phase 4's
per-model cache turns out to be — this phase does not decide their
lifetime/ownership, only their construction recipe. `CMakeLists.txt` needs
two new `gte_add_shader()` calls, unconditional (not gated behind
`GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` — GPU skinning must work in
a non-Editor, release build too, since it is a genuine gameplay/runtime
feature, not a debug tool).

### 3.5 — Dispatch math

`ComputeDispatch.h`'s `ComputeGroupCount(vertexCount, 256)` (matching
`local_size_x = 256` above) — never plain integer division. Document the
chosen `local_size_x` value as a named constant living next to whatever
C++ call site dispatches it (per `ComputeDispatch.h`'s own documented
convention — "each concrete compute shader's own local size is restated as
a named C++ constant... never centralized").

## Step 4: What We Will NOT Do (Focus)

- **No shader-side pose evaluation of any kind.** The kernel consumes an
  already-fully-resolved `boneMatrices[]` array — it has no idea what IK,
  append-inheritance, or forward kinematics even are, and never will (see
  the master strategy's Step 4).
- **No dynamic branching on vertex-count thresholds inside the shader.**
  `kMinVerticesToParallelize`'s CPU-side "small models run inline" logic
  has no GPU equivalent — a GPU dispatch's own fixed overhead is
  categorically different from a job-scheduling overhead, and Phase 5/7
  decide, in C++, whether to dispatch at all; the shader itself is always
  written the same way regardless of model size.
- **No attempt to hand-optimize the kernel (subgroup ops, shared-memory
  caching of bone matrices, etc.) in this phase.** Get it correct and
  dispatchable first; Phase 6/7's actual measured performance data is what
  should motivate any future optimization pass, not premature guessing.
- **No support for a bone count large enough to need more than a plain
  `mat4[]` storage buffer indexed by a `uint`.** No skeletal LOD, no bone
  palette compression.

## Step 5: Their Role (What does this mean for you?)

- Land both `.comp` files under `src/Shaders/`, wired into
  `CMakeLists.txt` via `gte_add_shader()`.
- Land the descriptor-set-layout + `ComputePipeline` construction code
  (exact home decided jointly with Phase 4 — likely
  `src/Renderer/GpuSkinning/GpuSkinningPipelines.h/.cpp`, constructed once
  and owned by whatever object Phase 4 introduces).
- **Before moving to Phase 3**, read `Animation/VertexSkinning.cpp` one
  more time, side by side with your final GLSL, and write a short,
  explicit note in your own phase completion report confirming: weight
  normalization assumption, accumulate/normalize order, and the
  no-valid-influence bind-pose fallback all match, line by line. This
  review is cheap now and expensive later — Phase 6's parity test is what
  *proves* it, but a careless mismatch found only then wastes an entire
  phase's worth of Phase 3/4/5 work built on top of a wrong kernel.
