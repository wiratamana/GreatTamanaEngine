// AtmosphereCommon.glsl
//
// Shared GLSL math for the Atmosphere Scattering + Aerial Perspective
// campaign (see task_manager/atmosphere-scattering-1/
// ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md). Populated starting Phase 3
// (ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md) - see
// src/Renderer/Atmosphere/AtmosphereMath.h's own file comment for why the
// CPU side is written FIRST and treated as the permanent ground truth
// ("oracle") every GLSL function here faithfully reproduces (SAME function
// names, SAME per-step numerical method), never the other way around. If a
// future GPU kernel and that CPU oracle ever disagree, the CPU oracle is
// right by definition and the GLSL here is what needs fixing.
//
// Every .comp/.frag file that #includes this one must also list it in its
// own gte_add_shader(... EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)
// call in the root CMakeLists.txt, so an edit here correctly triggers glslc
// to recompile every shader that depends on it (see
// cmake/CompileShaders.cmake's own EXTRA_DEPENDS doc comment).
//
// COORDINATE CONVENTION - identical to AtmosphereMath.h's own: the planet's
// CENTER is the origin of the local coordinate frame every function below
// operates in (Length(positionKm) == planetRadiusKm + heightAboveGroundKm).
// This is deliberately NOT the engine's world-space/per-camera atmosphere
// frame (that composition is a later phase's own concern, see
// AtmosphereFrameUniforms in AtmosphereTypes.h).

#ifndef ATMOSPHERE_COMMON_GLSL
#define ATMOSPHERE_COMMON_GLSL

// Mirrors src/Renderer/Atmosphere/AtmosphereTypes.h's AtmosphereParametersGpu
// EXACTLY - same field names, same order, same std140/std430-compatible
// 16-byte-group layout (a vec3 immediately followed by a float packs into
// one 16-byte group with zero gap in both GLSL layouts, so this struct is
// binary-identical whether the buffer block wrapping it below is declared
// std140 or std430). Any change to the C++ struct MUST be mirrored here by
// hand - this campaign deliberately has no shader reflection (see
// ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md's own "Cross-Cutting Engineering
// Rules").
struct AtmosphereParametersGpu {
    vec3 rayleighScattering;
    float rayleighDensityExpScale;

    vec3 mieScattering;
    float miePhaseG;

    vec3 mieAbsorption;
    float mieDensityExpScale;

    vec3 ozoneAbsorption;
    float ozoneTentCenterKm;

    vec3 groundAlbedo;
    float ozoneTentHalfWidthKm;

    float planetRadiusKm;
    float atmosphereThicknessKm;
    float _pad0;
    float _pad1;
};

const float kAtmospherePi = 3.14159265358979323846;

// ----------------------------------------------------------------------
// Density profiles / extinction - transcribed from
// src/Renderer/Atmosphere/AtmosphereMath.cpp's RayleighDensityAtHeight()/
// MieDensityAtHeight()/OzoneDensityAtHeight()/
// ComputeExtinctionCoefficientAtHeight(), which themselves transcribe
// _reference/pl-sky/shaders/sky.inc's pl_height_factor_rayleigh()/
// pl_height_factor_mie()/pl_height_factor_ozone()/pl_calculate_coefficients().
// ----------------------------------------------------------------------

float RayleighDensityAtHeight(AtmosphereParametersGpu params, float heightKm)
{
    return exp(-heightKm * params.rayleighDensityExpScale);
}

float MieDensityAtHeight(AtmosphereParametersGpu params, float heightKm)
{
    return exp(-heightKm * params.mieDensityExpScale);
}

float OzoneDensityAtHeight(AtmosphereParametersGpu params, float heightKm)
{
    if (params.ozoneTentHalfWidthKm <= 0.0) {
        return 0.0;
    }
    float distanceFromCenter = abs(heightKm - params.ozoneTentCenterKm);
    return max(0.0, 1.0 - distanceFromCenter / params.ozoneTentHalfWidthKm);
}

