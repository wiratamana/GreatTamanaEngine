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

#endif // ATMOSPHERE_COMMON_GLSL
