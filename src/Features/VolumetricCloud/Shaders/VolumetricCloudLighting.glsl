// Light of one cloud sample. Included by VolumetricCloud.comp after its density helpers: the sun march reads
// the density through CloudLightDensity().

const float kPi = 3.14159265359;
const float kMinPhaseDenominator = 1e-4;
// Per octave colour of the multiple scattering at full blue shift; red and green fall, blue rises.
const vec3 kOctaveTint = vec3(0.9, 0.97, 1.1);
// Sun light at full warmth: the blue falls first, like light that crossed more air.
const vec3 kSunWarmTint = vec3(1.0, 0.88, 0.7);
const float kGlowEccentricity = 0.3;

struct CloudLightInput {
    vec3 position;          // World position of the sample, metres.
    float density;          // 0..1 after the height gradient.
    float heightFraction;   // 0 at the layer base, 1 at the top.
    float extinction;       // 1/m at this sample.
    float stepLength;       // Metres the sample stands for.
    float distanceAlongRay; // Metres from the eye.
    vec3 viewDirection;     // Unit vector from the eye toward the sample.
    vec3 sunDirection;      // Unit vector toward the sun.
    vec3 sunRadiance;       // Sun light, already scaled by the sun intensity setting.
    vec3 ambientRadiance;   // Sky light, already scaled by the ambient setting.
    float sunOpticalDepth;  // From SunOpticalDepth(), possibly reused from a nearby sample.
    float skyVisibility;    // 0..1 share of the sky light not blocked by cloud above, from SkyVisibility().
    vec3 groundRadiance;    // Sunlight bounced up from the ground, zero when the ground gets no sun.
    float lodDensity;       // 0..1 density without the fine erosion (the smooth body), for the in-scatter term.
};

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denominator = max(1.0 + g2 - 2.0 * g * cosTheta, kMinPhaseDenominator);
    return (1.0 - g2) / (4.0 * kPi * denominator * sqrt(denominator));
}

float Draine(float cosTheta, float g, float alpha)
{
    float shape = (1.0 + alpha * cosTheta * cosTheta) / (1.0 + alpha * (1.0 + 2.0 * g * g) / 3.0);
    return HenyeyGreenstein(cosTheta, g) * shape;
}

// Phase of one scattering octave. `eccentricity` scales every g, so later octaves turn more isotropic.
float CloudPhase(float cosTheta, float eccentricity)
{
    if (params.lightMisc.x >= 0.5) {
        float hg = HenyeyGreenstein(cosTheta, params.drainePhase.x * eccentricity);
        float draine = Draine(cosTheta, params.drainePhase.y * eccentricity, params.drainePhase.z);
        return mix(hg, draine, params.drainePhase.w);
    }
    float forward = HenyeyGreenstein(cosTheta, params.lightPhase.x * eccentricity);
    float backward = HenyeyGreenstein(cosTheta, params.lightPhase.y * eccentricity);
    return mix(forward, backward, params.lightPhase.z);
}

// Density for the sun march: the shape and the height gradient, without the fine erosion.
float CloudLightDensity(vec3 position)
{
    vec2 offset = position.xz - params.eyeAndPlanetRadius.xz;
    float altitude = Altitude(dot(offset, offset), position.y);
    float h = Saturate((altitude - params.layer.x) / max(params.layer.y - params.layer.x, kMinWidth));
    float factor;
    float shape = CloudShape(position, h, factor);
    return CloudCurve(shape) * factor;
}

// Optical depth from a sample toward the sun. Steps double in length, so a few samples cover the reach.
float SunOpticalDepth(vec3 position, vec3 sunDirection)
{
    uint steps = uint(params.lightMarch.x);
    float reach = params.lightMarch.y * (params.layer.y - params.layer.x);
    float unit = reach / (exp2(float(steps)) - 1.0);
    float segment = unit;
    float travelled = 0.0;
    float opticalDepth = 0.0;
    for (uint i = 0u; i < steps; ++i) {
        float centre = travelled + 0.5 * segment;
        vec3 samplePosition = position + sunDirection * centre;
        // The nearest sample also carries the fine erosion, which gives the self-shadowing its detail.
        float sampleDensity = (i == 0u && params.lightMisc.w > 0.0)
            ? CloudDensityAt(samplePosition, params.lightMisc.w)
            : CloudLightDensity(samplePosition);
        opticalDepth += sampleDensity * segment;
        travelled += segment;
        segment *= 2.0;
    }
    return opticalDepth * params.density.y;
}