vec3 ComputeExtinctionCoefficientAtHeight(AtmosphereParametersGpu params, float heightKm)
{
    float rayleighDensity = RayleighDensityAtHeight(params, heightKm);
    float mieDensity = MieDensityAtHeight(params, heightKm);
    float ozoneDensity = OzoneDensityAtHeight(params, heightKm);

    return params.rayleighScattering * rayleighDensity
        + (params.mieScattering + params.mieAbsorption) * mieDensity
        + params.ozoneAbsorption * ozoneDensity;
}

// The atmosphere's outer boundary radius from the planet's center -
// mirrors AtmosphereParameters.h's AtmosphereRadiusKm() exactly.
float AtmosphereRadiusKm(AtmosphereParametersGpu params)
{
    return params.planetRadiusKm + params.atmosphereThicknessKm;
}

// ----------------------------------------------------------------------
// Sphere-intersection helpers. Two DISTINCT helpers exist on purpose - see
// each one's own comment - mirroring the reference implementation's own
// split between "distance to the outer atmosphere boundary" (used inside
// the optical-depth integral below) and "does this ray hit the ground
// first" (a LUT-shape-specific ground-occlusion test, never needed by the
// general-purpose optical-depth integral itself - see AtmosphereMath.h's
// own doc comment on ComputeOpticalDepthToTopOfAtmosphere() for why that
// function deliberately does NOT do this check).
// ----------------------------------------------------------------------

// Mirrors AtmosphereMath.cpp's anonymous-namespace DistanceToSphereBoundary()
// exactly: solves for the LARGER (far) root - the correct "distance to exit
// the atmosphere" answer for an origin already inside (or on) the sphere.
// `direction` must already be normalized (the quadratic's own `a`
// coefficient is assumed exactly 1.0). Returns a negative value if the ray
// never reaches the sphere at all.
float DistanceToAtmosphereOuterBoundary(vec3 originKm, vec3 direction, float radiusKm)
{
    float b = 2.0 * dot(originKm, direction);
    float c = dot(originKm, originKm) - radiusKm * radiusKm;
    float discriminant = b * b - 4.0 * c;
    if (discriminant < 0.0) {
        return -1.0;
    }
    float sqrtDiscriminant = sqrt(discriminant);
    return (-b + sqrtDiscriminant) * 0.5;
}

// A NEW helper (no CPU-oracle counterpart - ground occlusion is explicitly
// out of AtmosphereMath.h's own scope, see its doc comment) - returns the
// NEAREST non-negative intersection distance against a sphere of radius
// `radiusKm`, or -1.0 if the ray misses it (or only intersects behind the
// origin). Mirrors _reference/pl-sky/shaders/sky.inc's
// pl_ray_intersect_sphere_nearest() (with `a` assumed exactly 1.0, since
// `direction` is unit-length here too).
float RayIntersectsSphereNearest(vec3 originKm, vec3 direction, float radiusKm)
{
    float b = 2.0 * dot(originKm, direction);
    float c = dot(originKm, originKm) - radiusKm * radiusKm;
    float discriminant = b * b - 4.0 * c;
    if (discriminant < 0.0) {
        return -1.0;
    }
    float sqrtDiscriminant = sqrt(discriminant);
    float t0 = (-b - sqrtDiscriminant) * 0.5;
    float t1 = (-b + sqrtDiscriminant) * 0.5;
    if (t0 < 0.0 && t1 < 0.0) {
        return -1.0;
    }
    if (t0 < 0.0) {
        return max(t1, 0.0);
    }
    if (t1 < 0.0) {
        return max(t0, 0.0);
    }
    return max(min(t0, t1), 0.0);
}

