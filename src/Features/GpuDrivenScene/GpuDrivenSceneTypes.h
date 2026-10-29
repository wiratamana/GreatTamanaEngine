#pragma once

#include "../../Core/FeatureKit/ClusterDrawTypes.h"
#include "../../Core/FeatureKit/EnumTable.h"
#include "../../Core/FeatureKit/FeatureDebug.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gte {

inline constexpr const char* kGpuDrivenSceneFeatureName = "GpuDrivenScene";

// ClusterId colors each cluster by its id; ClusterPhase colors by draw phase (first red, second green).
enum class GpuDrivenSceneDebugView : std::uint8_t { Off, ClusterId, ClusterPhase };

inline constexpr std::array kGpuDrivenSceneDebugViewTable{
    fx::EnumEntry<GpuDrivenSceneDebugView>{ GpuDrivenSceneDebugView::Off, "Off" },
    fx::EnumEntry<GpuDrivenSceneDebugView>{ GpuDrivenSceneDebugView::ClusterId, "ClusterId" },
    fx::EnumEntry<GpuDrivenSceneDebugView>{ GpuDrivenSceneDebugView::ClusterPhase, "ClusterPhase" },
};

// What is culled in every view, on both draw paths.
enum class GpuCullMode : std::uint8_t { NoCull, Frustum, HiZ };

inline constexpr std::array kCullModeTable{
    fx::EnumEntry<GpuCullMode>{ GpuCullMode::NoCull, "NoCull" },
    fx::EnumEntry<GpuCullMode>{ GpuCullMode::Frustum, "Frustum" },
    fx::EnumEntry<GpuCullMode>{ GpuCullMode::HiZ, "HiZ" },
};

// Hi-Z state of a view.
enum class OcclusionState : std::uint8_t { NotRequested, Applied, Unusable, NoCullCamera };

inline constexpr std::array kOcclusionStateTable{
    fx::EnumEntry<OcclusionState>{ OcclusionState::NotRequested, "NotRequested" },
    fx::EnumEntry<OcclusionState>{ OcclusionState::Applied, "Applied" },
    fx::EnumEntry<OcclusionState>{ OcclusionState::Unusable, "Unusable" },
    fx::EnumEntry<OcclusionState>{ OcclusionState::NoCullCamera, "NoCullCamera" },
};

inline constexpr std::array kHizReasonTable{
    fx::EnumEntry<HizReason>{ HizReason::None, "None" },
    fx::EnumEntry<HizReason>{ HizReason::Unavailable, "Unavailable" },
    fx::EnumEntry<HizReason>{ HizReason::TooLarge, "TooLarge" },
    fx::EnumEntry<HizReason>{ HizReason::NothingInView, "NothingInView" },
};

// Whose camera a view culls with: its own, the primary view's, or nobody's (no test runs).
enum class CullCameraSource : std::uint8_t { Own, Primary, None };

inline constexpr std::array kCullCameraSourceTable{
    fx::EnumEntry<CullCameraSource>{ CullCameraSource::Own, "own" },
    fx::EnumEntry<CullCameraSource>{ CullCameraSource::Primary, "primary" },
    fx::EnumEntry<CullCameraSource>{ CullCameraSource::None, "none" },
};

// State of the CPU draw cull of a view.
enum class CpuCullState : std::uint8_t { Active, Off, SuppressedCapture, NoCamera };

inline constexpr std::array kCpuCullStateTable{
    fx::EnumEntry<CpuCullState>{ CpuCullState::Active, "active" },
    fx::EnumEntry<CpuCullState>{ CpuCullState::Off, "off" },
    fx::EnumEntry<CpuCullState>{ CpuCullState::SuppressedCapture, "suppressed: frame capture" },
    fx::EnumEntry<CpuCullState>{ CpuCullState::NoCamera, "no camera" },
};

