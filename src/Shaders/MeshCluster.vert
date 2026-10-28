#version 450

// Cluster mesh vertex shader: gl_InstanceIndex is the firstInstance of the draw command, a slot in `visible`.
// Same outputs as Mesh.vert, so Mesh.frag is reused.

#include "ClusterDrawBuffers.glsl"

// Same 128-byte block as every pipeline; only viewProj is read here.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) flat out uint outClusterId;

invariant gl_Position;

void main()
{
    uvec2 entry = visible.items[gl_InstanceIndex];
    mat4 model = instances.items[entry.x].world;
    vec4 world = model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * world;
    outWorldPos = world.xyz;
    // mat3(model) is only exact for uniform scale (no inverse-transpose).
    outWorldNormal = mat3(model) * inNormal;
    outClusterId = entry.y;
}
