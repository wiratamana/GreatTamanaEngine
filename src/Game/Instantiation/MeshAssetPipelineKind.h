#pragma once

#include <cstddef>
#include <cstdint>

namespace gte {

// Which draw pipeline one submesh part uses.
enum class MeshAssetPipelineKind : std::uint8_t { Untextured, Textured, TexturedAlphaTest, Pbr, PbrAlphaTest };

constexpr MeshAssetPipelineKind ChooseMeshAssetPipelineKind(
    bool hasMaterialTexture, bool tangentLayout, bool alphaTested) noexcept
{
    if (!hasMaterialTexture) {
        return MeshAssetPipelineKind::Untextured;
    }
    if (tangentLayout) {
        return alphaTested ? MeshAssetPipelineKind::PbrAlphaTest : MeshAssetPipelineKind::Pbr;
    }
    return alphaTested ? MeshAssetPipelineKind::TexturedAlphaTest : MeshAssetPipelineKind::Textured;
}

// True when textured submeshes can share one tangent-layout vertex buffer.
// Skinned meshes never do: GPU skinning only supports the position/normal/uv vertex formats.
constexpr bool CanUseTangentLayout(
    bool skinned, std::size_t vertexCount, std::size_t tangentCount, bool everyTexturedSliceIsPbr) noexcept
{
    return !skinned && vertexCount > 0 && tangentCount == vertexCount && everyTexturedSliceIsPbr;
}

} // namespace gte
