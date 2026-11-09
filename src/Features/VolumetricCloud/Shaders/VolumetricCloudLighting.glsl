// Light of one cloud sample. Included by VolumetricCloud.comp after its density helpers, so a better model
// can march toward the sun with CloudDensityAt() without touching the ray march.

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
};

// Radiance a sample scatters toward the eye per unit of absorbed light. The march weights it by
// transmittance * (1 - exp(-extinction * step)).
vec3 CloudSourceRadiance(CloudLightInput s)
{
    float cosSun = dot(s.viewDirection, s.sunDirection);
    float forward = 1.0 + 1.5 * pow(max(cosSun, 0.0), 8.0);
    float height = 0.35 + 0.65 * pow(s.heightFraction, 0.6);
    float selfShadow = exp(-1.5 * s.density);
    vec3 sun = s.sunRadiance * forward * height * (0.25 + 0.75 * selfShadow);
    vec3 sky = s.ambientRadiance * (0.4 + 0.6 * s.heightFraction);
    return sun + sky;
}
