#pragma once

// network-impl-6 campaign, Phase 3
// (task_manager/network-impl-6/PHASE3_VOLUME_RAYMARCH_PREVIEW_RENDERER.md) -
// the pure, CPU-only, Tier-1-testable "oracle" for the Volume Texture
// Preview feature's fixed default camera + raymarch proxy-box setup -
// mirrors src/Renderer/Atmosphere/AtmosphereMath.h's own role exactly (see
// AGENTS.md, "Atmosphere Scattering": "every .comp shader is a faithful
// GLSL transcription of this file's own math... if they ever disagree, the
// CPU oracle is right by definition"). This file has ZERO Vulkan dependency
// at all - just plain floats/gte::Vec3.

#include "../Math/Vec3.h"

namespace gte {

// Deterministic, fixed default camera + proxy-box setup for the Volume
// Texture Preview feature (network-impl-6 campaign) - given ONLY a
// volume's own texel dimensions (no other input, no query parameters - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision 3/7), computes
// everything src/Shaders/VolumeTexturePreview.comp needs to build camera
// rays and the proxy box's local-space extents.
struct VolumeCameraSetup {
    Vec3 eyePosition; // World-space (== the box's own local space; no separate world transform exists for this feature).
    Vec3 forward; // Normalized, points from eye toward the box's center.
    Vec3 right; // Normalized, camera-local +X.
    Vec3 up; // Normalized, camera-local +Y.
    float tanHalfFovY = 0.0f; // Vertical half-field-of-view tangent, for ray direction construction.
    Vec3 boxHalfExtents; // The raymarch proxy box's own local-space half-extents (box is centered at the ORIGIN, i.e. spans [-boxHalfExtents, +boxHalfExtents] - NOT [0,1] - see ComputeVolumeCameraSetup()'s own doc comment for why).
};

// boxHalfExtents = 0.5 * (width, height, depth) / max(width, height, depth)
// - i.e. the LONGEST texel dimension always maps to a half-extent of
// exactly 0.5 (a full extent of 1.0), and the other two are scaled down
// proportionally - see PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision 7. `width`/`height`/`depth` must all be > 0 (a value <= 0 is
// clamped to 1 defensively, mirroring VolumeTexture's own "clamp to at
// least 1" constructor discipline).
//
// The camera is placed at a FIXED yaw/pitch (an isometric-like angle: 45
// degrees azimuth, ~35.264 degrees elevation - atan(1/sqrt(2)), the same
// "true isometric" angle many 3D modeling tools default an orbit-camera
// icon view to) around the box's center (the origin), at a distance
// computed to fit the box's own bounding sphere (radius =
// Length(boxHalfExtents)) inside a fixed, generous field of view (45
// degrees vertical) with a small margin - deterministic and depends only
// on `width`/`height`/`depth`.
VolumeCameraSetup ComputeVolumeCameraSetup(int width, int height, int depth);

// Ray-box intersection against a box centered at the origin with the given
// half-extents (Vec3, one entry per axis) - a straight C++ port of the
// how-to reference material's own RayBox() HLSL function (see
// task_manager/network-impl-6/how-to-instruction.txt), generalized from a
// unit [0,1] box to an arbitrary-half-extent, origin-centered box. Returns
// false (tEnter/tExit left untouched) if the ray never enters the box, or
// only enters it entirely behind the ray's origin.
bool IntersectRayBox(const Vec3& rayOrigin, const Vec3& rayDirection, const Vec3& boxHalfExtents, float& outTEnter,
    float& outTExit);

} // namespace gte