// ----------------------------------------------------------------------
// Optical depth / transmittance - transcribed from AtmosphereMath.cpp's
// ComputeOpticalDepthToTopOfAtmosphere()/ComputeTransmittanceToTopOfAtmosphere(),
// SAME numerical method (advance-then-sample, `sampleCount` fixed-length
// forward steps) as _reference/pl-sky/shaders/sky_transmission_lut.comp, so
// this is a like-for-like parity target for Phase 9's own GPU-vs-CPU
// validation. Deliberately does NOT test for/zero out a ray that hits the
// ground first - exactly like the CPU oracle, ground occlusion is a
// LUT-shape-specific concern for whichever .comp file calls this (see
// RayIntersectsSphereNearest() above).
// ----------------------------------------------------------------------

vec3 ComputeOpticalDepthToTopOfAtmosphere(AtmosphereParametersGpu params, vec3 positionKm, vec3 direction, int sampleCount)
{
    if (sampleCount <= 0) {
        return vec3(0.0);
    }

    vec3 normalizedDirection = normalize(direction);

    float atmosphereRadiusKm = AtmosphereRadiusKm(params);
    float pathLengthKm = DistanceToAtmosphereOuterBoundary(positionKm, normalizedDirection, atmosphereRadiusKm);
    if (pathLengthKm <= 0.0) {
        return vec3(0.0);
    }

    float stepLengthKm = pathLengthKm / float(sampleCount);
    vec3 step = normalizedDirection * stepLengthKm;

    vec3 opticalDepth = vec3(0.0);
    vec3 currentPositionKm = positionKm;

    for (int i = 0; i < sampleCount; ++i) {
        // Advance THEN sample - matches AtmosphereMath.cpp's own per-step
        // order exactly (and, through it, the cloned reference's own
        // `tCurrentPos += tStep;` happening before that step's density
        // read).
        currentPositionKm += step;

        float heightKm = max(length(currentPositionKm) - params.planetRadiusKm, 0.0);
        opticalDepth += ComputeExtinctionCoefficientAtHeight(params, heightKm) * stepLengthKm;
    }

    return opticalDepth;
}

vec3 ComputeTransmittanceToTopOfAtmosphere(AtmosphereParametersGpu params, vec3 positionKm, vec3 direction, int sampleCount)
{
    vec3 opticalDepth = ComputeOpticalDepthToTopOfAtmosphere(params, positionKm, direction, sampleCount);
    return exp(-opticalDepth);
}

// ----------------------------------------------------------------------
// Phase functions - transcribed from AtmosphereMath.cpp's
// RayleighPhaseFunction()/CornetteShanksMiePhaseFunction() (themselves
// transcribed from sky.inc's pl_phase_rayleigh()/pl_phase_cornette_shanks()).
// Not needed by the Transmittance LUT itself, but declared here (rather
// than deferred to Phase 4) since they are genuinely shared, unconditional
// math with no LUT-specific shape of their own.
// ----------------------------------------------------------------------

float RayleighPhaseFunction(float cosTheta)
{
    return 3.0 / (16.0 * kAtmospherePi) * (1.0 + cosTheta * cosTheta);
}

float CornetteShanksMiePhaseFunction(float g, float cosTheta)
{
    float numerator = 3.0 / (8.0 * kAtmospherePi) * (1.0 - g * g) * (1.0 + cosTheta * cosTheta);
    float denominator = (2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * cosTheta, 1.5);
    return numerator / denominator;
}

// ----------------------------------------------------------------------
// Transmittance LUT UV <-> (height, zenith) parameterization - transcribed
// from _reference/pl-sky/shaders/sky_transmission_lut.comp's own texel
// decode (see task_manager/atmosphere-scattering-1/ATMOSPHERE_REFERENCE_NOTES.md,
// Section 3): a plain LINEAR height/angle grid (NOT the Bruneton
// asin-based remap) - `lutGridUv` is `texel / (resolution - 1)` per axis,
// exactly as AtmosphereTransmittanceLut.comp computes it, NOT a
// texel-CENTER UV.
// ----------------------------------------------------------------------

