# PHASE1 — Foundations: Bounds, Indirect Types, and Render-Graph Vocabulary

## Parent
`PHASE0_MASTER_STRATEGY.md` — read it first, in full, before starting here.

## Step 1: The Goal (Where are we going?)

Establish every PURE, Tier-1-testable data type and math routine this
campaign needs, plus the one small render-graph vocabulary extension
(`ResourceAccess::VertexShaderStorageRead`), with **zero Vulkan device
dependency and zero behavior change to any existing pass**. Mirrors the
original Render Graph campaign's own Phase 1 discipline ("build the pure
vocabulary first, fully independent of the live-device half that consumes it
later") and `GpuSkinningTypes.h`'s own precedent exactly.

By the end of this phase:
1. A local-space `AABB` type exists and can be computed from a raw CPU-side
   vertex-position array.
2. `IndirectDrawCommand` (a plain mirror of `VkDrawIndexedIndirectCommand`)
   and `GpuCullingInstanceInput` (the per-instance struct the culling compute
   shader will read, and the vertex shader will also read for its model
   matrix) exist, with `static_assert`-verified sizes/layouts.
3. Pure, hand-verifiable frustum-plane-extraction and AABB-vs-frustum
   intersection math exist and are covered by Tier-1 tests using known,
   simple, hand-computed matrices — this is the reference implementation
   PHASE3's real GLSL shader will be checked against.
4. `RenderGraphTypes.h`'s `ResourceAccess` enum gains
   `VertexShaderStorageRead`, with full `IsWriteAccess()`/`ToString()`/
   `RequiredStateFor()` handling and matching Tier-1 tests.
