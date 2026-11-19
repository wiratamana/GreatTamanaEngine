// Light of one cloud sample. Included by VolumetricCloud.comp after its density helpers: the sun march reads
// the density through CloudLightDensity().

const float kPi = 3.14159265359;
const float kMinPhaseDenominator = 1e-4;
// Nearly neutral octave tint balances mid-tones without turning upper creases blue.
const vec3 kOctaveTint = vec3(0.99, 0.995, 1.004);
// Warm sunlight tint at low sun elevations; high sun retains a subtle ivory cast.
const vec3 kSunWarmTint = vec3(1.0, 0.82, 0.62);
const vec3 kLowSunApricotTint = vec3(1.12, 0.96, 0.81);
const float kHighSunWarmKeep = 0.16;
const float kGlowEccentricity = 0.3;
const float kSilverPhaseCap = 4.0;
const float kMaxSourceLuma = 7.5;
const vec3 kCloudLumaWeights = vec3(0.2126, 0.7152, 0.0722);
// Share of its segment a sun sample may move (1 = anywhere inside).
const float kLightJitterSpan = 0.9;
// Low-sun ramp below 15 degrees elevation (sin 15 deg = 0.259).
const float kLowSunWarmthStart = 0.10;
const float kLowSunWarmthEnd = 0.26;
const float kLowSunWarmthGain = 2.95;
// Golden-apricot crown warmth below 22 degrees sun elevation (sin 22 deg = 0.375) on lit crowns.
const float kCrownApricotSunStart = 0.18;
const float kCrownApricotSunEnd = 0.38;
const float kCrownApricotGain = 1.20;
const vec3 kCrownApricotTint = vec3(1.18, 0.95, 0.68);
const float kCrownApricotHeightStart = 0.40;
const float kCrownApricotHeightEnd = 0.78;
const float kCrownApricotTauDecay = 1.60;
// Light-path extinction, octave attenuation, smooth mid-tau bell, and deep multi-scatter damping.
const float kSunExtinctionScale = 0.44;
const float kOctaveAttenuationScale = 0.75;
const float kMultiScatterDepthScale = 1.35;
const float kMultiScatterWeight = 0.92;
const float kMultiScatterIsoMix = 0.20;
const float kMultiScatterDeepTauStart = 0.85;
const float kMultiScatterDeepTauEnd = 3.80;
const float kMultiScatterDeepDamp = 0.22;
const float kMidScatterTauDecay = 0.85;
const float kMidScatterBellNorm = 2.31;
const float kMidScatterHeightStart = 0.22;
const float kMidScatterHeightEnd = 0.44;
const float kMidScatterGain = 0.18;
const vec3 kMidScatterTint = vec3(1.07, 1.00, 0.90);
const float kMidBandNeutralMix = 0.58;
const float kPowderBlendScale = 0.35;
const float kPowderDensityDecay = 3.0;
const float kPowderTauDecay = 1.6;
const float kPowderDensityFadeEnd = 0.30;
// Direct sun gain and side-scatter floor on the upper half for warm-white sunlit domes.
const float kUpperDirectSunGain = 1.58;
const float kUpperSunStart = 0.28;
const float kUpperSunEnd = 0.66;
const float kUpperSidePhaseFloor = 0.068;
const float kCrownLiftStrength = 1.18;
// Macro interior and backlit-core optical-depth shading from height and sun path tau.
const float kInteriorFloorStart = 0.18;
const float kInteriorFloorEnd = 0.60;
const float kInteriorSunFloor = 0.015;
const float kInteriorAlignAway = 0.86;
const float kInteriorAlignToward = 0.92;
const float kUpperSkyVisFloor = 0.44;
const float kInteriorShadeTauStart = 0.85;
const float kInteriorShadeTauEnd = 3.60;
const float kInteriorSunDamp = 0.15;
const float kInteriorAmbDamp = 0.16;
const float kBacklitCoreTauStart = 1.50;
const float kBacklitCoreTauEnd = 4.20;
const float kBacklitCoreSunDamp = 0.30;
const float kBacklitCoreAmbDamp = 0.16;
// Two-octave low-extinction scatter and sky-bounce gradient for shaded thick slabs.
const float kThickSlabExtinctionScale = 0.35;
const float kThickSlabSecondOctave = 0.58;
const float kThickSlabScatterGain = 0.26;
const vec3 kThickSlabWarmTint = vec3(1.08, 0.99, 0.88);
const float kThickSlabSkyBounce = 0.20;
const float kThickSlabTauPivotSq = 4.0;
const float kThickSlabViewStart = 0.24;
const float kThickSlabViewEnd = 0.46;
// Macro-density and optical-depth ambient occlusion for shadowed concavities, floored at belly luma.
const float kCreviceBodyStart = 0.42;
const float kCreviceBodyEnd = 0.94;
const float kCreviceTauDecay = 0.28;
const float kCreviceSkySpan = 0.75;
const float kCreviceDensityScale = 0.58;
const float kCreviceAoPower = 1.30;
const float kCreviceAoStrength = 0.70;
const float kCreviceHeightStart = 0.26;
const float kCreviceHeightEnd = 0.40;
const float kCreviceDensityGateStart = 0.15;
const float kCreviceDensityGateEnd = 0.55;
const float kCreviceTopStart = 0.58;
const float kCreviceTopEnd = 0.70;
const float kCreviceTopKeep = 0.30;
const float kCreviceLitTransStart = 0.24;
const float kCreviceLitTransEnd = 0.46;
const float kCreviceLitKeep = 0.30;
// Periwinkle-slate belly shade, low-sun 3500K mid-tone warmth, and upper-crease ambient tint.
const vec3 kShadeTint = vec3(0.89, 0.96, 1.07);
const vec3 kBellyCyanSlate = vec3(0.81, 0.91, 1.21);
const float kBellyCoolMix = 0.72;
const vec3 kLowSunVioletSlate = vec3(1.10, 0.88, 1.32);
const float kLowSunVioletMix = 0.64;
const vec3 kLowSunMidWarmTint = vec3(1.28, 0.97, 0.54);
const float kLowSunMidWarmMix = 0.11;
const float kLowSunMidTauDecay = 0.45;
const vec3 kUpperCreaseTint = vec3(1.10, 1.01, 0.88);
const float kUpperAmbientScale = 0.75;
const float kBellyBaseFloor = 0.62;
const float kUpperBaseFloor = 0.75;
const float kBellySkyVisFloor = 0.47;
const float kBellyAmbientScale = 0.85;
const float kLowSunBellyLift = 0.14;
const float kAmbientGain = 1.32;
// Limits direct sun and ground bounce in the lower condensation belly.
const float kBaseSunKeep = 0.02;
const float kBaseSunFadeStart = 0.16;
const float kBaseSunFadeEnd = 0.46;
const float kGroundBounceTop = 0.28;
const float kGroundBounceScale = 0.075;
const float kGroundBounceDecay = 1.15;
// Masks forward scatter to thin optical depths and damps near-sun glare smoothly across tau.
const float kForwardHeightStart = 0.24;
const float kForwardHeightEnd = 0.62;
const float kForwardDepthDecay = 1.50;
const float kThickGlareTauStart = 0.65;
const float kThickGlareTauEnd = 2.45;
const float kThickGlareDrop = 0.30;
const float kThickGlarePhaseDrop = 0.22;
const float kGlowRiseDecay = 2.20;
const float kGlowFallDecay = 1.35;
const float kGlowNorm = 1.25;
// Soft knees let ordinary sunlit tops reach display white while preventing flat overhead blowout.
const float kMultiScatterShoulder = 0.26;
const float kScatterShoulder = 1.60;
const float kSunShoulderKnee = 1.75;
const float kSunShoulderCeiling = 3.00;
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

