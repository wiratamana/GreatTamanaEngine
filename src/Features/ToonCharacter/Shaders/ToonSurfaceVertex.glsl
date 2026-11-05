// Vertex stage of the toon surface and overlay passes. Include after DirectionalLighting, MaterialParams, ToonCommon.
// Per-pass defines: *_OVERLAY_CLASS clips one class group; TOON_DEPTH_NUDGE pulls depth toward the camera.

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec2 outUv;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec3 outHeadForward;
layout(location = 4) out float outModelScale;

invariant gl_Position;

void main()
{
    bool overlayClass = ToonClassOf(uMaterial.pbr.y) == kToonHairOverlay;
#if defined(TOON_SKIP_OVERLAY_CLASS)
    if (overlayClass) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
#elif defined(TOON_KEEP_ONLY_OVERLAY_CLASS)
    if (!overlayClass) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
#endif
    vec4 world = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProj * world;
#ifdef TOON_DEPTH_NUDGE
    gl_Position.z -= TOON_DEPTH_NUDGE * gl_Position.w;
#endif
    outWorldPos = world.xyz;
    // mat3(model) is exact only for rotation and uniform scale.
    outWorldNormal = mat3(pc.model) * inNormal;
    outUv = inUv;
    outModelScale = length(pc.model[0].xyz); // World size of one model unit, for the shadow reach.
    // The model front is -Z in model space.
    outHeadForward = normalize(mat3(pc.model) * vec3(0.0, 0.0, -1.0));
}
