#pragma once

#include "../../Core/FeatureKit/ClusterDrawTypes.h"
#include "../../Core/FeatureKit/FeatureDebug.h"
#include "../../Core/Extent.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gte {

inline constexpr const char* kGpuDrivenSceneFeatureName = "GpuDrivenScene";

enum class GpuDrivenSceneDebugView : std::uint8_t { Off, ClusterId };
inline constexpr std::uint8_t kGpuDrivenSceneDebugViewCount = 2;

struct GpuDrivenSceneSettings {
    bool enabled = true;
    bool frustum = true;
    // Back-face cone test. Only correct for closed meshes; never applied to depth-only lanes.
    bool cone = false;
    // Hi-Z occlusion test against last frame's depth. Guarded against cuts, resizes and moved instances.
    bool occlusion = false;
    // Keeps the cull camera of the first frozen frame while the draw camera moves.
    bool freezeCulling = false;
    // Reads the visible-cluster count back. Costs three extra passes per part slot.
    bool collectStats = false;
    GpuDrivenSceneDebugView debugView = GpuDrivenSceneDebugView::Off;
};

// Out-of-range enum values fall back to Off.
constexpr GpuDrivenSceneSettings SanitizeGpuDrivenSceneSettings(GpuDrivenSceneSettings s) noexcept
{
    if (static_cast<std::uint8_t>(s.debugView) >= kGpuDrivenSceneDebugViewCount) {
        s.debugView = GpuDrivenSceneDebugView::Off;
    }
    return s;
}

constexpr std::string_view GpuDrivenSceneDebugViewName(GpuDrivenSceneDebugView view) noexcept
{
    switch (view) {
    case GpuDrivenSceneDebugView::Off: return "Off";
    case GpuDrivenSceneDebugView::ClusterId: return "ClusterId";
    }
    return "Off";
}

inline std::optional<GpuDrivenSceneDebugView> ParseGpuDrivenSceneDebugView(std::string_view text) noexcept
{
    for (const GpuDrivenSceneDebugView view : { GpuDrivenSceneDebugView::Off, GpuDrivenSceneDebugView::ClusterId }) {
        if (text == GpuDrivenSceneDebugViewName(view)) {
            return view;
        }
    }
    return std::nullopt;
}

enum class GpuDrivenSceneSetResult : std::uint8_t { Applied, UnknownKey, BadValue };

// Applies one key=value pair of a debug request to `settings`. `settings` is unchanged unless Applied.
inline GpuDrivenSceneSetResult ApplyGpuDrivenSceneSetting(
    GpuDrivenSceneSettings& settings, std::string_view key, std::string_view value) noexcept
{
    const auto assignBool = [&](bool& member) {
        const std::optional<bool> parsed = fx::ParseDebugBool(value);
        if (!parsed) {
            return GpuDrivenSceneSetResult::BadValue;
        }
        member = *parsed;
        return GpuDrivenSceneSetResult::Applied;
    };
    if (key == "enabled") {
        return assignBool(settings.enabled);
    }
    if (key == "frustum") {
        return assignBool(settings.frustum);
    }
    if (key == "cone") {
        return assignBool(settings.cone);
    }
    if (key == "freezeCulling") {
        return assignBool(settings.freezeCulling);
    }
    if (key == "debugView") {
        const std::optional<GpuDrivenSceneDebugView> parsed = ParseGpuDrivenSceneDebugView(value);
        if (!parsed) {
            return GpuDrivenSceneSetResult::BadValue;
        }
        settings.debugView = *parsed;
        return GpuDrivenSceneSetResult::Applied;
    }
    if (key == "collectStats") {
        return assignBool(settings.collectStats);
    }
    if (key == "occlusion") {
        return assignBool(settings.occlusion);
    }
    return GpuDrivenSceneSetResult::UnknownKey;
}

// Cull camera state of one view. Cleared by a resize and by a disabled feature.
struct FrozenCullCamera {
    bool frozen = false;
    fx::ClusterCamera camera;
    Extent2D extent{};
};

// Picks the camera the cull uses. The first frozen frame stores `live`; later frames keep it.
inline const fx::ClusterCamera& ResolveCullCamera(FrozenCullCamera& state, bool featureEnabled, bool freezeRequested,
    const fx::ClusterCamera& live, Extent2D extent) noexcept
{
    const bool resized = state.frozen && (state.extent.width != extent.width || state.extent.height != extent.height);
    if (!featureEnabled || !freezeRequested || resized) {
        state.frozen = false;
    }
    if (featureEnabled && freezeRequested && !state.frozen && live.valid) {
        state.frozen = true;
        state.camera = live;
        state.extent = extent;
    }
    return state.frozen ? state.camera : live;
}

constexpr std::string_view ClusterInertReasonName(fx::ClusterInertReason reason) noexcept
{
    switch (reason) {
    case fx::ClusterInertReason::None: return "none";
    case fx::ClusterInertReason::DeviceCaps: return "device caps";
    case fx::ClusterInertReason::SharedScope: return "shared scope";
    case fx::ClusterInertReason::CaptureArmed: return "capture armed";
    case fx::ClusterInertReason::ToggledOff: return "toggled off";
    case fx::ClusterInertReason::InvalidState: return "invalid state";
    }
    return "none";
}

// Why a view drew nothing through the cluster path; empty when it did or when there was nothing to draw.
inline std::string DescribeClusterOutcome(std::size_t candidateCount, const fx::ClusterStats& stats)
{
    if (stats.inertReason != fx::ClusterInertReason::None) {
        return std::string(ClusterInertReasonName(stats.inertReason));
    }
    if (candidateCount > 0 && !stats.declared) {
        return "refused (limits, budget, resources or a pass; see the log)";
    }
    return {};
}

// What a view reported at its last callback.
struct GpuDrivenSceneViewSnapshot {
    std::uint64_t frameIndex = 0;
    bool rendered = false;
    bool frozen = false;
    std::uint32_t claimedEntities = 0;
    std::uint32_t parts = 0;
    bool occlusionUsable = false; // The pyramid of the last frame was valid and bound to the cull this frame.
    std::uint64_t testedClusters = 0;
    std::uint32_t visibleClusters = 0;
    std::uint64_t visibleFrame = 0;
    bool statsCollected = false; // The count readback was on this frame; visibleValid is false otherwise.
    bool visibleValid = false;
    fx::ClusterInertReason inertReason = fx::ClusterInertReason::None;
    std::string refusal;
    std::uint64_t ledgerBytes = 0;
};

} // namespace gte