// Sun light reaching a sample: upper-boosted direct scatter, depth-damped multi-scattering, and upper silver rim.
vec3 SunScatter(float cosTheta, float opticalDepth, float heightFraction, float forwardMask)
{
    uint octaves = uint(params.lightMarch.z);
    float lightTau = opticalDepth * kSunExtinctionScale;
    float octaveAttenuation = params.lightScatter.y * kOctaveAttenuationScale;
    float attenuation = 1.0;
    float contribution = 1.0;
    float eccentricity = 1.0;
    vec3 tint = mix(vec3(1.0), kOctaveTint, params.skyShade2.y);
    vec3 tintPower = vec3(1.0);
    vec3 singleScatter = vec3(0.0);
    vec3 multiScatter = vec3(0.0);
    float sideLiftMask = smoothstep(0.42, 0.78, heightFraction) * exp(-lightTau * 0.80);
    float towardSun = Saturate(cosTheta);
    float thickGlareGate = smoothstep(kThickGlareTauStart, kThickGlareTauEnd, opticalDepth);
    float thickGlareDamp = 1.0 - kThickGlareDrop * thickGlareGate;
    for (uint i = 0u; i < octaves; ++i) {
        float rawPhase = CloudPhase(cosTheta, eccentricity);
        float phase = rawPhase;
        if (cosTheta > 0.0) {
            float sidePhase = CloudPhase(0.0, eccentricity);
            phase = mix(sidePhase, rawPhase, forwardMask);
            phase *= 1.0 - kThickGlarePhaseDrop * towardSun * towardSun * thickGlareGate * (1.0 - sideLiftMask);
        }
        if (i == 0u) {
            phase = mix(phase, max(phase, kUpperSidePhaseFloor), sideLiftMask);
        } else {
            phase = mix(phase, 1.0 / (4.0 * kPi), kMultiScatterIsoMix);
        }
        float depthScale = (i == 0u) ? 1.0 : kMultiScatterDepthScale;
        vec3 term = tintPower * (contribution * phase * exp(-lightTau * attenuation * depthScale));
        if (i == 0u) {
            singleScatter = term;
        } else {
            multiScatter += term * kMultiScatterWeight;
        }
        attenuation *= octaveAttenuation;
        contribution *= params.lightScatter.z;
        eccentricity *= params.lightScatter.w;
        tintPower *= tint;
    }
    singleScatter *= mix(1.0, kUpperDirectSunGain, smoothstep(kUpperSunStart, kUpperSunEnd, heightFraction));
    float deepMsGate = smoothstep(kMultiScatterDeepTauStart, kMultiScatterDeepTauEnd, opticalDepth);
    multiScatter *= 1.0 - kMultiScatterDeepDamp * deepMsGate;
    multiScatter = multiScatter / (1.0 + multiScatter / kMultiScatterShoulder);
    float midTransBell = opticalDepth * exp(-opticalDepth * kMidScatterTauDecay) * kMidScatterBellNorm;
    float midShelfMask = midTransBell * smoothstep(kMidScatterHeightStart, kMidScatterHeightEnd, heightFraction);
    multiScatter *= vec3(1.0) + kMidScatterGain * kMidScatterTint * midShelfMask;
    vec3 scatter = singleScatter + multiScatter;
    scatter = scatter / (1.0 + scatter / kScatterShoulder);
    float silverPhase = kSilverPhaseCap * (1.0 - exp(-HenyeyGreenstein(cosTheta, params.lightScatter.x) / kSilverPhaseCap));
    float silverMask = smoothstep(0.28, 0.68, heightFraction) * exp(-opticalDepth * 2.20) * thickGlareDamp;
    float silver = params.lightPhase.w * silverPhase * silverMask;
    return scatter + vec3(silver);
}

