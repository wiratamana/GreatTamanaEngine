// Light of one cloud sample. Included by VolumetricCloud.comp after its density helpers: the sun march reads
// the density through CloudLightDensity().

const float kPi = 3.14159265359;
const float kMinPhaseDenominator = 1e-4;
// Gentle cool shift on deeper scattering octaves so mid-tones stay neutral while tops stay warm.
const vec3 kOctaveTint = vec3(0.94, 0.98, 1.03);
// Sun light at full warmth: ivory-gold on sunlit tops, stronger when the sun sits low.
const vec3 kSunWarmTint = vec3(1.0, 0.84, 0.63);
const float kGlowEccentricity = 0.3;
const float kSilverPhaseCap = 4.0;
const float kMaxSourceLuma = 5.0;
const vec3 kCloudLumaWeights = vec3(0.2126, 0.7152, 0.0722);
// Share of its segment a sun sample may move (1 = anywhere inside).
const float kLightJitterSpan = 0.9;
const float kLowSunWarmthStart = 0.14;
const float kLowSunWarmthEnd = 0.31;
const float kLowSunWarmthGain = 1.65;
// Cool cyan-slate shade tint for shadowed cloud bases; avoids warm violet or beige casts.
const vec3 kShadeTint = vec3(0.92, 1.02, 0.90);
const vec3 kBellyCyanSlate = vec3(0.81, 0.98, 1.58);
// Keeps direct and scattered sun out of the lower condensation belly where underside opacity forms.
const float kBaseSunKeep = 0.02;
const float kBaseSunFadeStart = 0.16;
const float kBaseSunFadeEnd = 0.46;
// Masks forward scatter and silver lining to thin optical depths and upper cloud parts.
const float kForwardHeightStart = 0.24;
const float kForwardHeightEnd = 0.62;
const float kForwardDepthDecay = 1.50;
// Soft knees that keep multi-scattered overhead puffs from flattening into a single white value.
const float kMultiScatterShoulder = 0.055;
const float kScatterShoulder = 0.28;
const float kSunRadianceShoulder = 2.4;
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

// Sun light reaching a sample: compressed multi-scattering octaves plus a thin upper-edge silver rim.
vec3 SunScatter(float cosTheta, float opticalDepth, float heightFraction, float forwardMask)
{
    uint octaves = uint(params.lightMarch.z);
    float attenuation = 1.0;
    float contribution = 1.0;
    float eccentricity = 1.0;
    vec3 tint = mix(vec3(1.0), kOctaveTint, params.skyShade2.y);
    vec3 tintPower = vec3(1.0);
    vec3 singleScatter = vec3(0.0);
    vec3 multiScatter = vec3(0.0);
    for (uint i = 0u; i < octaves; ++i) {
        float rawPhase = CloudPhase(cosTheta, eccentricity);
        float phase = cosTheta > 0.0 ? mix(CloudPhase(0.0, eccentricity), rawPhase, forwardMask) : rawPhase;
        float depthScale = (i == 0u) ? 1.0 : 1.45;
        vec3 term = tintPower * (contribution * phase * exp(-opticalDepth * attenuation * depthScale));
        if (i == 0u) {
            singleScatter = term;
        } else {
            multiScatter += term * 0.62;
        }
        attenuation *= params.lightScatter.y;
        contribution *= params.lightScatter.z;
        eccentricity *= params.lightScatter.w;
        tintPower *= tint;
    }
    multiScatter = multiScatter / (1.0 + multiScatter / kMultiScatterShoulder);
    vec3 scatter = singleScatter + multiScatter;
    scatter = scatter / (1.0 + scatter / kScatterShoulder);
    float silverPhase = kSilverPhaseCap * (1.0 - exp(-HenyeyGreenstein(cosTheta, params.lightScatter.x) / kSilverPhaseCap));
    float silverMask = smoothstep(0.28, 0.68, heightFraction) * exp(-opticalDepth * 2.20);
    float silver = params.lightPhase.w * silverPhase * silverMask;
    return scatter + vec3(silver);
}

