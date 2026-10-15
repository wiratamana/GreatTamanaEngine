#pragma once

#include <cstdint>

namespace gte {

// Feature module name, render-feature scope owner and log category.
inline constexpr const char* kShadowFeatureName = "Shadow";

// Shadow tunables. Always pass through SanitizeShadowSettings() before use in a pass body.
struct ShadowSettings {
    // Locked on the first depth-pass frame; the shadow map cannot be resized later.
    std::uint32_t mapResolution = 2048;

    float orthoHalfExtentWorld = 20.0f; // Half-size of the light frustum box, world units.
    float nearZ = 0.1f;
    float farZ = 200.0f;
    float depthBias = 0.0025f;
    float strength = 0.6f; // 0 = no darkening, 1 = fully black in shadow.
};

// Scene Services binding 8 layout (std140, 112 bytes). Must match SceneGlobalUniformBlock in DirectionalLightingAndReceiverMask.glsl.
struct SceneLightingUniformData {
    float lightViewProjection[16];
    float sunDirectionAndBias[4];
    float sunIlluminanceAndStrength[4];
    float shadowTexelSizeAndPad[4];
};
static_assert(sizeof(SceneLightingUniformData) == 112,
    "SceneLightingUniformData must match DirectionalLightingAndReceiverMask.glsl's own SceneGlobalUniformBlock size.");

} // namespace gte
