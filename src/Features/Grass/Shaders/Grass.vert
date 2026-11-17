#version 450

// Builds one grass blade from gl_VertexIndex and its instance. No vertex input; the cull already did the wind maths.
// Triangle list: list vertex v maps to strip vertex v / 3 + v % 3 (side = i & 1, row = i >> 1).
// LOD0 = look.w segments, LOD1 = 2, LOD2 = one triangle (tip width 0). One draw per LOD with firstInstance 0.
#include "GrassCommon.glsl"

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
    vec4 frame = unpackSnorm4x8(inst.frame);
    vec4 ground = unpackUnorm4x8(inst.ground);
    vec2 flutter = unpackHalf2x16(inst.flutter);
    float height = hw.x;
    float width = hw.y;
    vec2 faceXz = normalize(frame.xy);
    vec2 bendDir = normalize(frame.zw);
    float rnd = float(inst.colorRnd & 255u) * (1.0 / 255.0);
    vec3 terrainColor = vec3(float(inst.colorRnd >> 24u), float((inst.colorRnd >> 16u) & 255u), float((inst.colorRnd >> 8u) & 255u)) * (1.0 / 255.0);

    uint v = uint(gl_VertexIndex);
    v = v / 3u + v % 3u;
    uint segments = pc.lod == 0u ? uint(params.look.w) : (pc.lod == 1u ? 2u : 1u);
    float t = min(float(v >> 1u) / float(segments), 1.0);
    float side = float(v & 1u) * 2.0 - 1.0;

    // Blade frame on the ground: side axis across the blade (yaw = atan(sin, cos) of faceXz).
    vec3 sideAxis = vec3(faceXz.y, 0.0, -faceXz.x);
    vec3 bendAxis = vec3(bendDir.x, 0.0, bendDir.y);

    // Quadratic curve root -> tip: control point straight above the root keeps the base upright.
    float sinA = ground.z;
    float cosA = sqrt(max(1.0 - sinA * sinA, 0.0));
    vec3 control = vec3(0.0, 0.5 * height, 0.0);
    vec3 tip = vec3(0.0, height * cosA, 0.0) + bendAxis * (height * sinA);
    float omt = 1.0 - t;
    vec3 curve = 2.0 * omt * t * control + t * t * tip;
    vec3 tangent = normalize((2.0 - 4.0 * t) * control + 2.0 * t * tip + vec3(0.0, 1e-4, 0.0));

    curve += (sideAxis * flutter.x + bendAxis * flutter.y) * (t * t);

    // Width tapers toward the tip; LOD2 tapers to a point.
    float taper = pc.lod == 2u ? 1.0 : params.blade.z;
    float widthAtT = width * (1.0 - taper * pow(t, 1.5));
    vec3 position = inst.root + curve + sideAxis * (side * 0.5 * widthAtT);

    // Normal: across the blade plane, rounded over its width, leaned toward up, then pulled toward the ground normal.
    vec3 normal = normalize(cross(sideAxis, tangent));
    normal = normalize(normal + sideAxis * (side * 0.35) + vec3(0.0, 0.5, 0.0));
    vec2 gxz = ground.xy * 2.0 - 1.0;
    vec3 groundNormal = vec3(gxz.x, sqrt(max(1.0 - dot(gxz, gxz), 0.0)), gxz.y);
    normal = normalize(mix(normal, groundNormal, params.look.y));

    outNormal = normal;
    outInfo = vec4(t, rnd, sinA, side);
    outPosRel = position;
    outGround = vec4(terrainColor, dot(groundNormal, normalize(params.sunDir.xyz)));
    outSunVis = ground.w;
    gl_Position = params.drawViewProjection * vec4(position, 1.0);
}
