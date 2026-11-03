#include "TemporalAaMath.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

constexpr float kMaxFeedback = 0.99f;

} // namespace

TemporalAaSettings SanitizeTemporalAaSettings(const TemporalAaSettings& settings) noexcept
{
    const TemporalAaSettings d{};
    TemporalAaSettings s = settings;
    s.jitterSampleCount = std::clamp(settings.jitterSampleCount, kTemporalAaMinSamples, kTemporalAaMaxSamples);
    s.jitterScale = fx::SanitizeFinite(settings.jitterScale, 0.0f, 8.0f, d.jitterScale);
    s.feedbackMin = fx::SanitizeFinite(settings.feedbackMin, 0.0f, kMaxFeedback, d.feedbackMin);
    s.feedbackMax = fx::SanitizeFinite(settings.feedbackMax, 0.0f, kMaxFeedback, d.feedbackMax);
    s.feedbackMax = std::max(s.feedbackMax, s.feedbackMin);
    s.clipGamma = fx::SanitizeFinite(settings.clipGamma, 0.25f, 4.0f, d.clipGamma);
    s.depthTolerance = fx::SanitizeFinite(settings.depthTolerance, 0.001f, 0.5f, d.depthTolerance);
    if (settings.debugView != TemporalAaDebugView::Off && settings.debugView != TemporalAaDebugView::JitterOnly) {
        s.debugView = TemporalAaDebugView::Off;
    }
    return s;
}

float TemporalAaHalton(std::uint32_t index, std::uint32_t base) noexcept
{
    if (base < 2) {
        return 0.0f;
    }
    const float inverseBase = 1.0f / static_cast<float>(base);
    float fraction = 1.0f;
    float result = 0.0f;
    for (std::uint32_t i = index; i > 0; i /= base) {
        fraction *= inverseBase;
        result += fraction * static_cast<float>(i % base);
    }
    return result;
}

Vec2 TemporalAaJitterPixels(std::uint64_t frameIndex, const TemporalAaSettings& safe) noexcept
{
    if (safe.jitterScale == 0.0f) {
        return Vec2{ 0.0f, 0.0f };
    }
    const std::uint32_t cycle = std::max(safe.jitterSampleCount, 1u);
    const std::uint32_t i = static_cast<std::uint32_t>(frameIndex % cycle) + 1u;
    return Vec2{ TemporalAaHalton(i, 2) - 0.5f, TemporalAaHalton(i, 3) - 0.5f } * safe.jitterScale;
}

Vec2 TemporalAaJitterNdc(Vec2 pixels, Extent2D extent) noexcept
{
    if (extent.width == 0 || extent.height == 0) {
        return Vec2{ 0.0f, 0.0f };
    }
    return Vec2{ 2.0f * pixels.x / static_cast<float>(extent.width), 2.0f * pixels.y / static_cast<float>(extent.height) };
}

TemporalAaJitterDecision DecideTemporalAaJitter(const TemporalAaJitterInputs& in) noexcept
{
    if (!in.primaryView) {
        return TemporalAaJitterDecision::NotPrimaryView;
    }
    if (!in.enabled) {
        return TemporalAaJitterDecision::Disabled;
    }
    if (in.captureArmed) {
        return TemporalAaJitterDecision::CaptureArmed;
    }
    if (!in.passesEnabled) {
        return TemporalAaJitterDecision::PassDisabled;
    }
    if (!in.cameraValid) {
        return TemporalAaJitterDecision::NoValidCamera;
    }
    if (in.extent.width < kTemporalAaMinViewSide || in.extent.height < kTemporalAaMinViewSide) {
        return TemporalAaJitterDecision::ExtentTooSmall;
    }
    if (!in.shaderReady) {
        return TemporalAaJitterDecision::ShadersNotReady;
    }
    if (!in.resolvedLastFrame) {
        return TemporalAaJitterDecision::NoHeartbeat;
    }
    return TemporalAaJitterDecision::Apply;
}

