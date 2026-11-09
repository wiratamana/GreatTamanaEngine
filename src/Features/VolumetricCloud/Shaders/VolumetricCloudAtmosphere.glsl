// Sky light for the clouds, read from the atmosphere's look-up tables. Included by VolumetricCloud.comp after
// the params block and the LUT bindings. Positions are in km from the planet centre (atmosphere space).

const int kAirSamples = 8;
const float kAmbientZenithWeight = 0.4;
const float kAmbientRingWeight = 0.15;
const float kAmbientRingElevation = 0.6109; // 35 degrees.

// Sky light that reaches the cloud, once per marched pixel. Set by main() before the march.
vec3 gSkyAmbient = vec3(0.0);

bool AtmosphereOn()
{
    return params.atmosphereMode.x > 0.5;
}

// Atmosphere space position of the point `offsetMetres` from the camera.
vec3 AtmospherePosition(vec3 offsetMetres)
{
    return params.atmosphereCamera.xyz + offsetMetres * 0.001;
}

// Share of the sun's light that is left at a position: the LUT transmittance times the planet's shadow.
vec3 SunTransmittanceAt(vec3 positionKm, vec3 sunDirection)
{
    AtmosphereParametersGpu air = atmosphere.values;
    float radiusKm = max(length(positionKm), 1e-3);
    float heightKm = clamp(radiusKm - air.planetRadiusKm, 0.0, air.atmosphereThicknessKm);
    vec2 uv = HeightZenithToTransmittanceLutUv(air, heightKm, dot(positionKm, sunDirection) / radiusKm);
    vec3 transmittance = textureLod(transmittanceLut, ClampLutUvHalfTexelInset(uv, textureSize(transmittanceLut, 0)), 0.0).rgb;
    return transmittance * PlanetVisibility(positionKm, sunDirection, air.planetRadiusKm);
}

// Light of the whole sky above the camera: the zenith and a ring at 35 degrees, from the sky-view LUT.
vec3 SkyAmbientRadiance()
{
    float planetRadiusKm = atmosphere.values.planetRadiusKm;
    float viewHeightKm = max(params.atmosphereCamera.y, planetRadiusKm);
    ivec2 size = textureSize(skyViewLut, 0);
    vec3 sum = kAmbientZenithWeight
        * textureLod(skyViewLut, ViewDirectionToSkyViewLutUv(vec3(0.0, 1.0, 0.0), size, viewHeightKm, planetRadiusKm), 0.0).rgb;
    float ringCos = cos(kAmbientRingElevation);
    float ringSin = sin(kAmbientRingElevation);
    for (int i = 0; i < 4; ++i) {
        float phi = (float(i) + 0.5) * 0.5 * kAtmospherePi;
        vec3 direction = vec3(ringCos * cos(phi), ringSin, ringCos * sin(phi));
        sum += kAmbientRingWeight
            * textureLod(skyViewLut, ViewDirectionToSkyViewLutUv(direction, size, viewHeightKm, planetRadiusKm), 0.0).rgb;
    }
    return sum * params.atmosphereMode.y * params.atmosphereMode.w;
}

// Light the air scatters toward the eye along the first `distanceKm` of a ray, in the units of the sky LUT
// (the caller applies the sky intensity). `transmittance` is what the air leaves of light behind that distance.
vec3 AirInscatter(vec3 rayDirection, float distanceKm, vec3 sunDirection, vec3 sunIlluminance, out vec3 transmittance)
{
    AtmosphereParametersGpu air = atmosphere.values;
    float scale = params.atmosphereMode.z;
    float cosTheta = dot(rayDirection, sunDirection);
    float rayleighPhase = RayleighPhaseFunction(cosTheta);
    float miePhase = CornetteShanksMiePhaseFunction(air.miePhaseG, cosTheta);
    ivec2 transmittanceSize = textureSize(transmittanceLut, 0);
    ivec2 multiScatteringSize = textureSize(multiScatteringLut, 0);

    vec3 luminance = vec3(0.0);
    vec3 remaining = vec3(1.0);
    float segmentKm = distanceKm / float(kAirSamples);
    for (int i = 0; i < kAirSamples; ++i) {
        float sampleKm = (float(i) + 0.5) * segmentKm;
        vec3 positionKm = ClampSampleToSurfaceKm(params.atmosphereCamera.xyz + rayDirection * sampleKm, air.planetRadiusKm);
        float radiusKm = max(length(positionKm), 1e-3);
        float heightKm = clamp(radiusKm - air.planetRadiusKm, 0.0, air.atmosphereThicknessKm);
        vec2 uv = HeightZenithToTransmittanceLutUv(air, heightKm, dot(positionKm, sunDirection) / radiusKm);
        vec3 sunTransmittance = textureLod(transmittanceLut, ClampLutUvHalfTexelInset(uv, transmittanceSize), 0.0).rgb;
        vec3 multiScattered = textureLod(multiScatteringLut, ClampLutUvHalfTexelInset(uv, multiScatteringSize), 0.0).rgb;

        vec3 scatterRayleigh = air.rayleighScattering * RayleighDensityAtHeight(air, heightKm) * scale;
        vec3 scatterMie = air.mieScattering * MieDensityAtHeight(air, heightKm) * scale;
        vec3 extinction = ComputeExtinctionCoefficientAtHeight(air, heightKm) * scale;
        float visibility = PlanetVisibility(positionKm, sunDirection, air.planetRadiusKm);

        vec3 direct = visibility * sunTransmittance * (scatterRayleigh * rayleighPhase + scatterMie * miePhase);
        vec3 source = sunIlluminance * (direct + multiScattered * (scatterRayleigh + scatterMie));
        luminance += remaining * IntegrateInscattering(source, extinction, segmentKm);
        remaining *= exp(-extinction * segmentKm);
    }
    transmittance = remaining;
    return luminance;
}
