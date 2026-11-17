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
    // Skip secondary LOD cross-fade copies so each blade casts at most one shadow triangle per cascade.
    float rootDist = length(inst.root);
    uint primaryLod = rootDist < params.lodDist.x ? 0u : (rootDist < params.lodDist.y ? 1u : 2u);
    if (primaryLod != pc.lod) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    uint v = uint(gl_VertexIndex);
    float t = v == 2u ? 1.0 : 0.0;
    float side = v == 0u ? -1.0 : (v == 1u ? 1.0 : 0.0);
    float rawWidth = unpackHalf2x16(inst.heightWidth).y;
    float widthAtT = GrassBladeWidth(rawWidth, 1.0, t, params.shapeWind.y);
    if (v < 2u) {
        widthAtT = max(widthAtT, pc.minWidth);
    }
    vec3 position = GrassBladePosition(inst, t, side, widthAtT);
    vec3 sunDir = length(params.sunDir.xyz) > 1e-6 ? normalize(params.sunDir.xyz) : vec3(0.0, 1.0, 0.0);
    position += sunDir * pc.biasM;
    gl_Position = pc.cascadeVpRel * vec4(position, 1.0);
}
