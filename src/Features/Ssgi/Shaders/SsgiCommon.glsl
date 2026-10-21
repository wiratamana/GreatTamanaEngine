// Helpers shared by the Ssgi shaders. Eye-relative camera model, no projection matrix.
#include "ScreenSpaceCommon.glsl"

const float kFootprintBiasScale = 1.5; // Minimum ray-origin offset, in GI texel footprints.
const vec3 kLumaWeights = vec3(0.2126, 0.7152, 0.0722);
const vec2 kR2 = vec2(0.7548776662, 0.5698402910);

bool IsBad(vec3 v)
{
    return any(isnan(v)) || any(isinf(v));
}

vec3 EyeRelativePoint(vec2 pixel, float viewZ, vec2 viewSize, vec3 right, vec3 down, vec3 forward, float pixelScale)
{
    vec2 offset = (pixel + 0.5 - 0.5 * viewSize) * pixelScale;
    return viewZ * (forward + right * offset.x + down * offset.y);
}

bool ProjectToPixel(vec3 p, vec2 viewSize, vec3 right, vec3 down, vec3 forward, float pixelScale, out vec2 pixel,
    out float viewZ)
{
    viewZ = dot(p, forward);
    pixel = vec2(0.0);
    if (viewZ < 1e-3) {
        return false;
    }
    float inv = 1.0 / (viewZ * pixelScale);
    pixel = 0.5 * viewSize + vec2(dot(p, right), dot(p, down)) * inv - 0.5;
    return true;
}

void BuildBasis(vec3 n, out vec3 t, out vec3 b)
{
    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

vec3 CosineHemisphere(vec2 u, vec3 n, vec3 t, vec3 b)
{
    float r = sqrt(u.x);
    float phi = 2.0 * kPi * u.y;
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - u.x, 0.0)));
}

// Per-ray random numbers: xy = hemisphere sample, z = step jitter.
vec3 RayNoise(ivec2 p, uint noiseFrame, uint rayCount, uint rayIndex)
{
    vec2 q = vec2(p);
    vec2 base = vec2(InterleavedGradientNoise(q), InterleavedGradientNoise(q.yx + vec2(5.3, 17.1)));
    float jitterBase = InterleavedGradientNoise(q * 1.37 + vec2(3.1, 5.7));
    float n = float(noiseFrame * rayCount + rayIndex);
    return vec3(fract(base + n * kR2), fract(jitterBase + n * 0.6180339887));
}
