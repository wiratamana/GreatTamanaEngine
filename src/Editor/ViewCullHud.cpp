#include "ViewCullHud.h"

#include <cstdio>

namespace gte {

namespace {

constexpr std::size_t kMaxHudLines = 12;

std::string Format(const char* format, auto... args)
{
    char buffer[160];
    const int written = std::snprintf(buffer, sizeof(buffer), format, args...);
    return written > 0 ? std::string(buffer) : std::string();
}

unsigned long long U64(std::uint64_t value) noexcept
{
    return static_cast<unsigned long long>(value);
}

const char* HizReasonText(HizReason reason) noexcept
{
    switch (reason) {
    case HizReason::None: return "unknown";
    case HizReason::NoHistory: return "no history yet";
    case HizReason::Resized: return "view resized";
    case HizReason::CameraCut: return "camera cut";
    case HizReason::WarmingUp: return "warming up";
    case HizReason::BuildOff: return "pyramid not built last frame";
    case HizReason::Unavailable: return "unavailable";
    }
    return "unknown";
}

// First line: the counts, or the reason there are none.
ViewCullHudLine CountsLine(const fx::ViewCullReport& r)
{
    if (!r.featureEnabled) {
        return { "GPU path inactive: feature disabled", ViewCullHudTone::Warn };
    }
    if (!r.cullCameraValid) {
        return { "GPU path inactive: camera is not valid", ViewCullHudTone::Warn };
    }
    if (r.inertReason != fx::ClusterInertReason::None) {
        return { "GPU path inactive: " + std::string(fx::ClusterInertReasonName(r.inertReason)), ViewCullHudTone::Warn };
    }
    if (r.refused) {
        return { "GPU path inactive: refused, see the log", ViewCullHudTone::Warn };
    }
    if (r.claimedInstances == 0) {
        return { "No cluster meshes on the GPU path", ViewCullHudTone::Dim };
    }
    if (!r.visibleValid) {
        return { "Clusters: no data yet", ViewCullHudTone::Dim };
    }
    const std::optional<std::uint64_t> hidden = fx::HiddenClusterCount(r.testedAtVisibleFrame, r.visible);
    if (!hidden) {
        return { "Counts are settling", ViewCullHudTone::Dim };
    }
    if (r.testedAtVisibleFrame == 0) {
        return { "Clusters: tested 0, visible 0, hidden 0", ViewCullHudTone::Normal };
    }
    const double percent = 100.0 * static_cast<double>(r.visible) / static_cast<double>(r.testedAtVisibleFrame);
    return { Format("Clusters: tested %llu, visible %u (%.1f%%), hidden %llu", U64(r.testedAtVisibleFrame), r.visible, percent,
                 U64(*hidden)),
        ViewCullHudTone::Normal };
}

} // namespace

std::vector<ViewCullHudLine> FormatViewCullHud(const fx::ViewCullReport& r)
{
    std::vector<ViewCullHudLine> lines;
    lines.reserve(6);
    lines.push_back(CountsLine(r));

    if (r.mode == fx::ViewCullMode::FrustumHiz) {
        switch (r.occlusion) {
        case fx::OcclusionState::Applied: lines.push_back({ "Hi-Z: active", ViewCullHudTone::Good }); break;
        case fx::OcclusionState::CameraNotLive:
            lines.push_back({ "Hi-Z: off, the cull camera is not the Scene camera", ViewCullHudTone::Warn });
            break;
        case fx::OcclusionState::Unusable:
            lines.push_back({ std::string("Hi-Z: not usable now (") + HizReasonText(r.hizReason) + ")", ViewCullHudTone::Warn });
            break;
        case fx::OcclusionState::NotRequested: break;
        }
    }

    if (r.frozen) {
        const std::uint64_t age = r.frame >= r.frozenFrame ? r.frame - r.frozenFrame : 0;
        lines.push_back({ Format("Frozen at frame %llu (age %llu)", U64(r.frozenFrame), U64(age)), ViewCullHudTone::Normal });
    } else {
        lines.push_back({ "Cull camera: Scene camera (live)", ViewCullHudTone::Normal });
    }

    if (r.claimedInstances > 0) {
        lines.push_back({ Format("Instances on the GPU path: %u", r.claimedInstances), ViewCullHudTone::Dim });
    }
    lines.push_back({ "Only cluster meshes are counted. CPU-drawn meshes are never culled here.", ViewCullHudTone::Dim });
    if (lines.size() > kMaxHudLines) {
        lines.resize(kMaxHudLines);
    }
    return lines;
}

} // namespace gte
