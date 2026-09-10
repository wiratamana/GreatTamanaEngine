#pragma once

#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <cstdint>

namespace gte {

// ============================================================================
// Atmosphere Scattering + Aerial Perspective - Phase 1: Physical Model
// Foundations
// ============================================================================
// See task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md
// for the full 9-phase campaign map and
// task_manager/atmosphere-scattering-1/
// ATMOSPHERE_PHASE1_REFERENCE_ANALYSIS_AND_PHYSICAL_MODEL_FOUNDATIONS_v1.md
// for this phase's own design reasoning. This file defines the plain GPU
// buffer LAYOUT every later phase's compute shader reads - it creates NO
// Vulkan buffers/descriptor sets itself (a future phase's own orchestration
// file does that, mirroring src/Renderer/GpuSkinning/'s own
// GpuSkinningTypes.h [pure data] vs. GpuSkinningPipelines.h [live-VkDevice
// orchestration] split).
//
// Deliberately Vulkan-header-free (no <volk.h>/<vulkan/vulkan.h> anywhere in
// this file) - mirrors src/Renderer/GpuTiming.h's own "zero Vulkan-header
// dependency" precedent exactly, so every struct here stays trivially
// Tier-1-testable with no live VkDevice at all (see
// tests/Renderer/Atmosphere/AtmosphereMathTests.cpp).
//
// PADDING/LAYOUT CONVENTION: every struct below is laid out as a sequence of
// 16-byte GROUPS, matching GLSL's std140 alignment rule for a vec3 (a vec3
// member aligns its GROUP to 16 bytes, and a scalar immediately following it
// packs into that same group's trailing 4 bytes with zero gap - this is
// exactly how the cloned reference's own plAtmosphereSettings struct
// packs its vec3+float pairs, see
// _reference/pl-sky/shaders/sky_interop.inc). As of the "Revision Notes"
// double-check pass on this phase's own strategy document, these structs
// will actually be bound as a read-only STORAGE buffer (std430), not a true
// `uniform` block, since the engine has no UBO descriptor plumbing yet - but
// std140 and std430 produce IDENTICAL padding for a flat, array-free struct
// like both of these, so every group below is std140-AND-std430-compatible
// with no changes needed either way.
//
// `Vec3` (src/Math/Vec3.h) is used directly as each group's vec3 member
// (rather than three separately-named float fields, unlike
// src/Renderer/GpuSkinning/GpuSkinningTypes.h's own per-vertex structs) since
// this file's structs are each created/read ONCE per frame (or once at
// startup), not packed by the thousand per vertex - ergonomics for
// AtmosphereMath.h's Vec3-based math clearly wins over the vertex-buffer
// case's own "avoid any assumption about a math type's memory layout"
// caution. The static_assert immediately below is what makes that
// assumption an explicit, checked one rather than a silent hope.
static_assert(sizeof(Vec3) == 12,
    "AtmosphereParametersGpu/AtmosphereFrameUniforms below assume Vec3 is a "
    "tightly-packed 3xfloat POD (see src/Math/Vec3.h) so a Vec3 field "
    "immediately followed by a float field packs into exactly 16 bytes, "
    "matching GLSL's std140/std430 vec3+scalar layout rule with zero extra "
    "compiler-inserted padding.");

// Every physical constant describing one planet's atmosphere - populated by
// AtmosphereParameters.h's MakeDefaultEarthAtmosphereParameters() with
// values transcribed verbatim from the cloned reference implementation
// (hoffstadt/pl-sky) - see task_manager/atmosphere-scattering-1/
// ATMOSPHERE_REFERENCE_NOTES.md, Section 1, for the full citation table.
// Every scattering/absorption coefficient is in units of PER KILOMETER
// (km^-1); every height/radius/thickness field is in KILOMETERS - matching
// the reference's own units exactly (its planetRadius = 6371.0f is
// unmistakably Earth's radius in km).
//
// DEVIATION FROM THIS PHASE'S OWN STRATEGY DOCUMENT (flagged per Phase 0's
// "the real source always wins" rule): the strategy document anticipated a
// Bruneton-style two-layer piecewise ozone absorption profile and a field
// named `atmosphereRadiusKm`. The ACTUAL cloned reference uses a much
// simpler single symmetric TENT profile (`ozoneTentCenterKm`/
// `ozoneTentHalfWidthKm` below - see AtmosphereMath.h's
// OzoneDensityAtHeight()) and stores the atmosphere's THICKNESS above the
// ground (`atmosphereThicknessKm`, matching every one of the reference's own
// ray/UV formulas, which consistently work in height-above-ground) rather
// than an absolute radius from the planet's center - AtmosphereRadiusKm()
// (AtmosphereParameters.h) is the one small helper that derives the latter
// from the former for any call site that needs it.
struct AtmosphereParametersGpu {
    // Group 1 (16 bytes): Rayleigh scattering coefficient at sea level - this
    // ALSO equals Rayleigh extinction (real air has ~zero Rayleigh
    // absorption, so there is no separate rayleighAbsorption field - matches
    // the reference exactly, see ATMOSPHERE_REFERENCE_NOTES.md Section 1) -
    // plus the reciprocal of the Rayleigh density profile's exponential
    // scale height (see AtmosphereMath.h's RayleighDensityAtHeight()).
    Vec3 rayleighScattering = Vec3(0.0058f, 0.0135f, 0.0331f);
    float rayleighDensityExpScale = 1.0f / 8.0f; // 1 / (8km scale height)

