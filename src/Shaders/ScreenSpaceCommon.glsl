// Helpers shared by screen-space compute and fragment shaders. Depth 1.0 means "no surface".
// Include once per shader, directly or through another include.
#include "InterleavedGradientNoise.glsl"
#include "SceneDepthConstants.glsl"

const float kPi = 3.14159265359;

float FetchDepth(sampler2D depthTex, ivec2 p)
{
    return texelFetch(depthTex, clamp(p, ivec2(0), textureSize(depthTex, 0) - ivec2(1)), 0).r;
}

// World position of depth pixel `p`; `size` is the depth texture size.
vec3 UnprojectDepth(mat4 invViewProjection, ivec2 p, float depth, ivec2 size)
{
    vec2 uv = (vec2(p) + 0.5) / vec2(size);
    vec4 world = invViewProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return world.xyz / world.w;
}

// Geometric normal from depth, using the neighbour with the smaller depth step on each axis.
// An empty neighbour is never chosen; with no usable neighbour the view direction is returned.
vec3 NormalFromDepth(sampler2D depthTex, mat4 invViewProjection, ivec2 p, float depth, vec3 position, vec3 eye)
{
    ivec2 size = textureSize(depthTex, 0);
    vec3 toEye = normalize(eye - position);
    float dxp = FetchDepth(depthTex, p + ivec2(1, 0));
    float dxn = FetchDepth(depthTex, p - ivec2(1, 0));
    float dyp = FetchDepth(depthTex, p + ivec2(0, 1));
    float dyn = FetchDepth(depthTex, p - ivec2(0, 1));
    float errXp = dxp >= kClearDepthThreshold ? 1e6 : abs(dxp - depth);
    float errXn = dxn >= kClearDepthThreshold ? 1e6 : abs(depth - dxn);
    float errYp = dyp >= kClearDepthThreshold ? 1e6 : abs(dyp - depth);
    float errYn = dyn >= kClearDepthThreshold ? 1e6 : abs(depth - dyn);
    if (min(errXp, errXn) > 1e5 || min(errYp, errYn) > 1e5) {
        return toEye;
    }

    vec3 dx = errXp < errXn ? UnprojectDepth(invViewProjection, p + ivec2(1, 0), dxp, size) - position
                            : position - UnprojectDepth(invViewProjection, p - ivec2(1, 0), dxn, size);
    vec3 dy = errYp < errYn ? UnprojectDepth(invViewProjection, p + ivec2(0, 1), dyp, size) - position
                            : position - UnprojectDepth(invViewProjection, p - ivec2(0, 1), dyn, size);

    // Normalize before the cross product: two tiny vectors must not hit a fixed epsilon.
    float lx = length(dx);
    float ly = length(dy);
    if (lx < 1e-7 || ly < 1e-7) {
        return toEye;
    }
    vec3 n = cross(dx / lx, dy / ly);
    float len = length(n);
    if (len < 1e-3) {
        return toEye;
    }
    n /= len;
    return dot(n, toEye) < 0.0 ? -n : n;
}

// Where the world point seen by texel `p` was in the previous frame, in history texel space.
// False when the point is behind the previous camera. `prevZ` is its previous view depth.
bool ReprojectToHistory(mat4 invViewProjection, mat4 prevViewProjection, vec3 eye, vec3 forward, vec3 prevEye,
    vec3 prevForward, ivec2 p, float divisor, vec2 depthSize, float viewZ, out vec2 historyTexel, out float prevZ)
{
    vec2 ndc = ((vec2(p) * divisor) + 0.5) / depthSize * 2.0 - 1.0;
    vec4 mid = invViewProjection * vec4(ndc, 0.5, 1.0);
    vec3 rayDir = mid.xyz / mid.w - eye;
    vec3 world = eye + rayDir * (viewZ / max(dot(rayDir, forward), 1e-4));

    vec4 prevClip = prevViewProjection * vec4(world, 1.0);
    prevZ = dot(world - prevEye, prevForward);
    historyTexel = vec2(0.0);
    if (prevClip.w <= 1e-4 || prevZ <= 1e-4) {
        return false;
    }
    vec2 prevUv = prevClip.xy / prevClip.w * 0.5 + 0.5;
    historyTexel = (prevUv * depthSize - 0.5) / divisor;
    return true;
}
