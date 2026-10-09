#pragma once

#include <cstdint>

namespace gte {

// Shadow feature tunables - plain data, no behavior. Always run through
// ShadowMath::SanitizeShadowSettings() before use; never read a raw field
// directly inside a render-graph pass body.
struct ShadowSettings {
    // Locked to whatever value this holds on the first frame Shadow.DepthPass
    // runs - the shadow map's render view cannot be resized later. Do not
    // expose a live Inspector slider for this field.
    std::uint32_t mapResolution = 2048;

    float orthoHalfExtentWorld = 20.0f; // Half-size of the light frustum box, world units.
    float nearZ = 0.1f;
    float farZ = 200.0f;
    float depthBias = 0.0025f;
    float strength = 0.6f; // 0 = no darkening, 1 = fully black in shadow.
};

// Packed, GPU-ready layout for Scene Services binding 8 - must match
// DirectionalLightingAndReceiverMask.glsl's own SceneGlobalUniformBlock
// byte-for-byte (112 bytes, fits Renderer's 128-byte ceiling, std140).
struct SceneLightingUniformData {
    float lightViewProjection[16];
    float sunDirectionAndBias[4];
    float sunIlluminanceAndStrength[4];
    float shadowTexelSizeAndPad[4];
};
static_assert(sizeof(SceneLightingUniformData) == 112,
    "SceneLightingUniformData must match DirectionalLightingAndReceiverMask.glsl's own SceneGlobalUniformBlock size.");

} // namespace gte
