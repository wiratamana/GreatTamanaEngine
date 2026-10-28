#include "GpuDrivenScenePanelText.h"

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

std::string_view CullModeLine(CullMode mode) noexcept
{
    switch (mode) {
    case CullMode::NoCull: return "Draws everything.";
    case CullMode::Frustum: return "Draws only what the game camera sees. The Scene view shows the same set.";
    case CullMode::HiZ: return "Frustum plus occlusion. Occlusion works on GPU-path meshes only.";
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
    return s.mode == CullMode::NoCull ? "Cull camera: none (culling is off)" : "Cull camera: none (no active camera)";
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
    if (s.mode == CullMode::HiZ && v.gpu.claimedEntities == 0 && v.cpuTested > 0) {
        return "No mesh is on the GPU path. Hi-Z cannot test CPU-drawn meshes.";
    }
    return {};
}

std::string_view CullHelpLine() noexcept
{
    return "Compare milliseconds. FPS is capped by vsync unless started with --no-vsync. "
           "Frustum is on by default. Hi-Z needs the primary view to be rendered.";
}

std::string_view ConeWarningLine() noexcept
{
    return "Only correct for closed meshes. The Scene view tests the cone from the game camera eye, "
           "so back faces can vanish there.";
}

} // namespace gte
