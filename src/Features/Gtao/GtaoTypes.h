#pragma once

#include "../../Core/Extent.h"
#include "../../Core/FeatureKit/ScreenSpaceView.h"
#include "../../Core/FeatureKit/Texture.h"
#include "../../Math/Mat4.h"

#include <cstdint>
#include <type_traits>

namespace gte {

// Name the feature registers under.
inline constexpr const char* kGtaoFeatureName = "Gtao";

// Largest sample radius in depth pixels; bounds the cost of close surfaces.
inline constexpr float kGtaoMaxScreenRadiusPixels = 192.0f;

enum class GtaoDebugView : std::uint8_t { Off, AoOnly };

enum class GtaoFrameStatus : std::uint8_t {
    NotRendered,
    Ok,
    NoValidCamera,
    ShadersNotReady,
    TargetFailed,
    PassDisabled,
};

// Gtao tunables. Stored only through SanitizeGtaoSettings().
struct GtaoSettings {
    float radius = 0.7f;                   // World units, 0.05..10.
    float falloffFraction = 0.6f;          // Share of the radius where occluders fade out, 0.05..1.
    std::uint32_t sliceCount = 2;          // 1..8
    std::uint32_t stepCount = 3;           // Steps per side, 1..12.
    float sampleDistributionPower = 2.0f;  // 1..4, higher puts samples closer to the pixel.
    float thinOccluderCompensation = 0.0f; // 0..0.7
    float finalPower = 1.6f;               // 0.5..4
    float minVisibility = 0.03f;           // 0..0.5
    float strength = 0.85f;                // 0..1, how much AO darkens the color.
    float maxDistance = 60.0f;             // AO fades out toward this view depth, 1..100000.
    float distanceFadeFraction = 0.25f;    // Of maxDistance, 0..0.9.
    std::uint32_t resolutionDivisor = 2;   // 1 or 2.
    bool spatialDenoise = true;
    bool temporal = true;
    float historyWeight = 0.9f;            // 0..0.98
    float depthSigma = 0.03f;              // Relative depth tolerance of the bilateral filters, 0.001..0.5.
    GtaoDebugView debugView = GtaoDebugView::Off;
};

// Channel "Result": occlusion pass to composite pass.
struct GtaoResult {
    fx::Texture ao; // x visibility, y view depth (0 = sky) per texel.
    std::uint32_t divisor = 1;
    fx::CameraBasis basis;
};

// What the panel and the endpoint read, one per view scope.
struct GtaoViewSnapshot {
    GtaoFrameStatus status = GtaoFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0;
    Extent2D aoExtent{};
    std::uint32_t divisor = 0;
    bool historyUsed = false;
    float screenRadiusAt5m = 0.0f;  // Depth pixels; helps tuning the radius.
    std::uint64_t textureBytes = 0; // AO textures used this frame.
};

// std430 block of GtaoMain.comp; member order matches the shader.
struct GtaoMainParamsGpu {
    Mat4 invViewProjection;
    float eyeAndPixelScale[4] = {};     // xyz eye, w pixel scale.
    float rightAndRadius[4] = {};       // xyz camera right, w effect radius.
    float downAndFalloffMul[4] = {};    // xyz pixel-down direction, w falloff mul.
    float forwardAndFalloffAdd[4] = {}; // xyz forward, w falloff add.
    float tuning[4] = {};               // x final power, y thin occluder, z distribution power, w min visibility.
    float distanceFade[4] = {};         // x fade start, y fade end, z max screen radius.
    std::uint32_t counts[4] = {};       // x slices, y steps, z noise frame, w divisor.
    float reserved[4] = {};
};
static_assert(sizeof(GtaoMainParamsGpu) == 192);
static_assert(std::is_trivially_copyable_v<GtaoMainParamsGpu>);

// Push block of GtaoComposite.frag; the full 128 bytes a device guarantees.
struct GtaoCompositePush {
    Mat4 invViewProjection;
    float eye[4] = {};
    float forward[4] = {};
    float strengthReservedSigma[4] = {}; // x strength, y reserved, z depth sigma.
    std::uint32_t divisorDebug[4] = {}; // x divisor, y debug mode (0 off, 1 AO only).
};
static_assert(sizeof(GtaoCompositePush) == 128);
static_assert(std::is_trivially_copyable_v<GtaoCompositePush>);

} // namespace gte
