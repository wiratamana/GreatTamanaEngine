#pragma once

#include "../../Core/FeatureKit/Texture.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace gte {

// Feature module name, render-feature scope owner and log category.
inline constexpr const char* kShadowFeatureName = "Shadow";

inline constexpr std::uint32_t kMaxShadowCascades = 4;

// Frames a depth-range sample stays usable without a newer one.
inline constexpr std::uint64_t kSdsmMaxHoldFrames = 120;

enum class ShadowDebugView : std::uint8_t { Off, CascadeColors, Mask };

enum class ShadowFrameStatus : std::uint8_t {
    NotRendered,
    Ok,
    NoValidCamera,
    ShadersNotReady,
    TargetFailed,
    PassDisabled,
};

// Shadow tunables. Stored only through SanitizeShadowSettings().
struct ShadowSettings {
    std::uint32_t cascadeCount = 4;     // 1..4
    std::uint32_t mapResolution = 2048; // Per cascade: 512, 1024, 2048 or 4096.
    float maxDistance = 150.0f;         // World units, clamped to the camera far plane.
    float splitLambda = 0.85f;          // 0 = uniform splits, 1 = logarithmic.
    float cascadeBlendFraction = 0.10f; // Of each cascade length, 0..0.5.
    float distanceFadeFraction = 0.10f; // Of maxDistance, 0..0.5.
    float casterRange = 200.0f;         // How far toward the sun a cascade still catches casters.
    bool cullCasters = true;
    float casterCullPaddingFraction = 0.25f; // Caster box padding, fraction of its largest extent.
    float constBiasTexels = 1.5f;
    float slopeBiasTexels = 1.0f;
    float normalOffsetTexels = 1.0f;
    float filterRadiusTexels = 1.5f;
    std::uint32_t filterSamples = 8; // 1, 4, 8 or 16.
    float strength = 0.6f;           // 0 = no darkening, 1 = black in shadow.
    bool stabilizeCascades = true;   // Snap cascade centers to whole texels.
    ShadowDebugView debugView = ShadowDebugView::Off;

    // Depth-range fitting of the cascade splits.
    bool useSdsm = true;
    float sdsmPaddingFraction = 0.15f; // Widens the measured range at both ends, 0..1.
    float sdsmShrinkSeconds = 0.3f;    // Time constant of the range shrink; growth is instant.
    std::uint32_t sdsmQuantizeSteps = 4; // Grid steps per octave, 0 = off.
};

// Camera data derived once per view and frame. A point at view depth z of corner i is eye + rays[i] * z.
struct ShadowViewFrustum {
    bool valid = false;
    Vec3 eye;
    Vec3 forward;
    float viewNear = 0.0f;
    float viewFar = 0.0f;
    std::array<Vec3, 4> rays{};
};

// Visible view-space depth range in world units.
struct ShadowDepthRange {
    float nearDepth = 0.0f;
    float farDepth = 0.0f;
};

// A decoded depth-range measurement and the frame it was recorded in.
struct ShadowDepthSample {
    ShadowDepthRange range;
    std::uint64_t frame = 0;
};

struct ShadowSdsmState {
    bool valid = false;
    float smoothNear = 0.0f; // Smoothed, not quantized.
    float smoothFar = 0.0f;
    ShadowDepthRange used;   // Quantized range the cascades use.
    ShadowDepthRange raw;    // Last measured range.
    std::uint64_t lastSampleFrame = 0;
};

struct ShadowCascade {
    Mat4 viewProjection;       // World to cascade clip (zero-to-one depth, flipped Y).
    float sliceStart = 0.0f;   // Camera view-depth range this cascade covers.
    float sliceEnd = 0.0f;
    float radius = 0.0f;       // Bounding sphere radius, world units.
    float texelWorldSize = 0.0f;
    float depthRange = 0.0f;   // Ortho depth span, world units.
    std::int64_t gridX = 0;    // Snapped light-space center in whole texels (0 when not stabilized).
    std::int64_t gridY = 0;
};

