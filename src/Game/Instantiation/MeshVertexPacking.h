#pragma once

#include "../../Math/Vec2.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/MeshVertex.h"

#include <vector>

namespace gte {

// Pure, shared vertex-packing helpers - the single home for logic that used
// to be hand-copied FOUR times across Game.cpp (once for the untextured
// submesh at initial load, once for the textured submesh at initial load,
// and once more for each of those two shapes again every frame inside the
// per-frame skinned re-upload loop - see
// GameInstantiationRefactorProposal.txt, Step 2.6/3.1). Both
// MeshAssetGpuCatalog.h (initial upload) and AnimationSystem.h (per-frame
// re-upload after skinning) call these same two functions instead of
// maintaining their own copies.
//
// Pure CPU-side data transforms - no GPU/Renderer/ECS dependency at all, so
// both are genuinely Tier-1-testable (see
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

} // namespace gte
