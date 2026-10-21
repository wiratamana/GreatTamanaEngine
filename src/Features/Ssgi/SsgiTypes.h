#pragma once

#include "../../Core/Extent.h"
#include "../../Core/FeatureKit/ScreenSpaceView.h"
#include "../../Core/FeatureKit/Texture.h"
#include "../../Math/Mat4.h"

#include <cstdint>
#include <type_traits>

namespace gte {

// Name the feature registers under.
inline constexpr const char* kSsgiFeatureName = "Ssgi";

// Minimum ray-origin offset, in GI texel footprints; absorbs depth storage precision.
inline constexpr float kSsgiFootprintBiasScale = 1.5f;

enum class SsgiDebugView : std::uint8_t { Off, GiOnly, Normals };

enum class SsgiFrameStatus : std::uint8_t {
    NotRendered,
    Ok,
    NoValidCamera,
    ShadersNotReady,
    TargetFailed,
    PassDisabled,
};

// Ssgi tunables. Stored only through SanitizeSsgiSettings().
struct SsgiSettings {
    float intensity = 1.0f;              // 0..4, multiplier of the added bounce. 0 turns the feature off.
    float maxRayDistance = 4.0f;         // 0.1..100, world units a ray may travel.
    float thickness = 0.5f;              // 0.01..5, depth band that counts as a hit.
    float depthBias = 0.03f;             // 0..1, minimum ray origin offset along the normal.
    std::uint32_t rayCount = 2;          // 1..8, rays per pixel per frame.
    std::uint32_t stepCount = 12;        // 4..48, steps per ray.
    float stepDistributionPower = 2.0f;  // 1..4, higher puts steps closer to the pixel.
    float endFadeFraction = 0.3f;        // 0..0.9, hit weight fades over this share of the ray end.
    float maxRadiance = 8.0f;            // 0.5..64, luminance cap of a light source.
    float albedoLuma = 0.5f;             // 0..0.95, assumed receiver brightness.
    float albedoSaturation = 0.7f;       // 0..1, how much of the scene hue is kept.
    float maxDistance = 80.0f;           // 1..100000, GI fades toward this view depth.
    float distanceFadeFraction = 0.25f;  // Of maxDistance, 0..0.9.
    std::uint32_t resolutionDivisor = 2; // 1 or 2.
    bool spatialDenoise = true;
    bool temporal = true;
    float historyWeight = 0.92f;         // 0..0.98
    float depthSigma = 0.03f;            // Relative depth tolerance of the bilateral filters, 0.001..0.5.
    bool applyInSceneView = true;        // false skips the Editor Scene view.
    SsgiDebugView debugView = SsgiDebugView::Off;
};

// Channel "Result": trace pass to composite pass.
struct SsgiResult {
    fx::Texture gi;     // rgb GI, a view depth (0 = no surface) per texel.
    fx::Texture normal; // xyz world normal per texel.
    std::uint32_t divisor = 1;
    fx::CameraBasis basis;
};

// What the panel and the endpoint read, one per view scope.
struct SsgiViewSnapshot {
    SsgiFrameStatus status = SsgiFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0;
    Extent2D giExtent{};
    std::uint32_t divisor = 0;
    bool historyUsed = false;
    std::uint64_t textureBytes = 0; // Ssgi textures used this frame.
};

// std430 block of SsgiPrepare.comp; member order matches the shader.
struct SsgiPrepareParamsGpu {
    Mat4 invViewProjection;
    float eye[4] = {};            // xyz eye.
    float forward[4] = {};        // xyz forward.
    float tuning[4] = {};         // x max radiance.
    std::uint32_t counts[4] = {}; // x divisor.
};
static_assert(sizeof(SsgiPrepareParamsGpu) == 128);
static_assert(std::is_trivially_copyable_v<SsgiPrepareParamsGpu>);

// std430 block of SsgiTrace.comp. The trace works eye-relative, so no eye is sent.
struct SsgiTraceParamsGpu {
    float rightAndMaxDistance[4] = {}; // xyz right, w max ray distance.
    float downAndThickness[4] = {};    // xyz down, w thickness.
    float forwardAndBias[4] = {};      // xyz forward, w depth bias.
    float viewAndFade[4] = {};         // xy full-res size, z fade start, w fade end.
    float tuning[4] = {};              // x distribution power, y end fade fraction, z pixel scale.
    std::uint32_t counts[4] = {};      // x rays, y steps, z noise frame, w divisor.
};
static_assert(sizeof(SsgiTraceParamsGpu) == 96);
static_assert(std::is_trivially_copyable_v<SsgiTraceParamsGpu>);

// Push block of SsgiComposite.frag; the full 128 bytes a device guarantees.
struct SsgiCompositePush {
    Mat4 invViewProjection;
    float eye[4] = {};
    float forward[4] = {};
    float intensityAlbedo[4] = {};      // x intensity, y albedo luma, z albedo saturation, w depth sigma.
    std::uint32_t divisorDebug[4] = {}; // x divisor, y debug mode (0 off, 1 GI only, 2 normals).
};
static_assert(sizeof(SsgiCompositePush) == 128);
static_assert(std::is_trivially_copyable_v<SsgiCompositePush>);

} // namespace gte
