# GPU Vertex Skinning — Phase 2: Compute Kernel(s) — Completion Report

Status: **DONE**. Implements
`task_manager/gpu_skinning/GPU_SKINNING_PHASE2_COMPUTE_KERNEL_STRATEGY_v1.md`
in full, per the scope fence set by
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md` ("Focus on selected section
only" — this report covers ONLY the "ON GOING" Phase 2 item; Phase 1 is
already `[DONE]` and unmodified except for one documented addendum — see
below; Phases 3-7 remain `[TODO]`, untouched).

## What was built

### 1. Two compute shaders (`src/Shaders/`)

- **`SkinVerticesPositionNormal.comp`** — the untextured-model kernel.
  Reads bindings 0-3 exactly per `GpuSkinningTypes.h`'s documented binding
  table (bind-pose vertices, skin weights, bone matrices, output), writes a
  tightly-packed 6-float-per-vertex output (`GpuSkinnedVertexPositionNormal`).
- **`SkinVerticesPositionNormalUv.comp`** — the textured-model kernel.
  Identical blend math, plus a straight UV pass-through and an 8-float-per-
  vertex output (`GpuSkinnedVertexPositionNormalUv`).

Both use `layout(local_size_x = 256) in;`, matching the new
`gte::kSkinningLocalSizeX` C++ constant (`GpuSkinningPipelines.h`) that any
future dispatch call site (Phase 3/5) must reference instead of
re-declaring its own magic number, exactly like `ComputeBlurValidation.cpp`
already does for `BoxBlur.comp`.

**The kernel body was verified line-by-line against the real
`Animation/VertexSkinning.cpp::SkinVertexRange()`**, per this phase's own
Step 5 instruction — not against the master strategy document's own
*illustrative* GLSL sketch, which turned out to differ from the real CPU
code in two ways worth recording here for whoever reads this next:

1. **Weight normalization is NOT assumed pre-normalized.** The real CPU
   code accumulates a `totalWeight` across the (up to 4) valid influences
   and divides the accumulated position by that actual total — it does
   **not** assume BDEF4 weights already sum to 1.0. The illustrative sketch
   in the Phase 2 strategy doc never divided by `totalWeight` at all. The
   shipped shader divides by `totalWeight`, exactly matching the CPU path.
2. **The accumulate-then-normalize order for normals**: both the CPU code
   and the shipped shader accumulate a weighted sum of per-bone-transformed
   normals across all valid influences, then call `normalize()` **once, at
   the end** — never per-influence. (Dividing the accumulated normal by
   `totalWeight` before normalizing would produce the same direction as not
   dividing at all, since `normalize()` discards magnitude — the shader
   does not divide the normal by `totalWeight`, matching the CPU code's own
   omission of that division for normals specifically.)
3. **The "unused slot" test.** The CPU code's real condition is
   `boneIndex < 0 || weight == 0.0f || static_cast<size_t>(boneIndex) >=
   boneCount` — three conditions, not one. Since Phase 1's `PackSkinWeights()`
   already translates every unused CPU slot (`boneIndex < 0`) into
   `boneIndex = 0, weight = 0.0` on the GPU side (see
   `GpuSkinningTypes.cpp`), the shader only needs `weight == 0.0 ||
   boneIndex >= boneCount` — the `boneIndex < 0` branch is structurally
   impossible once packed (GLSL's `uint` can't be negative), and the
   out-of-bounds bounds check is kept as a direct mirror of the CPU code's
   own defensive third condition, checked against
   `boneMatrices.matrices.length()` (GLSL's runtime array length for the
   last member of a `std430` buffer block).
4. **No-valid-influence fallback**: `totalWeight > 0.0` gates between the
   blended result and the bind pose, for **both** position and normal —
   matches the CPU code's own `if (totalWeight > 0.0f) { ... } else {
   outPositions[i] = bindPosition; outNormals[i] = bindNormal; }` exactly.

`Mat4::TransformPoint()`/`Mat4::TransformVector()` (used by the CPU code)
were also confirmed, by reading `Mat4.h`'s own doc comments, to be exactly
`(m * vec4(p, 1.0)).xyz` and `mat3(m) * v` respectively (w=1 vs. w=0,
top-left 3×3 only, no inverse-transpose) — i.e. **not**
`Mat4::TransformNormal()` (which uses `Inverse().Transposed()` for correct
behavior under non-uniform scale). The shader deliberately uses
`mat3(m) * bindNormal`, matching the CPU code's actual call
(`TransformVector`), not the more "textbook-correct" normal transform —
this is a faithful mirror of the oracle, not an independent judgment call,
per this phase's own explicit instruction not to "improve" on the CPU math.

### 2. A documented, additive extension to Phase 1's binding table

Phase 1's fixed 4-binding table (bind pose / skin weights / bone matrices /
output) has **no slot for a UV buffer at all** — an oversight discovered
only once actually writing the Uv-variant kernel (UVs never change under
skinning and must be copied straight through from a bind-pose UV buffer,
but Phase 1 never allocated it a binding number). Per the master strategy's
own explicit instruction ("if the GLSL side turns out to want a different
physical layout than what Phase 1 specified, that is a Phase 1 revision,
not a Phase 2 workaround... write this dependency down loudly"), this was
resolved as a **documented, additive** extension rather than a silent
workaround:

- **Binding 4** (`readonly buffer`, plain `vec2[]`, matching `GpuUv` exactly)
  was added, used **only** by the `PositionNormalUv` variant's own
  descriptor-set layout — the untextured `PositionNormal` variant is
  completely unaffected and still uses exactly bindings 0-3.
- `GpuSkinningTypes.h`'s own binding-table comment (the single source of
  truth the strategy docs themselves designate) was updated with a
  "PHASE 2 ADDENDUM" note explaining this addition and pointing at
  `SkinVerticesPositionNormalUv.comp`'s own header comment for the full
  reasoning. No existing struct, `static_assert`, or pack function in
  `GpuSkinningTypes.h`/`.cpp` was touched — `GpuUv` (already defined in
  Phase 1, already unused until now) is exactly what this binding needed,
  with zero changes.

### 3. C++ pipeline/descriptor-set-layout scaffolding

New module, `src/Renderer/GpuSkinning/GpuSkinningPipelines.h/.cpp`:

- **`GpuSkinningPipelines`** — owns the two `VkDescriptorSetLayout`s (4
  bindings for `PositionNormal`, 5 for `PositionNormalUv`) and the two
  `ComputePipeline`s built against them, one per `.comp` file above. Built
  lazily via `EnsureInitialized(Renderer&)` — idempotent, safe to call every
  frame — mirroring `ComputeBlurValidation::EnsureInitialized()`'s own
  proven pattern exactly (including its RAII destructor shape: the two
  plain `VkDescriptorSetLayout` handles are destroyed by hand in this
  class's destructor, since they aren't wrapped in any existing RAII type,
  while the two `ComputePipeline`s clean up themselves).
- **`kSkinningLocalSizeX`** — a named `constexpr std::uint32_t` (256),
  exposed from this header so a future Phase 3/5 dispatch call site has a
  single, shared place to reference this shader-side constant from, rather
  than re-declaring its own copy (the exact convention `ComputeDispatch.h`
  documents and `ComputeBlurValidation.cpp` already follows for
  `kBoxBlurLocalSizeX`/`kBoxBlurLocalSizeY`).
- Both `ComputePipeline`s are constructed via
  `Renderer::CreateComputePipeline()` — no new Renderer/GpuResourceFactory
  method was needed; that factory method already existed from the earlier,
  general compute-shader campaign this phase builds on.
- **Deliberately does NOT**: allocate any per-model `ComputeDescriptorSet`
  (that is Phase 4's job — a `GpuSkinningPipelines` instance is meant to be
  shared/held by whatever Phase 4's per-model cache turns out to be, per
  this phase's own Step 3.4/Step 5 instructions), decide dispatch math
  beyond exposing the shared `kSkinningLocalSizeX` constant, or touch
  `RenderGraph`/`AnimationSystem` in any way (that is Phase 3/5's job).

## Wiring

- `CMakeLists.txt` (root):
  - Added `src/Renderer/GpuSkinning/GpuSkinningPipelines.h`/`.cpp` to
    `gte_core`'s source list, right after `GpuSkinningTypes.cpp`.
  - Added two **unconditional** `gte_add_shader()` calls (not gated behind
    `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL`, unlike `BoxBlur.comp`) —
    per the strategy doc's own explicit instruction: GPU skinning is a
    genuine gameplay/runtime feature, not an Editor-only debug tool, and
    must compile/stage in a release (`-DGTE_ENABLE_EDITOR=OFF`) build too.
    Placed right after the existing `TexturedMesh.vert/.frag` calls, with a
    comment explaining the unconditional placement.

No test-file changes were made this phase — see "What was deliberately NOT
done" below for why.

## What was deliberately NOT done (per Phase 2's own "What We Will NOT Do")

- No shader-side pose evaluation (IK, append-inheritance, forward
  kinematics) — the kernel consumes an already-fully-resolved
  `boneMatrices[]` array and has no idea any of that exists.
- No dynamic branching on vertex-count thresholds inside the shader — the
  CPU-side `kMinVerticesToParallelize` "small models run inline" logic has
  no GPU equivalent; that decision (whether to dispatch the GPU kernel at
  all) belongs to Phase 3/5's C++ code, never this shader.
- No hand-optimization (subgroup ops, shared-memory bone-matrix caching) —
  correctness and dispatchability first; Phase 6/7's actual measured
  performance data is what should motivate any future optimization, not
  premature guessing.
- No support for more than 4 bone influences per vertex, and no bone-palette
  compression — matches the CPU path's own hard cap and Phase 1's own
  buffer layout exactly.
- **No RenderGraph pass declaration, no descriptor-set allocation, no
  per-model buffer/mesh lifetime code, and no `AnimationSystem`/`Game.cpp`
  changes of any kind.** This phase's deliverable is exactly: two `.comp`
  files, one small C++ pipeline-construction class, and the CMake wiring
  for both. Phase 3 (RenderGraph Synchronization) and Phase 4 (Per-Model
  Resource Management) are what actually dispatch this kernel and manage
  its per-model resources — neither was touched.

## Verification performed

**Fast compile check only** (per this session's own instructions — no full
build/regression test; that is explicitly deferred to later):

- `cmake --build build --target gte_core` — succeeded; `GpuSkinningTypes.cpp`
  and the new `GpuSkinningPipelines.cpp` compiled and linked into
  `libgte_core.a` with zero errors/warnings related to this change.
- `cmake --build build --target GreatTamanaEngine` — succeeded. Critically,
  this is the step that actually invokes `glslc` on both new `.comp` files
  (`gte_add_shader()`'s real compile step only runs as part of building the
  executable target, not the static library) — both
  `SkinVerticesPositionNormal.comp` and `SkinVerticesPositionNormalUv.comp`
  compiled to valid SPIR-V with **zero GLSL compile errors/warnings**,
  confirming the hand-written `std430` struct declarations (`BindPoseVertex`,
  `SkinWeightsEntry`), the runtime-array `.length()` bounds check, and the
  manual flat-float-array output indexing are all syntactically and
  semantically valid GLSL. Both `.spv` files were staged next to the built
  `.exe` alongside every other engine shader.
- `cmake --build build --target GreatTamanaEngineTests` — succeeded; the
  test binary links cleanly against the updated `gte_core` (no new test
  file was added this phase — see below — so this is purely a "did adding
  two new translation units break anything else" check).
- Did **not** run the full test suite (`ctest`), a clean `build_joboff`
  verification build, or any runtime/GPU-device smoke test — all explicitly
  deferred to "later, after everything done" per this session's
  instructions. **No live-`VkDevice` dispatch of either kernel has been
  performed yet** — that first happens naturally once Phase 3/4/5 actually
  wire a real dispatch call site into a running frame; Phase 6 is where a
  genuine CPU-vs-GPU numeric parity check happens.

### Why no new automated test file was added this phase

Per `AGENTS.md`'s standing "every change to Tier 1 code must come with a
matching test change" rule, this deserves an explicit note: everything
genuinely new and independently *testable* in this phase (the binding
table extension, the pack functions) belongs to Phase 1 and already has its
own Tier-1 coverage (`GpuSkinningTypesTests.cpp`) unaffected by this phase's
changes. The two things this phase actually adds — GLSL shader source, and
a thin C++ class that only calls straight into `Renderer::
CreateComputePipeline()`/`DescriptorSetLayoutBuilder`/`vkDestroyDescriptorSetLayout`
— are **Tier 2 by construction** (they need a live `VkDevice` to do
anything at all; there is no pure-logic subset to extract, unlike e.g.
`DrawStats.h`/`ComputeDispatch.h`'s own ceiling-division math, which
`GpuSkinningPipelines.h` reuses rather than reimplements). This mirrors
`ComputePipeline`/`ComputeDescriptorSet`/`ComputeBlurValidation` itself,
none of which have automated tests either — see `TESTING.md`'s own
"Tier 2" bucket description. `kSkinningLocalSizeX`, the one plain constant
this phase introduces, has no meaningful independent behavior to assert
beyond "does it equal 256", which would be a test of a literal, not of
logic.

## Notes for future phases

- **Phase 3 (RenderGraph Synchronization)** is what actually declares a
  `rg::RenderGraphBuilder::AddComputePass("SkinModel:<name>", ...)` using
  these two `ComputePipeline`s and dispatches via
  `Renderer::Dispatch(pipeline, descriptorSet, pushConstants, ...,
  ComputeGroupCount(vertexCount, kSkinningLocalSizeX), 1, 1)` — mirroring
  `ComputeBlurValidation::AddPass()`'s own shape closely. Remember Phase 3
  v2's own Step 3.6 WAW-hazard mitigation (a fake `ReadBuffer()` on the
  output handle before the real `WriteBuffer()`) applies to **this**
  output buffer specifically.
- **Phase 4 (Per-Model Resource Management)** is what actually allocates a
  `ComputeDescriptorSet` per distinct shared-vertex-buffer group (via
  `Renderer::AllocateComputeDescriptorSet(gpuSkinningPipelines.PositionNormalDescriptorSetLayout()`
  or `...PositionNormalUvDescriptorSetLayout()`, depending on whether the
  group is textured) and calls `Rewrite()` on it exactly once, at
  registration time, per that phase's own documented "descriptor sets never
  need re-`Rewrite()`ing after creation" rule. Phase 4 also decides where a
  single, shared `GpuSkinningPipelines` instance actually lives (this
  phase's own strategy document expected it to be owned by whatever Phase
  4's per-model cache turns out to be — `GpuSkinningPipelines` itself has no
  opinion and was written to be embeddable there directly).
- **The push-constant layout is intentionally minimal**: a single
  `uint32_t vertexCount`, nothing else — both `.comp` files' own
  `PushConstants` block matches this exactly. If Phase 3/5 need to pass
  anything else to the kernel per dispatch (unlikely, per this campaign's
  own "What We Will NOT Do" — no LOD, no per-instance parameters), that is
  a Phase 3/5 concern, not something this phase pre-guessed.
- **Binding 4's addition is scoped to the Uv variant's descriptor-set
  layout only** — a future reader extending the `PositionNormal` (no-UV)
  variant must not accidentally reuse `PositionNormalUvDescriptorSetLayout()`
  for an untextured model; `GpuSkinningPipelines` exposes the two layouts
  as clearly separate accessors specifically to keep this mistake
  structurally awkward to make by accident.