float TemporalAaFeedback(float lumaCurrent, float lumaHistory, float feedbackMin, float feedbackMax) noexcept
{
    const float peak = std::max(std::max(lumaCurrent, lumaHistory), kTemporalAaLumaFloor);
    const float diff = std::abs(lumaCurrent - lumaHistory) / peak;
    const float k = std::isfinite(diff) ? std::clamp(1.0f - diff, 0.0f, 1.0f) : 0.0f;
    return feedbackMin + (feedbackMax - feedbackMin) * k * k;
}

TemporalAaParamsGpu MakeTemporalAaParams(const fx::CameraBasis& basis, const fx::TemporalHistory& previous,
    bool historyValid, const TemporalAaSettings& safe, const Mat4& invViewProjection, Extent2D extent) noexcept
{
    TemporalAaParamsGpu p;
    p.invViewProjection = invViewProjection;
    p.prevViewProjection = previous.viewProjection;
    fx::PackVec3(p.eye, basis.eye);
    fx::PackVec3(p.forward, basis.forward);
    fx::PackVec3(p.prevEye, previous.eye);
    fx::PackVec3(p.prevForward, previous.forward);

    p.blend[0] = safe.feedbackMin;
    p.blend[1] = safe.feedbackMax;
    p.blend[2] = safe.depthTolerance;
    p.blend[3] = safe.clipGamma;
    p.look[0] = safe.debugView == TemporalAaDebugView::JitterOnly ? 1.0f : 0.0f;

    p.counts[0] = historyValid ? 1u : 0u;
    p.counts[1] = extent.width;
    p.counts[2] = extent.height;
    return p;
}

std::string_view TemporalAaStatusName(TemporalAaFrameStatus status) noexcept
{
    switch (status) {
    case TemporalAaFrameStatus::NotRendered: return "NotRendered";
    case TemporalAaFrameStatus::Ok: return "Ok";
    case TemporalAaFrameStatus::Disabled: return "Disabled";
    case TemporalAaFrameStatus::PassDisabled: return "PassDisabled";
    case TemporalAaFrameStatus::ShadersNotReady: return "ShadersNotReady";
    case TemporalAaFrameStatus::TargetFailed: return "TargetFailed";
    case TemporalAaFrameStatus::NoValidCamera: return "NoValidCamera";
    case TemporalAaFrameStatus::ExtentTooSmall: return "ExtentTooSmall";
    case TemporalAaFrameStatus::ViewNotSupported: return "ViewNotSupported";
    case TemporalAaFrameStatus::CaptureArmed: return "CaptureArmed";
    }
    return "NotRendered";
}

std::string_view TemporalAaJitterDecisionName(TemporalAaJitterDecision decision) noexcept
{
    switch (decision) {
    case TemporalAaJitterDecision::NotRun: return "NotRun";
    case TemporalAaJitterDecision::Apply: return "Apply";
    case TemporalAaJitterDecision::NotPrimaryView: return "NotPrimaryView";
    case TemporalAaJitterDecision::Disabled: return "Disabled";
    case TemporalAaJitterDecision::CaptureArmed: return "CaptureArmed";
    case TemporalAaJitterDecision::PassDisabled: return "PassDisabled";
    case TemporalAaJitterDecision::NoValidCamera: return "NoValidCamera";
    case TemporalAaJitterDecision::ExtentTooSmall: return "ExtentTooSmall";
    case TemporalAaJitterDecision::ShadersNotReady: return "ShadersNotReady";
    case TemporalAaJitterDecision::NoHeartbeat: return "NoHeartbeat";
    }
    return "NotRun";
}

std::string_view TemporalAaDebugViewName(TemporalAaDebugView view) noexcept
{
    switch (view) {
    case TemporalAaDebugView::Off: return "Off";
    case TemporalAaDebugView::JitterOnly: return "JitterOnly";
    }
    return "Off";
}

} // namespace gte