    // Group 2 (16 bytes): Mie scattering coefficient - intentionally GREY
    // (all three channels equal, mirroring the reference's own single float
    // `scatteringMieGround` broadcast to every channel - Mie scattering off
    // aerosols is essentially wavelength-independent, unlike Rayleigh) -
    // plus the Cornette-Shanks phase function's asymmetry parameter g.
    Vec3 mieScattering = Vec3(0.006f, 0.006f, 0.006f);
    float miePhaseG = 0.76f;

    // Group 3 (16 bytes): Mie absorption coefficient (also grey; derived
    // from the reference's own extinctionMieGround(0.00666) -
    // scatteringMieGround(0.006) = 0.00066) plus the reciprocal of the Mie
    // density profile's exponential scale height.
    Vec3 mieAbsorption = Vec3(0.00066f, 0.00066f, 0.00066f);
    float mieDensityExpScale = 1.0f / 1.2f; // 1 / (1.2km scale height)

    // Group 4 (16 bytes): ozone absorption coefficient (the Chappuis
    // absorption band - ozone scatters essentially nothing, only absorbs)
    // plus the ozone "tent" density profile's center altitude.
    Vec3 ozoneAbsorption = Vec3(0.00065f, 0.00188f, 0.00008f);
    float ozoneTentCenterKm = 25.0f;

    // Group 5 (16 bytes): ground albedo (the fraction of incident light a
    // planet-surface hit reflects back - consumed by the Multi-Scattering
    // LUT's ground-bounce term, Phase 4) plus the ozone tent's half-width.
    Vec3 groundAlbedo = Vec3(0.3f, 0.3f, 0.3f);
    float ozoneTentHalfWidthKm = 15.0f;

    // Group 6 (16 bytes): planet/atmosphere sizing, plus two explicitly
    // reserved padding floats (never silently relying on compiler-inserted
    // tail padding) so this whole struct's size is a clean multiple of 16
    // bytes throughout.
    float planetRadiusKm = 6371.0f;
    float atmosphereThicknessKm = 100.0f;
    float _pad0 = 0.0f;
    float _pad1 = 0.0f;
};
static_assert(sizeof(AtmosphereParametersGpu) == 96,
    "AtmosphereParametersGpu must be exactly six 16-byte std140/std430-"
    "compatible groups (6 * 16 = 96 bytes) - see each group's own doc "
    "comment above.");

// The PER-FRAME values every atmosphere-sampling pass from Phase 5 onward
// needs, threaded in alongside AtmosphereParametersGpu above (which changes
// only when the atmosphere's own physical constants change, i.e. almost
// never).
struct AtmosphereFrameUniforms {
    // Group 1 (16 bytes): the camera's position in ATMOSPHERE-SPACE
    // kilometers (see AtmosphereParameters.h's
    // WorldPositionToAtmosphereSpaceKm()) - i.e. height above the planet's
    // center in the same coordinate frame every AtmosphereMath.h function
    // above works in, NOT the engine's raw world-space Transform::position -
    // plus one reserved padding float.
    Vec3 cameraPositionAtmosphere = Vec3::Zero();
    float _pad0 = 0.0f;

    // Group 2 (16 bytes): normalized direction FROM the scene TOWARD the
    // sun, plus one reserved padding float.
    Vec3 sunDirection = Vec3::Up();
    float _pad1 = 0.0f;

    // Group 3 (16 bytes): the sun's color/intensity (illuminance), plus one
    // reserved padding float.
    Vec3 sunIlluminance = Vec3::One();
    float _pad2 = 0.0f;

    // Groups 4-7 (64 bytes, Phase 6 -
    // ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md): this VIEW's
    // combined view-projection matrix, ALREADY INVERTED on the CPU side
    // (never inverted in the shader - a 4x4 inverse is comparatively
    // expensive and this value is only ever computed once per view per
    // frame here, vs. once per froxel COLUMN if done in
    // AtmosphereAerialPerspectiveVolume.comp instead). Needed to reconstruct
    // a world-space view-ray DIRECTION for a given froxel column
    // (AtmosphereCommon.glsl's FroxelColumnToViewRayDirection() unprojects
    // two NDC points - near/far plane - through this matrix and takes their
    // difference, which is handedness/projection-convention-agnostic,
    // unlike pl-sky's own inverse-projection-diagonal shortcut - see this
    // phase's own completion report for the full reasoning). `mat4` is
    // ALREADY exactly 4 vec4 columns (64 bytes, 16-byte-aligned per column)
    // in both this engine's own Mat4 layout (column-major, see Math/Mat4.h)
    // AND GLSL's std140/std430 mat4 layout - no padding needed, matching
    // Mat4::Data()'s existing "uploads with zero transpose" contract.
    // Defaults to Identity() (never Mat4()'s own all-zero default - see
    // Mat4.h) so a caller that forgets to set this (e.g. the Sky-View LUT's
    // own call site, which does not need this field at all) still uploads
    // an invertible, harmless placeholder rather than a singular all-zero
    // matrix.
    Mat4 invViewProjection = Mat4::Identity();
};
static_assert(sizeof(AtmosphereFrameUniforms) == 112,
    "AtmosphereFrameUniforms must be exactly three 16-byte std140/std430-"
    "compatible groups (3 * 16 = 48 bytes) plus one 64-byte mat4 group "
    "(48 + 64 = 112 bytes) - see each group's own doc comment above.");

} // namespace gte