// Nubis in-scatter probability: shades cavities and bases while masking thin-rim lift to upper thin edges.
float InScatterProbability(float lodDensity, float h, float cosTheta, float forwardMask)
{
    float d = Saturate(lodDensity);
    float expo = clamp(Remap(h, 0.28, 0.85, 0.60, 1.80), 0.60, 1.80);
    float depthP = 0.28 + 0.72 * pow(max(d, 1e-5), expo);
    float vertP = pow(clamp(Remap(h, 0.06, 0.34, 0.30, 1.0), 0.30, 1.0), 0.80);
    float towardSun = Saturate(cosTheta);
    float rimLift = towardSun * towardSun * (1.0 - d) * 0.20 * forwardMask;
    return mix(Saturate(depthP * vertP), 1.0, rimLift);
}

// Subtle warm-white ivory cast at high sun and golden warmth when the sun sits below 15 degrees.
float EffectiveSunWarmth(float baseWarmth, float sunUp)
{
    float base = Saturate(baseWarmth);
    if (base <= 0.0) {
        return 0.0;
    }
    float lowSunBoost = 1.0 - smoothstep(kLowSunWarmthStart, kLowSunWarmthEnd, sunUp);
    return Saturate(base * (kHighSunWarmKeep + kLowSunWarmthGain * lowSunBoost));
}

