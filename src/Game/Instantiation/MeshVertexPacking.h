#pragma once

#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/MeshVertex.h"

#include <cstdint>
#include <vector>

namespace gte {

// Pure, shared vertex-packing helpers - the single home for logic that used
// to be hand-copied FOUR times across Game.cpp (once for the untextured
// submesh at initial load, once for the textured submesh at initial load,
// and once more for each of those two shapes again every frame inside the
// per-frame skinned re-upload loop - see
// GameInstantiationRefactorProposal.txt, Step 2.6/3.1). Both
// MeshAssetGpuCatalog.h (initial upload) and AnimationSystem.h (per-frame
// re-upload after skinning) call these same functions instead of
// maintaining their own copies.
//
// Pure CPU-side data transforms - no GPU/Renderer/ECS dependency at all, so
// all of these are genuinely Tier-1-testable (see
// tests/Game/MeshVertexPackingTests.cpp).

// Packs `positions`/`normals` into a MeshVertex array (the untextured
// PositionNormal layout - see Renderer/MeshVertex.h), one entry per
// position. Falls back to Vec3::Up() per-vertex whenever `normals` doesn't
// match `positions` in size (e.g. a source mesh with no normals at all) -
// the exact same defensive fallback the original inline loops used.
std::vector<MeshVertex> PackMeshVertices(const std::vector<Vec3>& positions, const std::vector<Vec3>& normals);

// Packs `positions`/`normals`/`uvs` into a MeshVertexUv array (the textured
// PositionNormalUv layout - see Renderer/MeshVertex.h), one entry per
// position. Same Vec3::Up() normal fallback as PackMeshVertices() above,
// plus a Vec2::Zero() fallback for `uvs` whenever it doesn't match
// `positions` in size.
std::vector<MeshVertexUv> PackMeshVertexUvs(
    const std::vector<Vec3>& positions, const std::vector<Vec3>& normals, const std::vector<Vec2>& uvs);

// Multithread CPU-skinning optimization, Stage 2/3 (see
// task_manager/optimizing_multi_thread_cpu_skinning/
// MULTITHREAD_CPU_SKINNING_OPTIMIZATION_STRATEGY_v1.md): the exact same
// per-vertex packing PackMeshVertices()/PackMeshVertexUvs() perform above,
// restricted to a single half-open [beginIndex, endIndex) subrange of
// vertices, writing into a caller-owned, caller-SIZED output vector rather
// than returning a freshly-allocated one. This is what lets
// AnimationSystem::Update() (src/Game/Animation/AnimationSystem.cpp):
//   (a) reuse the SAME scratch output vector across frames instead of
//       heap-allocating a fresh one every single frame (Stage 3 - see the
//       strategy document's own "stop re-allocating scratch buffers every
//       frame" section), and
//   (b) split one model's own vertex array into several DISJOINT batches
//       and pack them in parallel via gte::Jobs::Dispatch(), exactly
//       mirroring Animation/VertexSkinning.h's SkinVertexRange() - the
//       same pattern Job System Phase 6 already established for the
//       skinning blend itself, now also applied to the packing step.
//
// `out` must already be sized to (at least) `positions.size()` BEFORE
// dispatching any batches - never resized here - since two concurrent
// batches writing into two different [begin, end) slices of the same
// vector must never race a third, hidden reallocation triggered by one of
// them calling resize(). `endIndex` is clamped internally against the real
// input/output sizes - never reads/writes out of bounds even if a caller
// passes a bad range.
void PackMeshVertexRange(std::uint32_t beginIndex, std::uint32_t endIndex, const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals, std::vector<MeshVertex>& out);

void PackMeshVertexUvRange(std::uint32_t beginIndex, std::uint32_t endIndex, const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals, const std::vector<Vec2>& uvs, std::vector<MeshVertexUv>& out);

} // namespace gte
