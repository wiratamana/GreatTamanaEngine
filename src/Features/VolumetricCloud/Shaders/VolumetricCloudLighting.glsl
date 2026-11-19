// Light of one cloud sample. Included by VolumetricCloud.comp after its density helpers: the sun march reads
// the density through CloudLightDensity().

const float kPi = 3.14159265359;
const float kMinPhaseDenominator = 1e-4;
// Per octave colour of the multiple scattering at full blue shift; red and green fall, blue rises.
const vec3 kOctaveTint = vec3(0.9, 0.97, 1.1);
// Sun light at full warmth: the blue falls first, like light that crossed more air.
const vec3 kSunWarmTint = vec3(1.0, 0.82, 0.6);
const float kGlowEccentricity = 0.3;
const float kSilverPhaseCap = 4.0;
const float kMaxSourceLuma = 12.0;
const vec3 kCloudLumaWeights = vec3(0.2126, 0.7152, 0.0722);
// Share of its segment a sun sample may move (1 = anywhere inside).
const float kLightJitterSpan = 0.9;
const float kLowSunWarmthStart = 0.14;
const float kLowSunWarmthEnd = 0.31;
const float kLowSunWarmthGain = 1.85;
// Sky light tint inside cloud shade: pulls cyan toward periwinkle.
const vec3 kShadeTint = vec3(1.03, 0.93, 1.02);
// Share of the sun light kept at the flat cloud base; the upper cloud keeps all of it.
const float kBaseSunKeep = 0.25;
// Height fraction where the base sun reduction has fully faded out; wide so it never reads as a band.
const float kBaseSunFadeEnd = 0.32;
// Share of the forward glow removed when looking straight into the sun.
const float kGlowSunDamp = 0.35;

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

// Per-ray random number in 0..1 that steps every frame so temporal integration averages light-march noise.
float gLightJitter = 0.5;

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
        float centre = travelled + (0.5 + (fract(gLightJitter + kGoldenRatio * float(i)) - 0.5) * kLightJitterSpan) * segment;
        vec3 samplePosition = position + sunDirection * centre;
        // Nearest two sun samples carry fine billow erosion for crisp floret self-shadow crevices.
        float sampleDensity = (i <= 1u && params.lightMisc.w > 0.0)
            ? CloudDensityAt(samplePosition, params.lightMisc.w * (i == 0u ? 1.0 : 0.55))
            : CloudLightDensity(samplePosition);
        opticalDepth += sampleDensity * segment;
        travelled += segment;
        segment *= 2.0;
    }
    return opticalDepth * params.density.y;
}

// Share of the sky light that is not blocked by cloud above a sample: two density samples straight up.
float SkyVisibility(vec3 position)
{
    float reach = params.skyShade.y;
    float lowSample = CloudLightDensity(position + vec3(0.0, 0.25 * reach, 0.0));
    float highSample = CloudLightDensity(position + vec3(0.0, 0.75 * reach, 0.0));
    float opticalDepth = (lowSample + highSample) * 0.5 * reach * params.density.y;
    return mix(params.skyShade.z, 1.0, exp(-opticalDepth * params.skyShade.x));
}

// Sun light reaching a sample: multiple-scattering octaves plus a thin-optical-depth silver rim lobe.
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
    float silverPhase = kSilverPhaseCap * (1.0 - exp(-HenyeyGreenstein(cosTheta, params.lightScatter.x) / kSilverPhaseCap));
    float silver = params.lightPhase.w * silverPhase * exp(-opticalDepth);
    vec3 scatter = sum + vec3(silver * exp(-opticalDepth * 0.90));
    scatter = scatter / (1.0 + scatter / 1.85);
    return scatter;
}

// Nubis in-scatter probability: shades cavities and bases softly with a subtle thin-rim lift toward the sun.
float InScatterProbability(float lodDensity, float h, float cosTheta)
{
    float d = Saturate(lodDensity);
    float expo = clamp(Remap(h, 0.3, 0.85, 0.5, 2.0), 0.5, 2.0);
    float depthP = 0.28 + 0.72 * pow(max(d, 1e-5), expo);
    float vertP = pow(clamp(Remap(h, 0.03, 0.18, 0.45, 1.0), 0.45, 1.0), 0.7);
    float towardSun = Saturate(cosTheta);
    float rimLift = towardSun * towardSun * (1.0 - d) * 0.20;
    return mix(Saturate(depthP * vertP), 1.0, rimLift);
}

