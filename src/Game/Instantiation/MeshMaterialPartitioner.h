#pragma once

#include "../../Assets/MaterialData.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// One contiguous run of a mesh's own index buffer, attributed to a single
// originating PMX material (or to no material at all - see materialIndex
// below). Pure output of PartitionMeshMaterials() below - deciding whether
// a given slice ends up in the "untextured merged" bucket or its own
// "textured" bucket is left entirely to the CALLER (that decision needs a
// resolved TextureHandle, which requires Renderer+AssetDatabase - genuinely
// impure), so this type stays free of any GPU/asset-resolution concept.
struct MeshMaterialSlice {
    std::size_t start = 0;
    std::size_t count = 0;

    // Index into the `materials` vector PartitionMeshMaterials() was given,
    // or -1 for the trailing "leftover" slice - everything not explicitly
    // claimed by any material's own indexCount run (the whole index range,
    // when `materials` is empty; only a genuine tail otherwise).
    std::int32_t materialIndex = -1;

    // Straight copy of the originating Material::name, or empty for the
    // leftover slice (materialIndex == -1) - see MeshAssetPart::name's own
    // doc comment (MeshAssetGpuCatalog.h) for what an empty value ends up
    // meaning for the spawned entity.
    std::string name;
};

// Splits [0, totalIndexCount) into contiguous per-material slices, in
// `materials` order (each material always owns a CONTIGUOUS run - see
// Material::indexCount's own doc comment, MaterialData.h), clamped
// defensively against a corrupt/mismatched source whose material index
// counts don't actually sum to `totalIndexCount` (a material run that would
// start past the end is clamped to zero length; one that would overrun the
// end is truncated to whatever remains) - exactly the same defensive
// start/count clamping Game::EnsureMeshAsset() used to do inline. Whatever
// remains after the last material's own run (normally nothing when
// `materials` covers the whole mesh, or the ENTIRE range when `materials`
// is empty) becomes one trailing slice with materialIndex == -1. A
// zero-length slice (either an explicit material clamped down to nothing,
// or an empty leftover tail) is never included in the result.
//
// Pure index-range math over plain data - no GPU/Renderer/ECS dependency at
// all, so this is genuinely Tier-1-testable (see
// tests/Game/MeshMaterialPartitionerTests.cpp). Never fails - always
// returns non-overlapping slices whose combined `count` is at most
// `totalIndexCount`.
std::vector<MeshMaterialSlice> PartitionMeshMaterials(std::size_t totalIndexCount, const std::vector<Material>& materials);

} // namespace gte
