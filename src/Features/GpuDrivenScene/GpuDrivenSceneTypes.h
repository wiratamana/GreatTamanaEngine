#pragma once

#include "../../Core/FeatureKit/ClusterDrawTypes.h"
#include "../../Core/FeatureKit/EnumTable.h"
#include "../../Core/FeatureKit/FeatureDebug.h"
#include "../../Core/FeatureKit/ViewCullLink.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gte {

inline constexpr const char* kGpuDrivenSceneFeatureName = "GpuDrivenScene";

enum class GpuDrivenSceneDebugView : std::uint8_t { Off, ClusterId };

inline constexpr std::array kGpuDrivenSceneDebugViewTable{
    fx::EnumEntry<GpuDrivenSceneDebugView>{ GpuDrivenSceneDebugView::Off, "Off" },
    fx::EnumEntry<GpuDrivenSceneDebugView>{ GpuDrivenSceneDebugView::ClusterId, "ClusterId" },
};

// The first group of tests drives the Game view and, while `sceneCull` is FollowSettings, the Scene view.
// The `scene*` fields and `ghost` drive the Scene view only.
struct GpuDrivenSceneSettings {
    bool enabled = true;
    bool frustum = true;
    // Back-face cone test. Only correct for closed meshes; never applied to depth-only lanes.
    bool cone = false;
    // Hi-Z occlusion test against last frame's depth. Guarded against cuts, resizes and moved instances.
    bool occlusion = false;
    // Keeps the cull camera of the first frozen frame while the draw camera moves (both views).
    bool freezeCulling = false;
    // Reads the visible-cluster count back. Costs three extra passes per part slot.
    bool collectStats = false;
    GpuDrivenSceneDebugView debugView = GpuDrivenSceneDebugView::Off;

    fx::ViewCullChoice sceneCull = fx::ViewCullChoice::FollowSettings;
    bool sceneCone = false; // Used only while sceneCull is not FollowSettings.
    fx::ViewCullSource sceneSource = fx::ViewCullSource::Live;
    std::uint32_t freezeSerial = 0; // A new value captures the frozen cull camera again.
    fx::GhostStyle ghost = fx::GhostStyle::NotDrawn;
};