// height, zenith -> LUT grid uv (the inverse of TransmittanceLutUvToHeightZenith()
// below) - needed by any LATER phase (4 onward) that SAMPLES this LUT given
// a real (heightKm, upDot) pair, not by the Transmittance LUT's own
// generation pass itself.
vec2 HeightZenithToTransmittanceLutUv(AtmosphereParametersGpu params, float heightKm, float upDot)
{
    float x = clamp(heightKm / max(params.atmosphereThicknessKm, 1e-6), 0.0, 1.0);
    float y = clamp(upDot * 0.5 + 0.5, 0.0, 1.0);
    return vec2(x, y);
}

// LUT grid uv -> (heightKm, upDot) - what AtmosphereTransmittanceLut.comp
// itself calls per-texel to decode which (height, zenith angle) sample it
// is generating. `upDot` is clamped away from exactly -1.0 (mirrors the
// reference's own `fUpDot = max(fUpDot, -0.999);` - avoids a degenerate
// straight-down view direction).
void TransmittanceLutUvToHeightZenith(AtmosphereParametersGpu params, vec2 lutGridUv, out float heightKm, out float upDot)
{
    heightKm = mix(0.0, params.atmosphereThicknessKm, clamp(lutGridUv.x, 0.0, 1.0));
    upDot = max(lutGridUv.y * 2.0 - 1.0, -0.999);
}
// ----------------------------------------------------------------------
// Scattering-only coefficient (Rayleigh scattering + Mie scattering, no
// absorption/ozone) - the companion split of
// ComputeExtinctionCoefficientAtHeight() above that AtmosphereMath.h's own
// doc comment predicted a later phase would need "once a later phase
// actually needs it" - Phase 4's Multi-Scattering LUT is that later phase.
// Transcribed from _reference/pl-sky/shaders/sky.inc's
// pl_calculate_coefficients(), its .tScatterRayleigh + .tScatterMie sum.
// ----------------------------------------------------------------------

vec3 ComputeScatteringCoefficientAtHeight(AtmosphereParametersGpu params, float heightKm)
{
    float rayleighDensity = RayleighDensityAtHeight(params, heightKm);
    float mieDensity = MieDensityAtHeight(params, heightKm);
    return params.rayleighScattering * rayleighDensity + params.mieScattering * mieDensity;
}

// ----------------------------------------------------------------------
// Analytic single-segment inscattering integral - the Multi-Scattering
// LUT's own per-ray-march-step accumulation formula, transcribed from
// _reference/pl-sky/shaders/sky.inc's pl_integrate_inscattering() (itself
// citing "Physically Based and Unified Volumetric Rendering in
// Frostbite", page 29): the closed-form integral of a constant
// scattering/extinction pair over one step of length `stepLengthKm`,
// avoiding a second, nested numerical sub-integration per step.
// ----------------------------------------------------------------------

vec3 IntegrateInscattering(vec3 scatteringCoefficient, vec3 extinctionCoefficient, float stepLengthKm)
{
    return (scatteringCoefficient - scatteringCoefficient * exp(-extinctionCoefficient * stepLengthKm))
        / max(extinctionCoefficient, vec3(1e-5));
}

// ----------------------------------------------------------------------
// Multi-Scattering LUT (Phase 4) - shared spherical direction-sampling
// pattern, transcribed EXACTLY from
// _reference/pl-sky/shaders/sky_multiscatter_lut.comp's own nested 8x8
// loop (iSampleCountSqrt = 8): a fixed, deterministic grid of directions
// with theta in [0, PI] sweeping the FULL sphere (not just a hemisphere)
// and phi in [0, 2*PI]. Kept EXACTLY as the reference's own (non-standard)
// axis assignment - the Y component uses sin(theta), not cos(theta) - per
// this campaign's own "transcribe the exact sample count and pattern
// pl-sky uses, per the reference notes, rather than inventing a different
// one" rule (see this phase's own strategy document, Step 3); this LUT has
// no CPU oracle to validate a "corrected" version against, so faithful
// transcription is the safer choice. `sinTheta` is handed back to the
// caller too - the same per-sample solid-angle weight the reference's own
// accumulation multiplies every contribution by, before dividing the
// running total by sampleCountSqrt^2 once at the end.
// ----------------------------------------------------------------------

