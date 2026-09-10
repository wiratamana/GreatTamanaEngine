#pragma once

#include "AtmosphereTypes.h"
#include "../../Math/Vec3.h"

namespace gte {

// ============================================================================
// AtmosphereMath.h - the PERMANENT CPU ORACLE for this campaign's density-
// profile/optical-depth/transmittance/phase-function math.
// ============================================================================
// See task_manager/atmosphere-scattering-1/
// ATMOSPHERE_PHASE1_REFERENCE_ANALYSIS_AND_PHYSICAL_MODEL_FOUNDATIONS_v1.md
// (Step 3.5) for this file's own design reasoning, and
// task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md
// ("Cross-Cutting Engineering Rules") for why this is a PERMANENT, campaign-
// wide discipline, not just this phase's own nice-to-have: every function
// below is a direct, faithful C++ transcription of the corresponding GLSL
// formula the cloned reference implementation (hoffstadt/pl-sky) uses (see
// task_manager/atmosphere-scattering-1/ATMOSPHERE_REFERENCE_NOTES.md for the
// exact citations), and every later phase's own `.comp`/`.frag` shader is
// expected to mirror THIS file's math, not the other way around. If a future
// GPU kernel and this CPU oracle ever disagree, this file is right by
// definition and the shader is what needs fixing - the exact same rule
// AGENTS.md's "GPU Vertex Skinning" section already established for
// Animation/VertexSkinning.cpp.
//
// Pure functions only - zero Vulkan/Renderer/ECS dependency of any kind, so
// this whole file is trivially Tier-1-testable (see
// tests/Renderer/Atmosphere/AtmosphereMathTests.cpp) with no live VkDevice/
// SDL window at all.
//
// COORDINATE CONVENTION for every `positionKm`/`direction` parameter below:
// the planet's CENTER is the origin of this local coordinate frame (matching
// the reference's own `vec3 P = vec3(0, height + planetRadius, 0); vec3
// earthCenter = vec3(0);` convention exactly - see
// _reference/pl-sky/shaders/sky_transmission_lut.comp) - i.e.
// `Length(positionKm) == planetRadiusKm + heightAboveGroundKm`. This is
// DELIBERATELY NOT the engine's own world-space/atmosphere-space-per-camera
// frame (AtmosphereFrameUniforms::cameraPositionAtmosphere, Phase 5's own
// concern) - every function here operates in this simpler, planet-centered
// frame, which is all the physical density-profile/optical-depth math itself
// ever needs.

// The Rayleigh molecular-scattering density profile - a simple exponential
// falloff with altitude (RayleighDensityAtHeight(0) == 1.0, decaying toward
// 0 as heightKm grows), scaled by AtmosphereParametersGpu::
// rayleighDensityExpScale (the reciprocal of the reference's own 8km scale
// height). Transcribed from _reference/pl-sky/shaders/sky.inc's
// `pl_height_factor_rayleigh()`.
float RayleighDensityAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept;

// The Mie aerosol-scattering density profile - the same exponential-falloff
// shape as RayleighDensityAtHeight() above, but with a much shorter 1.2km
// scale height (AtmosphereParametersGpu::mieDensityExpScale) since aerosols
// are concentrated much closer to the ground than air molecules.
// Transcribed from sky.inc's `pl_height_factor_mie()`.
float MieDensityAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept;

// The ozone absorption density profile - a symmetric "tent" function
// (linearly rising from 0, peaking at 1.0 at AtmosphereParametersGpu::
// ozoneTentCenterKm, then linearly falling back to 0 by
// ozoneTentCenterKm +/- ozoneTentHalfWidthKm), modeling the real ozone
// layer's concentration around ~25km altitude. Transcribed from sky.inc's
// `pl_height_factor_ozone()`. A non-positive ozoneTentHalfWidthKm degrades
// to 0.0 everywhere (never divides by zero).
float OzoneDensityAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept;

// Combines all three density profiles above into the total EXTINCTION
// coefficient (scattering + absorption, per channel) at a given altitude -
// the quantity Beer's law attenuates light by. Rayleigh contributes its
// scattering coefficient directly (Rayleigh's own absorption is ~zero - see
// AtmosphereTypes.h); Mie contributes scattering PLUS absorption; ozone
// contributes absorption only (it scatters essentially nothing). Transcribed
// from sky.inc's `pl_calculate_coefficients()` (this function returns just
// its `.tExtinction` member - the scattering-only half is not needed by any
// Phase 1 function, and will get its own accessor once a later phase
// actually needs it, e.g. Phase 3's real Transmittance LUT GLSL/CPU parity
// check may want it directly rather than re-deriving it).
Vec3 ComputeExtinctionCoefficientAtHeight(const AtmosphereParametersGpu& params, float heightKm) noexcept;

// Numerically integrates the extinction coefficient along a ray from
// positionKm (planet-centered, see this file's own coordinate convention
// above) in the given direction, out to the atmosphere's own outer boundary
// - i.e. the OPTICAL DEPTH from positionKm to the top of the atmosphere.
// `direction` need not be pre-normalized (this function normalizes it
// internally). Uses the SAME numerical method as the reference
// implementation's own Transmittance LUT compute shader (a fixed sampleCount
// of forward ray-march steps, sampling the density profile mid-step after
// each advance - see sky_transmission_lut.comp), so Phase 9's GPU-vs-CPU
// parity check compares like for like. Returns Vec3::Zero() for a
// non-positive sampleCount or a degenerate (non-positive-length) path.
//
// Deliberately does NOT test for/zero out a ray that hits the ground first -
// unlike the name might suggest at a glance, this always integrates all the
// way to the outer atmosphere boundary along the given direction, even for a
// direction that would geometrically pass through the planet first. Ground
// occlusion (the reference's own `tIntersection.bHitEarth ? vec3(0) : ...`
// branch) is a LUT-shape-specific concern belonging to whichever later phase
// builds the real Transmittance LUT (Phase 3), not to this general-purpose
// integral.
Vec3 ComputeOpticalDepthToTopOfAtmosphere(
    const AtmosphereParametersGpu& params, Vec3 positionKm, Vec3 direction, int sampleCount) noexcept;

// exp(-opticalDepth), per channel - the fraction of light that survives the
// trip from positionKm to the top of the atmosphere along `direction`. This
// is the exact value Phase 3's Transmittance LUT stores per texel, and is
// what Phase 9's validation tool numerically compares the real compute
// shader's output texture against.
Vec3 ComputeTransmittanceToTopOfAtmosphere(
    const AtmosphereParametersGpu& params, Vec3 positionKm, Vec3 direction, int sampleCount) noexcept;

// The Rayleigh angular scattering phase function, normalized so its integral
// over the full sphere (4*PI steradians) is 1.0. `cosTheta` is the cosine of
// the angle between the incoming and outgoing scattering directions.
// Transcribed from sky.inc's `pl_phase_rayleigh()`.
float RayleighPhaseFunction(float cosTheta) noexcept;

// The Cornette-Shanks approximation to the Mie angular scattering phase
// function (also normalized over the full sphere), parameterized by the
// asymmetry factor `g` (AtmosphereParametersGpu::miePhaseG) - `g` close to 1
// means strongly forward-scattering, matching real aerosol behavior more
// closely than the simpler Henyey-Greenstein phase function it refines.
// Transcribed from sky.inc's `pl_phase_cornette_shanks()`.
float CornetteShanksMiePhaseFunction(float g, float cosTheta) noexcept;

} // namespace gte
