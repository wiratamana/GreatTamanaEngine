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

// atmosphere-scattering-3 campaign, Phase 2
// (task_manager/atmosphere-scattering-3/PHASE2_ATMOSPHERE_AWARE_PREVIEW_CAMERA_FRAMING.md)
// - a SECOND, dedicated camera + proxy-box setup, used ONLY for the Aerial
// Perspective froxel volume's own HTTP preview (auto-selected by
// VolumeTexturePreviewRenderer::RenderPreview() based on its own
// `interpretation` parameter - mirrors VolumeTexturePreviewInterpretation's
// existing selection convention exactly, see VolumeTexturePreviewRenderer.h).
//
// UNLIKE ComputeVolumeCameraSetup() above, this deliberately does NOT scale
// boxHalfExtents proportionally to the volume's raw texel counts - for a
// camera-frustum-shaped froxel LUT, width/height (screen-space column/row
// index) and depth (camera-relative distance SLICE index) are fundamentally
// different UNITS that merely happen to be stored inside the same 3D
// texture; treating all three as comparable physical lengths (which is the
// textbook-CORRECT thing ComputeVolumeCameraSetup() does for an actual
// spatial volume, e.g. a smoke/cloud Texture3D) squashes the ONE axis that
// carries this LUT's entire near/far story into an imperceptible sliver -
// see PHASE1_ROOT_CAUSE_INSTRUMENTATION_AND_REGRESSION_TESTS.md's own
// characterization test (VolumeTexturePreviewMathTests.cpp,
// AerialPerspectiveVolumeDimensionsProduceSeverelyFlattenedDepthAxisUnderGenericFunction)
// for the exact measured numbers this function exists to avoid repeating.
//
// `width`/`height`/`depth` are accepted (matching ComputeVolumeCameraSetup()'s
// own signature, so both functions are trivially interchangeable at the call
// site) but deliberately UNUSED for the box shape itself - kept as
// parameters purely so a future maintainer isn't surprised the two
// functions don't share a signature; do not remove them.
VolumeCameraSetup ComputeAtmosphereAerialPerspectivePreviewCameraSetup(int width, int height, int depth);

// Ray-box intersection against a box centered at the origin with the given
// half-extents (Vec3, one entry per axis) - a straight C++ port of the
// how-to reference material's own RayBox() HLSL function (see
// task_manager/network-impl-6/how-to-instruction.txt), generalized from a
// unit [0,1] box to an arbitrary-half-extent, origin-centered box. Returns
// false (tEnter/tExit left untouched) if the ray never enters the box, or
// only enters it entirely behind the ray's origin.
bool IntersectRayBox(const Vec3& rayOrigin, const Vec3& rayDirection, const Vec3& boxHalfExtents, float& outTEnter,
    float& outTExit);

// atmosphere-scattering-3 campaign, Phase 3
// (task_manager/atmosphere-scattering-3/PHASE3_FRUSTUM_SHAPED_RAYMARCH_PROXY.md)
// - a raymarch proxy shaped like a real camera view frustum: local +Z spans
// [-halfDepth, +halfDepth], and the cross-section half-width/half-height
// grows LINEARLY from (0, 0) at z=-halfDepth (the near "apex" end) to
// (farHalfWidth, farHalfHeight) at z=+halfDepth (the far, wide end). Because
// the taper is LINEAR, every side wall is a flat PLANE (never a curved
// surface) - this is what keeps IntersectRayFrustum() below a
// straightforward generalization of IntersectRayBox()'s own slab method,
// rather than needing genuinely curved-surface intersection math.
struct FrustumProxy {
    float halfDepth = 0.0f;
    float farHalfWidth = 0.0f;
    float farHalfHeight = 0.0f;
};

// A direct C++ implementation of clipping a ray against FrustumProxy's six
// bounding half-spaces (2 for the near/far Z caps, 4 for the four linearly-
// tapering side walls) via the standard Cyrus-Beck-style "compute one
// [tEnter, tExit] t-range per half-space, intersect them all" technique -
// the same overall shape as IntersectRayBox()'s own min/max slab reduction,
// generalized from axis-aligned planes to arbitrary ones. Returns false
// (tEnter/tExit untouched) if the ray never enters the frustum, or only
// enters it entirely behind the ray's own origin - same contract as
// IntersectRayBox().
bool IntersectRayFrustum(const Vec3& rayOrigin, const Vec3& rayDirection, const FrustumProxy& frustum, float& outTEnter,
    float& outTExit);

// Maps a point already known to be INSIDE (or on the boundary of) a
// FrustumProxy - in the frustum's own local space, i.e. the same space
// IntersectRayFrustum() above operates in - to normalized [0,1]^3 texture
// space, mirroring IntersectRayBox()'s callers' own
// `(localPos / boxHalfExtents) * 0.5 + 0.5` formula, generalized for a
// shape whose X/Y cross-section size depends on Z. `localPos.z == -halfDepth`
// (the apex) maps to `uvw.x == uvw.y == 0.5` (dividing by a
// cross-section size of exactly 0 is avoided via a small epsilon floor -
// see the .cpp implementation).
Vec3 MapFrustumLocalPositionToUvw(const Vec3& localPos, const FrustumProxy& frustum);

// atmosphere-scattering-3, Phase 3 - derives the Aerial Perspective preview's
// own FrustumProxy directly from the SAME two constants
// (kAerialPreviewXYHalfExtent/kAerialPreviewDepthHalfExtent)
// ComputeAtmosphereAerialPerspectivePreviewCameraSetup() already uses for its
// (superseded, for this shape) boxHalfExtents - single source of truth for
// both. Non-static so VolumeTexturePreviewRenderer.cpp can call it.
FrustumProxy ComputeAtmosphereAerialPerspectivePreviewFrustum();

} // namespace gte
