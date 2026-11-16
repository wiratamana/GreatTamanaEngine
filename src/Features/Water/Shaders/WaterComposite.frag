#version 450
// Copies the shaded water into the scene color and its depth into the scene depth. Alpha 0 keeps the scene pixel.

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 inUv;

layout(binding = 0) uniform sampler2D waterColor; // rgb radiance, a coverage.
layout(binding = 1) uniform sampler2D waterDepth; // Window depth of the surface.

void main()
{
    ivec2 q = clamp(ivec2(gl_FragCoord.xy), ivec2(0), textureSize(waterColor, 0) - ivec2(1));
    vec4 water = texelFetch(waterColor, q, 0);
    if (water.a <= 0.0) {
        discard;
    }
    gl_FragDepth = texelFetch(waterDepth, q, 0).r;
    outColor = vec4(water.rgb, 1.0);
}
