// Shared atmosphere math. Positions are in km from the planet center: length(positionKm) = planet radius + height.
// The CPU functions in AtmosphereMath.h use the same names and steps and are the reference for these.

#ifndef ATMOSPHERE_COMMON_GLSL
#define ATMOSPHERE_COMMON_GLSL

// GPU layout of AtmosphereParametersGpu (vec3 + float pairs fill one 16-byte group in std140 and std430).
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
    float multiScatteringStrength;
    float _pad1;
};

const float kAtmospherePi = 3.14159265358979323846;

// Density profiles and extinction at a height above the ground (km).
float RayleighDensityAtHeight(AtmosphereParametersGpu params, float heightKm)
{
    return exp(-heightKm * params.rayleighDensityExpScale);
}

float MieDensityAtHeight(AtmosphereParametersGpu params, float heightKm)
{
    return exp(-heightKm * params.mieDensityExpScale);
}

// Ozone density is a symmetric tent: 1 at the center height, 0 outside the half width.
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

// Radius of the atmosphere's outer boundary from the planet center.
float AtmosphereRadiusKm(AtmosphereParametersGpu params)
{
    return params.planetRadiusKm + params.atmosphereThicknessKm;
}

// Distance along a unit `direction` to leave a sphere the origin is inside of (the far root); negative on a miss.
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

// Nearest non-negative hit distance of a unit `direction` against a sphere; -1.0 on a miss or a hit behind the origin.
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

// Optical depth to the top of the atmosphere in `sampleCount` fixed steps. Does not test for ground hits.
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
        // Advance, then sample: the CPU reference steps in the same order.
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

// Rayleigh and Cornette-Shanks Mie phase functions; cosTheta is the angle between light and view directions.
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

// (height, zenith cosine) -> transmittance LUT grid uv (x = height, y = zenith, both linear); inverse of the decode below.
// The grid uv is texel / (size - 1), not a texel-center uv.
vec2 HeightZenithToTransmittanceLutUv(AtmosphereParametersGpu params, float heightKm, float upDot)
{
    float x = clamp(heightKm / max(params.atmosphereThicknessKm, 1e-6), 0.0, 1.0);
    float y = clamp(upDot * 0.5 + 0.5, 0.0, 1.0);
    return vec2(x, y);
}

// Transmittance LUT grid uv -> (height, zenith cosine); upDot stays above -1 to avoid a degenerate straight-down ray.
void TransmittanceLutUvToHeightZenith(AtmosphereParametersGpu params, vec2 lutGridUv, out float heightKm, out float upDot)
{
    heightKm = mix(0.0, params.atmosphereThicknessKm, clamp(lutGridUv.x, 0.0, 1.0));
    upDot = max(lutGridUv.y * 2.0 - 1.0, -0.999);
}

// Rayleigh + Mie scattering only (no absorption).
vec3 ComputeScatteringCoefficientAtHeight(AtmosphereParametersGpu params, float heightKm)
{
    float rayleighDensity = RayleighDensityAtHeight(params, heightKm);
    float mieDensity = MieDensityAtHeight(params, heightKm);
    return params.rayleighScattering * rayleighDensity + params.mieScattering * mieDensity;
}

// Closed-form inscattering over one step of constant scattering and extinction (avoids a nested integration).
vec3 IntegrateInscattering(vec3 scatteringCoefficient, vec3 extinctionCoefficient, float stepLengthKm)
{
    return (scatteringCoefficient - scatteringCoefficient * exp(-extinctionCoefficient * stepLengthKm))
        / max(extinctionCoefficient, vec3(1e-5));
}

// Direction on a fixed sampleCountSqrt x sampleCountSqrt grid over the full sphere; sinTheta is the sample's weight.
// Y uses sin(theta) (not cos): the sample pattern is fixed, only the weights depend on it.
vec3 MultiScatteringSampleDirection(int sampleIndexTheta, int sampleIndexPhi, int sampleCountSqrt, out float sinTheta)
{
    float sampleCountSqrtRcp = 1.0 / float(sampleCountSqrt);
    float theta = kAtmospherePi * float(sampleIndexTheta) * sampleCountSqrtRcp;
    float phi = 2.0 * kAtmospherePi * (float(sampleIndexPhi) + 0.5) * sampleCountSqrtRcp;

    sinTheta = sin(theta);
    return vec3(sinTheta * cos(phi), sinTheta, sinTheta * sin(phi));
}

// Multi-scattering LUT grid uv -> (height, zenith cosine). The caller builds the grid uv as texel / size.
void MultiScatteringLutUvToHeightZenith(AtmosphereParametersGpu params, vec2 lutGridUv, out float heightKm, out float upDot)
{
    heightKm = mix(0.0, params.atmosphereThicknessKm, clamp(lutGridUv.x, 0.0, 1.0));
    upDot = max(lutGridUv.y * 2.0 - 1.0, -0.999);
}

// Per-view, per-frame values; the layout matches AtmosphereFrameUniforms in AtmosphereTypes.h.
struct AtmosphereFrameUniforms {
    vec3 cameraPositionAtmosphere;
    float _pad0;

    vec3 sunDirection;
    float _pad1;

    vec3 sunIlluminance;
    float _pad2;

    // Inverse view-projection, column-major like Mat4::Data().
    mat4 invViewProjection;

