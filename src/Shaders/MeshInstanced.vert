#version 450

// Instanced mesh vertex shader: the model matrix comes from the per-instance buffer, indexed by gl_InstanceIndex.
// Same outputs as Mesh.vert, so Mesh.frag is reused.

// Same 128-byte block as every pipeline; only viewProj is read here.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

// Must match GpuCullingInstanceInput in CullingTypes.h (112 bytes, std430); only worldMatrix is read.
struct InstanceData {
    mat4 worldMatrix;
    vec4 aabbMin;
    vec4 aabbMax;
    uvec4 meta;
};

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer {
    InstanceData instances[];
} instanceBuffer;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec3 outWorldPos;

void main()
{
    mat4 model = instanceBuffer.instances[gl_InstanceIndex].worldMatrix;
    vec4 world = model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * world;
    outWorldPos = world.xyz;
    // mat3(model) is only exact for uniform scale (no inverse-transpose).
    outWorldNormal = mat3(model) * inNormal;
}
