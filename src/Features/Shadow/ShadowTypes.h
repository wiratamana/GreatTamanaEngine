#pragma once

#include "../../Math/Mat4.h"

#include <cstdint>
#include <type_traits>

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

// Scene Services binding 8 (std140, 112 bytes). Member names and order match SceneGlobalUniformBlock in
// DirectionalLightingAndReceiverMask.glsl; a test compares the offsets.
struct SceneLightingUniformData {
    Mat4 lightViewProjection;
    float sunDirectionAndBias[4];
    float sunIlluminanceAndStrength[4];
    float occlusionTexelSizeAndPad[4];
};
static_assert(sizeof(SceneLightingUniformData) == 112);
static_assert(std::is_trivially_copyable_v<SceneLightingUniformData>);

// Matches the std430 block in ShadowMask.comp (144 bytes).
struct ShadowMaskParamsGpu {
    Mat4 invViewProjection;
    Mat4 lightViewProjection;
    float depthBias = 0.0f;
    float unused = 0.0f;
    float shadowTexelSize = 0.0f; // 1 / shadow map resolution.
    float pad = 0.0f;
};
static_assert(sizeof(ShadowMaskParamsGpu) == 144);
static_assert(std::is_trivially_copyable_v<ShadowMaskParamsGpu>);

// Strength push block of ShadowComposite.frag.
struct CompositeParams {
    float strength;
};
static_assert(sizeof(CompositeParams) == 4);

} // namespace gte
