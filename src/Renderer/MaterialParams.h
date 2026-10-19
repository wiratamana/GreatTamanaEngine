#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace gte {

// Per-material shader parameters (set 0, binding 1 of the textured shaders; std140, 32 bytes).
// NOTE: Must match the `MaterialParams` block in 'Shaders/TexturedMesh.frag' and 'TexturedMeshAlphaTest.frag'.
struct MaterialParams {
    float baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // Linear RGBA multiplier on the texture.
    float alphaCutoff = 0.5f;                              // Texels with alpha below this are discarded (alpha-test shader).
    float reserved[3] = { 0.0f, 0.0f, 0.0f };

    friend bool operator==(const MaterialParams&, const MaterialParams&) = default;
};
static_assert(sizeof(MaterialParams) == 32);
static_assert(std::is_trivially_copyable_v<MaterialParams>);

// Builds the block from material values. A non-finite value becomes its default; every value is clamped to 0..1.
inline MaterialParams MakeMaterialParams(float r, float g, float b, float a, float alphaCutoff) noexcept
{
    const auto unit = [](float v, float fallback) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : fallback; };
    MaterialParams params;
    params.baseColorFactor[0] = unit(r, 1.0f);
    params.baseColorFactor[1] = unit(g, 1.0f);
    params.baseColorFactor[2] = unit(b, 1.0f);
    params.baseColorFactor[3] = unit(a, 1.0f);
    params.alphaCutoff = unit(alphaCutoff, 0.5f);
    return params;
}

} // namespace gte