// Nubis in-scatter probability: shades cavities and bases while masking thin-rim lift to upper thin edges.
float InScatterProbability(float lodDensity, float h, float cosTheta, float forwardMask)
{
    float d = Saturate(lodDensity);
    float expo = clamp(Remap(h, 0.28, 0.85, 0.65, 2.0), 0.65, 2.0);
    float depthP = 0.24 + 0.76 * pow(max(d, 1e-5), expo);
    float vertP = pow(clamp(Remap(h, 0.06, 0.34, 0.30, 1.0), 0.30, 1.0), 0.80);
    float towardSun = Saturate(cosTheta);
    float rimLift = towardSun * towardSun * (1.0 - d) * 0.20 * forwardMask;
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
    float tau = s.sunOpticalDepth;
    float forwardHeight = smoothstep(kForwardHeightStart, kForwardHeightEnd, s.heightFraction);
    float forwardMask = forwardHeight * exp(-tau * kForwardDepthDecay);
    vec3 sun = vec3(0.0);
    if (dot(s.sunRadiance, s.sunRadiance) > 0.0) {
        vec3 scatter = SunScatter(cosTheta, tau, s.heightFraction, forwardMask);
        float powder = 0.35 + 0.65 * (1.0 - exp(-3.5 * s.density));
        float awayFromSun = Saturate(0.5 - 0.5 * cosTheta);
        float powderWeight = params.lightMarch.w * smoothstep(0.0, 0.08, s.density) * awayFromSun * awayFromSun * (1.0 - Saturate(cosTheta));
        scatter *= mix(1.0, powder, powderWeight);
        // Shades thin edges and cavities; farField.w = 0 turns it off.
        scatter *= mix(1.0, InScatterProbability(s.lodDensity, s.heightFraction, cosTheta, forwardMask), params.farField.w);
        // Upper and mid-body diffusion keeps puff interiors luminous without warming shadowed bellies.
        float diffMask = smoothstep(0.24, 0.58, s.heightFraction) * exp(-0.55 * tau);
        vec3 diffuse = vec3(params.lightTune.y * 0.45 * diffMask * (tau / (tau + 1.8)) / (4.0 * kPi));
        // Forward subsurface glow restricted to thin upper parts so bellies and thick cores do not glow.
        float towardSun = Saturate(cosTheta);
        float glowGate = smoothstep(0.05, 0.50, tau) * exp(-tau * 1.35) * forwardHeight;
        vec3 glow = vec3(params.lightTune.z * 0.75 * s.lodDensity * glowGate * HenyeyGreenstein(cosTheta, kGlowEccentricity));
        glow *= 1.0 - kGlowSunDamp * towardSun * towardSun;
        // Sunlit upper-crown lift makes cauliflower dome tops gleam cleanly above mid-tones.
        float crownLift = smoothstep(0.45, 0.92, s.heightFraction) * exp(-tau * 0.80) * 0.18;
        vec3 crown = vec3(crownLift / (4.0 * kPi));
        float warmth = EffectiveSunWarmth(params.lightTune2.x, s.sunDirection.y);
        float topWarmMask = smoothstep(0.32, 0.75, s.heightFraction) * exp(-tau * 0.55);
        vec3 warmTint = mix(vec3(1.0), kSunWarmTint, Saturate(warmth * (0.30 + 0.85 * topWarmMask)));
        // Gently neutralizes low-sun chroma inside mid-tones so tops read warm and mid-tones read neutral.
        float sunInLuma = dot(s.sunRadiance, kCloudLumaWeights);
        float midBand = smoothstep(0.25, 0.45, s.heightFraction) * (1.0 - smoothstep(0.55, 0.82, s.heightFraction)) * smoothstep(0.5, 1.8, tau);
        vec3 sunInput = mix(s.sunRadiance, vec3(sunInLuma), 0.40 * midBand);
        float baseSun = mix(kBaseSunKeep, 1.0, smoothstep(kBaseSunFadeStart, kBaseSunFadeEnd, s.heightFraction));
        sun = sunInput * warmTint * (scatter + diffuse + glow + crown) * baseSun;
        float sunLuma = dot(sun, kCloudLumaWeights);
        sun /= (1.0 + sunLuma / kSunRadianceShoulder);
    }
    // Cool cyan-slate sky shade in shadowed bases, desaturating toward neutral in sunlit upper clouds.
    float baseFloor = max(params.lightMisc.y, 0.46);
    float skyShare = mix(baseFloor, 1.0, pow(max(s.heightFraction, 1e-5), 0.75)) * s.skyVisibility;
    float ambLuma = dot(s.ambientRadiance, kCloudLumaWeights);
    vec3 coolAmbient = mix(s.ambientRadiance * kShadeTint, kBellyCyanSlate * ambLuma, 0.78);
    float shadowCool = Saturate((1.0 - smoothstep(0.18, 0.54, s.heightFraction)) * 0.78 + (1.0 - exp(-tau * 0.55)) * 0.22);
    vec3 shadedAmbient = mix(vec3(ambLuma), coolAmbient, shadowCool);
    float litAmbientDamp = mix(1.0, 0.46, smoothstep(0.28, 0.72, s.heightFraction) * exp(-tau * 0.80));
    float lowSunBellyLift = 1.0 + 0.38 * (1.0 - smoothstep(0.12, 0.32, s.sunDirection.y)) * (1.0 - smoothstep(0.16, 0.46, s.heightFraction));
    vec3 ambient = shadedAmbient * (skyShare * litAmbientDamp * lowSunBellyLift * 1.24);
    // Ground bounce reaches only unoccluded lower rims so warm ground light cannot muddy shadowed bellies.
    float lowRim = 1.0 - smoothstep(0.0, 0.38, s.heightFraction);
    float groundShare = lowRim * lowRim * 0.18 * exp(-0.65 * tau);
    return sun + ambient + s.groundRadiance * groundShare;
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