struct ShadowCascadeSet {
    bool valid = false;
    std::uint32_t count = 0;
    std::array<ShadowCascade, kMaxShadowCascades> cascades{};
    Vec3 cameraPosition;
    Vec3 cameraForward;
    Vec3 directionTowardSun = Vec3::Up();
    float shadowDistance = 0.0f; // Fade end: min(camera far, maxDistance).
    float fadeStart = 0.0f;
    bool sdsmActive = false;     // Splits were fitted to a measured range.
    ShadowDepthRange fittedRange;
};

// Channel "Map": cascade pass to mask pass.
struct ShadowMapData {
    std::array<fx::Texture, kMaxShadowCascades> maps{};
    std::uint32_t resolution = 0; // Size the maps really have this frame.
    ShadowCascadeSet cascades;
};

// What the panel and the HTTP endpoint read, one per view scope.
struct ShadowViewSnapshot {
    ShadowFrameStatus status = ShadowFrameStatus::NotRendered;     // Cascade stage.
    ShadowFrameStatus maskStatus = ShadowFrameStatus::NotRendered; // Mask stage.
    std::uint64_t frameIndex = 0;
    ShadowCascadeSet cascades;
    float sunElevationDegrees = 0.0f;
    std::uint32_t resolutionInUse = 0;
    float effectiveMaxDistance = 0.0f;
    std::uint64_t cascadeBytes = 0; // Memory of the cascade maps this view owns.
    ShadowSdsmState sdsm;
    std::uint64_t sdsmAgeFrames = 0;
    float deltaSeconds = 0.0f;
};

// Result of the depth reduction pass (8 bytes). Positive float bits order like the floats.
struct ShadowDepthRangeGpu {
    std::uint32_t minDepthBits = 0xFFFFFFFFu;    // Bits of the smallest view depth.
    std::uint32_t invMaxDepthBits = 0xFFFFFFFFu; // ~bits of the largest view depth.
};
static_assert(sizeof(ShadowDepthRangeGpu) == 8);
static_assert(std::is_trivially_copyable_v<ShadowDepthRangeGpu>);

// std430 block of ShadowMask.comp; member names match the shader.
struct ShadowMaskParamsGpu {
    Mat4 invViewProjection;
    Mat4 cascadeViewProjection[kMaxShadowCascades];
    float cascadeSplitFar[4] = {};    // View depth where cascade i ends.
    float cascadeBlendWidth[4] = {};  // Blend band width at the end of cascade i.
    float cascadeTexelWorld[4] = {};
    float cascadeDepthRange[4] = {};
    float cameraPositionAndFadeStart[4] = {}; // xyz eye, w fade start.
    float cameraForwardAndFadeEnd[4] = {};    // xyz forward, w fade end.
    float sunDirection[4] = {};               // xyz toward sun.
    float biasParams[4] = {};   // x const, y normal offset, z effective slope (texels).
    float filterRadiusTexels = 0.0f;
    float texelUvSize = 0.0f; // 1 / map resolution.
    std::uint32_t filterSamples = 1;
    std::uint32_t cascadeCount = 1;
};
static_assert(sizeof(ShadowMaskParamsGpu) == 464);
static_assert(std::is_trivially_copyable_v<ShadowMaskParamsGpu>);

// Push block of ShadowComposite.frag.
struct CompositeParams {
    float strength = 0.0f;
    std::uint32_t debugMode = 0; // 0 off, 1 cascade colors, 2 mask.
    std::uint32_t cascadeCount = 0;
    std::uint32_t pad = 0;
};
static_assert(sizeof(CompositeParams) == 16);
static_assert(std::is_trivially_copyable_v<CompositeParams>);

// Push block of ShadowDepthReduce.comp.
struct DepthReducePush {
    Mat4 invViewProjection;
    float eyeAndPad[4] = {};
    float forwardAndPad[4] = {};
};
static_assert(sizeof(DepthReducePush) == 96);
static_assert(std::is_trivially_copyable_v<DepthReducePush>);

} // namespace gte