5. The still-missing buffer-side `ComputeShaderWrite -> IndirectCommandRead`
   hand-simulated barrier regression test is added to
   `RenderGraphBarrierPlannerTests.cpp` (closing a gap flagged, but never
   closed, by the compute-shader campaign's own Phase 5).

## Step 2: The Situation (Where are we now?)

- `src/Math/` has `Vec2.h`/`Vec3.h`/`Vec4.h`/`Mat4.h`/`Quat.h` — no `AABB`
  type, no frustum-plane type, anywhere.
- `src/Renderer/Mesh.h` carries `m_vertexBuffer`/`m_vertexCount`/
  `m_indexBuffer`/`m_indexCount` only — no bounds field of any kind.
- `src/Renderer/GpuSkinning/GpuSkinningTypes.h` is the exact precedent to
  mirror for the new GPU-buffer-layout structs: Vulkan-header-free, every
  struct that crosses into a compute shader's `std430 readonly buffer` block
  is explicitly padded to a vec4-multiple size with a `static_assert`
  documenting exactly why, and every CPU->GPU packing function is a pure,
  Tier-1-testable free function (`PackBindPoseVertices()` etc.) — never
  inlined into a live-device call site.
- `RenderGraphTypes.h`'s `ResourceAccess` enum (line ~158) has exactly 6
  values today (`ColorAttachmentWrite`/`DepthStencilAttachmentReadWrite`/
  `ShaderRead`/`TransferSrc`/`TransferDst`/`ComputeShaderRead`/
  `ComputeShaderWrite`/`IndirectCommandRead`/`VertexBufferRead` — 9 total,
  the doc comment undercounts). `IsWriteAccess()`/`ToString()`
  (`RenderGraphTypes.cpp`) and `RequiredStateFor()`
  (`RenderGraphBarrierPlanner.cpp`) are all deliberately exhaustive switches
  with **no `default:` case** — confirmed directly by reading all three — so
  adding a new enumerator without updating all three fails to compile, by
  design. This is the extension point this phase uses.
- `RequiredStateFor(ResourceAccess::ShaderRead, ...)` returns
  `VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT` — confirmed by direct read of
  `RenderGraphBarrierPlanner.cpp`. **A vertex-shader storage-buffer read
  declared as plain `ShaderRead` today would produce a barrier with the wrong
  destination stage — a real, silent GPU hazard, not just an inaccurate
  label.** This is why Locked Design Decision 5 (PHASE0) requires a genuinely
  new enumerator, not reuse of `ShaderRead`.
- `tests/Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp` already has
  `RequiredStateForIndirectCommandRead` (single-state check) and
  `ComputeShaderWriteFollowedByShaderReadEmitsExactlyOneCorrectBarrier` (a
  full two-state hand-simulated TEXTURE-side transition test) — but **no
  buffer-side `ComputeShaderWrite -> IndirectCommandRead` two-state
  transition test exists** (confirmed: `search_in_dir` for
  "ComputeShaderWrite -> IndirectCommandRead" only finds a comment
  cross-reference at line 197, describing a test that was never actually
  written).
- No frustum-plane extraction or AABB-vs-frustum test exists anywhere in this
  codebase (confirmed via repo-wide search for "Frustum" — the only hits are
  `atmosphere-scattering-3`'s own unrelated `FrustumProxy` raymarch-proxy
  shape, a completely different concept: a tapering-box RAYMARCH BOUNDS for a
  volume-texture preview, not a camera view-frustum culling test).

## Step 3: The Plan

### 3.1 — `src/Renderer/Culling/CullingTypes.h` / `.cpp` (new folder + files)

Mirrors `src/Renderer/GpuSkinning/GpuSkinningTypes.h`'s exact shape and
header-comment discipline (Vulkan-header-free; every GPU-facing struct is
`std430`-padded with a `static_assert`; every packing function is pure).

```cpp
// Local-space (object-space) axis-aligned bounding box - describes GEOMETRY,
// shared by every instance drawn with a given Mesh, never per-entity.
struct AABB {
    Vec3 min{ 0.0f, 0.0f, 0.0f };
    Vec3 max{ 0.0f, 0.0f, 0.0f };
};

// Computes the tightest AABB enclosing every position in `positions` - pure,
// no GPU/Renderer dependency. Returns AABB{} (a degenerate, zero-sized box at
// the origin) for an empty `positions` - never divides by zero, never reads
// out of bounds.
AABB ComputeLocalAABB(const std::vector<Vec3>& positions);

// A single frustum clip plane, in the form `dot(normal, p) + distance >= 0`
// == "p is on the INSIDE of this plane". Pure data, no Vulkan dependency.
struct Plane {
    Vec3 normal{ 0.0f, 0.0f, 1.0f };
    float distance = 0.0f;
};

// Extracts the 6 frustum clip planes (left/right/bottom/top/near/far) from a
// combined view-projection matrix, via the standard Gribb/Hartmann row/column
// extraction method. MUST be hand-verified against Math/Mat4.h's ACTUAL
// storage convention (row-major vs. column-major, and whether Mat4's own
// operator() / element-access reads a "row" or a "column") before trusting
// this against real camera data - read Math/Mat4.h's own class comment and
// existing Mat4 tests first; do not assume a convention. Add a Tier-1 test
// using a KNOWN, simple, hand-computed view-projection (e.g. an orthographic
// projection looking down -Z with no rotation) whose 6 plane equations can be
// verified by hand, BEFORE trusting this function against
// Camera::ProjectionMatrix()/ViewMatrix() real output.
std::array<Plane, 6> ExtractFrustumPlanes(const Mat4& viewProjection);

// True if `worldAABB` intersects (or is fully inside) every one of the 6
// `planes` - the standard "positive vertex" (p-vertex) test: for each plane,
// pick the AABB corner furthest along that plane's normal; if even that
// corner is outside, the whole box is outside. A CONSERVATIVE test (never
// FALSE-culls a box that's actually at least partially visible; may accept a
// box that's actually fully outside in rare corner cases near a plane edge -
// the industry-standard, acceptable trade-off for real-time frustum culling).
bool AABBIntersectsFrustum(const AABB& worldAABB, const std::array<Plane, 6>& planes);

// GPU-side mirror of VkDrawIndexedIndirectCommand (Renderer/IndirectDrawTypes.h,
// see 3.2 below - kept in a SEPARATE file since it has nothing to do with
// culling specifically, only with indirect draws generally).

// GPU-side per-instance input the culling compute shader reads AND the new
// instanced vertex shader ALSO reads (for its own model matrix) - std430,
// 16-byte-aligned blocks throughout (mirrors GpuBindPoseVertex's own padding
// discipline). `worldMatrix` is a plain 16-float column array - VERIFY
// Math/Mat4.h's own memory layout before writing the packing function below;
// GLSL's std430 `mat4` is column-major, 4 vec4 columns - if Math/Mat4.h is
// row-major internally, PackCullingInstanceInput() below must transpose, not
// just memcpy.
struct GpuCullingInstanceInput {
    float worldMatrix[16] = { 0.0f };
    float aabbMinX = 0.0f, aabbMinY = 0.0f, aabbMinZ = 0.0f, _padMin = 0.0f;
    float aabbMaxX = 0.0f, aabbMaxY = 0.0f, aabbMaxZ = 0.0f, _padMax = 0.0f;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t vertexOffset = 0;
    std::uint32_t _pad = 0;
};
static_assert(sizeof(GpuCullingInstanceInput) == 112,
    "GpuCullingInstanceInput must match a std430 mat4 + vec4 + vec4 + uvec4 layout exactly");
// NOTE (worth knowing, not urgent, flagged during the general campaign
// double-check): firstIndex/indexCount/vertexOffset are identical for every
// instance in one batch (they describe the shared Mesh, not the per-entity
// Transform) - storing them per-instance here is a deliberate, ACCEPTED
// redundancy (112 bytes/instance instead of a smaller shared value), not a
// correctness bug. A future bandwidth-sensitive revision could hoist these
// three fields to a per-batch push constant/uniform instead of duplicating
// them per instance - not attempted in this campaign, since it would touch
// this already-locked struct shape PHASE3's shader and PHASE4's packing code
// both depend on; a genuine follow-up item only.

// Packs one instance's world matrix + world-space AABB (ALREADY TRANSFORMED
// by the caller - see PHASE4's own per-frame packing step, which transforms
// each Mesh's own LOCAL AABB by that entity's world matrix once per frame on
// the CPU, exactly as cheap as the existing per-frame Transform resolution
// already is) + this batch's shared mesh geometry offsets into the GPU
// layout above. Pure function, no GPU/Renderer dependency.
GpuCullingInstanceInput PackCullingInstanceInput(const Mat4& worldMatrix, const AABB& worldAABB,
    std::uint32_t firstIndex, std::uint32_t indexCount, std::int32_t vertexOffset);

// Transforms a LOCAL-space AABB by `worldMatrix` into a new, axis-aligned
// WORLD-space AABB (the standard "transform all 8 corners, take the min/max"
// approach - simple, always correct, conservative under non-uniform
// scale/rotation). Pure function.
AABB TransformAABB(const AABB& localAABB, const Mat4& worldMatrix);
```

### 3.2 — `src/Renderer/IndirectDrawTypes.h` (new file)

```cpp
#include <volk.h>

// Byte-for-byte mirror of VkDrawIndexedIndirectCommand - kept as this
// engine's OWN named type (rather than using the Vulkan struct directly
// everywhere) purely so a future reader/test never needs a live VkDevice
// just to talk about "one indirect draw command's shape" - mirrors
// GpuSkinningTypes.h's own "Vulkan-header-free where possible" discipline,
// EXCEPT this one file legitimately needs <volk.h> for the static_assert
// cross-check below (VkDrawIndexedIndirectCommand's own definition), same
// carve-out RenderGraphTypes.h already documents for itself.
struct IndirectDrawCommand {
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t firstIndex = 0;
    std::int32_t vertexOffset = 0;
    std::uint32_t firstInstance = 0;
};
static_assert(sizeof(IndirectDrawCommand) == sizeof(VkDrawIndexedIndirectCommand),
    "IndirectDrawCommand must stay byte-for-byte identical to VkDrawIndexedIndirectCommand - "
    "this buffer is bound directly as the argument buffer to vkCmdDrawIndexedIndirect(Count)");
static_assert(sizeof(IndirectDrawCommand) == 20, "VkDrawIndexedIndirectCommand's own documented size");
```

### 3.3 — `RenderGraphTypes.h`/`.cpp` — new `ResourceAccess::VertexShaderStorageRead`

- Add the enumerator (append at the END of the enum, matching this file's own
  "never insert in the middle" convention already documented for
  `PassRecord`'s trailing fields).
- Update `IsWriteAccess()` (`RenderGraphTypes.cpp`) — `false`.
- Update `ToString()` (`RenderGraphTypes.cpp`) — `"VertexShaderStorageRead"`.
- Update `RequiredStateFor()` (`RenderGraphBarrierPlanner.cpp`) — buffer-only
  in practice (mirror `IndirectCommandRead`/`VertexBufferRead`'s own
  `layout` = `VK_IMAGE_LAYOUT_UNDEFINED` convention):
  ```cpp
  case ResourceAccess::VertexShaderStorageRead:
      return ResourceState{
          VK_IMAGE_LAYOUT_UNDEFINED,
          VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
          VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      };
  ```
- `TargetsDepthState()`/`IsColorAttachmentWriteAccess()`
  (`RenderGraphBarrierPlanner.cpp`) — both should return `false` for it; add
  it to both functions' existing exhaustive `EXPECT_FALSE(...)` test lists in
  `RenderGraphBarrierPlannerTests.cpp`.
- Add to `RenderGraphTypesTests.cpp`: a
  `VertexShaderStorageReadIsNotAWrite` case (mirrors
  `IndirectCommandReadIsNotAWrite`), add it to whatever exhaustive
  `ToString()`-round-trip test list already iterates every enumerator, and a
  `ToString()` case confirming `"VertexShaderStorageRead"`.

### 3.4 — The missing buffer-side barrier regression test

Add to `RenderGraphBarrierPlannerTests.cpp`, immediately after the existing
`ComputeShaderWriteFollowedByShaderReadEmitsExactlyOneCorrectBarrier` test
(mirror its exact shape, buffer-flavored):

```cpp
TEST(RenderGraphBarrierPlannerTest, ComputeShaderWriteFollowedByIndirectCommandReadEmitsExactlyOneCorrectBarrier)
{
    const ResourceState afterComputeWrite = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);
    const ResourceState afterIndirectRead = RequiredStateFor(ResourceAccess::IndirectCommandRead, false);

    ASSERT_TRUE(RequiresBarrier(afterComputeWrite, afterIndirectRead));

    const VkBufferMemoryBarrier2 barrier =
        BuildBufferMemoryBarrier2(VK_NULL_HANDLE, 0, VK_WHOLE_SIZE, afterComputeWrite, afterIndirectRead);

    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
}
```

Also add a `ComputeShaderWriteFollowedByVertexShaderStorageReadEmitsExactlyOneCorrectBarrier`
sibling test proving the new PHASE1.3 enumerator's own barrier fields
(`VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT` / `VK_ACCESS_2_SHADER_STORAGE_READ_BIT`).

### 3.5 — New test files

- `tests/Renderer/Culling/CullingTypesTests.cpp` (new folder, mirroring
  `tests/Renderer/GpuSkinning/GpuSkinningTypesTests.cpp`'s own precedent) —
  covers `ComputeLocalAABB()` (empty input, single point, a simple cube's 8
  corners), `TransformAABB()` (identity matrix is a no-op; a pure translation
  shifts min/max by exactly that amount; a 90-degree rotation produces the
  expected re-oriented box), `ExtractFrustumPlanes()` (a hand-computed simple
  case — VERIFY the exact expected numbers against Math/Mat4.h's real
  convention before writing expectations, do not guess), `AABBIntersectsFrustum()`
  (a box fully inside all 6 planes; a box fully outside one plane; a box
  straddling one plane), and `PackCullingInstanceInput()`/`static_assert`
  sizes.
- Update `tests/CMakeLists.txt` to add the new test source file(s), mirroring
  how every existing `tests/<Layer>/` folder is already registered.

### What We Will NOT Do (this phase)

- No compute shader, no `Renderer`/`Pipeline`/`ComputePipeline` involvement of
  any kind — everything in this phase is pure, Vulkan-header-free-or-adjacent
  data/math, callable and testable with zero live `VkDevice`.
- No `Mesh.h`/`MeshRenderer.h`/`RenderSystem.h` changes yet (PHASE2/4).
- No change to any existing pass's declared reads/writes, and no change to
  any existing `ResourceAccess` enumerator's own existing behavior — this
  phase is purely additive.

### Compile check

Fast, targeted incremental build only (per PHASE0's Locked Design Decision 9)
— build just the affected target(s)
(`gte_core`/`GreatTamanaEngineTests`), never a full regression run. Confirm
the new Tier-1 tests pass. Write `PHASE1_COMPLETION_REPORT.md` next to this
file (what was built, exact final struct sizes, and — importantly — the
ACTUAL Math/Mat4.h storage convention discovered while writing
`ExtractFrustumPlanes()`/`PackCullingInstanceInput()`, since PHASE3's shader
and PHASE4's packing code both depend on getting this exactly right). Commit.
