#include "ClusterViewerWindowInternal.h"

#include "CameraPick.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gte {

using namespace viewer_detail;

namespace {

ClusterViewSlot ToSlot(ClusterViewerImageTarget target) noexcept
{
    return target == ClusterViewerImageTarget::DebugPreview ? ClusterViewSlot::Preview : ClusterViewSlot::Main;
}

} // namespace

ClusterViewerResult ClusterViewerWindow::Open(Renderer& renderer, const std::string& absolutePath)
{
    if (!IsOpen()) {
        Show();
    }
    m_pendingPath.clear();
    LoadMesh(renderer, absolutePath);
    if (!m_valid) {
        const ClusterViewerError error = m_load.banner == ClusterViewerBanner::NoDeviceSupport
            ? ClusterViewerError::Unsupported
            : ClusterViewerError::NoValidMesh;
        return MakeError(error, m_load.text);
    }
    return MakeOk(m_load.text);
}

ClusterViewerReport ClusterViewerWindow::Report() const
{
    ClusterViewerReport report;
    report.open = IsOpen();
    report.path = m_assetPath;
    report.valid = m_valid;
    report.clusterPathSupported = !m_rendererFailed && m_load.banner != ClusterViewerBanner::NoDeviceSupport;

    report.mesh.vertices = m_vertexCount;
    report.mesh.triangles = m_triangleCount;
    report.mesh.clusters = m_clusterCount;
    report.mesh.clusterModes = m_load.clusterModes;
    report.mesh.note = m_load.text;

    const auto add = [&](const char* key, std::string value) { report.settings.push_back({ key, std::move(value) }); };
    const auto number = [](float v) { return std::to_string(v); };
    const auto boolean = [](bool v) { return std::string(v ? "true" : "false"); };
    add("drawMode", std::string(ClusterViewerDrawModeName(m_settings.drawMode)));
    add("frustum", boolean(m_settings.frustum));
    add("cone", boolean(m_settings.cone));
    add("occlusion", boolean(m_settings.occlusion));
    add("culledStyle", std::string(ClusterViewerCulledStyleName(m_settings.culledStyle)));
    add("debugCamera", boolean(m_debug.show));
    add("cameraPreview", boolean(m_settings.cameraPreview));
    add("cameraSelected", boolean(m_debug.selected));
    add("showSceneCamera", boolean(m_settings.showSceneCamera));
    add("planes", boolean(m_settings.showPlanes));
    const Vec3 euler = m_debug.transform.rotation.ToEulerDegrees();
    add("camX", number(m_debug.transform.position.x));
    add("camY", number(m_debug.transform.position.y));
    add("camZ", number(m_debug.transform.position.z));
    add("camPitch", number(euler.x));
    add("camYaw", number(euler.y));
    add("camRoll", number(euler.z));
    add("camFov", number(m_debug.fovYDegrees));
    add("camNear", number(m_debug.nearZ));
    add("camFar", number(m_debug.farZ));
    add("camAspect", number(m_debug.aspect));
    add("orbitYaw", number(m_orbit.yawDeg));
    add("orbitPitch", number(m_orbit.pitchDeg));
    add("orbitDistance", number(m_orbit.distance));
    add("orbitTargetX", number(m_orbit.target.x));
    add("orbitTargetY", number(m_orbit.target.y));
    add("orbitTargetZ", number(m_orbit.target.z));
    add("encodeWidth", std::to_string(m_settings.encodeWidth));

    ClusterViewerCullReport& cull = report.cull;
    cull.ok = m_hasRun && ActiveRunError().empty() && m_stats.valid;
    cull.total = m_run.counts.total;
    cull.passedFrustum = m_run.counts.passedFrustum;
    cull.passedCone = m_run.counts.passedCone;
    cull.visible = m_run.counts.visible;
    cull.visibleTriangles = m_run.counts.visibleTriangles;
    cull.runMilliseconds = m_run.runMilliseconds;
    cull.occlusionApplied = m_run.occlusionApplied;
    cull.hizCut = m_run.hizCut;
    cull.nested = m_stats.valid;
    cull.error = ActiveRunError().empty() ? m_stats.error : ActiveRunError();
    if (m_stats.valid) {
        cull.byFrustum = m_stats.byFrustum;
        cull.byCone = m_stats.byCone;
        cull.byOcclusion = m_stats.byOcclusion;
    }

    report.hiz.captured = m_hiz.has_value();
    report.hiz.stale = HizIsStale();
    report.hiz.width = m_hiz ? m_hiz->width : 0;
    report.hiz.height = m_hiz ? m_hiz->height : 0;

    report.truth.valid = m_truthValid;
    report.truth.stale = TruthIsStale();
    report.truth.seen = m_truth.seen;
    report.truth.falseCulls = m_truth.falseCulls;
    report.truth.conservative = m_truth.conservative;
    report.truth.invalidIds = static_cast<std::uint32_t>(std::min<std::uint64_t>(m_truthInvalidIds, 0xFFFFFFFFu));
    report.truth.pixelsChecked = m_truthPixels;

    report.debugCamera.show = m_debug.show;
    report.debugCamera.selected = m_debug.selected;
    Vec2 apex;
    if (ProjectToScreen(m_debug.transform.position, OrbitViewProjection(ViewAspect()), Vec2(0.0f, 0.0f), Vec2(1.0f, 1.0f), apex)) {
        report.debugCamera.apexX = apex.x;
        report.debugCamera.apexY = apex.y;
        report.debugCamera.screenVisible = apex.x >= 0.0f && apex.x <= 1.0f && apex.y >= 0.0f && apex.y <= 1.0f;
    }
    return report;
}

