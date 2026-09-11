#pragma once

#include "../../Math/Vec3.h"

namespace gte {

// ============================================================================
// AtmosphereAerialPerspectiveCompositeMath.h - the PERMANENT CPU ORACLE for
// the Aerial Perspective Composite pass's own per-pixel BRANCH + FINAL BLEND
// decision (src/Shaders/AtmosphereAerialPerspectiveComposite.comp's main()).
// ============================================================================
// atmosphere-scattering-4 campaign - see
// task_manager/atmosphere-scattering-4/PHASE0_MASTER_STRATEGY.md and
// task_manager/atmosphere-scattering-4/AERIAL_PERSPECTIVE_NO_GEOMETRY_BUG_REPORT_20260911.md
// for the full root-cause writeup this file fixes.
//
// Confirmed bug: the composite shader used to run its full
// `sceneColor * transmittance + inScattering` blend UNCONDITIONALLY, even for
// pixels with no opaque geometry drawn into them (rawDepth still at the
// frame's own clear value, 1.0) - double-fogging a sky pixel the Sky
// Background pass had already finished, correctly, earlier in the same
// frame. This file is the reviewed, tested ground truth
// AtmosphereAerialPerspectiveComposite.comp's own GLSL must mirror exactly -
// the same "CPU oracle first, GLSL mirrors it, and if they ever disagree the
// CPU oracle is right by definition" discipline AGENTS.md's "Atmosphere
// Scattering" section already establishes for AtmosphereMath.h, applied here
// to a brand new, narrowly-scoped file (NOT added to AtmosphereMath.h itself
// - see PHASE0's own Locked Design Decision 2/5 for why not: this has nothing
// to do with density/optical-depth math, and AtmosphereMath.h/
// AtmosphereCommon.glsl's shared oracle functions are locked against
// feature-local additions).
//
// DELIBERATE SCOPE LIMIT: this oracle does NOT reproduce the volume's own
// trilinear 3D-texture sample, Z-slice-from-distance mapping, half-texel
// bias, or first-slice fade-in mix - that is GPU-texture-sampling machinery
// with no meaningful CPU equivalent, and is completely unrelated to this
// bug. `sampledAerialRgb`/`sampledAerialA` below are the ALREADY-sampled,
// ALREADY-first-slice-blended texel values (exactly what the shader's own
// local `aerial` variable holds immediately before its final two lines) -
// this oracle only covers the branch and the blend arithmetic that follows.
//
// Pure functions only - zero Vulkan/Renderer/ECS dependency, so this whole
// file is trivially Tier-1-testable (see
// tests/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMathTests.cpp)
// with no live VkDevice/SDL window at all.

// The exact depth-comparison threshold the shader uses to detect "nothing
// was drawn at this pixel this frame" (still at the frame's own clear depth
// value). MUST match AtmosphereAerialPerspectiveComposite.comp's own literal
// `0.999999` exactly - GLSL cannot #include a C++ header, so this constant is
// deliberately duplicated in both places; if this value is ever changed here,
// the SAME literal must be changed in the shader in the same commit (see this
// file's own unit tests for the exact boundary this guards).
constexpr float kAerialPerspectiveFarPlaneDepthThreshold = 0.999999f;

// Returns true when a pixel has no opaque geometry drawn into it this frame
// (rawDepth is still at, or beyond, the clear-depth threshold above) - the
// Sky Background pass has ALREADY produced this pixel's final, correct color
// earlier in the same frame (see this file's own header comment), so the
// Aerial Perspective composite must treat it as a pure pass-through rather
// than sampling/blending the aerial-perspective volume at all.
bool ShouldBypassAerialPerspectiveComposite(float rawDepth) noexcept;

// Mirrors AtmosphereAerialPerspectiveComposite.comp's own final per-pixel
// color decision exactly:
//   - if ShouldBypassAerialPerspectiveComposite(rawDepth): returns
//     sceneColorRgb UNCHANGED (no aerial/strength math involved at all -
//     this is the fix for the no-geometry double-fogging bug).
//   - otherwise: returns sceneColorRgb * transmittance + inScattering, where
//     inScattering = sampledAerialRgb * strength and
//     transmittance  = mix(1.0, sampledAerialA, strength)
//     (unchanged from the shader's pre-existing, already-correct blend
//     formula for real opaque geometry).
//
// `sampledAerialRgb`/`sampledAerialA` must already be the POST-first-slice-
// fade-in-blend texel (see this file's own "DELIBERATE SCOPE LIMIT" note
// above) - this function does not know about, or need, the volume's raw
// texel/slice machinery at all.
Vec3 ComputeAerialPerspectiveCompositeColor(float rawDepth, const Vec3& sceneColorRgb,
    const Vec3& sampledAerialRgb, float sampledAerialA, float strength) noexcept;

} // namespace gte
