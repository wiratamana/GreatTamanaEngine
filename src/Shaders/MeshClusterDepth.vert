#version 450

// Cluster mesh vertex shader for depth-only targets: position only, same expression order as Mesh.vert.
// Serves every vertex layout: only attribute 0 is read.

#include "ClusterDrawBuffers.glsl"

// Same 128-byte block as every pipeline; only viewProj is read here.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;

invariant gl_Position;

void main()
{
    uvec2 entry = visible.items[gl_InstanceIndex];
    mat4 model = instances.items[entry.x].world;
    vec4 world = model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * world;
}
