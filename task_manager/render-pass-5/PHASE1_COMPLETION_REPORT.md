# PHASE1 — Completion Report: Foundations: Bounds, Indirect Types, and Render-Graph Vocabulary

## Status: DONE

Implemented exactly as scoped in `PHASE1_FOUNDATIONS_BOUNDS_INDIRECT_TYPES_AND_VOCABULARY.md`,
with zero Vulkan-device dependency and zero behavior change to any existing
pass. No `PHASE0_COMPLETION_REPORT.md`/prior `PHASEn_COMPLETION_REPORT.md`
existed yet in this folder (checked via `browse_dir` before starting, per
this campaign's own working agreement) — this is the first one.

## What was built

### New files

- **`src/Renderer/Culling/CullingTypes.h`/`.cpp`** (new folder) — mirrors
  `src/Renderer/GpuSkinning/GpuSkinningTypes.h`'s exact shape/discipline
  (Vulkan-header-free, every packing function pure):
  - `AABB` (local-space min/max) + `ComputeLocalAABB(positions)`.
  - `Plane` (`dot(normal, p) + distance >= 0` == inside) +
    `ExtractFrustumPlanes(viewProjection)` — standard Gribb/Hartmann row
    extraction, each plane NORMALIZED (divides by the raw row's normal
    length) so `distance` is a genuinely meaningful signed distance, not
    just a same-sign scalar. Guards a degenerate zero-length normal instead
    of dividing by (near) zero.
  - `AABBIntersectsFrustum(worldAABB, planes)` — the standard conservative
    "positive vertex" (p-vertex) test.
  - `GpuCullingInstanceInput` (112 bytes, `static_assert`-verified) +
    `PackCullingInstanceInput(worldMatrix, worldAABB, firstIndex, indexCount, vertexOffset)`.
  - `TransformAABB(localAABB, worldMatrix)` — transforms all 8 corners, takes
    the min/max (the standard, always-correct, conservative-under-rotation
    approach).
- **`src/Renderer/IndirectDrawTypes.h`** (new file) — `IndirectDrawCommand`,
  a byte-for-byte mirror of `VkDrawIndexedIndirectCommand` (20 bytes,
  `static_assert`-verified against the real Vulkan struct's own `sizeof`, plus
  a second `static_assert` pinning the literal `20`). Kept in its own file,
  separate from `CullingTypes.h`, exactly as scoped (it has nothing to do
  with culling specifically, only with indirect draws generally) — the one
  file in this phase that legitimately includes `<volk.h>`.
- **`tests/Renderer/Culling/CullingTypesTests.cpp`** (new folder) — 10 new
  Tier-1 tests: `ComputeLocalAABB()` (empty input, single point, a shuffled
  cube's 8 corners), `TransformAABB()` (identity is a no-op, a pure
  translation shifts min/max by exactly that amount, a 90-degree yaw
  produces the expected re-oriented box for a deliberately ASYMMETRIC box so
  the test can't pass by symmetry-coincidence), `ExtractFrustumPlanes()`
  (a hand-computed simple orthographic case — see "Math/Mat4.h storage
  convention" below for how these exact expected numbers were derived),
  `AABBIntersectsFrustum()` (fully inside, fully outside the right plane,
  straddling the right plane), `PackCullingInstanceInput()` (field-for-field,
  including a direct `worldMatrix.Data()` comparison), and both
  `GpuCullingInstanceInput`'s and `IndirectDrawCommand`'s documented byte
  sizes.

### Modified files

- **`src/Renderer/RenderGraph/RenderGraphTypes.h`** — appended
  `ResourceAccess::VertexShaderStorageRead` at the END of the enum (never
  inserted in the middle, per this file's own convention), with a doc
  comment cross-referencing this campaign and explaining the distinction
  from `ShaderRead`/`ComputeShaderRead`/`VertexBufferRead`.
- **`src/Renderer/RenderGraph/RenderGraphTypes.cpp`** — `IsWriteAccess()`
  (`false`) and `ToString()` (`"VertexShaderStorageRead"`) both updated;
  both switches are still deliberately exhaustive with no `default:` case.
- **`src/Renderer/RenderGraph/RenderGraphBarrierPlanner.cpp`** —
  `RequiredStateFor()` updated: `VK_IMAGE_LAYOUT_UNDEFINED` /
  `VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT` /
  `VK_ACCESS_2_SHADER_STORAGE_READ_BIT` (buffer-only in practice, same
  `layout` convention as `IndirectCommandRead`/`VertexBufferRead`).
- **`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`** — added
  `VertexShaderStorageReadIsNotAWrite`, added the new enumerator to the
  `ToString()` exhaustive-coverage array, and added its `EXPECT_STREQ` case.
- **`tests/Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp`** —
  added `RequiredStateForVertexShaderStorageRead`; added the new enumerator
  to both `TargetsDepthStateIsTrueOnlyForDepthStencilAttachmentReadWrite`'s
  and `IsColorAttachmentWriteAccessIsTrueOnlyForColorAttachmentWrite`'s
  exhaustive lists (both `EXPECT_FALSE`); added the two missing
  hand-simulated buffer-side barrier regression tests immediately after
  `ComputeShaderWriteFollowedByShaderReadEmitsExactlyOneCorrectBarrier`:
  - `ComputeShaderWriteFollowedByIndirectCommandReadEmitsExactlyOneCorrectBarrier`
    (the gap flagged, but never closed, by the compute-shader campaign's own
    Phase 5 — confirmed via `search_in_dir` before starting that only a
    comment cross-reference existed, never the actual test).
  - `ComputeShaderWriteFollowedByVertexShaderStorageReadEmitsExactlyOneCorrectBarrier`
    (this phase's own new enumerator's sibling case).
- **`CMakeLists.txt`** (root) — added
  `src/Renderer/Culling/CullingTypes.h`/`.cpp` and
  `src/Renderer/IndirectDrawTypes.h` to `gte_core`'s `target_sources` list,
  immediately after the `GpuSkinning` block (mirroring that folder's own
  placement).
- **`tests/CMakeLists.txt`** — added
  `Renderer/Culling/CullingTypesTests.cpp` to `GTE_TEST_SOURCES`
  (immediately after `Renderer/GpuSkinning/GpuSkinningTypesTests.cpp`), plus
  a matching descriptive entry in this file's own header-comment test
  taxonomy.

## Exact final struct sizes

- `GpuCullingInstanceInput` = **112 bytes** (`mat4` (64) + `vec4` (16) +
  `vec4` (16) + `uvec4` (16) = 112 — matches the phase doc's own
  `static_assert` exactly, confirmed both at compile time and by a runtime
  `EXPECT_EQ(sizeof(...), 112u)` test).
- `IndirectDrawCommand` = **20 bytes** (`uint32*3 + int32 + uint32`),
  confirmed byte-for-byte identical to the real `VkDrawIndexedIndirectCommand`
  via `static_assert(sizeof(IndirectDrawCommand) == sizeof(VkDrawIndexedIndirectCommand))`
  — this compiled cleanly against the real Vulkan header, so no hidden
  padding/alignment mismatch exists between the two.

## Math/Mat4.h storage convention (the load-bearing finding this report exists to record)

Verified directly against `src/Math/Mat4.h`/`.cpp` before writing
`ExtractFrustumPlanes()`/`PackCullingInstanceInput()`, exactly as the phase
document required (do not assume, verify):

1. **`Mat4` is COLUMN-MAJOR storage** — `Vec4 columns[4]`, and
   `operator()(row, col)` is defined as `columns[col][row]`. This is stated
   explicitly in `Mat4.h`'s own class comment, and confirmed by
   `operator*(const Mat4&, const Vec4&)` in `Mat4.cpp` matching the
   column-major convention exactly.
2. **`Mat4::Data()` already returns a contiguous column-major `float[16]` —
   bit-identical to what a GLSL `mat4` (std430 or otherwise) expects.**
   Consequence: **`PackCullingInstanceInput()` is a straight `memcpy` of
   `worldMatrix.Data()` into `GpuCullingInstanceInput::worldMatrix` — NO
   transpose is needed.** This was the one genuinely open question the phase
   doc flagged ("if Math/Mat4.h is row-major internally, must transpose") —
   confirmed false; Mat4 is already column-major and GPU-upload-ready as-is,
   matching this engine's existing GPU-upload precedent (push-constant model
   matrices already rely on this same fact, per `Pipeline.h`'s own
   documented convention).
3. **`operator()(row, col)` ALWAYS returns the conventional mathematical
   row/col entry, regardless of the underlying column-major physical
   storage.** This means the standard Gribb/Hartmann frustum-plane-extraction
   formulas (which are conventionally written in terms of a row-major matrix
   `M(i, j)`) can be written directly as `m(row, col)` in `ExtractFrustumPlanes()`
   with ZERO adjustment for storage order — `m(3, 0) + m(0, 0)` etc. is
   exactly the textbook formula, no transpose, no column/row swap. This was
   verified by hand-deriving `Mat4::OrthographicLH_ZO()`'s own concrete
   matrix entries (from `Mat4.cpp`) and computing all 6 plane equations
   symbolically before writing a single line of `ExtractFrustumPlanes()`,
   then confirming the implementation reproduces those exact hand-derived
   numbers in `CullingTypesTests.cpp`'s
   `ExtractFrustumPlanesFromOrthographicProjectionMatchesHandComputedValues`
   test (viewProjection = `OrthographicLH_ZO(-1, 1, -2, 2, 0.5, 10)`, identity
   view ⇒ camera at the origin looking down +Z):
   - Left = `(1, 0, 0)`, distance `1` (inside ⇔ `x >= -1`)
   - Right = `(-1, 0, 0)`, distance `1` (inside ⇔ `x <= 1`)
   - Bottom = `(0, 1, 0)`, distance `2` (inside ⇔ `y >= -2`)
   - Top = `(0, -1, 0)`, distance `2` (inside ⇔ `y <= 2`)
   - Near = `(0, 0, 1)`, distance `-0.5` (inside ⇔ `z >= 0.5` — this
     engine's VULKAN ZERO-TO-ONE depth range, NOT OpenGL's `z >= -w`)
   - Far = `(0, 0, -1)`, distance `10` (inside ⇔ `z <= 10`)

   **Consequence for PHASE3**: the future `Shaders/FrustumCull.comp` GLSL
   shader can use the exact same Gribb/Hartmann row-extraction formulas
   against its own `mat4 viewProjection` uniform with no adjustment either —
   GLSL's `mat4` is column-major with `m[col][row]` indexing, and
   `m(row, col)` on the CPU side already reads the identical logical entry,
   so the CPU oracle (`ExtractFrustumPlanes()`) and the future GPU shader can
   be written from the textbook formula independently and are guaranteed to
   agree, with no silent row/column-order mismatch risk between the two.

## Tier-1 test results (targeted, per Locked Design Decision 9)

Built only the affected targets (`gte_core` + `GreatTamanaEngineTests`) via
`cmake --build build --target GreatTamanaEngineTests --config Debug` after a
`cmake -S . -B build` reconfigure (needed since new source files were added
to the explicit `target_sources` lists, which aren't glob-based). Build
succeeded with no warnings/errors. Ran the filtered subset of tests this
phase actually touches:

```
tests\GreatTamanaEngineTests.exe --gtest_filter=CullingTypes.*:RenderGraphResourceAccessTest.*:RenderGraphBarrierPlannerTest.*
```

Result: **52/52 tests passed** (10 new `CullingTypes` tests, 12
`RenderGraphResourceAccessTest` tests including the new
`VertexShaderStorageReadIsNotAWrite`, and 30 `RenderGraphBarrierPlannerTest`
tests including the 3 new ones this phase added). No full `cmake --build build`
+ full `ctest` regression pass was run, per `PHASE0_MASTER_STRATEGY.md`'s
Locked Design Decision 9 (reserved for PHASE7 only).

## Deviations from the phase document

None. Every struct shape, function signature, and enumerator value matches
`PHASE1_FOUNDATIONS_BOUNDS_INDIRECT_TYPES_AND_VOCABULARY.md` exactly. The one
addition beyond the doc's literal text is normalizing each `Plane` in
`ExtractFrustumPlanes()` (dividing by the raw extracted normal's own length)
— the phase doc's pseudocode didn't show this step explicitly, but its own
doc comment already called for "the standard Gribb/Hartmann row/column
extraction method," which conventionally includes normalization so `distance`
is a genuine signed distance rather than an arbitrarily-scaled value.
`AABBIntersectsFrustum()`'s own boolean test is invariant to this either way
(a positive rescale of one plane's `(normal, distance)` pair never changes
the sign of `dot(normal, p) + distance`), so this is a safe, backward-neutral
addition, not a behavior change relative to the doc's intent.

## No ambiguity requiring `ask_questions`

Every design question this phase touches was already pinned down by
`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decisions and this phase's own
document (exact struct shapes, exact field names/order, exact enumerator
name/semantics, exact barrier field values). The one genuinely open question
the phase doc flagged ("Mat4's storage convention — verify, don't assume")
was resolved by direct inspection of `Mat4.h`/`.cpp` (see above), not by
asking the user — this matches the phase doc's own instruction to verify
via source inspection first.

## Next phase

PHASE2 (`PHASE2_INSTANCED_DRAW_PRIMITIVE_AND_INDIRECT_SUBMIT.md`) can now
depend on: `AABB`/`GpuCullingInstanceInput`/`IndirectDrawCommand` all
existing with locked, `static_assert`-verified shapes; `PackCullingInstanceInput()`
performing a straight, transpose-free `memcpy` of `Mat4::Data()`; and
`ResourceAccess::VertexShaderStorageRead` being fully wired through
`IsWriteAccess()`/`ToString()`/`RequiredStateFor()` with matching Tier-1
coverage, ready for PHASE2's new instanced graphics pass to declare against
its per-instance input buffer.