vec3 MultiScatteringSampleDirection(int sampleIndexTheta, int sampleIndexPhi, int sampleCountSqrt, out float sinTheta)
{
    float sampleCountSqrtRcp = 1.0 / float(sampleCountSqrt);
    float theta = kAtmospherePi * float(sampleIndexTheta) * sampleCountSqrtRcp;
    float phi = 2.0 * kAtmospherePi * (float(sampleIndexPhi) + 0.5) * sampleCountSqrtRcp;

    sinTheta = sin(theta);
    return vec3(sinTheta * cos(phi), sinTheta, sinTheta * sin(phi));
}

// Multi-Scattering LUT UV <-> (height, zenith) parameterization -
// transcribed from _reference/pl-sky/shaders/sky_multiscatter_lut.comp's
// own texel decode (see ATMOSPHERE_REFERENCE_NOTES.md, Section 3): the
// SAME linear height/upDot formula as TransmittanceLutUvToHeightZenith()
// above, but note the deliberate OFF-BY-ONE-TEXEL difference in how the
// CALLER (AtmosphereMultiScatteringLut.comp) computes `lutGridUv` itself -
// `texel / resolution` here, NOT `texel / (resolution - 1)` like the
// Transmittance LUT - confirmed present in the real cloned source, not a
// transcription typo.
void MultiScatteringLutUvToHeightZenith(AtmosphereParametersGpu params, vec2 lutGridUv, out float heightKm, out float upDot)
{
    heightKm = mix(0.0, params.atmosphereThicknessKm, clamp(lutGridUv.x, 0.0, 1.0));
    upDot = max(lutGridUv.y * 2.0 - 1.0, -0.999);
}

// ----------------------------------------------------------------------
// AtmosphereFrameUniforms (Phase 5, ATMOSPHERE_PHASE5_SKYVIEW_LUT_v1.md) -
// mirrors src/Renderer/Atmosphere/AtmosphereTypes.h's AtmosphereFrameUniforms
// EXACTLY (same field names/order/16-byte-group layout convention as
// AtmosphereParametersGpu above) - the campaign's first genuinely PER-FRAME
// GPU buffer (camera height + sun direction change every frame, unlike the
// session-stable AtmosphereParametersGpu above, which only changes when the
// atmosphere's own physical constants change). Bound as ANOTHER read-only
// STORAGE buffer, never a true uniform block - same "Revision Notes" rule
// as AtmosphereParametersGpu (no VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER support
// exists in the engine today).
// ----------------------------------------------------------------------

struct AtmosphereFrameUniforms {
    vec3 cameraPositionAtmosphere;
    float _pad0;

    vec3 sunDirection;
    float _pad1;

    vec3 sunIlluminance;
    float _pad2;
};

// ----------------------------------------------------------------------
// Planet self-shadow visibility test - whether the sun is visible (1.0) or
// blocked by the planet's own bulk (0.0) from a given point, biased a tiny
// distance off the surface along its own local "up" to avoid a degenerate
// self-intersection exactly at height == 0. Transcribed from
// _reference/pl-sky/shaders/sky.inc's pl_planet_visibility() - shared here
// (rather than inlined once into the Sky-View LUT below) since Phase 6's
// aerial-perspective volume is expected to need the exact same self-shadow
// test along its own ray-march.
// ----------------------------------------------------------------------

