#pragma once

#include "../../Core/Extent.h"
#include "../../Core/FeatureKit/EnumTable.h"
#include "../../Core/FeatureKit/ScreenSpaceView.h"
#include "../../Math/Mat4.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace gte {

// Name the feature registers under.
inline constexpr const char* kTemporalAaFeatureName = "TemporalAa";

inline constexpr std::uint32_t kTemporalAaMinSamples = 2;
inline constexpr std::uint32_t kTemporalAaMaxSamples = 16;
inline constexpr std::uint32_t kTemporalAaMinViewSide = 8; // Smaller views are not resolved.
inline constexpr float kTemporalAaLumaFloor = 0.2f;        // Same constant as the shader's feedback weight.

enum class TemporalAaDebugView : std::uint8_t { Off, JitterOnly }; // JitterOnly: jitter on, history ignored.

inline constexpr std::array kTemporalAaDebugViewTable{
    fx::EnumEntry<TemporalAaDebugView>{ TemporalAaDebugView::Off, "Off" },
    fx::EnumEntry<TemporalAaDebugView>{ TemporalAaDebugView::JitterOnly, "JitterOnly" },
};

// Tunables. Stored only through SanitizeTemporalAaSettings().
struct TemporalAaSettings {
    bool enabled = true;
    std::uint32_t jitterSampleCount = 8; // 2..16, length of the Halton cycle.
    float jitterScale = 1.0f;            // 0..8. 1 = +/-0.5 pixel. 0 = no jitter. Above 1 is for debugging only.
    float feedbackMin = 0.7f;            // 0..0.99, history share when current and history differ a lot.
    float feedbackMax = 0.9f;            // feedbackMin..0.99, history share when they agree.
    float clipGamma = 0.75f;             // 0.25..4, size of the color clip box in sigmas.
    float depthTolerance = 0.03f;        // 0.001..0.5, relative view depth tolerance of the history depth check.
    TemporalAaDebugView debugView = TemporalAaDebugView::Off;
};

// What the resolve pass reported.
enum class TemporalAaFrameStatus : std::uint8_t {
    NotRendered, Ok, Disabled, PassDisabled, ShadersNotReady, TargetFailed,
    NoValidCamera, ExtentTooSmall, ViewNotSupported, CaptureArmed };

inline constexpr std::array kTemporalAaFrameStatusTable{
    fx::EnumEntry<TemporalAaFrameStatus>{ TemporalAaFrameStatus::NotRendered, "NotRendered" },
    fx::EnumEntry<TemporalAaFrameStatus>{ TemporalAaFrameStatus::Ok, "Ok" },
    fx::EnumEntry<TemporalAaFrameStatus>{ TemporalAaFrameStatus::Disabled, "Disabled" },
    fx::EnumEntry<TemporalAaFrameStatus>{ TemporalAaFrameStatus::PassDisabled, "PassDisabled" },
    fx::EnumEntry<TemporalAaFrameStatus>{ TemporalAaFrameStatus::ShadersNotReady, "ShadersNotReady" },
    fx::EnumEntry<TemporalAaFrameStatus>{ TemporalAaFrameStatus::TargetFailed, "TargetFailed" },
    fx::EnumEntry<TemporalAaFrameStatus>{ TemporalAaFrameStatus::NoValidCamera, "NoValidCamera" },
    fx::EnumEntry<TemporalAaFrameStatus>{ TemporalAaFrameStatus::ExtentTooSmall, "ExtentTooSmall" },
    fx::EnumEntry<TemporalAaFrameStatus>{ TemporalAaFrameStatus::ViewNotSupported, "ViewNotSupported" },
    fx::EnumEntry<TemporalAaFrameStatus>{ TemporalAaFrameStatus::CaptureArmed, "CaptureArmed" },
};

// What the jitter step decided. The order here is the order of the checks in DecideTemporalAaJitter().
enum class TemporalAaJitterDecision : std::uint8_t {
    NotRun, Apply, NotPrimaryView, Disabled, CaptureArmed, PassDisabled,
    NoValidCamera, ExtentTooSmall, ShadersNotReady, NoHeartbeat };

inline constexpr std::array kTemporalAaJitterDecisionTable{
    fx::EnumEntry<TemporalAaJitterDecision>{ TemporalAaJitterDecision::NotRun, "NotRun" },
    fx::EnumEntry<TemporalAaJitterDecision>{ TemporalAaJitterDecision::Apply, "Apply" },
    fx::EnumEntry<TemporalAaJitterDecision>{ TemporalAaJitterDecision::NotPrimaryView, "NotPrimaryView" },
    fx::EnumEntry<TemporalAaJitterDecision>{ TemporalAaJitterDecision::Disabled, "Disabled" },
    fx::EnumEntry<TemporalAaJitterDecision>{ TemporalAaJitterDecision::CaptureArmed, "CaptureArmed" },
    fx::EnumEntry<TemporalAaJitterDecision>{ TemporalAaJitterDecision::PassDisabled, "PassDisabled" },
    fx::EnumEntry<TemporalAaJitterDecision>{ TemporalAaJitterDecision::NoValidCamera, "NoValidCamera" },
    fx::EnumEntry<TemporalAaJitterDecision>{ TemporalAaJitterDecision::ExtentTooSmall, "ExtentTooSmall" },
    fx::EnumEntry<TemporalAaJitterDecision>{ TemporalAaJitterDecision::ShadersNotReady, "ShadersNotReady" },
    fx::EnumEntry<TemporalAaJitterDecision>{ TemporalAaJitterDecision::NoHeartbeat, "NoHeartbeat" },
};

struct TemporalAaJitterInputs {
    bool primaryView = false;
    bool enabled = false;
    bool captureArmed = false;
    bool passesEnabled = false;     // Resolve and copy passes both on.
    bool cameraValid = false;
    Extent2D extent{};
    bool shaderReady = false;
    bool resolvedLastFrame = false; // The heartbeat.
};

// What the endpoint and the panel read, one per view scope.
struct TemporalAaViewSnapshot {
    TemporalAaFrameStatus status = TemporalAaFrameStatus::NotRendered;
    std::uint64_t frameIndex = 0; // Frame of `status`.
    bool historyUsed = false;
    std::uint32_t stableFrames = 0;
    Extent2D extent{};
    std::uint64_t textureBytes = 0;
    TemporalAaJitterDecision jitterDecision = TemporalAaJitterDecision::NotRun;
    std::uint64_t jitterFrameIndex = 0; // Frame of `jitterDecision`.
    bool jitterApplied = false;         // A non-zero jitter reached the engine.
    float jitterPixelX = 0.0f;
    float jitterPixelY = 0.0f;
};

// std430 block of TemporalAaResolve.comp; member order matches the shader.
struct TemporalAaParamsGpu {
    Mat4 invViewProjection;
    Mat4 prevViewProjection;
    float eye[4] = {};
    float forward[4] = {};
    float prevEye[4] = {};
    float prevForward[4] = {};
    float blend[4] = {};          // x feedback min, y feedback max, z depth tolerance, w clip gamma.
    float look[4] = {};           // x debug view (0 off, 1 jitter only), yzw reserved.
    std::uint32_t counts[4] = {}; // x history valid (0 or 1), y width, z height, w reserved.
};
static_assert(sizeof(TemporalAaParamsGpu) == 240);
static_assert(std::is_trivially_copyable_v<TemporalAaParamsGpu>);

} // namespace gte
