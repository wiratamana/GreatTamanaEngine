#version 450
// Copies display-ready color into the swapchain image.
layout(binding = 0) uniform sampler2D sceneColor;
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(texture(sceneColor, inUv).rgb, 1.0);
}
