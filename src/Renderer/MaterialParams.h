#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace gte {

// Per-material shader parameters (set 0, binding 1; std140, 48 bytes).
// NOTE: Must match the `MaterialParamsBlock` in 'Shaders/MaterialParams.glsl'.
struct MaterialParams {
    float baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // Linear RGBA multiplier on the texture.
    float alphaCutoff = 0.5f;                              // Texels with alpha below this are discarded.
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float toonClass = 0.0f;     // Toon shader class id (see ToonMaterialClass.h). Unused by PBR shaders.
    float edgeWidthScale = 0.0f; // Outline width multiplier of this material.
    float drawEdge = 0.0f;       // 1 = the outline pass draws this material.

    friend bool operator==(const MaterialParams&, const MaterialParams&) = default;
};
static_assert(sizeof(MaterialParams) == 48);
static_assert(std::is_trivially_copyable_v<MaterialParams>);

// Builds the block from material values. A non-finite value becomes its default.
// Colors, cutoff, metallic, roughness and occlusion clamp to 0..1; normal scale to 0..8.
inline MaterialParams MakeMaterialParams(float r, float g, float b, float a, float alphaCutoff,
    float metallic = 1.0f, float roughness = 1.0f, float normalScale = 1.0f,
    float occlusionStrength = 1.0f) noexcept
{
    // -0 becomes +0, so equal values always have equal bytes (the material cache hashes the bytes).
    const auto range = [](float v, float fallback, float hi) {
        const float c = std::isfinite(v) ? std::clamp(v, 0.0f, hi) : fallback;
        return c == 0.0f ? 0.0f : c;
    };
    MaterialParams params;
    params.baseColorFactor[0] = range(r, 1.0f, 1.0f);
    params.baseColorFactor[1] = range(g, 1.0f, 1.0f);
    params.baseColorFactor[2] = range(b, 1.0f, 1.0f);
    params.baseColorFactor[3] = range(a, 1.0f, 1.0f);
    params.alphaCutoff = range(alphaCutoff, 0.5f, 1.0f);
    params.metallicFactor = range(metallic, 1.0f, 1.0f);
    params.roughnessFactor = range(roughness, 1.0f, 1.0f);
    params.normalScale = range(normalScale, 1.0f, 8.0f);
    params.occlusionStrength = range(occlusionStrength, 1.0f, 1.0f);
    return params;
}

// Default-factor material carrying toon data. Edge scale clamps to 0..8; a non-finite scale becomes 0.
inline MaterialParams MakeToonMaterialParams(std::uint32_t classId, float edgeScale, bool drawEdge) noexcept
{
    MaterialParams params;
    params.toonClass = static_cast<float>(classId);
    const float scale = std::isfinite(edgeScale) ? std::clamp(edgeScale, 0.0f, 8.0f) : 0.0f;
    params.edgeWidthScale = scale == 0.0f ? 0.0f : scale; // Normalizes -0 so equal values have equal bytes.
    params.drawEdge = drawEdge ? 1.0f : 0.0f;
    return params;
}

} // namespace gte
