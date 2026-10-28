#version 450

// Cluster vertex shader for position + normal + uv + tangent meshes: writes the varyings of PbrMesh.vert, so the
// PBR fragment shader is reused. The cluster id feeds the debug tint.

#include "ClusterDrawBuffers.glsl"

// Same 128-byte block as every pipeline; viewProj is read here and the first word of `model` is the draw tag.
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outUv;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec4 outWorldTangent;
layout(location = 4) flat out float outWindingSign;
layout(location = 5) flat out uint outClusterId;
layout(location = 6) flat out uint outDrawTag;

invariant gl_Position;

void main()
{
    uvec2 entry = visible.items[gl_InstanceIndex];
    mat4 model = instances.items[entry.x].world;
    vec4 world = model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * world;
    outWorldPos = world.xyz;

    mat3 m = mat3(model);
    // The cofactor matrix is determinant * inverse-transpose: exact for any scale.
    mat3 cofactor = mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
    float windingSign = determinant(m) < 0.0 ? -1.0 : 1.0;   // A mirroring matrix flips winding and bitangent.
    outWorldNormal = cofactor * inNormal * windingSign;
    outWorldTangent = vec4(m * inTangent.xyz, inTangent.w * windingSign);
    outWindingSign = windingSign;
    outUv = inUv;
    outClusterId = entry.y;
    outDrawTag = floatBitsToUint(pc.model[0][0]);
}
