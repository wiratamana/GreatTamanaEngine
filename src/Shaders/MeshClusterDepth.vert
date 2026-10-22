#version 450

// Cluster mesh vertex shader for depth-only targets: position only, same expression order as Mesh.vert.

// Same 128-byte block as every pipeline; only viewProj is read here.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

struct ClusterInstance {
    mat4 world;
    uvec4 info;
};

layout(std430, set = 0, binding = 0) readonly buffer VisibleBlock {
    uvec2 items[]; // x = instance index, y = cluster index.
} visible;

layout(std430, set = 0, binding = 1) readonly buffer InstancesBlock {
    ClusterInstance items[];
} instances;

layout(location = 0) in vec3 inPosition;

invariant gl_Position;

void main()
{
    uvec2 entry = visible.items[gl_InstanceIndex];
    mat4 model = instances.items[entry.x].world;
    vec4 world = model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * world;
}