    // Aerial perspective ray-march tunables.
    float aerialPerspectiveMaxDistanceKm;
    float aerialPerspectiveDepthExponent;
    float aerialPerspectiveSamplesPerSliceAsFloat;
    float aerialPerspectiveScatteringExaggeration;
};

// 1.0 when the sun is visible from the point, 0.0 when the planet blocks it (biased off the surface).
float PlanetVisibility(vec3 positionKm, vec3 directionToSun, float planetRadiusKm)
{
    float radialLengthKm = length(positionKm);
    vec3 upAtPosition = positionKm / max(radialLengthKm, 1e-20);

    float surfaceEpsilonKm = max(planetRadiusKm * 1e-7, 1e-6);
    vec3 biasedPositionKm = positionKm + upAtPosition * surfaceEpsilonKm;

    float groundHitDistanceKm = RayIntersectsSphereNearest(biasedPositionKm, directionToSun, planetRadiusKm);
    return (groundHitDistanceKm >= 0.0) ? 0.0 : 1.0;
}

// Keeps a bilinear sample inside the LUT's texel centers so it never blends with the far edge.
vec2 ClampLutUvHalfTexelInset(vec2 uv, ivec2 lutSize)
{
    vec2 halfTexel = 0.5 / vec2(lutSize);
    return clamp(uv, halfTexel, vec2(1.0) - halfTexel);
}

// Sky-view LUT: up is +Y. Elevation is remapped non-linearly (more texels near the horizon), azimuth linearly.

// Texel-center uv -> unit uv where the first and last texel centers are 0 and 1.
float SkyViewLutSubUvToUnit(float subUv, float resolution)
{
    if (resolution <= 1.0) {
        return 0.0;
    }
    return clamp((subUv * resolution - 0.5) / (resolution - 1.0), 0.0, 1.0);
}

// Inverse of SkyViewLutSubUvToUnit().
float SkyViewLutUnitToSubUv(float unitUv, float resolution)
{
    if (resolution <= 1.0) {
        return 0.5;
    }
    return (clamp(unitUv, 0.0, 1.0) * (resolution - 1.0) + 0.5) / resolution;
}

// Texel-center uv -> view direction; viewHeightKm is the eye's distance from the planet center.
vec3 SkyViewLutUvToViewDirection(vec2 subUv, ivec2 lutSize, float viewHeightKm, float planetRadiusKm)
{
    vec2 uv = vec2(SkyViewLutSubUvToUnit(subUv.x, float(lutSize.x)), SkyViewLutSubUvToUnit(subUv.y, float(lutSize.y)));

    viewHeightKm = max(viewHeightKm, planetRadiusKm);

    float horizonDistanceKm = sqrt(max(viewHeightKm * viewHeightKm - planetRadiusKm * planetRadiusKm, 0.0));
    float cosBeta = clamp(horizonDistanceKm / max(viewHeightKm, 1e-6), 0.0, 1.0);
    float beta = acos(cosBeta);

    // Angle from local up to the geometric horizon: PI/2 on the ground, larger above it.
    float zenithToHorizonAngle = kAtmospherePi - beta;

    float viewZenithAngle;
    if (uv.y < 0.5) {
        // Upper half: zenith (0) to horizon (0.5), quadratic.
        float coord = uv.y * 2.0;
        coord = 1.0 - coord;
        coord *= coord;
        coord = 1.0 - coord;
        viewZenithAngle = zenithToHorizonAngle * coord;
    } else {
        // Lower half: horizon (0.5) to nadir (1.0).
        float coord = uv.y * 2.0 - 1.0;
        coord *= coord;
        viewZenithAngle = zenithToHorizonAngle + beta * coord;
    }

    float phi = (0.5 - uv.x) * 2.0 * kAtmospherePi;

    float sinZenith = sin(viewZenithAngle);
    float cosZenith = cos(viewZenithAngle);

    return vec3(sinZenith * cos(phi), cosZenith, sinZenith * sin(phi));
}

// View direction -> texel-center uv; the inverse of SkyViewLutUvToViewDirection().
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

// Z-slice <-> view-depth mapping of the aerial perspective volume.
#include "VolumetricFroxelMath.glsl"

// World-space view ray of a froxel column: unprojects the near and far points at the column center.
// Works for any invertible view-projection matrix. froxelXY is a 0-based texel index.
vec3 FroxelColumnToViewRayDirection(ivec2 froxelXY, ivec2 volumeXYSize, mat4 invViewProjection)
{
    vec2 uv = (vec2(froxelXY) + vec2(0.5)) / vec2(volumeXYSize);
    vec2 ndc = uv * 2.0 - 1.0;

    // Vulkan clip depth is zero-to-one: near plane z = 0, far plane z = 1.
    vec4 nearClip = vec4(ndc, 0.0, 1.0);
    vec4 farClip = vec4(ndc, 1.0, 1.0);

    vec4 nearWorld4 = invViewProjection * nearClip;
    vec4 farWorld4 = invViewProjection * farClip;

    vec3 nearWorld = nearWorld4.xyz / nearWorld4.w;
    vec3 farWorld = farWorld4.xyz / farWorld4.w;

    // Only the direction is used; world space and atmosphere space share their orientation.
    return normalize(farWorld - nearWorld);
}

#endif // ATMOSPHERE_COMMON_GLSL
