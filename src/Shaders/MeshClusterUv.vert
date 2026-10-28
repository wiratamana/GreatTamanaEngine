#version 450

// Cluster vertex shader for position + normal + uv meshes: writes the varyings of TexturedMesh.vert, so the
// textured fragment shader is reused. The cluster id feeds the debug tint.

#include "ClusterDrawBuffers.glsl"

// Same 128-byte block as every pipeline; only viewProj is read here.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outUv;
layout(location = 2) out vec3 outWorldPos;
layout(location = 5) flat out uint outClusterId;

invariant gl_Position;

void main()
{
    uvec2 entry = visible.items[gl_InstanceIndex];
    mat4 model = instances.items[entry.x].world;
    vec4 world = model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * world;
    outWorldPos = world.xyz;
    // mat3(model) is exact only for rotation and uniform scale; no inverse-transpose.
    outWorldNormal = mat3(model) * inNormal;
    outUv = inUv;
    outClusterId = entry.y;
}
