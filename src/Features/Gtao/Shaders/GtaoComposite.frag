#version 450
// Writes the private target: rgb = linear scene color with AO applied, a = coverage. a == 0 keeps the scene color.
layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 inUv;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D sceneDepth;
layout(binding = 2) uniform sampler2D aoTexture; // x visibility, y view depth (0 = sky).

layout(push_constant) uniform PushConstants {
    mat4 invViewProjection;
    vec4 eye;
    vec4 forward;
    vec4 strengthReservedSigma; // x strength, y reserved, z depth sigma.
    uvec4 divisorDebug;         // x divisor, y debug mode (0 off, 1 AO only).
} pc;

const float kSkyDepth = 0.999999;

// Depth-aware bilinear upsample. AO texel i sits at depth pixel i * divisor.
float UpsampleAo(ivec2 q, float viewZ)
{
    ivec2 aoSize = textureSize(aoTexture, 0);
    vec2 t = vec2(q) / float(max(pc.divisorDebug.x, 1u));
    ivec2 base = ivec2(floor(t));
    vec2 f = fract(t);
    float tolerance = max(viewZ * pc.strengthReservedSigma.z, 1e-4);
    float sum = 0.0;
    float weightSum = 0.0;
    for (int i = 0; i < 4; ++i) {
        ivec2 o = ivec2(i & 1, i >> 1);
        vec2 a = texelFetch(aoTexture, clamp(base + o, ivec2(0), aoSize - ivec2(1)), 0).rg;
        float bilinear = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
        float depthWeight = a.y > 0.0 ? exp(-abs(a.y - viewZ) / tolerance) : 0.0;
        sum += a.x * bilinear * depthWeight;
        weightSum += bilinear * depthWeight;
    }
    if (weightSum < 1e-3) {
        ivec2 nearest = clamp(base + ivec2(round(f)), ivec2(0), aoSize - ivec2(1));
        return texelFetch(aoTexture, nearest, 0).r;
    }
    return sum / weightSum;
}

void main()
{
    ivec2 depthSize = textureSize(sceneDepth, 0);
    ivec2 q = clamp(ivec2(inUv * vec2(depthSize)), ivec2(0), depthSize - ivec2(1));
    float depth = texelFetch(sceneDepth, q, 0).r;
    if (depth >= kSkyDepth) {
        outColor = vec4(0.0);
        return;
    }

    vec4 world = pc.invViewProjection * vec4((vec2(q) + 0.5) / vec2(depthSize) * 2.0 - 1.0, depth, 1.0);
    float viewZ = dot(world.xyz / world.w - pc.eye.xyz, pc.forward.xyz);
    float ao = UpsampleAo(q, viewZ);

    if (pc.divisorDebug.y == 1u) {
        outColor = vec4(vec3(ao), 1.0);
        return;
    }

    float factor = mix(1.0, ao, pc.strengthReservedSigma.x);
    vec3 scene = texture(sceneColor, inUv).rgb;
    if (factor > 0.9999) {
        outColor = vec4(scene, 1.0); // Unoccluded: scene color stays unchanged.
        return;
    }
    outColor = vec4(scene * factor, 1.0);
}
