#version 450

// Toon overlay vertex shader. Only HairOverlay parts stay; they sit slightly toward the camera.
#define DIRECTIONAL_LIGHTING_NO_SHADOWS
#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"
#include "ToonCommon.glsl"

#define TOON_KEEP_ONLY_OVERLAY_CLASS
#define TOON_DEPTH_NUDGE 2e-5
#include "ToonSurfaceVertex.glsl"
