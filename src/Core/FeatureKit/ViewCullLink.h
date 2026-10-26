#pragma once

// Plain data between a view panel and the feature that culls the secondary view.
// The feature owns the link; the editor holds a shared copy. Main thread only.

#include "ClusterDrawTypes.h"
#include "EnumTable.h"
#include "../../Math/Mat4.h"
#include "../../Renderer/Culling/ClusterCullMath.h"

#include <array>
#include <cstdint>
#include <optional>

namespace gte::fx {

// Culling tests of the secondary view. FollowSettings uses the tests of the whole feature.
enum class ViewCullChoice : std::uint8_t { FollowSettings, Off, Frustum, FrustumHiz };
enum class ViewCullSource : std::uint8_t { Live, Frozen };
enum class GhostStyle : std::uint8_t { NotDrawn };
enum class ViewCullMode : std::uint8_t { Off, Frustum, FrustumHiz, Custom };
enum class OcclusionState : std::uint8_t { NotRequested, Applied, Unusable, CameraNotLive };

inline constexpr std::array kViewCullChoiceTable{
    EnumEntry<ViewCullChoice>{ ViewCullChoice::FollowSettings, "FollowSettings" },
    EnumEntry<ViewCullChoice>{ ViewCullChoice::Off, "Off" },
    EnumEntry<ViewCullChoice>{ ViewCullChoice::Frustum, "Frustum" },
    EnumEntry<ViewCullChoice>{ ViewCullChoice::FrustumHiz, "FrustumHiz" },
};
inline constexpr std::array kViewCullSourceTable{
    EnumEntry<ViewCullSource>{ ViewCullSource::Live, "Live" },
    EnumEntry<ViewCullSource>{ ViewCullSource::Frozen, "Frozen" },
};
inline constexpr std::array kGhostStyleTable{
    EnumEntry<GhostStyle>{ GhostStyle::NotDrawn, "NotDrawn" },
};
inline constexpr std::array kViewCullModeTable{
    EnumEntry<ViewCullMode>{ ViewCullMode::Off, "Off" },
    EnumEntry<ViewCullMode>{ ViewCullMode::Frustum, "Frustum" },
    EnumEntry<ViewCullMode>{ ViewCullMode::FrustumHiz, "FrustumHiz" },
    EnumEntry<ViewCullMode>{ ViewCullMode::Custom, "Custom" },
};
inline constexpr std::array kOcclusionStateTable{
    EnumEntry<OcclusionState>{ OcclusionState::NotRequested, "NotRequested" },
    EnumEntry<OcclusionState>{ OcclusionState::Applied, "Applied" },
    EnumEntry<OcclusionState>{ OcclusionState::Unusable, "Unusable" },
    EnumEntry<OcclusionState>{ OcclusionState::CameraNotLive, "CameraNotLive" },
};
inline constexpr std::array kHizReasonTable{
    EnumEntry<HizReason>{ HizReason::None, "None" },
    EnumEntry<HizReason>{ HizReason::NoHistory, "NoHistory" },
    EnumEntry<HizReason>{ HizReason::Resized, "Resized" },
    EnumEntry<HizReason>{ HizReason::CameraCut, "CameraCut" },
    EnumEntry<HizReason>{ HizReason::WarmingUp, "WarmingUp" },
    EnumEntry<HizReason>{ HizReason::BuildOff, "BuildOff" },
    EnumEntry<HizReason>{ HizReason::Unavailable, "Unavailable" },
};

// Written by the panel only. The feature consumes it once per frame and clears the one-shot flags.
struct ViewCullRequest {
    bool pending = false; // A user change is waiting.
    ViewCullChoice choice = ViewCullChoice::FollowSettings;
    bool cone = false;
    ViewCullSource source = ViewCullSource::Live;
    GhostStyle ghost = GhostStyle::NotDrawn;
    bool freezeHere = false; // One-shot: capture the cull camera again.
    bool resetAll = false;   // One-shot: back to the defaults.
    bool hudShown = false;   // Set in every frame the HUD is drawn; consumed by the feature.
};

// Written by the feature only, for the secondary view.
struct ViewCullReport {
    bool valid = false;    // The feature wrote this report at least once.
    bool rendered = false; // The secondary view ran in the last frames and had pixels.
    bool featureEnabled = true;
    std::uint64_t frame = 0;
    ViewCullChoice choice = ViewCullChoice::FollowSettings; // Applied now, also after an HTTP change.
    ViewCullMode mode = ViewCullMode::Custom;
    bool cone = false;
    ViewCullSource source = ViewCullSource::Live;
    GhostStyle ghost = GhostStyle::NotDrawn;
    std::uint64_t tested = 0; // Current frame.
    std::uint64_t testedAtVisibleFrame = 0;
    std::uint32_t visible = 0;
    bool visibleValid = false;
    std::uint64_t hidden = 0;
    bool hiddenValid = false; // visibleValid and visible <= testedAtVisibleFrame.
    std::uint32_t claimedInstances = 0;
    OcclusionState occlusion = OcclusionState::NotRequested;
    HizReason hizReason = HizReason::None;
    bool frozen = false;
    std::uint64_t frozenFrame = 0;
    bool cameraValid = true; // The live camera of the view.
    bool cullCameraValid = false;
    Mat4 cullViewProjection = Mat4::Identity(); // The matrix the cull used.
    ClusterInertReason inertReason = ClusterInertReason::None;
    bool refused = false;
};

struct ViewCullLink {
    ViewCullRequest request;
    ViewCullReport report;
};

// Clusters tested at the frame of the count minus clusters visible; empty when the count is larger.
constexpr std::optional<std::uint64_t> HiddenClusterCount(std::uint64_t testedAtVisibleFrame, std::uint32_t visible) noexcept
{
    if (visible > testedAtVisibleFrame) {
        return std::nullopt;
    }
    return testedAtVisibleFrame - visible;
}

} // namespace gte::fx