float PlanetVisibility(vec3 positionKm, vec3 directionToSun, float planetRadiusKm)
{
    float radialLengthKm = length(positionKm);
    vec3 upAtPosition = positionKm / max(radialLengthKm, 1e-20);

    float surfaceEpsilonKm = max(planetRadiusKm * 1e-7, 1e-6);
    vec3 biasedPositionKm = positionKm + upAtPosition * surfaceEpsilonKm;

    float groundHitDistanceKm = RayIntersectsSphereNearest(biasedPositionKm, directionToSun, planetRadiusKm);
    return (groundHitDistanceKm >= 0.0) ? 0.0 : 1.0;
}

// ----------------------------------------------------------------------
// Half-texel-inset LUT UV clamp - keeps a bilinear sample strictly inside a
// LUT's own valid texel-center range, never letting it filter against (and
// blend in) whatever lies just past the texture's hard edge. Transcribed
// from _reference/pl-sky/shaders/sky.inc's pl_clamp_lut_uv().
// ----------------------------------------------------------------------

vec2 ClampLutUvHalfTexelInset(vec2 uv, ivec2 lutSize)
{
    vec2 halfTexel = 0.5 / vec2(lutSize);
    return clamp(uv, halfTexel, vec2(1.0) - halfTexel);
}

// ----------------------------------------------------------------------
// Sky-View LUT (Phase 5) - UV <-> view-direction parameterization,
// transcribed from _reference/pl-sky/shaders/sky.inc's
// skyLutSubUvToUnit()/skyLutUnitToSubUv()/fromSkyLut()/pl_to_sky_lut() (see
// ATMOSPHERE_REFERENCE_NOTES.md, Section 3): a non-linear, HORIZON-BIASED
// elevation remap (extra texel density is spent near the geometric
// horizon, where the sky's own appearance changes fastest as you look
// toward/past it) plus a plain linear full-360-degree azimuth remap.
// "up" is +Y here, matching this file's own planet-centered positionKm
// convention (Length(positionKm) == planetRadiusKm + heightAboveGroundKm -
// see this file's own top-of-file COORDINATE CONVENTION comment): the
// reference's own prose comments claim the local up is "negative Y", but
// its ACTUAL code (acos(direction.y) treated directly as the zenith angle)
// behaves exactly as if +Y were up - this transcription follows the real
// code, not the comment (Phase 0's own "the real source always wins"
// rule).
// ----------------------------------------------------------------------

// Converts a texel-center UV into a unit UV whose first/last texel centers
// represent exactly 0/1 - mirrors skyLutSubUvToUnit() exactly.
float SkyViewLutSubUvToUnit(float subUv, float resolution)
{
    if (resolution <= 1.0) {
        return 0.0;
    }
    return clamp((subUv * resolution - 0.5) / (resolution - 1.0), 0.0, 1.0);
}

// The inverse of SkyViewLutSubUvToUnit() above - mirrors skyLutUnitToSubUv().
float SkyViewLutUnitToSubUv(float unitUv, float resolution)
{
    if (resolution <= 1.0) {
        return 0.5;
    }
    return (clamp(unitUv, 0.0, 1.0) * (resolution - 1.0) + 0.5) / resolution;
}

