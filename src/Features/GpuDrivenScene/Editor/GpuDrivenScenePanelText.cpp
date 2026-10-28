#include "GpuDrivenScenePanelText.h"

#include <cstdio>

namespace gte {

namespace {

std::string_view HizReasonText(HizReason reason) noexcept
{
    switch (reason) {
    case HizReason::None: return "not ready";
    case HizReason::Unavailable: return "not available";
    case HizReason::TooLarge: return "view is too large";
    }
    return "not ready";
}

} // namespace

std::string_view CullModeLine(GpuCullMode mode) noexcept
{
    switch (mode) {
    case GpuCullMode::NoCull: return "Draws everything.";
    case GpuCullMode::Frustum: return "Draws only what the game camera sees. The Scene view shows the same set.";
    case GpuCullMode::HiZ: return "Frustum plus occlusion. Occlusion works on GPU-path meshes only.";
    }
    return {};
}

std::string GpuPathLine(const GpuDrivenSceneViewSnapshot& v)
{
    const GpuPathReport& g = v.gpu;
    std::string line = "GPU path: " + std::to_string(g.claimedEntities) + " entities, " +
        std::to_string(g.testedClusters) + (g.sharedSurvivors ? " clusters (primary view's survivors), " : " clusters tested, ");
    if (g.visibleValid) {
        line += std::to_string(g.visibleClusters) + " visible";
    } else {
        line += g.statsCollected ? "visible count not ready" : "stats are off";
    }
    return line;
}

std::string CpuPathLine(const GpuDrivenSceneViewSnapshot& v)
{
    return "CPU path: " + std::to_string(v.cpuTested) + " tested, " + std::to_string(v.cpuCulled) + " culled, " +
        std::to_string(v.cpuUnbounded) + " kept without bounds (last frame)";
}

std::string CpuReasonsLine(const GpuDrivenSceneViewSnapshot& v)
{
    const CpuPathReasons& r = v.gpu.cpuPath;
    if (r.Total() == 0) {
        return {};
    }
    std::string line = "On the CPU path (no Hi-Z):";
    const auto add = [&line](std::uint32_t count, std::string_view reason) {
        if (count > 0) {
            line += (line.back() == ':' ? " " : ", ") + std::to_string(count) + " " + std::string(reason);
        }
    };
    add(r.noClusterTable, "without a cluster table (alpha-tested, refused or primitive)");
    add(r.noSlot, "without a free part slot");
    add(r.skinned, "skinned");
    add(r.tooFewInstances, "with too few instances");
    add(r.unsupportedLayout, "with an unsupported layout or no material");
    add(r.noIndexBuffer, "without an index buffer");
    add(r.nonFinite, "with a non-finite matrix");
    return line;
}

std::string HizLine(const GpuDrivenSceneViewSnapshot& v, bool secondary)
{
    switch (v.gpu.occlusion) {
    case OcclusionState::Applied: return secondary ? "Hi-Z: primary view's survivors" : "Hi-Z: active";
    case OcclusionState::NotRequested: return "Hi-Z: off";
    case OcclusionState::NoCullCamera: return "Hi-Z: no cull camera";
    case OcclusionState::Unusable: break;
    }
    if (secondary && v.gpu.hizReason == HizReason::Unavailable) {
        return "Hi-Z: primary view only (acts as Frustum)";
    }
    return "Hi-Z: off (" + std::string(HizReasonText(v.gpu.hizReason)) + ")";
}

std::string CullCameraLine(const GpuDrivenSceneSettings& s, const GpuDrivenSceneViewSnapshot& v)
{
    switch (v.cullCamera) {
    case CullCameraSource::Own: return "Cull camera: own";
    case CullCameraSource::Primary: return "Cull camera: game camera";
    case CullCameraSource::None: break;
    }
    return s.mode == GpuCullMode::NoCull ? "Cull camera: none (culling is off)" : "Cull camera: none (no active camera)";
}

std::string CpuCullStateLine(const GpuDrivenSceneViewSnapshot& v)
{
    switch (v.cpuCullState) {
    case CpuCullState::Active: return {};
    case CpuCullState::Off: return "CPU cull: off";
    case CpuCullState::SuppressedCapture: return "CPU cull suppressed: frame capture";
    case CpuCullState::NoCamera: return "CPU cull: no camera";
    }
    return {};
}

std::string_view NoGpuPathLine(const GpuDrivenSceneSettings& s, const GpuDrivenSceneViewSnapshot& v) noexcept
{
    if (s.mode == GpuCullMode::HiZ && v.gpu.claimedEntities == 0 && v.cpuTested > 0) {
        return "No mesh is on the GPU path. Hi-Z cannot test CPU-drawn meshes.";
    }
    return {};
}

std::string_view CullHelpLine() noexcept
{
    return "Compare milliseconds. FPS is capped by vsync unless started with --no-vsync. "
           "Frustum is on by default. Hi-Z needs the primary view to be rendered.";
}

std::string ReasonsLine(const GpuDrivenSceneViewSnapshot& v)
{
    const GpuPathReport& g = v.gpu;
    if (!g.statsCollected) {
        return "Reasons: enable stats (Advanced) to see why clusters were rejected";
    }
    if (!g.reasons.valid) {
        return "Reasons: not ready";
    }
    const fx::ClusterReasonCounts& r = g.reasons;
    return "Rejected by frustum " + std::to_string(r.frustum) + ", cone " + std::to_string(r.cone) + ", pyramid " +
        std::to_string(r.pyramid) + "; kept " + std::to_string(r.kept) + " (first phase drew " +
        std::to_string(r.history) + " from history)";
}

std::string InertLine(const GpuDrivenSceneSettings& s, const GpuDrivenSceneViewSnapshot& v)
{
    if (!s.enabled || !v.gpu.refusal.empty()) {
        return {}; // The refusal text already says why.
    }
    switch (v.gpu.inertReason) {
    case fx::ClusterInertReason::None: break;
    case fx::ClusterInertReason::DeviceCaps: return "GPU path inert: the device lacks multi draw indirect";
    case fx::ClusterInertReason::SharedScope: return "GPU path inert: no view to draw into";
    case fx::ClusterInertReason::CaptureArmed: return "GPU path inert: a frame capture is armed (Frame Debugger)";
    case fx::ClusterInertReason::ToggledOff: return "GPU path inert: its render passes are toggled off";
    case fx::ClusterInertReason::InvalidState: return "GPU path inert: the draw state is invalid";
    }
    if (v.gpu.claimedEntities == 0 && v.cpuTested > 0) {
        return "GPU path idle: no mesh qualified. It stays off in the pipelined regime (see the Render Graph panel)";
    }
    return {};
}

CullTimeSplits SplitCullTime(std::span<const CullTimeRow> rows, std::string_view cullFeature)
{
    struct Sum {
        double total = 0.0;
        double cull = 0.0;
        std::uint32_t passes = 0;
        std::uint32_t measured = 0;
        std::uint32_t cullPasses = 0;
        std::uint32_t cullMeasured = 0;
    };
    Sum primary;
    Sum secondary;
    for (const CullTimeRow& row : rows) {
        if (row.culled || row.view == CullTimeView::Other) {
            continue;
        }
        Sum& sum = row.view == CullTimeView::Primary ? primary : secondary;
        const bool isCull = row.owner == cullFeature;
        ++sum.passes;
        sum.cullPasses += isCull ? 1 : 0;
        if (row.ms.has_value()) {
            ++sum.measured;
            sum.total += *row.ms;
            sum.cullMeasured += isCull ? 1 : 0;
            sum.cull += isCull ? *row.ms : 0.0;
        }
    }
    const auto finish = [](const Sum& sum) {
        CullTimeSplit split;
        split.passes = sum.passes;
        split.measured = sum.measured;
        const bool totalKnown = sum.passes > 0 && sum.measured == sum.passes;
        const bool cullKnown = sum.cullPasses > 0 && sum.cullMeasured == sum.cullPasses;
        if (totalKnown) {
            split.total = sum.total;
        }
        if (cullKnown) {
            split.cull = sum.cull;
        }
        if (totalKnown && cullKnown) {
            split.other = sum.total - sum.cull;
        }
        return split;
    };
    return CullTimeSplits{ finish(primary), finish(secondary) };
}

std::string CullTimeLine(const CullTimeSplit& split)
{
    const auto text = [](const std::optional<double>& ms) {
        if (!ms.has_value()) {
            return std::string("-");
        }
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.2f", *ms);
        return std::string(buffer);
    };
    return "GPU time: total " + text(split.total) + " ms, culling feature " + text(split.cull) +
        " ms, everything else " + text(split.other) + " ms";
}

std::string_view ConeWarningLine() noexcept
{
    return "Only correct for closed meshes. The Scene view tests the cone from the game camera eye, "
           "so back faces can vanish there.";
}

} // namespace gte
