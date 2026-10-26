#include "ClusterViewerWindowInternal.h"

#include "CameraFrustumOverlay.h"
#include "ClusterToolUiState.h"
#include "EditorContext.h"
#include "ProjectRootPath.h"
#include "SceneCameraFrustum.h"
#include "TransformGizmo.h"
#include "../Assets/AssetTypes.h"
#include "../Assets/GtaFile.h"
#include "../Assets/MeshClusterValidation.h"
#include "../Assets/MeshFile.h"
#include "../Core/Logging.h"
#include "../ECS/Registry.h"
#include "../Math/MathTypes.h"
#include "../Renderer/Culling/ClusterCullMath.h"
#include "../Renderer/Renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <system_error>
#include <tuple>

namespace gte {

using namespace viewer_detail;

namespace {

constexpr const char* kLog = "ClusterViewer";
constexpr int kPreviewWidth = 320;
constexpr int kDefaultWidth = 960;
constexpr int kDefaultHeight = 544;
constexpr auto kStatInterval = std::chrono::milliseconds(500);
constexpr std::uint32_t kMeshRetryUiFrames = 60; // UI frames between two upload retries

std::array<float, 16> ToArray(const Mat4& m) noexcept
{
    std::array<float, 16> a{};
    std::copy_n(m.Data(), 16, a.begin());
    return a;
}

} // namespace

ClusterViewerWindow::ClusterViewerWindow() = default;

ClusterViewerWindow::~ClusterViewerWindow()
{
    Reset();
}

void ClusterViewerWindow::RequestOpen(const std::string& absoluteGtaPath)
{
    m_open = true;
    m_pendingPath = absoluteGtaPath;
}

void ClusterViewerWindow::Reset()
{
    Close();
    m_pendingPath.clear();
    m_hasLoaded = false;
    m_assetPath.clear();
    m_absolutePath.clear();
    m_ctx = nullptr;
    m_registry = nullptr;
}

void ClusterViewerWindow::CancelGizmo()
{
    if (!m_gizmoDrawn) {
        return;
    }
    GizmoIdScope scope("ClusterViewerDebugCamera");
    CancelGizmoInteraction();
    m_gizmoDrawn = false;
}

void ClusterViewerWindow::Close()
{
    CancelGizmo();
    m_open = false;
    m_orbiting = false;
    m_panning = false;
    m_valid = false;
    m_load = ClusterViewerLoadDecision{};
    m_rendererFailed = false;
    ClearDerived();
    m_renderer.reset(); // idles the GPU, then frees every buffer, target and UI handle
}

void ClusterViewerWindow::ClearDerived()
{
    m_mainKeyValid = false;
    m_mainTexture = UiTextureHandle::Invalid;
    m_previewKeyValid = false;
    m_previewActive = false;
    m_previewTexture = UiTextureHandle::Invalid;
    m_hasRun = false;
    m_run = ClusterViewRender{};
    m_stats = ClusterViewerStats{};
    m_runError.clear();
    m_cameraError.clear();
    m_previewError.clear();
    m_truthValid = false;
    m_truth = ClusterTruthResult{};
    m_truthError.clear();
    m_hiz.reset();
    m_hizError.clear();
    if (m_renderer) {
        m_renderer->ReleaseHiz();
    }
}

ClusterViewerWindow::FileIdentity ClusterViewerWindow::StatFile(const std::filesystem::path& path)
{
    FileIdentity identity;
    std::error_code timeEc;
    std::error_code sizeEc;
    identity.writeTime = std::filesystem::last_write_time(path, timeEc);
    identity.size = std::filesystem::file_size(path, sizeEc);
    identity.ok = !timeEc && !sizeEc;
    return identity;
}

bool ClusterViewerWindow::EnsureRenderer(Renderer& renderer)
{
    if (m_renderer) {
        return true;
    }
    if (m_rendererFailed) {
        return false;
    }
    std::string whyNot;
    m_renderer = ClusterViewRenderer::TryCreate(renderer, &whyNot);
    if (!m_renderer) {
        m_rendererFailed = true;
        m_rendererWhyNot = std::move(whyNot);
        return false;
    }
    m_rendererWhyNot.clear();
    return true;
}

void ClusterViewerWindow::LoadMesh(Renderer& renderer, const std::string& path)
{
    const bool sameAsset = m_hasLoaded && path == m_assetPath;
    m_assetPath = path;
    m_hasLoaded = true;
    m_lastStat = std::chrono::steady_clock::now();
    ClearDerived();
    ++m_dataGeneration;
    m_valid = false;
    m_load = ClusterViewerLoadDecision{};
    m_meshRetryPending = false;
    m_vertexCount = 0;
    m_triangleCount = 0;
    m_clusterCount = 0;

    std::filesystem::path absolute;
    if (const std::optional<std::string> invalid = ValidateMeshAssetPath(path, ResolveProjectRootDirectory(), absolute)) {
        m_absolutePath = absolute;
        m_identity = StatFile(absolute);
        m_load.banner = ClusterViewerBanner::DecodeFailed;
        m_load.text = *invalid;
        CancelGizmo();
        m_renderer.reset();
        GTE_LOG_WARNING(kLog, "open refused: " + *invalid);
        return;
    }
    m_absolutePath = absolute;
    m_identity = StatFile(absolute);

    ClusterViewerLoadInput input;
    input.pathSupported = EnsureRenderer(renderer);
    if (!input.pathSupported) {
        CancelGizmo();
        m_load = DecideClusterViewerLoad(input);
        if (!m_rendererWhyNot.empty()) {
            m_load.text = m_rendererWhyNot;
        }
        return;
    }
    input.maxClusters = m_renderer->MaxClusters();

    // The decoded mesh and the vertex copy die at the end of this block; only counts and bounds are kept.
    ClusterViewMeshResult meshResult;
    bool uploaded = false;
    {
        std::optional<GtaFileData> gta = ReadGtaFile(absolute);
        if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
            input.decodeError = "The file is not a readable mesh asset.";
        } else {
            std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload, &input.clusterTailRejection);
            std::vector<std::uint8_t>().swap(gta->payload);
            if (!mesh.has_value()) {
                input.decodeError = "The mesh payload cannot be decoded.";
            } else {
                input.decodeOk = true;
                const MeshPreviewData preview = BuildMeshPreviewData(mesh->positions, mesh->normals);
                input.vertexCount = preview.vertices.size();
                input.indexCount = mesh->indices.size();
                input.indicesValid = ValidateMeshIndices(preview.vertices.size(), mesh->indices, nullptr);
                input.clusterCount = mesh->clusters.size();
                input.tableValid = !mesh->clusters.empty()
                    && ValidateClusterTable(mesh->clusters, mesh->indices.size(), &input.tableWhy);

                m_load = DecideClusterViewerLoad(input);
                if (m_load.showMesh) {
                    const std::span<const MeshCluster> clusters = m_load.clusterModes
                        ? std::span<const MeshCluster>(mesh->clusters)
                        : std::span<const MeshCluster>();
                    meshResult = m_renderer->SetMesh(preview.vertices, mesh->indices, clusters);
                    uploaded = meshResult.meshSet;
                    m_vertexCount = preview.vertices.size();
                    m_triangleCount = mesh->indices.size() / 3;
                    m_center = preview.center;
                    m_radius = preview.radius;
                }
            }
        }
    }

    if (!input.decodeOk) {
        m_load = DecideClusterViewerLoad(input);
    }
    if (m_load.showMesh && !uploaded) {
        m_load.showMesh = false;
        m_load.clusterModes = false;
        m_load.banner = ClusterViewerBanner::DecodeFailed;
        m_load.text = meshResult.note.empty() ? std::string("The mesh could not be uploaded.") : meshResult.note;
    } else if (m_load.clusterModes && !meshResult.clustersUsable) {
        m_load.clusterModes = false;
        m_load.banner = ClusterViewerBanner::DamagedTable;
        m_load.text = "The cluster table is damaged: " + meshResult.note + ". Cluster modes are unavailable.";
    }

    m_valid = m_load.showMesh;
    if (!m_valid) {
        CancelGizmo();
        GTE_LOG_WARNING(kLog, "mesh not shown: " + m_load.text);
        if (meshResult.retryable) {
            m_meshRetryPending = true; // the renderer stays: the GPU may still read the old mesh
            m_meshRetryFrames = kMeshRetryUiFrames;
        } else {
            m_renderer.reset(); // nothing stays on the GPU for a mesh that is not shown
        }
        return;
    }
    m_clusterCount = m_renderer->ClusterCount();

    if (!sameAsset) {
        m_orbit.Frame(m_center, m_radius);
        ResetDebugCamera(false);
    } else {
        const float ratio = m_radius / std::max(m_orbit.radius, 1.0e-6f);
        m_orbit.radius = m_radius;
        if (ratio > 2.0f || ratio < 0.5f) {
            ResetDebugCamera(true);
        }
    }
}