// Decodes a Sky-View LUT texel-center UV into a normalized VIEW DIRECTION -
// this LUT's own GENERATION-time parameterization (what
// AtmosphereSkyViewLut.comp itself calls per-texel). Mirrors fromSkyLut()
// exactly: a horizon-biased non-linear elevation remap (quadratic from
// zenith to horizon, sqrt from horizon to nadir) plus a linear 360-degree
// azimuth remap. `viewHeightKm` is the eye's distance from the planet
// CENTER (planetRadiusKm + heightAboveGroundKm, never just the height
// alone).
vec3 SkyViewLutUvToViewDirection(vec2 subUv, ivec2 lutSize, float viewHeightKm, float planetRadiusKm)
{
    vec2 uv = vec2(SkyViewLutSubUvToUnit(subUv.x, float(lutSize.x)), SkyViewLutSubUvToUnit(subUv.y, float(lutSize.y)));

    viewHeightKm = max(viewHeightKm, planetRadiusKm);

    float horizonDistanceKm = sqrt(max(viewHeightKm * viewHeightKm - planetRadiusKm * planetRadiusKm, 0.0));
    float cosBeta = clamp(horizonDistanceKm / max(viewHeightKm, 1e-6), 0.0, 1.0);
    float beta = acos(cosBeta);

    // Angle measured from local up to the geometric horizon - ground
    // level: PI/2; above ground: greater than PI/2.
    float zenithToHorizonAngle = kAtmospherePi - beta;

    float viewZenithAngle;
    if (uv.y < 0.5) {
        // Upper, non-ground-intersecting half: uv.y = 0 -> zenith, uv.y =
        // 0.5 -> geometric horizon.
        float coord = uv.y * 2.0;
        coord = 1.0 - coord;
        coord *= coord;
        coord = 1.0 - coord;
        viewZenithAngle = zenithToHorizonAngle * coord;
    } else {
        // Lower, ground-intersecting half: uv.y = 0.5 -> geometric
        // horizon, uv.y = 1.0 -> nadir.
        float coord = uv.y * 2.0 - 1.0;
        coord *= coord;
        viewZenithAngle = zenithToHorizonAngle + beta * coord;
    }

    // Full 360-degree azimuth mapping.
    float phi = (0.5 - uv.x) * 2.0 * kAtmospherePi;

    float sinZenith = sin(viewZenithAngle);
    float cosZenith = cos(viewZenithAngle);

    return vec3(sinZenith * cos(phi), cosZenith, sinZenith * sin(phi));
}

// The inverse of SkyViewLutUvToViewDirection() above - encodes a normalized
// view DIRECTION into this LUT's own texel-center UV. Not needed by this
// LUT's own generation pass (above), but declared now (mirrors
// HeightZenithToTransmittanceLutUv()'s own "needed by a LATER phase that
// SAMPLES this LUT" precedent) for Phase 7's Sky Background pass, which
// will sample this LUT given a per-pixel camera ray direction. Mirrors
// pl_to_sky_lut() exactly.
vec2 ViewDirectionToSkyViewLutUv(vec3 direction, ivec2 lutSize, float viewHeightKm, float planetRadiusKm)
{
    direction = normalize(direction);
    viewHeightKm = max(viewHeightKm, planetRadiusKm);

    float horizonDistanceKm = sqrt(max(viewHeightKm * viewHeightKm - planetRadiusKm * planetRadiusKm, 0.0));
    float cosBeta = clamp(horizonDistanceKm / max(viewHeightKm, 1e-6), 0.0, 1.0);
    float beta = acos(cosBeta);
    float zenithToHorizonAngle = kAtmospherePi - beta;

    float viewZenithAngle = acos(clamp(direction.y, -1.0, 1.0));

    vec2 uv;
    if (viewZenithAngle <= zenithToHorizonAngle) {
        float coord = viewZenithAngle / max(zenithToHorizonAngle, 1e-6);
        coord = 1.0 - coord;
        coord = sqrt(max(coord, 0.0));
        coord = 1.0 - coord;
        uv.y = coord * 0.5;
    } else {
        float coord = (viewZenithAngle - zenithToHorizonAngle) / max(beta, 1e-6);
        coord = sqrt(clamp(coord, 0.0, 1.0));
        uv.y = 0.5 + coord * 0.5;
    }

    float phi = atan(direction.z, direction.x);
    uv.x = fract(0.5 - phi / (2.0 * kAtmospherePi));

    return vec2(SkyViewLutUnitToSubUv(uv.x, float(lutSize.x)), SkyViewLutUnitToSubUv(uv.y, float(lutSize.y)));
}

#endif // ATMOSPHERE_COMMON_GLSL