// Share of the sky light that is not blocked by cloud above a sample: two density samples straight up.
// A thick cloud over the sample shades it; an open top sees the whole sky.
float SkyVisibility(vec3 position)
{
    float reach = params.skyShade.y;
    float lowSample = CloudLightDensity(position + vec3(0.0, 0.25 * reach, 0.0));
    float highSample = CloudLightDensity(position + vec3(0.0, 0.75 * reach, 0.0));
    float opticalDepth = (lowSample + highSample) * 0.5 * reach * params.density.y;
    return mix(params.skyShade.z, 1.0, exp(-opticalDepth * params.skyShade.x));
}

// Light reaching a sample from the sun: octaves of b^i * phase_i * exp(-tau * a^i), plus the silver lobe.
// Octave i is tinted by `octaveTint`^i, so the deep multiple scattering leans toward the sky colour.
vec3 SunScatter(float cosTheta, float opticalDepth)
{
    uint octaves = uint(params.lightMarch.z);
    float attenuation = 1.0;
    float contribution = 1.0;
    float eccentricity = 1.0;
    vec3 tint = mix(vec3(1.0), kOctaveTint, params.skyShade2.y);
    vec3 tintPower = vec3(1.0);
    vec3 sum = vec3(0.0);
    for (uint i = 0u; i < octaves; ++i) {
        sum += tintPower * (contribution * CloudPhase(cosTheta, eccentricity) * exp(-opticalDepth * attenuation));
        attenuation *= params.lightScatter.y;
        contribution *= params.lightScatter.z;
        eccentricity *= params.lightScatter.w;
        tintPower *= tint;
    }
    float silver = params.lightPhase.w * HenyeyGreenstein(cosTheta, params.lightScatter.x) * exp(-opticalDepth);
    return sum + vec3(silver);
}

// Chance that light scattered into a sample comes out toward the eye (Nubis, Decima): low at the cloud edge and in
// cavities (little cloud around to scatter into), low at the base, high in the core and the upper body.
// Looking toward the sun the edges stay bright (silver lining), so the term fades to 1 there.
float InScatterProbability(float lodDensity, float h, float cosTheta)
{
    float expo = clamp(Remap(h, 0.3, 0.85, 0.5, 2.0), 0.5, 2.0);
    float depthP = 0.05 + pow(Saturate(lodDensity), expo);
    float vertP = pow(clamp(Remap(h, 0.07, 0.14, 0.1, 1.0), 0.1, 1.0), 0.8);
    float towardSun = Saturate(cosTheta);
    return mix(Saturate(depthP * vertP), 1.0, towardSun * towardSun * 0.8);
}

// Radiance a sample scatters toward the eye per unit of absorbed light. The march weights it by
// transmittance * opacity of the step.
vec3 CloudSourceRadiance(CloudLightInput s)
{
    float cosTheta = dot(s.viewDirection, s.sunDirection);
    vec3 sun = vec3(0.0);
    if (dot(s.sunRadiance, s.sunRadiance) > 0.0) {
        vec3 scatter = SunScatter(cosTheta, s.sunOpticalDepth);
        float powder = 1.0 - exp(-4.0 * s.density);
        float awayFromSun = Saturate(0.5 - 0.5 * cosTheta);
        scatter *= mix(1.0, powder, params.lightMarch.w * awayFromSun);
        // The in-scatter chance darkens edges and cavities; the strength knob is farField.w, 0 = off.
        scatter *= mix(1.0, InScatterProbability(s.lodDensity, s.heightFraction, cosTheta), params.farField.w);
        // The diffusion term is the light that bounced around inside the cloud: it decays slowly with optical depth.
        float tau = s.sunOpticalDepth;
        vec3 diffuse = vec3(params.lightTune.y * (tau / (tau + 2.0)) / (1.0 + 0.15 * tau) / (4.0 * kPi));
        // Inner glow: looking toward the sun, the deep light of a thick cloud shines through its lobes.
        float glowDecay = mix(0.25, mix(0.25, 0.05, s.lodDensity), Saturate(cosTheta / 0.9));
        vec3 glow = vec3(params.lightTune.z * s.lodDensity * exp(-tau * glowDecay) * HenyeyGreenstein(cosTheta, kGlowEccentricity));
        sun = s.sunRadiance * mix(vec3(1.0), kSunWarmTint, params.lightTune2.x) * (scatter + diffuse + glow);
    }
    float skyShare = mix(params.lightMisc.y, 1.0, pow(s.heightFraction, 0.7)) * s.skyVisibility;
    // Light bounced up from the ground reaches the low part of the cloud.
    float groundShare = (1.0 - s.heightFraction) * (1.0 - s.heightFraction);
    return sun + s.ambientRadiance * skyShare + s.groundRadiance * groundShare;
}
