#version 450

// Fragment shader for the Atmosphere Sky Background pass (Phase 7 -
// task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md).
// For every pixel where nothing real was ever drawn this frame (see
// AtmosphereSkyBackground.vert's own header comment on the depth trick that
// guarantees this), samples this VIEW's own Sky-View LUT (Phase 5,
// AtmosphereSkyViewLut.comp) given a per-pixel camera ray reconstructed
// from `invViewProjection` - mirrors src/Shaders/SceneGrid.frag's own
// near/far-NDC-unprojection technique exactly (see that file's own
// SELF-CONSISTENCY NOTE, which applies identically here:
// AtmosphereSkyBackground.vert synthesizes gl_Position directly from a raw
// NDC full-screen triangle with no camera/projection matrix involved, so
// unprojecting THIS shader's own inNdcXY through invViewProjection
// reconstructs the correct world-space ray through the exact same physical
// pixel the real scene geometry was drawn to - no extra Y-flip needed).
//
// A simple, fixed exponential ("Filmic"-style, 1 - exp(-x * exposure))
// tonemap is applied before output - the Sky-View LUT's own raw HDR
// luminance can exceed 1.0 near the sun (see AtmosphereSkyViewLut.comp's
// own header comment/ATMOSPHERE_PHASE5_COMPLETION_REPORT.md), while this
// engine's Game/Scene View color attachments are plain, non-HDR
// UNORM/SRGB formats (Renderer::ColorFormat()) with no separate HDR
// exposure pass of their own. `kSkyExposure` is a deliberate, hardcoded
// simplification (a real, user-tunable exposure control belongs to a
// future AtmosphereSettings-driven Editor control, e.g. Phase 8/9 - see
// this phase's own completion report) - not a physically-derived constant.

layout(push_constant) uniform PushConstants {
    mat4 invViewProjection;
    float eyeHeightKm;
    float planetRadiusKm;
    float _pad0;
    float _pad1;
} pc;

layout(binding = 0) uniform sampler2D skyViewLut;

#include "AtmosphereCommon.glsl"

layout(location = 0) in vec2 inNdcXY;

layout(location = 0) out vec4 outColor;

const float kSkyExposure = 12.0;

void main()
{
    vec4 nearH = pc.invViewProjection * vec4(inNdcXY, 0.0, 1.0);
    vec4 farH = pc.invViewProjection * vec4(inNdcXY, 1.0, 1.0);
    if (abs(nearH.w) < 1e-6 || abs(farH.w) < 1e-6) {
        discard;
    }
    vec3 nearPoint = nearH.xyz / nearH.w;
    vec3 farPoint = farH.xyz / farH.w;
    vec3 viewDirection = normalize(farPoint - nearPoint);

    float viewHeightKm = max(pc.eyeHeightKm, pc.planetRadiusKm);
    ivec2 lutSize = textureSize(skyViewLut, 0);
    vec2 uv = ViewDirectionToSkyViewLutUv(viewDirection, lutSize, viewHeightKm, pc.planetRadiusKm);

    vec3 radiance = texture(skyViewLut, uv).rgb;
    vec3 tonemapped = vec3(1.0) - exp(-max(radiance, vec3(0.0)) * kSkyExposure);

    outColor = vec4(tonemapped, 1.0);
}