ClusterViewerResult ClusterViewerWindow::Set(std::span<const KeyValue> pairs)
{
    if (!IsOpen()) {
        return MakeError(ClusterViewerError::NotOpen, "the viewer is not open");
    }
    std::vector<ClusterViewerKeyValue> parsed;
    parsed.reserve(pairs.size());
    for (const KeyValue& pair : pairs) {
        parsed.emplace_back(pair.key, pair.value);
    }
    const ClusterViewerSettings savedSettings = m_settings;
    const DebugCameraState savedDebug = m_debug;
    const ClusterViewerSetResult result = ParseClusterViewerSet(parsed, m_settings, m_debug);
    if (!result.Ok()) {
        return MakeError(ClusterViewerError::BadInput, result.error);
    }
    if (result.copySceneCamera && !CopySceneCamera()) {
        m_settings = savedSettings;
        m_debug = savedDebug;
        return MakeError(ClusterViewerError::Conflict, "no Camera entity is selected in the Scene");
    }
    ApplyOrbitRequest(result.orbit);
    if (!m_debug.show) {
        m_debug.selected = false;
    }
    return MakeOk("applied");
}

ClusterViewerResult ClusterViewerWindow::CaptureHiz()
{
    ClusterViewerResult result = CaptureHizNow();
    if (result.ok) {
        EnsureRequestSize();
        RenderMain(m_requestWidth, m_requestHeight);
    }
    return result;
}

ClusterViewerPickResult ClusterViewerWindow::Pick(float x, float y)
{
    ClusterViewerPickResult out;
    if (const std::optional<ClusterViewerResult> notReady = NotReady(false)) {
        out.error = notReady->error;
        out.message = notReady->message;
        return out;
    }
    if (!(x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f)) {
        out.error = ClusterViewerError::BadInput;
        out.message = "x and y must be in 0..1";
        return out;
    }
    out.ok = true;
    if (!m_debug.show || !DebugCameraIsValid(m_debug)) {
        out.message = "the debug camera is not shown";
        return out;
    }
    const auto [requestWidth, requestHeight] = RequestSize();
    const float width = static_cast<float>(requestWidth);
    const float height = static_cast<float>(requestHeight);
    const CameraPickCandidate candidate{ 1, DebugFrustum() };
    const std::optional<CameraPickHit> hit = PickCamera(std::span<const CameraPickCandidate>(&candidate, 1),
        OrbitViewProjection(ViewAspect()), Vec2(0.0f, 0.0f), Vec2(width, height), Vec2(x * width, y * height));
    if (hit) {
        out.hit = true;
        m_debug.selected = true;
    }
    return out;
}

std::optional<ClusterViewerImage> ClusterViewerWindow::CaptureImage(ClusterViewerImageTarget target, bool overlay)
{
    if (NotReady(false)) {
        return std::nullopt;
    }
    const ClusterViewSlot slot = ToSlot(target);
    if (slot == ClusterViewSlot::Main) {
        EnsureRequestSize();
        RenderMain(m_requestWidth, m_requestHeight);
        if (m_mainTexture == UiTextureHandle::Invalid) {
            return std::nullopt;
        }
    } else {
        RenderPreview();
        if (!m_previewActive || m_previewTexture == UiTextureHandle::Invalid) {
            return std::nullopt;
        }
    }
    std::optional<CapturedPixels> pixels = m_renderer->CaptureSlot(slot);
    if (!pixels) {
        return std::nullopt;
    }
    ClusterViewerImage image;
    image.pixels = std::move(*pixels);
    if (overlay && slot == ClusterViewSlot::Main && image.pixels.width > 0 && image.pixels.height > 0) {
        AppendOverlay(OrbitViewProjection(ViewAspect()), Vec2(0.0f, 0.0f),
            Vec2(static_cast<float>(image.pixels.width), static_cast<float>(image.pixels.height)), false, image.overlay,
            image.overlayQuads, nullptr);
    }
    return image;
}

} // namespace gte