struct GpuDrivenSceneSettings {
    // GPU (cluster) path on/off. Never changes the CPU draw cull.
    bool enabled = true;
    GpuCullMode mode = GpuCullMode::Frustum;
    // Back-face cone test. Only correct for closed meshes; never applied to depth-only lanes.
    bool cone = false;
    // Reads the visible-cluster count and the reason counters back. Costs three extra passes per part slot.
    bool collectStats = false;
    GpuDrivenSceneDebugView debugView = GpuDrivenSceneDebugView::Off;
};

// Out-of-range enum values fall back to their defaults.
constexpr GpuDrivenSceneSettings SanitizeGpuDrivenSceneSettings(GpuDrivenSceneSettings s) noexcept
{
    const GpuDrivenSceneSettings defaults{};
    if (!fx::EnumKnown(kGpuDrivenSceneDebugViewTable, s.debugView)) {
        s.debugView = defaults.debugView;
    }
    if (!fx::EnumKnown(kCullModeTable, s.mode)) {
        s.mode = defaults.mode;
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
    GpuDrivenSceneSettingKey{ "mode", "NoCull, Frustum or HiZ",
        &detail::SetEnum<&GpuDrivenSceneSettings::mode, kCullModeTable> },
    GpuDrivenSceneSettingKey{ "cone", "true or false", &detail::SetBool<&GpuDrivenSceneSettings::cone> },
    GpuDrivenSceneSettingKey{ "collectStats", "true or false", &detail::SetBool<&GpuDrivenSceneSettings::collectStats> },
    GpuDrivenSceneSettingKey{ "debugView", "Off, ClusterId or ClusterPhase",
        &detail::SetEnum<&GpuDrivenSceneSettings::debugView, kGpuDrivenSceneDebugViewTable> },
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
};

// Cluster tests of a view. Both views use the same table.
constexpr ViewCullTests ResolveCullTests(const GpuDrivenSceneSettings& s) noexcept
{
    switch (s.mode) {
    case GpuCullMode::NoCull: return { false, false };
    case GpuCullMode::Frustum:
    case GpuCullMode::HiZ: return { true, s.cone };
    }
    return {};
}

// Only the primary view builds a pyramid.
constexpr bool ViewWantsHiz(const GpuDrivenSceneSettings& s, bool secondary) noexcept
{
    return s.enabled && s.mode == GpuCullMode::HiZ && !secondary;
}

// The secondary view never culls with its own camera.
constexpr CullCameraSource ChooseCullCamera(bool secondary, GpuCullMode mode, bool primaryValid, bool liveValid) noexcept
{
    if (mode == GpuCullMode::NoCull) {
        return CullCameraSource::None;
    }
    if (secondary) {
        return primaryValid ? CullCameraSource::Primary : CullCameraSource::None;
    }
    return liveValid ? CullCameraSource::Own : CullCameraSource::None;
}

// Hi-Z state of a view for the panel.
constexpr OcclusionState OcclusionStateOf(bool requested, bool cullCameraAvailable, bool applied) noexcept
{
    if (!requested) {
        return OcclusionState::NotRequested;
    }
    if (!cullCameraAvailable) {
        return OcclusionState::NoCullCamera;
    }
    return applied ? OcclusionState::Applied : OcclusionState::Unusable;
}

// ---------------------------------------------------------------------------------------------
// Counts
// ---------------------------------------------------------------------------------------------

// Clusters tested at the frame of the count minus clusters visible; empty when the count is larger.
constexpr std::optional<std::uint64_t> HiddenClusterCount(std::uint64_t testedAtVisibleFrame, std::uint32_t visible) noexcept
{
    if (visible > testedAtVisibleFrame) {
        return std::nullopt;
    }
    return testedAtVisibleFrame - visible;
}

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

// A chain with instances that declared nothing, skipped nothing and had no deliberate reason.
constexpr bool ClusterOutcomeRefused(std::size_t candidateCount, const fx::ClusterStats& stats) noexcept
{
    return stats.inertReason == fx::ClusterInertReason::None && candidateCount > 0 && !stats.declared && stats.skippedParts == 0;
}

// Every part of the chain was outside the cull frustum: nothing is drawn and the visible count is exactly 0.
constexpr bool ChainSkippedEverything(const fx::ClusterStats& stats) noexcept
{
    return stats.parts == 0 && stats.skippedParts > 0;
}

// Instances the cluster path owns this frame: drawn parts plus skipped parts.
constexpr std::uint32_t ClaimedInstancesOf(const fx::ClusterStats& stats) noexcept
{
    return stats.instances + stats.skippedInstances;
}

// Why a view drew nothing through the cluster path; empty when it did, when every part was outside the view,
// or when there was nothing to draw.
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

// Renderables that stayed on the CPU draw (no Hi-Z there), by reason. `noSlot` counts instances of parts that found no
// part slot; the other fields come from candidate selection.
struct CpuPathReasons {
    std::uint32_t skinned = 0;
    std::uint32_t noIndexBuffer = 0;
    std::uint32_t unsupportedLayout = 0;
    std::uint32_t noClusterTable = 0; // Alpha-tested or refused textured slices, and untextured meshes without a table.
    std::uint32_t tooFewInstances = 0;
    std::uint32_t nonFinite = 0;
    std::uint32_t noSlot = 0;

    constexpr std::uint32_t Total() const noexcept
    {
        return skinned + noIndexBuffer + unsupportedLayout + noClusterTable + tooFewInstances + nonFinite + noSlot;
    }
};

constexpr CpuPathReasons CpuPathReasonsOf(const fx::ClusterRejectCounts& rejected, std::uint32_t noSlotInstances) noexcept
{
    return CpuPathReasons{ rejected.skinned, rejected.noIndexBuffer, rejected.unsupportedLayout, rejected.noClusterTable,
        rejected.tooFewInstances, rejected.nonFinite, noSlotInstances };
}

// What the cluster pass reported for a view. Reads as all zero in a frame where the pass did not run.
struct GpuPathReport {
    std::uint32_t claimedEntities = 0; // Instances of drawn parts plus instances of skipped parts.
    std::uint32_t parts = 0;
    std::uint32_t skippedParts = 0;     // Parts outside the cull frustum: no pass, entities still claimed.
    std::uint32_t skippedInstances = 0; // Instances of those parts.
    CpuPathReasons cpuPath;
    OcclusionState occlusion = OcclusionState::NotRequested;
    HizReason hizReason = HizReason::None;
    CullCameraSource hizSource = CullCameraSource::None;
    bool sharedSurvivors = false; // The view drew the primary view's survivors instead of culling.
    std::uint64_t testedClusters = 0;          // Current frame.
    std::uint64_t testedAtVisibleFrame = 0;    // Tested at the frame the visible count belongs to.
    std::uint32_t visibleClusters = 0;
    std::uint64_t visibleFrame = 0;
    bool statsCollected = false; // The count readback was on this frame; visibleValid is false otherwise.
    fx::ClusterReasonCounts reasons; // Items by outcome, two frames old; valid only when statsCollected.
    bool visibleValid = false;
    std::uint64_t hidden = 0;
    bool hiddenValid = false; // visibleValid and visible <= testedAtVisibleFrame.
    fx::ClusterInertReason inertReason = fx::ClusterInertReason::None;
    bool refused = false;
    std::string refusal;
    std::uint64_t ledgerBytes = 0;
};

// What a view reported at its last callback. Liveness, camera and CPU fields come from the always-on cull pass;
// `gpu` comes from the cluster pass.
struct GpuDrivenSceneViewSnapshot {
    std::uint64_t frameIndex = 0;
    bool rendered = false;
    bool enabled = true;
    bool hasPixels = true;
    bool cameraValid = true;
    GpuPathReport gpu;

    CullCameraSource cullCamera = CullCameraSource::None;

    // CPU draw cull of this view, written by the always-on cull pass. Counts are one frame old.
    CpuCullState cpuCullState = CpuCullState::Off;
    std::uint32_t cpuTested = 0;
    std::uint32_t cpuCulled = 0;
    std::uint32_t cpuUnbounded = 0;
};

} // namespace gte