// Keeps daytime clouds warm-ivory while boosting golden warmth when the sun sits below 18 degrees.
float EffectiveSunWarmth(float baseWarmth, float sunUp)
{
    float base = Saturate(baseWarmth);
    if (base <= 0.0) {
        return 0.0;
    }
    float lowSunBoost = 1.0 - smoothstep(kLowSunWarmthStart, kLowSunWarmthEnd, sunUp);
    return Saturate(base * (1.0 + kLowSunWarmthGain * lowSunBoost));
}

// Radiance a sample scatters toward the eye per unit of absorbed light.
vec3 CloudSourceRadiance(CloudLightInput s)
{
    float cosTheta = dot(s.viewDirection, s.sunDirection);
    vec3 sun = vec3(0.0);
    if (dot(s.sunRadiance, s.sunRadiance) > 0.0) {
        vec3 scatter = SunScatter(cosTheta, s.sunOpticalDepth);
        float powder = 0.35 + 0.65 * (1.0 - exp(-3.5 * s.density));
        float awayFromSun = Saturate(0.5 - 0.5 * cosTheta);
        float powderWeight = params.lightMarch.w * smoothstep(0.0, 0.08, s.density) * awayFromSun * awayFromSun * (1.0 - Saturate(cosTheta));
        scatter *= mix(1.0, powder, powderWeight);
        // Shades thin edges and cavities; farField.w = 0 turns it off.
        scatter *= mix(1.0, InScatterProbability(s.lodDensity, s.heightFraction, cosTheta), params.farField.w);
        // Slow-decaying diffusion keeps thick cloud cores softly luminous.
        float tau = s.sunOpticalDepth;
        vec3 diffuse = vec3(params.lightTune.y * (tau / (tau + 2.0)) / (1.0 + 0.15 * tau) / (4.0 * kPi));
        // Forward glow builds after thin optical depth and decays in dense self-shadowed cores.
        float glowDecay = mix(0.45, 0.30, Saturate(cosTheta));
        float glowDepthGate = smoothstep(0.3, 2.0, s.sunOpticalDepth);
        vec3 glow = vec3(params.lightTune.z * s.lodDensity * glowDepthGate * exp(-tau * glowDecay) * HenyeyGreenstein(cosTheta, kGlowEccentricity));
        glow *= 1.0 - kGlowSunDamp * Saturate(cosTheta) * Saturate(cosTheta);
        // Sunlit upper-crown multi-scatter lift makes cauliflower dome tops gleam cleanly.
        float crownLift = smoothstep(0.30, 0.90, s.heightFraction) * exp(-s.sunOpticalDepth * 0.45) * 0.26;
        vec3 crown = vec3(crownLift / (4.0 * kPi));
        float warmth = EffectiveSunWarmth(params.lightTune2.x, s.sunDirection.y);
        float baseSun = mix(kBaseSunKeep, 1.0, smoothstep(0.0, kBaseSunFadeEnd, s.heightFraction));
        sun = s.sunRadiance * mix(vec3(1.0), kSunWarmTint, warmth) * (scatter + diffuse + glow + crown) * baseSun;
    }
    float skyShare = mix(params.lightMisc.y, 1.0, pow(max(s.heightFraction, 1e-5), 0.7)) * s.skyVisibility;
    // Light bounced up from the ground reaches the low part of the cloud where ground is sunlit.
    float groundShare = (1.0 - s.heightFraction) * (1.0 - s.heightFraction);
    groundShare *= mix(0.15, 1.0, exp(-0.15 * s.sunOpticalDepth));
    return sun + s.ambientRadiance * kShadeTint * skyShare + s.groundRadiance * groundShare;
}

// Smooth luminance ceiling: keeps hue, leaves normal cloud brightness intact and approaches maxLuma smoothly.
vec3 BoundCloudLuma(vec3 radiance, float maxLuma)
{
    radiance = max(radiance, vec3(0.0));
    float luma = dot(radiance, kCloudLumaWeights);
    float ratio = luma / max(maxLuma, 1e-4);
    float r2 = ratio * ratio;
    float scale = ratio > 1e4 ? (1.0 / ratio) : inversesqrt(sqrt(1.0 + r2 * r2));
    return radiance * scale;
}
