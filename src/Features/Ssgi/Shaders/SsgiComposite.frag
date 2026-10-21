#version 450
// Writes the private target: rgb = scene color plus bounce light, a = coverage. a == 0 keeps the scene color.
#include "SsgiCommon.glsl"

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 inUv;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler2D giTexture;  // rgb GI, a view depth (0 = no surface).
layout(binding = 3) uniform sampler2D ssgiNormal; // xyz world normal; shown by the normals debug view.

layout(push_constant) uniform PushConstants {
    mat4 invViewProjection;
    vec4 eye;
    vec4 forward;
    vec4 intensityAlbedo; // x intensity, y albedo luma, z albedo saturation, w depth sigma.
    uvec4 divisorDebug;   // x divisor, y debug mode (0 off, 1 GI only, 2 normals).
} pc;

// Hue from the scene color, brightness from a constant.
vec3 AlbedoGuess(vec3 scene, float luma, float saturation)
{
    float lum = dot(scene, kLumaWeights);
    vec3 chroma = lum > 1e-4 ? scene / lum : vec3(1.0);
    chroma = mix(vec3(1.0), chroma, saturation);
    chroma = clamp(chroma / max(dot(chroma, kLumaWeights), 1e-4), vec3(0.25), vec3(4.0));
    return clamp(luma * chroma, vec3(0.0), vec3(0.95));
}

// Depth-aware bilinear upsample. GI texel i sits at depth pixel i * divisor.
vec3 UpsampleGi(ivec2 q, float viewZ)
{
    ivec2 giSize = textureSize(giTexture, 0);
    vec2 t = vec2(q) / float(max(pc.divisorDebug.x, 1u));
    ivec2 base = ivec2(floor(t));
    vec2 f = fract(t);
    float tolerance = max(viewZ * pc.intensityAlbedo.w, 1e-4);
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    for (int i = 0; i < 4; ++i) {
        ivec2 o = ivec2(i & 1, i >> 1);
        vec4 a = texelFetch(giTexture, clamp(base + o, ivec2(0), giSize - ivec2(1)), 0);
        float bilinear = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
        float depthWeight = a.a > 0.0 ? exp(-abs(a.a - viewZ) / tolerance) : 0.0;
        sum += a.rgb * bilinear * depthWeight;
        weightSum += bilinear * depthWeight;
    }
    if (weightSum < 1e-3) {
        ivec2 nearest = clamp(base + ivec2(round(f)), ivec2(0), giSize - ivec2(1));
        return texelFetch(giTexture, nearest, 0).rgb;
    }
    return sum / weightSum;
}

void main()
{
    ivec2 depthSize = textureSize(sceneDepth, 0);
    ivec2 q = clamp(ivec2(inUv * vec2(depthSize)), ivec2(0), depthSize - ivec2(1));
    float depth = texelFetch(sceneDepth, q, 0).r;
    if (depth >= kEmptyDepth) {
        outColor = vec4(0.0); // No surface: keep the scene color.
        return;
    }

    if (pc.divisorDebug.y == 2u) {
        ivec2 normalSize = textureSize(ssgiNormal, 0);
        ivec2 nq = clamp(q / int(max(pc.divisorDebug.x, 1u)), ivec2(0), normalSize - ivec2(1));
        outColor = vec4(texelFetch(ssgiNormal, nq, 0).xyz * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 world = UnprojectDepth(pc.invViewProjection, q, depth, depthSize);
    float viewZ = dot(world - pc.eye.xyz, pc.forward.xyz);
    vec3 gi = UpsampleGi(q, viewZ);

    vec3 scene = texture(sceneColor, inUv).rgb;
    vec3 bounce = AlbedoGuess(scene, pc.intensityAlbedo.y, pc.intensityAlbedo.z) * gi * pc.intensityAlbedo.x;
    if (IsBad(bounce)) {
        bounce = vec3(0.0);
    }
    if (pc.divisorDebug.y == 1u) {
        outColor = vec4(bounce, 1.0);
        return;
    }
    outColor = vec4(scene + bounce, 1.0);
}
