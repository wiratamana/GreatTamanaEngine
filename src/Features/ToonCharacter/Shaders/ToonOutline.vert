#version 450

// Toon outline hull: vertices move along the clip-space normal by a constant pixel width.
// Parts without an edge are moved outside the clip volume.

#define DIRECTIONAL_LIGHTING_NO_SHADOWS
#include "DirectionalLighting.glsl"
#include "MaterialParams.glsl"
#include "ToonCommon.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec2 outUv;
layout(location = 1) flat out int outClass;

void main()
{
    int cls = ToonClassOf(uMaterial.pbr.y);
    outUv = inUv;
    outClass = cls;
    if (uMaterial.pbr.w < 0.5 || ToonClassHasNoOutline(cls)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    ToonLookParams look = ReadToonLook();
    vec4 clip = pc.viewProj * (pc.model * vec4(inPosition, 1.0));
    vec2 dir = (mat3(pc.viewProj) * (mat3(pc.model) * inNormal)).xy;
    float len = length(dir);
    dir = (len > 1e-5) ? dir / len : vec2(0.0);

    // One NDC unit of height is outlineNdcY; width is scaled by the aspect so the line is isotropic in pixels.
    vec2 widthNdc = vec2(look.outlineNdcY / look.viewportAspect, look.outlineNdcY) * uMaterial.pbr.z;
    clip.xy += dir * widthNdc * clip.w;
    gl_Position = clip;
}
