#version 450
// Writes the private target: rgb = scene color, a = coverage. A pixel with a == 0 keeps the scene color.
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D shadowMask;
layout(binding = 2) uniform sampler2D cascadeId; // (cascade index + 1) / 8, 0 = none.
layout(location = 0) in vec2 inUv;

layout(push_constant) uniform PushConstants {
    float strength;
    uint debugMode; // 0 off, 1 cascade colors, 2 mask.
    uint cascadeCount;
    uint pad;
} pc;

const vec3 kCascadeColors[4] = vec3[4](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 1.0, 0.0));

void main()
{
    vec3 scene = texture(sceneColor, inUv).rgb;
    float mask = texture(shadowMask, inUv).r;
    int id = int(round(texture(cascadeId, inUv).r * 8.0));

    if (pc.debugMode == 1u) {
        int last = int(clamp(pc.cascadeCount, 1u, 4u)) - 1;
        outColor = id > 0 ? vec4(mix(scene, kCascadeColors[min(id - 1, last)], 0.5), 1.0) : vec4(0.0);
    } else if (pc.debugMode == 2u) {
        outColor = id > 0 ? vec4(vec3(mask), 1.0) : vec4(0.0);
    } else {
        outColor = vec4(scene * (1.0 - pc.strength * mask), mask);
    }
}
