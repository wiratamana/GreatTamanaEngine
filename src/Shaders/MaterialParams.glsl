// Per-material parameters of the textured shaders (set 0, binding 1).
#ifndef MATERIAL_PARAMS_GLSL
#define MATERIAL_PARAMS_GLSL

// Must match MaterialParams in src/Renderer/MaterialParams.h (32 bytes).
layout(set = 0, binding = 1, std140) uniform MaterialParamsBlock {
    vec4 baseColorFactor; // Linear RGBA multiplier on the texture.
    vec4 params;          // x = alpha cutoff, yzw reserved
} uMaterial;

#endif // MATERIAL_PARAMS_GLSL