void ClusterViewerWindow::PollFile(Renderer& renderer)
{
    if (!m_hasLoaded || m_absolutePath.empty()) {
        return;
    }
    if (m_meshRetryPending) {
        if (renderer.DeviceLost()) {
            m_meshRetryPending = false;
            m_load.text = "The GPU device is lost; the mesh cannot be uploaded.";
        } else if (m_meshRetryFrames > 0) {
            --m_meshRetryFrames;
        } else {
            LoadMesh(renderer, m_assetPath);
            return;
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastStat < kStatInterval) {
        return;
    }
    m_lastStat = now;
    if (StatFile(m_absolutePath) != m_identity) {
        LoadMesh(renderer, m_assetPath);
    }
}

ClusterCullFlags ClusterViewerWindow::CurrentFlags() const noexcept
{
    ClusterCullFlags flags = ClusterCullFlags::None;
    if (m_settings.frustum) {
        flags = flags | ClusterCullFlags::Frustum;
    }
    if (m_settings.cone) {
        flags = flags | ClusterCullFlags::BackfaceCone;
    }
    if (m_settings.occlusion) {
        flags = flags | ClusterCullFlags::Occlusion;
    }
    return flags;
}

std::pair<int, int> ClusterViewerWindow::RequestSize() const
{
    if (m_requestWidth > 0 && m_requestHeight > 0) {
        return { m_requestWidth, m_requestHeight };
    }
    return { kDefaultWidth, kDefaultHeight };
}

void ClusterViewerWindow::EnsureRequestSize()
{
    std::tie(m_requestWidth, m_requestHeight) = RequestSize();
}

float ClusterViewerWindow::ViewAspect() const noexcept
{
    const auto [width, height] = RequestSize();
    return static_cast<float>(width) / static_cast<float>(height);
}

const std::string& ClusterViewerWindow::ActiveRunError() const noexcept
{
    return m_cameraError.empty() ? m_runError : m_cameraError;
}

Mat4 ClusterViewerWindow::OrbitViewProjection(float aspect) const
{
    return m_orbit.Projection(aspect) * m_orbit.View();
}

FrustumInput ClusterViewerWindow::DebugFrustum() const
{
    FrustumInput input;
    input.position = m_debug.transform.position;
    input.rotation = m_debug.transform.rotation;
    input.fovYDegrees = m_debug.fovYDegrees;
    input.nearZ = m_debug.nearZ;
    input.farZ = m_debug.farZ;
    input.aspect = m_debug.aspect;
    return input;
}

std::optional<FrustumInput> ClusterViewerWindow::SelectedSceneCamera() const
{
    if (m_ctx == nullptr || m_registry == nullptr || m_ctx->selection.Kind() != InspectorSelectionKind::Entity) {
        return std::nullopt;
    }
    const Entity entity = m_ctx->selection.SelectedEntity();
    if (!m_registry->IsAlive(entity)) {
        return std::nullopt;
    }
    // An entity without a Camera gives an invalid frustum.
    const FrustumInput input = MakeSceneCameraFrustum(*m_registry, entity, GameViewAspect(*m_ctx));
    if (!FrustumInputIsValid(input)) {
        return std::nullopt;
    }
    return input;
}

bool ClusterViewerWindow::CopySceneCamera()
{
    const std::optional<FrustumInput> camera = SelectedSceneCamera();
    if (!camera) {
        return false;
    }
    m_debug.transform.position = camera->position;
    m_debug.transform.rotation = camera->rotation;
    m_debug.fovYDegrees = camera->fovYDegrees;
    m_debug.nearZ = camera->nearZ;
    m_debug.farZ = camera->farZ;
    m_debug = ClampDebugCamera(m_debug);
    return true;
}

void ClusterViewerWindow::ResetDebugCamera(bool keepShow)
{
    const bool show = m_debug.show;
    m_debug = DefaultDebugCamera(m_center, m_radius);
    m_debug.show = keepShow && show;
}

void ClusterViewerWindow::SnapDebugCameraToView()
{
    m_debug.transform.position = m_orbit.Eye();
    m_debug.transform.rotation = ClusterViewerLookRotation(m_orbit.Eye(), m_orbit.target);
    m_debug = ClampDebugCamera(m_debug);
}

void ClusterViewerWindow::ApplyOrbitRequest(const ClusterViewerOrbitRequest& request)
{
    if (request.yawDeg) {
        m_orbit.yawDeg = *request.yawDeg;
    }
    if (request.pitchDeg) {
        m_orbit.pitchDeg = std::clamp(*request.pitchDeg, -OrbitCamera::kMaxPitchDegrees, OrbitCamera::kMaxPitchDegrees);
    }
    if (request.distance) {
        m_orbit.distance = std::clamp(*request.distance, m_orbit.MinDistance(), m_orbit.MaxDistance());
    }
    if (request.targetX) {
        m_orbit.target.x = *request.targetX;
    }
    if (request.targetY) {
        m_orbit.target.y = *request.targetY;
    }
    if (request.targetZ) {
        m_orbit.target.z = *request.targetZ;
    }
}

std::pair<int, int> ClusterViewerWindow::VerifySize() const
{
    const int width = std::clamp(static_cast<int>(m_settings.encodeWidth), 64, kMaxImageSize);
    const float aspect = (std::isfinite(m_debug.aspect) && m_debug.aspect > 0.0f) ? m_debug.aspect : 16.0f / 9.0f;
    const int height = std::clamp(static_cast<int>(std::lround(static_cast<float>(width) / aspect)), 1, kMaxImageSize);
    return { width, height };
}

void ClusterViewerWindow::AppendOverlay(const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax, bool debugHovered,
    std::vector<OverlaySegment2D>& out, std::vector<OverlayQuad2D>& quads, std::vector<OverlayLabel2D>* labels) const
{
    if (m_settings.showSceneCamera) {
        if (const std::optional<FrustumInput> sceneCamera = SelectedSceneCamera()) {
            AppendCameraOverlay(*sceneCamera, true, viewProjection, rectMin, rectMax, kSceneCameraViewerColor, 1.0f, out, &quads);
        }
    }
    if (m_debug.show && DebugCameraIsValid(m_debug)) {
        const bool strong = m_debug.selected || debugHovered;
        const OverlayColor color = m_debug.selected ? kDebugCameraSelectedColor : kDebugCameraColor;
        AppendCameraOverlay(DebugFrustum(), true, viewProjection, rectMin, rectMax, color, strong ? 2.5f : 1.5f, out,
            m_debug.selected ? &quads : nullptr);
        if (m_settings.showPlanes) {
            const std::array<Plane, 6> planes = ExtractFrustumPlanes(DebugCameraViewProjection(m_debug));
            AppendPlaneArrowOverlay(DebugFrustum(), planes, viewProjection, rectMin, rectMax, color, 1.5f, out, labels);
        }
    }
}

ClusterViewerWindow::FrameKey ClusterViewerWindow::MakeKey(const ClusterViewFrame& frame, std::uint32_t drawMode) const
{
    FrameKey key;
    key.viewProjection = ToArray(frame.viewProjection);
    key.cullViewProjection = ToArray(frame.cull.viewProjection);
    key.eye = { frame.cull.eye.x, frame.cull.eye.y, frame.cull.eye.z };
    key.width = frame.width;
    key.height = frame.height;
    key.drawMode = drawMode;
    key.flags = static_cast<std::uint32_t>(frame.cull.flags);
    key.dim = frame.dimCulled;
    key.hizCaptureId = m_hiz ? m_hiz->captureId : 0;
    key.dataGeneration = m_dataGeneration;
    return key;
}

void ClusterViewerWindow::RenderMain(int width, int height)
{
    if (!m_renderer || !m_valid) {
        return;
    }
    const ClusterViewerDrawMode mode = m_load.clusterModes ? m_settings.drawMode : ClusterViewerDrawMode::Normal;

    ClusterViewFrame frame;
    frame.viewProjection = OrbitViewProjection(static_cast<float>(width) / static_cast<float>(height));
    frame.width = width;
    frame.height = height;
    frame.shade = mode == ClusterViewerDrawMode::Normal ? ClusterViewShade::Clay : ClusterViewShade::Tint;
    if (mode == ClusterViewerDrawMode::Cull) {
        std::string why;
        if (!DebugCameraIsValid(m_debug, &why)) {
            m_cameraError = "debug camera: " + why;
            return;
        }
        frame.cull.flags = CurrentFlags();
        frame.cull.viewProjection = DebugCameraViewProjection(m_debug);
        frame.cull.eye = m_debug.transform.position;
        frame.dimCulled = m_settings.culledStyle == ClusterViewerCulledStyle::Dim;
    }
    m_cameraError.clear();

    const FrameKey key = MakeKey(frame, static_cast<std::uint32_t>(mode));
    if (m_mainKeyValid && key == m_mainKey) {
        return;
    }
    m_mainKey = key;
    m_mainKeyValid = true;

    const ClusterViewRender run = m_renderer->Render(ClusterViewSlot::Main, frame);
    if (!run.ok) {
        m_mainTexture = UiTextureHandle::Invalid; // a failed run may have replaced the target
        m_runError = run.note.empty() ? std::string("the viewer run failed") : run.note;
        return;
    }
    m_runError.clear();
    m_mainTexture = run.texture;
    m_run = run;
    m_stats = BuildClusterViewerStats(run.counts);
    m_hasRun = true;
}

void ClusterViewerWindow::RenderPreview()
{
    if (!m_renderer) {
        return;
    }
    const bool wanted = m_settings.cameraPreview && m_debug.show && m_load.clusterModes && DebugCameraIsValid(m_debug);
    if (!wanted) {
        m_previewError.clear();
        if (m_previewActive) {
            m_renderer->ReleaseTargets();
            m_previewActive = false;
            m_previewTexture = UiTextureHandle::Invalid;
            m_previewKeyValid = false;
        }
        return;
    }

    const Mat4 viewProjection = DebugCameraViewProjection(m_debug);
    ClusterViewFrame frame;
    frame.viewProjection = viewProjection;
    frame.width = kPreviewWidth;
    frame.height = std::clamp(static_cast<int>(std::lround(static_cast<float>(kPreviewWidth) / m_debug.aspect)), 1, kMaxImageSize);
    frame.shade = ClusterViewShade::Tint;
    frame.cull.flags = CurrentFlags();
    frame.cull.viewProjection = viewProjection;
    frame.cull.eye = m_debug.transform.position;

    const FrameKey key = MakeKey(frame, static_cast<std::uint32_t>(ClusterViewerDrawMode::Cull));
    if (m_previewKeyValid && key == m_previewKey) {
        return;
    }
    m_previewKey = key;
    m_previewKeyValid = true;
    m_previewActive = true;

    const ClusterViewRender run = m_renderer->Render(ClusterViewSlot::Preview, frame);
    if (!run.ok) {
        m_previewTexture = UiTextureHandle::Invalid;
        m_previewError = "camera preview: " + run.note;
        return;
    }
    m_previewError.clear();
    m_previewTexture = run.texture;
    m_previewWidth = frame.width;
    m_previewHeight = frame.height;
}

std::optional<ClusterViewerResult> ClusterViewerWindow::NotReady(bool needClusterModes) const
{
    if (!m_open) {
        return MakeError(ClusterViewerError::NotOpen, "the viewer is not open");
    }
    if (m_load.banner == ClusterViewerBanner::NoDeviceSupport) {
        return MakeError(ClusterViewerError::Unsupported, m_load.text);
    }
    if (!m_valid || !m_renderer) {
        return MakeError(ClusterViewerError::NoValidMesh, m_load.text.empty() ? std::string("no valid mesh is loaded") : m_load.text);
    }
    if (needClusterModes && !m_load.clusterModes) {
        return MakeError(ClusterViewerError::Conflict, m_load.text.empty() ? std::string("cluster modes are unavailable") : m_load.text);
    }
    return std::nullopt;
}

ClusterViewerResult ClusterViewerWindow::CaptureHizNow()
{
    if (const std::optional<ClusterViewerResult> notReady = NotReady(true)) {
        return *notReady;
    }
    std::string why;
    if (!DebugCameraIsValid(m_debug, &why)) {
        return MakeError(ClusterViewerError::BadInput, "debug camera: " + why);
    }
    const auto [width, height] = VerifySize();
    const std::optional<ClusterViewHizInfo> info = m_renderer->CaptureHiz(
        DebugCameraViewProjection(m_debug), m_debug.transform.position, width, height);
    // The renderer owns the capture; a failed capture may have dropped the old one.
    m_hiz = m_renderer->HizCapture();
    m_mainKeyValid = false;
    m_previewKeyValid = false;
    if (!info || !m_hiz) {
        m_hizError = "the Hi-Z capture failed (see the ClusterView log)";
        return MakeError(ClusterViewerError::Conflict, m_hizError);
    }
    m_hizError.clear();
    m_hizForward = m_debug.transform.rotation.RotateVector(Vec3::Forward());
    m_hizTime = std::chrono::steady_clock::now();
    return MakeOk("Hi-Z captured " + std::to_string(width) + "x" + std::to_string(height));
}

ClusterViewerResult ClusterViewerWindow::Verify()
{
    if (const std::optional<ClusterViewerResult> notReady = NotReady(true)) {
        return *notReady;
    }
    std::string why;
    if (!DebugCameraIsValid(m_debug, &why)) {
        return MakeError(ClusterViewerError::BadInput, "debug camera: " + why);
    }
    const auto [width, height] = VerifySize();
    const Mat4 viewProjection = DebugCameraViewProjection(m_debug);
    const Vec3 eye = m_debug.transform.position;
    const ClusterCullFlags flags = CurrentFlags();

    m_truthValid = false;
    m_truthError.clear();

    const ClusterViewCullResult cull = m_renderer->RunCull({ flags, viewProjection, eye });
    if (!cull.ok) {
        m_truthError = "verify: " + cull.note;
        return MakeError(ClusterViewerError::Conflict, m_truthError);
    }
    const std::optional<ClusterViewHizInfo> hiz = m_renderer->HizCapture();

    ClusterIdDecode decode;
    {
        // The id pixels live only inside this block.
        const std::optional<CapturedPixels> pixels = m_renderer->RenderIdPass(
            viewProjection, width, height, HasClusterCullFlag(flags, ClusterCullFlags::BackfaceCone));
        // The id target and the preview target are freed after a verify; the preview re-renders when it is on.
        m_previewTexture = UiTextureHandle::Invalid;
        m_previewKeyValid = false;
        m_previewActive = false;
        m_renderer->ReleaseTargets();
        if (!pixels) {
            m_truthError = "verify: the id pass failed (see the ClusterView log)";
            return MakeError(ClusterViewerError::Conflict, m_truthError);
        }
        decode = DecodeIdImage(*pixels, m_renderer->ClusterCount());
    }
    if (!decode.ok) {
        m_truthError = "verify: " + decode.error;
        return MakeError(ClusterViewerError::Conflict, m_truthError);
    }
    const ClusterTruthResult truth = CompareClusterTruth(cull.survivors, decode.pixelCount);
    if (!truth.ok) {
        m_truthError = "verify: " + truth.error;
        return MakeError(ClusterViewerError::Conflict, m_truthError);
    }

    m_truth = truth;
    m_truthInvalidIds = decode.invalidIds;
    m_truthPixels = decode.pixelsChecked;
    m_truthVerdict = ClassifyFalseCulls(truth.falseCulls, cull.occlusionApplied, hiz, viewProjection, eye, width, height);
    m_truthKey = MakeVerifyKey(m_debug, flags, hiz ? hiz->captureId : 0, width, height, m_dataGeneration);
    m_truthValid = true;
    return MakeOk(std::to_string(truth.falseCulls) + " false culls, " + std::to_string(truth.conservative)
        + " conservative, " + std::to_string(truth.seen) + " clusters seen");
}

bool ClusterViewerWindow::TruthIsStale() const
{
    if (!m_truthValid) {
        return false;
    }
    const auto [width, height] = VerifySize();
    return IsStale(m_truthKey,
        MakeVerifyKey(m_debug, CurrentFlags(), m_hiz ? m_hiz->captureId : 0, width, height, m_dataGeneration));
}

bool ClusterViewerWindow::HizIsStale(float* moved, float* turnedDegrees, bool* sizeDiffers) const
{
    if (!m_hiz) {
        return false;
    }
    const Vec3 forward = m_debug.transform.rotation.RotateVector(Vec3::Forward());
    const float movedDistance = Length(m_debug.transform.position - m_hiz->eye);
    const float cosine = std::clamp(Dot(forward, m_hizForward), -1.0f, 1.0f);
    const float turned = RadToDeg(std::acos(cosine));
    const auto [width, height] = VerifySize();
    const bool differs = width != m_hiz->width || height != m_hiz->height;
    if (moved) {
        *moved = movedDistance;
    }
    if (turnedDegrees) {
        *turnedDegrees = turned;
    }
    if (sizeDiffers) {
        *sizeDiffers = differs;
    }
    return movedDistance > 1.0e-3f || turned > 0.05f || differs;
}

std::string ClusterViewerWindow::HizStatusText() const
{
    if (!m_hiz) {
        return "no Hi-Z capture";
    }
    const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_hizTime).count();
    char text[160];
    std::snprintf(text, sizeof(text), "captured %dx%d at (%.1f, %.1f, %.1f), %.0f s ago", m_hiz->width, m_hiz->height,
        m_hiz->eye.x, m_hiz->eye.y, m_hiz->eye.z, age);
    return text;
}

} // namespace gte
