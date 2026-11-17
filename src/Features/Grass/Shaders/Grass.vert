#version 450

// Builds one grass blade from gl_VertexIndex and its instance; LOD0 = look.w quads, LOD1 = 2, LOD2 = 1 triangle.
#include "GrassBlade.glsl"

layout(std430, binding = 1) readonly buffer GrassInstancesBlock {
    GrassInstance items[];
} instances;

// NOTE: must match GrassPushGpu in GrassTypes.h and GrassPush in Grass.frag (16 bytes).
layout(push_constant) uniform GrassPush {
    uint lod;
    uint instanceBase;
    uint reserved0;
    uint reserved1;
} pc;

layout(location = 0) out vec3 outNormal;       // Eye-relative world axes, not yet facing the viewer.
layout(location = 1) out vec4 outInfo;         // x = t root..tip, y = per-blade random, z = sin(tip angle), w = side -1..1.
layout(location = 2) out vec3 outPosRel;       // Eye-relative position.
layout(location = 3) flat out vec4 outGround;  // rgb = terrain colour, a = ground normal . sun.
layout(location = 4) flat out float outSunVis; // Per-blade sun visibility (cascades and cloud shadow).
layout(location = 5) out vec3 outTangent;      // Blade spine tangent for anisotropic Kajiya-Kay sheen.
layout(location = 6) flat out uint outPatch;   // Low nibble of colorRnd: 0..12 meadow/tuft tint, 13..15 blade kind.

void main()
{
    GrassInstance inst = instances.items[pc.instanceBase + uint(gl_InstanceIndex)];
    vec2 hw = unpackHalf2x16(inst.heightWidth);
    vec4 ground = unpackUnorm4x8(inst.ground);
    float height = hw.x;
    float width = hw.y;
    uint low8 = inst.colorRnd & 255u;
    float rnd = float((low8 >> 4u) & 15u) * (1.0 / 15.0);
    uint patchNibble = low8 & 15u;
    vec3 terrainColor = vec3(float(inst.colorRnd >> 24u), float((inst.colorRnd >> 16u) & 255u), float((inst.colorRnd >> 8u) & 255u)) * (1.0 / 255.0);

    uint v = uint(gl_VertexIndex);
    v = v / 3u + v % 3u;
    uint segments = pc.lod == 0u ? uint(params.look.w) : (pc.lod == 1u ? 2u : 1u);
    float t = min(float(v >> 1u) / float(segments), 1.0);
    float side = float(v & 1u) * 2.0 - 1.0;

    // Base-heavy width profile tapers toward the tip; near wildflowers thin the stalk and flare the blossom head x3.
    float taper = pc.lod == 2u ? 1.0 : params.blade.z;
    float shapeBlend = params.shapeWind.y;
    float widthAtT = GrassBladeWidth(width, taper, t, shapeBlend);
    float dRoot = length(inst.root);
    if (patchNibble == kGrassKindFlower && pc.lod == 0u) {
        float flowerVis = 1.0 - smoothstep(0.65 * params.lodDist.x, 0.95 * params.lodDist.x, dRoot);
        float stalkW = width * 0.45 * (1.0 - 0.35 * t);
        float flowerW = mix(stalkW, stalkW * 3.0, smoothstep(0.85, 1.0, t));
        widthAtT = mix(widthAtT, flowerW, flowerVis);
    }

    // Evaluate twisted spine (twist on LOD0 and LOD1 only to keep distant vertex ALU minimal).
    float twistStrength = pc.lod < 2u ? shapeBlend : 0.0;
    vec3 sideAxis;
    vec3 tangent;
    vec3 spine = GrassBladeSpine(inst, height, ground, t, rnd, twistStrength, sideAxis, tangent);

    // Clamp blade width to at least minPixelWidth screen pixels (lodFar.w = metres per metre of distance) so distant blades do not shimmer.
    widthAtT = max(widthAtT, params.lodFar.w * dRoot);
    if (pc.lod == 2u && v == 2u) {
        side = 0.0;
    }
    vec3 position = spine + sideAxis * (side * 0.5 * widthAtT);

    // Normal: rounded across the twisted blade width, leaned toward up, then blended toward ground normal with distance.
    vec3 normal = normalize(cross(sideAxis, tangent) + sideAxis * (side * 0.35) + vec3(0.0, 0.5, 0.0));
    vec2 gxz = ground.xy * 2.0 - 1.0;
    vec3 groundNormal = vec3(gxz.x, sqrt(max(1.0 - dot(gxz, gxz), 0.0)), gxz.y);
    float toGround = mix(params.look.y, max(params.look.y, 0.92), smoothstep(12.0, 70.0, dRoot) * params.variety2.y);
    normal = mix(normal, groundNormal, toGround); // Grass.frag renormalises.

    outNormal = normal;
    outInfo = vec4(t, rnd, ground.z, side);
    outPosRel = position;
    outGround = vec4(terrainColor, dot(groundNormal, normalize(params.sunDir.xyz)));
    outSunVis = ground.w;
    outTangent = tangent;
    outPatch = patchNibble;
    gl_Position = params.drawViewProjection * vec4(position, 1.0);
}
