#include "GpuDrivenScenePanelText.h"

namespace gte {

namespace {

std::string_view HizReasonText(HizReason reason) noexcept
{
    switch (reason) {
    case HizReason::None: return "not ready";
    case HizReason::NoHistory:
    case HizReason::WarmingUp: return "warming up";
    case HizReason::Resized: return "view resized";
    case HizReason::CameraCut: return "camera cut";
    case HizReason::BuildOff: return "pyramid build is off";
    case HizReason::Unavailable: return "not available";
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
    std::string line = "GPU path: " + std::to_string(v.claimedEntities) + " entities, " +
        std::to_string(v.testedClusters) + " clusters tested, ";
    if (v.visibleValid) {
        line += std::to_string(v.visibleClusters) + " visible";
    } else {
        line += v.statsCollected ? "visible count not ready" : "stats are off";
    }
    return line;
}

std::string CpuPathLine(const GpuDrivenSceneViewSnapshot& v)
{
    return "CPU path: " + std::to_string(v.cpuTested) + " tested, " + std::to_string(v.cpuCulled) + " culled, " +
        std::to_string(v.cpuUnbounded) + " kept without bounds (last frame)";
}

std::string HizLine(const GpuDrivenSceneViewSnapshot& v, bool secondary)
{
    switch (v.occlusion) {
    case OcclusionState::Applied: return "Hi-Z: active";
    case OcclusionState::NotRequested: return "Hi-Z: off";
    case OcclusionState::NoCullCamera: return "Hi-Z: no cull camera";
    case OcclusionState::Unusable: break;
    }
    if (secondary && v.hizReason == HizReason::Unavailable) {
        return "Hi-Z: shared pyramid not available (acts as Frustum)";
    }
    return "Hi-Z: off (" + std::string(HizReasonText(v.hizReason)) + ")";
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
    if (s.mode == CullMode::HiZ && v.claimedEntities == 0 && v.cpuTested > 0) {
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
