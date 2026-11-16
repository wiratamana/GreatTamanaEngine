#version 450

// Builds one grass blade from gl_VertexIndex and its instance. No vertex input.
// Strip order: vertex i -> side = i & 1, row = i >> 1. LOD0 = 5 segments, LOD1 = 2, LOD2 = one triangle (tip width 0).
// Run as one draw per LOD with firstInstance 0; pc.instanceBase is that LOD's region start.
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
layout(location = 1) out vec4 outInfo;     // x = t root..tip, y = per-blade random, z = sway angle, w = side -1..1.
layout(location = 2) out vec3 outPosRel;   // Eye-relative position.

void main()
{
    GrassInstance inst = instances.items[pc.instanceBase + uint(gl_InstanceIndex)];
    vec3 root = inst.posRot.xyz;
    float yaw = inst.posRot.w;
    float height = inst.shape.x;
    float width = inst.shape.y;
    float lean = inst.shape.z;
    float rnd = inst.shape.w;

    // Strip vertex index; a triangle list (kGrassFlagTriangleList) maps list vertex v to strip vertex tri + corner.
    uint v = uint(gl_VertexIndex);
    if ((uint(params.grid.w) & kGrassFlagTriangleList) != 0u) {
        v = v / 3u + v % 3u;
    }
    uint segments = pc.lod == 0u ? 5u : (pc.lod == 1u ? 2u : 1u);
    float t = min(float(v >> 1u) / float(segments), 1.0);
    float side = float(v & 1u) * 2.0 - 1.0;

    // Blade frame on the ground: side axis across the blade, face axis along its normal.
    float sy = sin(yaw);
    float cy = cos(yaw);
    vec3 sideAxis = vec3(cy, 0.0, -sy);
    vec3 faceAxis = vec3(sy, 0.0, cy);

    // Wind: slow wave plus gusts, both read at a pattern position that drifts with the shared wind offset.
    vec2 pattern = params.cameraWorld.xy + root.xz + params.wind1.yz;
    vec2 windDir = params.wind0.xy;
    float speedFactor = 1.0 - exp(-0.25 * params.wind0.z);
    float wave = 0.5 + 0.5 * sin(dot(pattern, windDir) * (6.2831853 / max(params.wind2.z, 1.0)));
    float gust = params.wind1.x * GrassValueNoise(pattern / max(params.wind2.w, 1.0));
    float windAngle = 0.6 * params.wind1.w * speedFactor * (0.25 + 0.5 * wave + 0.9 * gust);

    // Lean along the blade face plus the wind push; the sum sets the tip angle and the bend direction.
    vec2 bend = faceAxis.xz * lean + windDir * windAngle;
    float bendLength = length(bend);
    float angle = min(bendLength, params.bladeB.w);
    vec2 bendDir = bendLength > 1e-4 ? bend / bendLength : faceAxis.xz;
    vec3 bendAxis = vec3(bendDir.x, 0.0, bendDir.y);

    // Quadratic curve root -> tip: control point straight above the root keeps the base upright.
    vec3 control = vec3(0.0, 0.5 * height, 0.0);
    vec3 tip = vec3(0.0, height * cos(angle), 0.0) + bendAxis * (height * sin(angle));
    float omt = 1.0 - t;
    vec3 curve = 2.0 * omt * t * control + t * t * tip;
    vec3 tangent = normalize((2.0 - 4.0 * t) * control + 2.0 * t * tip + vec3(0.0, 1e-4, 0.0));

    // Tip flutter: fast, small, growing with t^2.
    float phase = rnd * 43.9823;
    float flutterTime = params.wind0.w * params.wind2.x;
    float flutterA = sin(flutterTime + phase + dot(pattern, vec2(0.9, 0.7)));
    float flutterB = sin(flutterTime * 1.37 + phase * 1.7);
    float flutterAmp = params.wind2.y * height * t * t * (0.04 + 0.12 * speedFactor * (0.5 + gust));
    curve += (sideAxis * flutterA + bendAxis * (0.6 * flutterB)) * flutterAmp;

    // Width tapers toward the tip; LOD2 tapers to a point.
    float taper = pc.lod == 2u ? 1.0 : params.blade.z;
    float widthAtT = width * (1.0 - taper * pow(t, 1.5));
    vec3 position = root + curve + sideAxis * (side * 0.5 * widthAtT);

    // Normal: across the blade plane, rounded over its width and leaned toward up so grass reads like the ground.
    vec3 normal = normalize(cross(sideAxis, tangent));
    normal = normalize(normal + sideAxis * (side * 0.35) + vec3(0.0, 0.5, 0.0));

    outNormal = normal;
    outInfo = vec4(t, rnd, windAngle, side);
    outPosRel = position;
    gl_Position = params.drawViewProjection * vec4(position, 1.0);
}