// Radiance a sample scatters toward the eye per unit of absorbed light.
vec3 CloudSourceRadiance(CloudLightInput s)
{
    float cosTheta = dot(s.viewDirection, s.sunDirection);
    float tau = s.sunOpticalDepth;
    float lightTau = tau * kSunExtinctionScale;
    float sunLightTrans = exp(-lightTau);
    float towardSun = Saturate(cosTheta);
    float thickGlareDamp = 1.0 - kThickGlareDrop * smoothstep(kThickGlareTauStart, kThickGlareTauEnd, tau);
    float forwardHeight = smoothstep(kForwardHeightStart, kForwardHeightEnd, s.heightFraction);
    float forwardMask = forwardHeight * exp(-tau * kForwardDepthDecay) * thickGlareDamp;
    float interiorHeight = smoothstep(kInteriorFloorStart, kInteriorFloorEnd, s.heightFraction);
    float sunAlign = mix(kInteriorAlignAway, kInteriorAlignToward, 0.5 + 0.5 * cosTheta);
    float lowSun = 1.0 - smoothstep(kLowSunWarmthStart, kLowSunWarmthEnd, s.sunDirection.y);
    // Smooth macro height and sun-path optical-depth masks shade interiors and thick cores without detail noise.
    float aboveBelly = smoothstep(0.20, 0.38, s.heightFraction);
    float midLowerMask = aboveBelly * (1.0 - smoothstep(0.46, 0.66, s.heightFraction));
    float deepCoreMask = aboveBelly * smoothstep(2.20, 5.00, tau);
    float interiorMacro = max(midLowerMask * smoothstep(kInteriorShadeTauStart, kInteriorShadeTauEnd, tau), deepCoreMask);
    float backlitCore = towardSun * smoothstep(kBacklitCoreTauStart, kBacklitCoreTauEnd, tau);
    // Shaded thick undersides and backlit slabs receive a vertical and lit-edge multi-scatter gradient.
    float closeSlab = smoothstep(kThickSlabViewStart, kThickSlabViewEnd, s.viewDirection.y);
    float closeEdgeGrad = mix(0.35, 1.0, 1.0 - smoothstep(0.45, 0.78, s.viewDirection.y)) * mix(0.65, 1.0, 0.5 + 0.5 * cosTheta);
    float backlitSlab = towardSun * smoothstep(0.20, 0.48, s.heightFraction) * (1.0 - 0.60 * smoothstep(0.54, 0.84, s.heightFraction));
    float slabMask = max(backlitSlab, 0.85 * closeSlab * closeEdgeGrad);
    vec3 sun = vec3(0.0);
    if (dot(s.sunRadiance, s.sunRadiance) > 0.0) {
        vec3 scatter = SunScatter(cosTheta, tau, s.heightFraction, forwardMask);
        float powder = 0.35 + 0.65 * (1.0 - exp(-kPowderDensityDecay * s.density)) * (1.0 - exp(-kPowderTauDecay * tau));
        float awayFromSun = Saturate(0.5 - 0.5 * cosTheta);
        float powderWeight = params.lightMarch.w * kPowderBlendScale * smoothstep(0.0, kPowderDensityFadeEnd, s.density) * awayFromSun * awayFromSun * (1.0 - towardSun);
        scatter *= mix(1.0, powder, powderWeight);
        // Shades thin edges and cavities; farField.w = 0 turns it off.
        scatter *= mix(1.0, InScatterProbability(s.lodDensity, s.heightFraction, cosTheta, forwardMask), params.farField.w);
        // Body diffusion and interior floor keep upper puffs soft while macro depth shades thick interiors.
        float diffMask = smoothstep(0.24, 0.58, s.heightFraction) * exp(-0.60 * lightTau);
        float diffBody = params.lightTune.y * 0.48 * diffMask * (lightTau / (lightTau + 1.6)) / (4.0 * kPi);
        float sunFloor = kInteriorSunFloor * interiorHeight * sunAlign * smoothstep(0.5, 2.8, tau);
        vec3 diffuse = vec3(max(diffBody, sunFloor));
        float sunDepthShade = (1.0 - kInteriorSunDamp * interiorMacro) * (1.0 - kBacklitCoreSunDamp * backlitCore);
        scatter *= sunDepthShade;
        diffuse *= sunDepthShade;
        // Smooth subsurface glow across thin upper rims with exponential tau falloff.
        float glowGate = (1.0 - exp(-tau * kGlowRiseDecay)) * exp(-tau * kGlowFallDecay) * kGlowNorm * forwardHeight * thickGlareDamp;
        vec3 glow = vec3(params.lightTune.z * 0.75 * s.lodDensity * glowGate * HenyeyGreenstein(cosTheta, kGlowEccentricity));
        glow *= 1.0 - kGlowSunDamp * towardSun * towardSun;
        // Sunlit upper-crown lift makes cauliflower dome tops gleam cleanly above creases and mid-tones.
        float crownLift = smoothstep(0.48, 0.90, s.heightFraction) * exp(-lightTau * 1.05) * kCrownLiftStrength;
        vec3 crown = vec3(crownLift / (4.0 * kPi));
        float warmth = EffectiveSunWarmth(params.lightTune2.x, s.sunDirection.y);
        float topWarmMask = smoothstep(0.32, 0.75, s.heightFraction) * exp(-lightTau * 0.55);
        vec3 warmTint = mix(vec3(1.0), kSunWarmTint, Saturate(warmth * (0.25 + 0.85 * topWarmMask)));
        float crownLowSun = 1.0 - smoothstep(kCrownApricotSunStart, kCrownApricotSunEnd, s.sunDirection.y);
        float litCrownMask = smoothstep(kCrownApricotHeightStart, kCrownApricotHeightEnd, s.heightFraction) * exp(-lightTau * kCrownApricotTauDecay);
        float crownApricot = Saturate(kCrownApricotGain * crownLowSun * litCrownMask);
        float apricotMask = lowSun * mix(0.65, 1.0, smoothstep(0.22, 0.70, s.heightFraction)) * exp(-lightTau * 0.35);
        warmTint *= mix(vec3(1.0), kLowSunApricotTint, apricotMask * (1.0 - litCrownMask));
        warmTint *= mix(vec3(1.0), kCrownApricotTint, crownApricot);
        // Daytime white balance on tops and warm-neutral mid-tones; lit crowns warm below 22 degrees.
        float sunInLuma = dot(s.sunRadiance, kCloudLumaWeights);
        float midBand = smoothstep(0.24, 0.38, s.heightFraction) * (1.0 - smoothstep(0.46, 0.64, s.heightFraction)) * smoothstep(0.6, 2.2, tau) * (1.0 - lowSun);
        float dayWhiteBalance = (1.0 - lowSun) * (1.0 - crownLowSun * litCrownMask) * mix(0.72, 0.52, smoothstep(0.52, 0.86, s.heightFraction));
        vec3 sunInput = mix(s.sunRadiance, vec3(sunInLuma), Saturate(max(dayWhiteBalance, kMidBandNeutralMix * midBand)));
        float baseSun = mix(kBaseSunKeep, 1.0, smoothstep(kBaseSunFadeStart, kBaseSunFadeEnd, s.heightFraction));
        float slabTau = lightTau * kThickSlabExtinctionScale;
        float slabPhase0 = mix(1.0 / (4.0 * kPi), HenyeyGreenstein(cosTheta, 0.38), 0.60);
        float slabPhase1 = mix(1.0 / (4.0 * kPi), HenyeyGreenstein(cosTheta, 0.20), 0.45);
        float slabMs = (slabPhase0 * exp(-slabTau) + kThickSlabSecondOctave * slabPhase1 * exp(-0.50 * slabTau)) * kThickSlabScatterGain;
        vec3 slabWarm = mix(vec3(1.0), kThickSlabWarmTint, exp(-slabTau * 0.65));
        sun = sunInput * warmTint * (scatter + diffuse + glow + crown) * baseSun;
        sun += sunInput * warmTint * slabWarm * (slabMs * slabMask);
        float sunLuma = dot(sun, kCloudLumaWeights);
        if (sunLuma > kSunShoulderKnee) {
            float excess = sunLuma - kSunShoulderKnee;
            float span = kSunShoulderCeiling - kSunShoulderKnee;
            float compressed = kSunShoulderKnee + span * excess / (span + excess);
            sun *= compressed / sunLuma;
        }
    }
    // Periwinkle-slate belly shade, low-sun 3500K mid-tone warmth, and cream-neutral upper-dome ambient.
    float bellyFloor = max(params.lightMisc.y, kBellyBaseFloor);
    float baseFloor = mix(bellyFloor, max(params.lightMisc.y, kUpperBaseFloor), smoothstep(0.34, 0.55, s.heightFraction));
    float bellyMask = 1.0 - smoothstep(0.18, 0.34, s.heightFraction);
    float upperSkyAlign = mix(0.88, mix(1.12, 0.96, smoothstep(0.85, 2.20, tau)), 0.5 + 0.5 * cosTheta);
    float upperSkyFloor = kUpperSkyVisFloor * interiorHeight * upperSkyAlign * (1.0 - 0.14 * smoothstep(0.85, 3.20, tau));
    float effectiveSkyVis = max(s.skyVisibility, max(upperSkyFloor, kBellySkyVisFloor * bellyMask));
    float skyShare = mix(baseFloor, 1.0, pow(max(s.heightFraction, 1e-5), 0.72)) * effectiveSkyVis;
    float ambLuma = dot(s.ambientRadiance, kCloudLumaWeights);
    vec3 coolAmbient = mix(s.ambientRadiance * kShadeTint, kBellyCyanSlate * ambLuma, kBellyCoolMix);
    float shadowCool = 1.0 - smoothstep(0.18, 0.42, s.heightFraction);
    vec3 shadedAmbient = mix(vec3(ambLuma), coolAmbient, shadowCool);
    float violetMask = lowSun * (1.0 - smoothstep(0.24, 0.52, s.heightFraction));
    shadedAmbient = mix(shadedAmbient, ambLuma * kLowSunVioletSlate, kLowSunVioletMix * violetMask);
    float upperCream = smoothstep(0.50, 0.78, s.heightFraction) * (1.0 - 0.50 * lowSun);
    shadedAmbient = mix(shadedAmbient, ambLuma * kUpperCreaseTint, 0.75 * upperCream);
    float sunChromaLuma = dot(s.sunRadiance, kCloudLumaWeights);
    vec3 sunChroma = sunChromaLuma > 1e-4 ? (s.sunRadiance / sunChromaLuma) : kLowSunMidWarmTint;
    vec3 lowSunMidTone = mix(kLowSunMidWarmTint, sunChroma, 0.45);
    lowSunMidTone /= max(dot(lowSunMidTone, kCloudLumaWeights), 1e-4);
    float lowSunMidMask = lowSun * smoothstep(0.28, 0.52, s.heightFraction) * exp(-lightTau * kLowSunMidTauDecay);
    shadedAmbient = mix(shadedAmbient, ambLuma * lowSunMidTone, kLowSunMidWarmMix * lowSunMidMask);
    float upperAmbientCut = mix(1.0, kUpperAmbientScale, smoothstep(0.32, 0.70, s.heightFraction));
    float litAmbientDamp = mix(1.0, 0.52, smoothstep(0.30, 0.74, s.heightFraction) * exp(-lightTau * 0.75));
    float bellyTrim = mix(1.0, kBellyAmbientScale, 1.0 - smoothstep(0.22, 0.30, s.heightFraction));
    float ambDepthShade = (1.0 - kInteriorAmbDamp * interiorMacro) * (1.0 - kBacklitCoreAmbDamp * aboveBelly * backlitCore);
    float lowSunBellyLift = 1.0 + kLowSunBellyLift * lowSun * (1.0 - smoothstep(0.16, 0.44, s.heightFraction));
    float thickTauRamp = (tau * tau) / (tau * tau + kThickSlabTauPivotSq);
    float thickSkyBounce = 1.0 + kThickSlabSkyBounce * thickTauRamp * slabMask;
    // Concavity occlusion gated by density, height, and sun transmittance, floored at the belly level.
    float creviceBody = smoothstep(kCreviceBodyStart, kCreviceBodyEnd, s.lodDensity);
    float skyOcclusion = Saturate((1.0 - s.skyVisibility) / kCreviceSkySpan);
    float sunOcclusion = 1.0 - exp(-tau * kCreviceTauDecay);
    float macroDensity = creviceBody * (0.15 + 0.40 * skyOcclusion + 0.45 * sunOcclusion);
    float creviceAo = pow(max(1.0 - clamp(macroDensity * kCreviceDensityScale, 0.0, 1.0), 1e-4), kCreviceAoPower);
    float creviceDensityGate = smoothstep(kCreviceDensityGateStart, kCreviceDensityGateEnd, s.density);
    float creviceTopKeep = mix(1.0, kCreviceTopKeep, smoothstep(kCreviceTopStart, kCreviceTopEnd, s.heightFraction));
    float creviceLitKeep = mix(1.0, kCreviceLitKeep, smoothstep(kCreviceLitTransStart, kCreviceLitTransEnd, sunLightTrans));
    float creviceMask = smoothstep(kCreviceHeightStart, kCreviceHeightEnd, s.heightFraction) * creviceDensityGate * creviceTopKeep * creviceLitKeep;
    vec3 ambient = shadedAmbient * (skyShare * upperAmbientCut * litAmbientDamp * bellyTrim * ambDepthShade * lowSunBellyLift * thickSkyBounce * kAmbientGain);
    vec3 bellyTintRef = mix(coolAmbient, ambLuma * kLowSunVioletSlate, kLowSunVioletMix * lowSun);
    float bellyScaleRef = mix(bellyFloor, 1.0, 0.22) * kBellySkyVisFloor * kBellyAmbientScale * (1.0 + kLowSunBellyLift * lowSun) * kAmbientGain;
    float bellyFloorLuma = dot(bellyTintRef, kCloudLumaWeights) * bellyScaleRef;
    float ambPreLuma = dot(ambient, kCloudLumaWeights);
    float aoFloor = Saturate((bellyFloorLuma - dot(sun, kCloudLumaWeights)) / max(ambPreLuma, 1e-4));
    ambient *= max(mix(1.0, creviceAo, kCreviceAoStrength * creviceMask), aoFloor);
    // Ground bounce reaches only unoccluded lower rims so warm ground light cannot muddy shadowed bellies.
    float lowRim = 1.0 - smoothstep(0.0, kGroundBounceTop, s.heightFraction);
    float groundShare = lowRim * lowRim * kGroundBounceScale * exp(-kGroundBounceDecay * tau);
    return sun + ambient + s.groundRadiance * groundShare;
}

// Smooth luminance ceiling: preserves hue, leaves normal cloud brightness intact, and approaches maxLuma smoothly.
vec3 BoundCloudLuma(vec3 radiance, float maxLuma)
{
    radiance = max(radiance, vec3(0.0));
    float luma = dot(radiance, kCloudLumaWeights);
    float ratio = luma / max(maxLuma, 1e-4);
    float r2 = ratio * ratio;
    float scale = ratio > 1e4 ? (1.0 / ratio) : inversesqrt(sqrt(1.0 + r2 * r2));
    return radiance * scale;
}
