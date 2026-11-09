// Light of one cloud sample. Included by VolumetricCloud.comp after its density helpers: the sun march reads
// the density through CloudLightDensity().

const float kPi = 3.14159265359;
const float kMinPhaseDenominator = 1e-4;

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
    return shape * factor;
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

// Light reaching a sample from the sun: octaves of b^i * phase_i * exp(-tau * a^i), plus the silver lobe.
float SunScatter(float cosTheta, float opticalDepth)
{
    uint octaves = uint(params.lightMarch.z);
    float attenuation = 1.0;
    float contribution = 1.0;
    float eccentricity = 1.0;
    float sum = 0.0;
    for (uint i = 0u; i < octaves; ++i) {
        sum += contribution * CloudPhase(cosTheta, eccentricity) * exp(-opticalDepth * attenuation);
        attenuation *= params.lightScatter.y;
        contribution *= params.lightScatter.z;
        eccentricity *= params.lightScatter.w;
    }
    float silver = params.lightPhase.w * HenyeyGreenstein(cosTheta, params.lightScatter.x) * exp(-opticalDepth);
    return sum + silver;
}

// Radiance a sample scatters toward the eye per unit of absorbed light. The march weights it by
// transmittance * opacity of the step.
vec3 CloudSourceRadiance(CloudLightInput s)
{
    float cosTheta = dot(s.viewDirection, s.sunDirection);
    vec3 sun = vec3(0.0);
    if (dot(s.sunRadiance, s.sunRadiance) > 0.0) {
        float scatter = SunScatter(cosTheta, s.sunOpticalDepth);
        float powder = 1.0 - exp(-4.0 * s.density);
        float awayFromSun = Saturate(0.5 - 0.5 * cosTheta);
        scatter *= mix(1.0, powder, params.lightMarch.w * awayFromSun);
        sun = s.sunRadiance * scatter;
    }
    float skyShare = mix(params.lightMisc.y, 1.0, pow(s.heightFraction, 0.7));
    return sun + s.ambientRadiance * skyShare;
}
