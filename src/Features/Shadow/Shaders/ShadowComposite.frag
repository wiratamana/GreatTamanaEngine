#version 450
// Writes privateTarget: RGB = scene color darkened by (1 - strength * mask),
// A = mask factor. A must be exactly 0 for an untouched pixel (see
// RenderFeatureBlend.comp's ScreenSpaceMask branch).
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D shadowMask;
layout(location = 0) in vec2 inUv;

layout(push_constant) uniform PushConstants {
    float strength;
} pc;

void main()
{
    float mask = texture(shadowMask, inUv).r;
    vec3 darkened = texture(sceneColor, inUv).rgb * (1.0 - pc.strength * mask);
    outColor = vec4(darkened, mask);
}
