#version 450

// Builds one grass blade from gl_VertexIndex and its instance; LOD0 = look.w quads, LOD1 = 2, LOD2 = 1 triangle.
#include "GrassBlade.glsl"

layout(std430, binding = 1) readonly buffer GrassInstancesBlock {
    GrassInstance items[];
} instances;

// NOTE: must match the block in Grass.frag.
layout(push_constant) uniform GrassPush {
    uint lod;
    uint instanceBase;
    uint reserved0;
    uint reserved1;
} pc;

layout(location = 0) out vec3 outNormal;   // Eye-relative world axes, not yet facing the viewer.
layout(location = 1) out vec4 outInfo;     // x = t root..tip, y = per-blade random, z = sin(tip angle), w = side -1..1.
layout(location = 2) out vec3 outPosRel;   // Eye-relative position.
layout(location = 3) flat out vec4 outGround; // rgb = terrain colour, a = ground normal . sun.
layout(location = 4) flat out float outSunVis; // Per-blade sun visibility (cascades and cloud shadow).

void main()
{
    GrassInstance inst = instances.items[pc.instanceBase + uint(gl_InstanceIndex)];
    vec2 hw = unpackHalf2x16(inst.heightWidth);
    vec4 ground = unpackUnorm4x8(inst.ground);
    float height = hw.x;
    float width = hw.y;
    float rnd = float(inst.colorRnd & 255u) * (1.0 / 255.0);
    vec3 terrainColor = vec3(float(inst.colorRnd >> 24u), float((inst.colorRnd >> 16u) & 255u), float((inst.colorRnd >> 8u) & 255u)) * (1.0 / 255.0);

    uint v = uint(gl_VertexIndex);
    v = v / 3u + v % 3u;
    uint segments = pc.lod == 0u ? uint(params.look.w) : (pc.lod == 1u ? 2u : 1u);
    float t = min(float(v >> 1u) / float(segments), 1.0);
    float side = float(v & 1u) * 2.0 - 1.0;

    // Width tapers toward the tip; LOD2 tapers to a point.
    float taper = pc.lod == 2u ? 1.0 : params.blade.z;
    float widthAtT = width * (1.0 - taper * pow(t, 1.5));
    vec3 sideAxis;
    vec3 tangent;
    vec3 position = GrassBladeSample(inst, height, ground, t, side, widthAtT, sideAxis, tangent);

    // Normal: across the blade plane, rounded over its width, leaned toward up, then pulled toward the ground normal.
    vec3 normal = normalize(cross(sideAxis, tangent));
    normal = normalize(normal + sideAxis * (side * 0.35) + vec3(0.0, 0.5, 0.0));
    vec2 gxz = ground.xy * 2.0 - 1.0;
    vec3 groundNormal = vec3(gxz.x, sqrt(max(1.0 - dot(gxz, gxz), 0.0)), gxz.y);
    normal = normalize(mix(normal, groundNormal, params.look.y));

    outNormal = normal;
    outInfo = vec4(t, rnd, ground.z, side);
    outPosRel = position;
    outGround = vec4(terrainColor, dot(groundNormal, normalize(params.sunDir.xyz)));
    outSunVis = ground.w;
    gl_Position = params.drawViewProjection * vec4(position, 1.0);
}
