#version 450

// Toon surface vertex shader. HairOverlay parts belong to the overlay pass and are clipped away here.
#define DIRECTIONAL_LIGHTING_NO_SHADOWS
#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"
#include "ToonCommon.glsl"

#define TOON_SKIP_OVERLAY_CLASS
#include "ToonSurfaceVertex.glsl"
