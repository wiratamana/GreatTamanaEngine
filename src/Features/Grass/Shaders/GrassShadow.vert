#version 450

// Builds a 3-vertex curved shadow blade (root-left, root-right, tip) for depth-only sun cascade draws.
#include "GrassBlade.glsl"

layout(std430, binding = 1) readonly buffer GrassInstancesBlock {
    GrassInstance items[];
} instances;

// NOTE: must match GrassShadowPushGpu in GrassTypes.h (80 bytes).
layout(push_constant) uniform GrassShadowPush {
    mat4 cascadeVpRel;
    uint lod;
    uint instanceBase;
    float minWidth;
    float biasM;
} pc;

void main()
{
    GrassInstance inst = instances.items[pc.instanceBase + uint(gl_InstanceIndex)];
    uint v = uint(gl_VertexIndex);
    float t = v == 2u ? 1.0 : 0.0;
    float side = v == 0u ? -1.0 : 1.0;
    float width = max(unpackHalf2x16(inst.heightWidth).y, pc.minWidth);
    vec3 position = GrassBladePosition(inst, t, side, width * (1.0 - t));
    vec3 sunDir = length(params.sunDir.xyz) > 1e-6 ? normalize(params.sunDir.xyz) : vec3(0.0, 1.0, 0.0);
    position += sunDir * pc.biasM;
    gl_Position = pc.cascadeVpRel * vec4(position, 1.0);
}