// Out-of-range enum values fall back to their defaults.
constexpr GpuDrivenSceneSettings SanitizeGpuDrivenSceneSettings(GpuDrivenSceneSettings s) noexcept
{
    if (!fx::EnumKnown(kGpuDrivenSceneDebugViewTable, s.debugView)) {
        s.debugView = GpuDrivenSceneDebugView::Off;
    }
    if (!fx::EnumKnown(fx::kViewCullChoiceTable, s.sceneCull)) {
        s.sceneCull = fx::ViewCullChoice::FollowSettings;
    }
    if (!fx::EnumKnown(fx::kViewCullSourceTable, s.sceneSource)) {
        s.sceneSource = fx::ViewCullSource::Live;
    }
    if (!fx::EnumKnown(fx::kGhostStyleTable, s.ghost)) {
        s.ghost = fx::GhostStyle::NotDrawn;
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// Debug keys
// ---------------------------------------------------------------------------------------------

enum class GpuDrivenSceneSetResult : std::uint8_t { Applied, UnknownKey, BadValue };

namespace detail {

template <bool GpuDrivenSceneSettings::*Member>
bool SetBool(GpuDrivenSceneSettings& s, std::string_view text) noexcept
{
    const std::optional<bool> parsed = fx::ParseDebugBool(text);
    if (!parsed) {
        return false;
    }
    s.*Member = *parsed;
    return true;
}

template <auto Member, const auto& Table>
bool SetEnum(GpuDrivenSceneSettings& s, std::string_view text) noexcept
{
    const auto parsed = fx::ParseEnum(Table, text);
    if (!parsed) {
        return false;
    }
    s.*Member = *parsed;
    return true;
}

} // namespace detail

// One writable key of the debug endpoint. `allowed` is the text of a bad-value error.
struct GpuDrivenSceneSettingKey {
    std::string_view name;
    std::string_view allowed;
    bool (*apply)(GpuDrivenSceneSettings&, std::string_view) noexcept;
};

inline constexpr std::array kGpuDrivenSceneSettingKeys{
    GpuDrivenSceneSettingKey{ "enabled", "true or false", &detail::SetBool<&GpuDrivenSceneSettings::enabled> },
    GpuDrivenSceneSettingKey{ "frustum", "true or false", &detail::SetBool<&GpuDrivenSceneSettings::frustum> },
    GpuDrivenSceneSettingKey{ "cone", "true or false", &detail::SetBool<&GpuDrivenSceneSettings::cone> },
    GpuDrivenSceneSettingKey{ "occlusion", "true or false", &detail::SetBool<&GpuDrivenSceneSettings::occlusion> },
    GpuDrivenSceneSettingKey{ "freezeCulling", "true or false", &detail::SetBool<&GpuDrivenSceneSettings::freezeCulling> },
    GpuDrivenSceneSettingKey{ "collectStats", "true or false", &detail::SetBool<&GpuDrivenSceneSettings::collectStats> },
    GpuDrivenSceneSettingKey{ "debugView", "Off or ClusterId",
        &detail::SetEnum<&GpuDrivenSceneSettings::debugView, kGpuDrivenSceneDebugViewTable> },
    GpuDrivenSceneSettingKey{ "sceneCull", "FollowSettings, Off, Frustum or FrustumHiz",
        &detail::SetEnum<&GpuDrivenSceneSettings::sceneCull, fx::kViewCullChoiceTable> },
    GpuDrivenSceneSettingKey{ "sceneCone", "true or false", &detail::SetBool<&GpuDrivenSceneSettings::sceneCone> },
    GpuDrivenSceneSettingKey{ "sceneSource", "Live or Frozen",
        &detail::SetEnum<&GpuDrivenSceneSettings::sceneSource, fx::kViewCullSourceTable> },
    GpuDrivenSceneSettingKey{ "ghost", "NotDrawn", &detail::SetEnum<&GpuDrivenSceneSettings::ghost, fx::kGhostStyleTable> },
};

inline const GpuDrivenSceneSettingKey* FindGpuDrivenSceneSettingKey(std::string_view key) noexcept
{
    for (const GpuDrivenSceneSettingKey& entry : kGpuDrivenSceneSettingKeys) {
        if (entry.name == key) {
            return &entry;
        }
    }
    return nullptr;
}

// Applies one key=value pair of a debug request to `settings`. `settings` is unchanged unless Applied.
inline GpuDrivenSceneSetResult ApplyGpuDrivenSceneSetting(
    GpuDrivenSceneSettings& settings, std::string_view key, std::string_view value) noexcept
{
    const GpuDrivenSceneSettingKey* entry = FindGpuDrivenSceneSettingKey(key);
    if (entry == nullptr) {
        return GpuDrivenSceneSetResult::UnknownKey;
    }
    return entry->apply(settings, value) ? GpuDrivenSceneSetResult::Applied : GpuDrivenSceneSetResult::BadValue;
}

// ---------------------------------------------------------------------------------------------
// Per-view culling plan
// ---------------------------------------------------------------------------------------------

struct ViewCullTests {
    bool frustum = false;
    bool cone = false;
    bool occlusion = false;
};

// Tests of one view. The Game view always follows the settings.
constexpr ViewCullTests ResolveViewCullTests(const GpuDrivenSceneSettings& s, bool sceneView) noexcept
{
    if (sceneView) {
        switch (s.sceneCull) {
        case fx::ViewCullChoice::Off: return { false, false, false };
        case fx::ViewCullChoice::Frustum: return { true, s.sceneCone, false };
        case fx::ViewCullChoice::FrustumHiz: return { true, s.sceneCone, true };
        case fx::ViewCullChoice::FollowSettings: break;
        }
    }
    return { s.frustum, s.cone, s.occlusion };
}

// The cone has its own checkbox and does not change the mode.
constexpr fx::ViewCullMode CullModeOf(const ViewCullTests& tests) noexcept
{
    if (tests.frustum) {
        return tests.occlusion ? fx::ViewCullMode::FrustumHiz : fx::ViewCullMode::Frustum;
    }
    return tests.occlusion ? fx::ViewCullMode::Custom : fx::ViewCullMode::Off;
}

constexpr void ApplyViewCullChoice(GpuDrivenSceneSettings& s, fx::ViewCullChoice choice, bool cone) noexcept
{
    s.sceneCull = choice;
    s.sceneCone = cone;
}

// True when the view culls with a frozen camera.
constexpr bool ViewFreezeRequested(const GpuDrivenSceneSettings& s, bool sceneView) noexcept
{
    return s.freezeCulling || (sceneView && s.sceneSource == fx::ViewCullSource::Frozen);
}

// The pyramid is built from a view's live camera depth; no other cull camera may use it.
constexpr bool HizMatchesCullCamera(bool hizRequested, bool cullIsLive) noexcept
{
    return hizRequested && cullIsLive;
}

// The pyramid is built from the live camera's depth, so a frozen view never needs it.
constexpr bool ViewWantsHiz(const GpuDrivenSceneSettings& s, bool sceneView) noexcept
{
    return s.enabled
        && HizMatchesCullCamera(ResolveViewCullTests(s, sceneView).occlusion, !ViewFreezeRequested(s, sceneView));
}

constexpr void FreezeSceneCullHere(GpuDrivenSceneSettings& s) noexcept
{
    s.sceneSource = fx::ViewCullSource::Frozen;
    ++s.freezeSerial;
}

// Defaults for every field except freezeSerial, so a freeze after a reset still captures anew.
constexpr GpuDrivenSceneSettings DefaultsKeepingSerial(const GpuDrivenSceneSettings& s) noexcept
{
    GpuDrivenSceneSettings d{};
    d.freezeSerial = s.freezeSerial;
    return d;
}

// Returns only the secondary-view fields to their defaults; every other field and the serial stay.
constexpr void ResetSceneCullFields(GpuDrivenSceneSettings& s) noexcept
{
    const GpuDrivenSceneSettings defaults{};
    s.sceneCull = defaults.sceneCull;
    s.sceneCone = defaults.sceneCone;
    s.sceneSource = defaults.sceneSource;
    s.ghost = defaults.ghost;
}

// Settings after the panel's request.
constexpr GpuDrivenSceneSettings ApplyViewCullRequest(GpuDrivenSceneSettings s, const fx::ViewCullRequest& r) noexcept
{
    if (r.resetAll) {
        ResetSceneCullFields(s);
    } else {
        ApplyViewCullChoice(s, r.choice, r.cone);
        s.sceneSource = r.source;
        s.ghost = r.ghost;
    }
    if (r.freezeHere && !r.resetAll) {
        FreezeSceneCullHere(s);
    }
    return SanitizeGpuDrivenSceneSettings(s);
}

// ---------------------------------------------------------------------------------------------
// Frozen cull camera
// ---------------------------------------------------------------------------------------------

// Cull camera state of one view. The frustum is world geometry, so a resize keeps it.
struct FrozenCullCamera {
    bool frozen = false;
    std::uint32_t serial = 0;
    std::uint64_t frame = 0; // Frame of the capture.
    fx::ClusterCamera camera;
};

// Picks the camera the cull uses. Not requested: unfrozen. Requested: captures `live` when nothing is frozen
// yet or `serial` changed. An invalid live camera is never captured.
inline const fx::ClusterCamera& ResolveCullCamera(FrozenCullCamera& state, bool freezeRequested, std::uint32_t serial,
    const fx::ClusterCamera& live, std::uint64_t frameIndex) noexcept
{
    if (!freezeRequested) {
        state.frozen = false;
    } else if ((!state.frozen || state.serial != serial) && live.valid) {
        state.frozen = true;
        state.serial = serial;
        state.frame = frameIndex;
        state.camera = live;
    }
    return state.frozen ? state.camera : live;
}

// Hi-Z state of a view; a cull camera other than the live one never uses the pyramid.
constexpr fx::OcclusionState OcclusionStateOf(bool requested, bool cullIsLive, bool applied) noexcept
{
    if (!requested) {
        return fx::OcclusionState::NotRequested;
    }
    if (!HizMatchesCullCamera(true, cullIsLive)) {
        return fx::OcclusionState::CameraNotLive;
    }
    return applied ? fx::OcclusionState::Applied : fx::OcclusionState::Unusable;
}

// ---------------------------------------------------------------------------------------------
// Counts
// ---------------------------------------------------------------------------------------------

// Tested clusters of the last four frames. The visible count is two frames old; this pairs it with its own frame.
class TestedHistory {
public:
    void Record(std::uint64_t frame, std::uint64_t tested) noexcept { m_ring[frame % m_ring.size()] = { frame, tested, true }; }

    std::optional<std::uint64_t> At(std::uint64_t frame) const noexcept
    {
        const Entry& entry = m_ring[frame % m_ring.size()];
        if (entry.valid && entry.frame == frame) {
            return entry.tested;
        }
        return std::nullopt;
    }

private:
    struct Entry {
        std::uint64_t frame = 0;
        std::uint64_t tested = 0;
        bool valid = false;
    };
    std::array<Entry, 4> m_ring{};
};

// Frames the count readback keeps running after the last frame the HUD was drawn.
inline constexpr std::uint8_t kStatsHoldFrames = 3;

// Keeps the readback on for a few frames after the HUD stops asking. Never touches the user's setting.
struct StatsHold {
    std::uint8_t frames = 0;

    // Call once per frame. True while the readback should run.
    constexpr bool Step(bool hudShown) noexcept
    {
        if (hudShown) {
            frames = kStatsHoldFrames;
            return true;
        }
        if (frames > 0) {
            --frames;
            return true;
        }
        return false;
    }
};

// A chain with instances that declared nothing and had no deliberate reason.
constexpr bool ClusterOutcomeRefused(std::size_t candidateCount, const fx::ClusterStats& stats) noexcept
{
    return stats.inertReason == fx::ClusterInertReason::None && candidateCount > 0 && !stats.declared;
}

// Why a view drew nothing through the cluster path; empty when it did or when there was nothing to draw.
inline std::string DescribeClusterOutcome(std::size_t candidateCount, const fx::ClusterStats& stats)
{
    if (stats.inertReason != fx::ClusterInertReason::None) {
        return std::string(fx::ClusterInertReasonName(stats.inertReason));
    }
    if (ClusterOutcomeRefused(candidateCount, stats)) {
        return "refused (limits, budget, resources or a pass; see the log)";
    }
    return {};
}

// Frames since the visible count was read back. 0 when no count exists (visibleFrame 0) or it is from the future.
constexpr std::uint64_t ClusterCountAgeFrames(std::uint64_t frame, std::uint64_t visibleFrame) noexcept
{
    return visibleFrame == 0 || visibleFrame > frame ? 0 : frame - visibleFrame;
}

// What a view reported at its last callback.
struct GpuDrivenSceneViewSnapshot {
    std::uint64_t frameIndex = 0;
    bool rendered = false;
    bool enabled = true;
    bool hasPixels = true;
    bool cameraValid = true;
    bool frozen = false;
    std::uint64_t frozenFrame = 0;
    bool cullCameraValid = false;
    Mat4 cullViewProjection = Mat4::Identity(); // The matrix the cull used.
    std::uint32_t claimedEntities = 0;
    std::uint32_t parts = 0;
    fx::OcclusionState occlusion = fx::OcclusionState::NotRequested;
    HizReason hizReason = HizReason::None;
    std::uint64_t testedClusters = 0;          // Current frame.
    std::uint64_t testedAtVisibleFrame = 0;    // Tested at the frame the visible count belongs to.
    std::uint32_t visibleClusters = 0;
    std::uint64_t visibleFrame = 0;
    bool statsCollected = false; // The count readback was on this frame; visibleValid is false otherwise.
    bool visibleValid = false;
    std::uint64_t hidden = 0;
    bool hiddenValid = false; // visibleValid and visible <= testedAtVisibleFrame.
    bool hizTrackingRequested = false; // This view asked for movement tracking in a recent frame.
    fx::ClusterInertReason inertReason = fx::ClusterInertReason::None;
    bool refused = false;
    std::string refusal;
    std::uint64_t ledgerBytes = 0;
};

// The panel's view of the secondary view snapshot. A pure function of its inputs.
inline fx::ViewCullReport MakeViewCullReport(const GpuDrivenSceneSettings& s, const GpuDrivenSceneViewSnapshot& v) noexcept
{
    fx::ViewCullReport r;
    r.valid = true;
    r.rendered = v.rendered && v.hasPixels;
    r.featureEnabled = v.enabled;
    r.frame = v.frameIndex;
    r.choice = s.sceneCull;
    r.mode = CullModeOf(ResolveViewCullTests(s, true));
    r.cone = s.sceneCone;
    r.source = s.sceneSource;
    r.ghost = s.ghost;
    r.tested = v.testedClusters;
    r.testedAtVisibleFrame = v.testedAtVisibleFrame;
    r.visible = v.visibleClusters;
    r.visibleValid = v.visibleValid;
    r.hidden = v.hidden;
    r.hiddenValid = v.hiddenValid;
    r.claimedInstances = v.claimedEntities;
    r.occlusion = v.occlusion;
    r.hizReason = v.hizReason;
    r.frozen = v.frozen;
    r.frozenFrame = v.frozenFrame;
    r.cameraValid = v.cameraValid;
    r.cullCameraValid = v.cullCameraValid;
    r.cullViewProjection = v.cullViewProjection;
    r.inertReason = v.inertReason;
    r.refused = v.refused;
    return r;
}

} // namespace gte
