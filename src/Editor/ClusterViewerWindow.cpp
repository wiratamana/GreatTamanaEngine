#include "ClusterViewerWindow.h"

#include "CameraFrustumOverlay.h"
#include "CameraPick.h"
#include "ClusterStatusText.h"
#include "ClusterToolUiState.h"
#include "EditorContext.h"
#include "ImGuiUniqueId.h"
#include "MeshClusterBuildRunner.h"
#include "ProjectRootPath.h"
#include "TransformGizmo.h"
#include "../Assets/AssetTypes.h"
#include "../Assets/GtaFile.h"
#include "../Assets/MeshClusterClone.h"
#include "../Assets/MeshClusterValidation.h"
#include "../Assets/MeshFile.h"
#include "../Core/Logging.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Registry.h"
#include "../ECS/TransformHierarchy.h"
#include "../Encoding/Utf8Path.h"
#include "../Renderer/Culling/ClusterCullMath.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <system_error>

namespace gte {

namespace {

constexpr const char* kLog = "ClusterViewer";
constexpr int kSizeStep = 32;
constexpr int kMaxImageSize = 8192;
// Frame key mode of the debug camera preview; follows the ClusterViewerDrawMode values.
constexpr std::uint32_t kPreviewKeyMode = static_cast<std::uint32_t>(ClusterViewerDrawMode::Cull) + 1;
constexpr int kPreviewWidth = 320;
constexpr int kDefaultWidth = 960;
constexpr int kDefaultHeight = 544;
constexpr float kSplitterThickness = 6.0f;
constexpr float kMinPaneWidth = 220.0f;
constexpr auto kStatInterval = std::chrono::milliseconds(500);
constexpr float kRadToDeg = 57.29578f;

const ImVec4 kRed(0.95f, 0.4f, 0.4f, 1.0f);
const ImVec4 kOrange(0.95f, 0.7f, 0.3f, 1.0f);
const ImVec4 kGreen(0.4f, 0.9f, 0.4f, 1.0f);
const ImVec4 kMagenta(1.0f, 0.3f, 0.85f, 1.0f);
const ImVec4 kGrey(0.6f, 0.6f, 0.6f, 1.0f);
const ImVec4 kBlue(0.55f, 0.75f, 1.0f, 1.0f);

std::array<float, 16> ToArray(const Mat4& m) noexcept
{
    std::array<float, 16> a{};
    std::copy_n(m.Data(), 16, a.begin());
    return a;
}

// Rounds up to a multiple of kSizeStep so a window resize does not re-make the target every frame.
int Quantize(float value) noexcept
{
    const int steps = static_cast<int>(std::ceil(value / static_cast<float>(kSizeStep)));
    return std::clamp(steps * kSizeStep, kSizeStep, kMaxImageSize);
}

ImTextureID ToImTexture(UiTextureHandle handle) noexcept
{
    return static_cast<ImTextureID>(static_cast<std::uint64_t>(handle));
}

ClusterViewerResult MakeError(ClusterViewerError error, std::string message)
{
    ClusterViewerResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

ClusterViewerResult MakeOk(std::string message)
{
    ClusterViewerResult result;
    result.ok = true;
    result.message = std::move(message);
    return result;
}

bool IsHardFailure(ClusterViewerBanner banner) noexcept
{
    return banner == ClusterViewerBanner::NoDeviceSupport || banner == ClusterViewerBanner::DecodeFailed
        || banner == ClusterViewerBanner::EmptyMesh || banner == ClusterViewerBanner::BadIndices;
}

} // namespace

ClusterViewerWindow::ClusterViewerWindow() = default;

ClusterViewerWindow::~ClusterViewerWindow()
{
    Reset();
}

void ClusterViewerWindow::Open(const std::string& absoluteGtaPath)
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
    m_vertexCount = 0;
    m_triangleCount = 0;
    m_clusterCount = 0;

    std::filesystem::path absolute;
    if (const std::optional<std::string> invalid = ValidateMeshAssetPath(path, ResolveProjectRootDirectory(), absolute)) {
        m_absolutePath = absolute;
        m_identity = StatFile(absolute);
        m_load.banner = ClusterViewerBanner::DecodeFailed;
        m_load.text = *invalid;
        m_renderer.reset();
        GTE_LOG_WARNING(kLog, "open refused: " + *invalid);
        return;
    }
    m_absolutePath = absolute;
    m_identity = StatFile(absolute);

    ClusterViewerLoadInput input;
    input.pathSupported = EnsureRenderer(renderer);
    if (!input.pathSupported) {
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
        GTE_LOG_WARNING(kLog, "mesh not shown: " + m_load.text);
        m_renderer.reset(); // nothing stays on the GPU for a mesh that is not shown
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

float ClusterViewerWindow::ViewAspect() const noexcept
{
    if (m_requestWidth > 0 && m_requestHeight > 0) {
        return static_cast<float>(m_requestWidth) / static_cast<float>(m_requestHeight);
    }
    return static_cast<float>(kDefaultWidth) / static_cast<float>(kDefaultHeight);
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
    const Camera* camera = m_registry->TryGetComponent<Camera>(entity);
    if (camera == nullptr) {
        return std::nullopt;
    }
    const Transform world = ComputeWorldTransform(*m_registry, entity);
    const Extent2D extent = m_ctx->desiredExtent;
    FrustumInput input;
    input.position = world.position;
    input.rotation = world.rotation;
    input.fovYDegrees = camera->fovYDegrees;
    input.nearZ = camera->nearZ;
    input.farZ = camera->farZ;
    input.aspect = (extent.width > 0 && extent.height > 0)
        ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
        : 16.0f / 9.0f;
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
        m_orbit.pitchDeg = std::clamp(*request.pitchDeg, kOrbitMinPitchDeg, kOrbitMaxPitchDeg);
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
    std::vector<OverlaySegment2D>& out, std::vector<OverlayQuad2D>& quads) const
{
    if (m_settings.showSceneCamera) {
        if (const std::optional<FrustumInput> sceneCamera = SelectedSceneCamera()) {
            AppendCameraOverlay(*sceneCamera, true, viewProjection, rectMin, rectMax, kSceneCameraViewerColor, 1.0f, out, &quads);
        }
    }
    if (m_debug.show && DebugCameraIsValid(m_debug)) {
        const bool strong = m_debug.selected || debugHovered;
        AppendCameraOverlay(DebugFrustum(), true, viewProjection, rectMin, rectMax,
            m_debug.selected ? kDebugCameraSelectedColor : kDebugCameraColor, strong ? 2.5f : 1.5f, out,
            m_debug.selected ? &quads : nullptr);
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
            m_runError = "debug camera: " + why;
            return;
        }
        if (m_runError.rfind("debug camera:", 0) == 0) {
            m_runError.clear();
        }
        frame.cull.flags = CurrentFlags();
        frame.cull.viewProjection = DebugCameraViewProjection(m_debug);
        frame.cull.eye = m_debug.transform.position;
        frame.dimCulled = m_settings.culledStyle == ClusterViewerCulledStyle::Dim;
    }

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

    const FrameKey key = MakeKey(frame, kPreviewKeyMode);
    if (m_previewKeyValid && key == m_previewKey) {
        return;
    }
    m_previewKey = key;
    m_previewKeyValid = true;
    m_previewActive = true;

    const ClusterViewRender run = m_renderer->Render(ClusterViewSlot::Preview, frame);
    if (!run.ok) {
        m_previewTexture = UiTextureHandle::Invalid;
        m_runError = "camera preview: " + run.note;
        return;
    }
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

ClusterViewerResult ClusterViewerWindow::VerifyNow()
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
    const float turned = std::acos(cosine) * kRadToDeg;
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

void ClusterViewerWindow::StartCloneFromViewer()
{
    ClusterToolUiState& ui = m_ctx->clusterTool;
    MeshClusterBuildRunner* const runner = m_ctx->clusterBuildRunner;
    if (runner == nullptr) {
        ui.lastCloneStatus = "The cluster build runner is not available.";
        return;
    }
    const std::filesystem::path destination = ChooseClusteredCloneDestination(m_absolutePath);
    if (destination.empty()) {
        ui.lastCloneStatus = "No free file name for the copy.";
        return;
    }
    ui.lastCloneStatus.clear();
    ui.startRefusedBusy = runner->StartClone(m_absolutePath, destination, MakeCloneOptions(ui), ui.openViewerAfterClone, 0)
        == MeshClusterBuildRunner::StartResult::Busy;
}

void ClusterViewerWindow::DrawCloneStatus()
{
    const ClusterToolUiState& ui = m_ctx->clusterTool;
    const MeshClusterBuildRunner* const runner = m_ctx->clusterBuildRunner;
    if (!ui.lastCloneStatus.empty()) {
        ImGui::TextColored(kOrange, "%s", ui.lastCloneStatus.c_str());
    }
    const ClusterStatus status = DescribeClusterStatus(MakeClusterStatusInput(ui, runner, m_absolutePath));
    if (status.text.empty()) {
        return;
    }
    switch (status.tone) {
    case ClusterStatusTone::Quiet: ImGui::TextDisabled("%s", status.text.c_str()); break;
    case ClusterStatusTone::Warn: ImGui::TextColored(kOrange, "%s", status.text.c_str()); break;
    case ClusterStatusTone::Good: ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", status.text.c_str()); break;
    case ClusterStatusTone::Bad: ImGui::TextColored(kRed, "%s", status.text.c_str()); break;
    }
}

ClusterViewerResult ClusterViewerWindow::Open(Renderer& renderer, const std::string& absolutePath)
{
    m_open = true;
    m_closeRequested = false;
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
    report.open = m_open;
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
    cull.ok = m_hasRun && m_run.ok;
    cull.total = m_run.counts.total;
    cull.passedFrustum = m_run.counts.passedFrustum;
    cull.passedCone = m_run.counts.passedCone;
    cull.visible = m_run.counts.visible;
    cull.visibleTriangles = m_run.counts.visibleTriangles;
    cull.runMilliseconds = m_run.runMilliseconds;
    cull.occlusionApplied = m_run.occlusionApplied;
    cull.hizCut = m_run.hizCut;
    cull.nested = m_stats.valid;
    cull.error = m_stats.valid ? m_runError : m_stats.error;
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
    if (!m_open) {
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

ClusterViewerResult ClusterViewerWindow::Verify()
{
    return VerifyNow();
}

ClusterViewerResult ClusterViewerWindow::CaptureHiz()
{
    ClusterViewerResult result = CaptureHizNow();
    if (result.ok) {
        RenderMain(m_requestWidth > 0 ? m_requestWidth : kDefaultWidth, m_requestHeight > 0 ? m_requestHeight : kDefaultHeight);
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
    if (m_requestWidth == 0) {
        m_requestWidth = kDefaultWidth;
        m_requestHeight = kDefaultHeight;
    }
    const float width = static_cast<float>(m_requestWidth);
    const float height = static_cast<float>(m_requestHeight);
    const CameraPickCandidate candidate{ 1, DebugFrustum() };
    const std::optional<CameraPickHit> hit = PickCamera(std::span<const CameraPickCandidate>(&candidate, 1),
        OrbitViewProjection(ViewAspect()), Vec2(0.0f, 0.0f), Vec2(width, height), Vec2(x * width, y * height));
    if (hit) {
        out.hit = true;
        m_debug.selected = true;
    }
    return out;
}

std::optional<ClusterViewerImage> ClusterViewerWindow::CaptureImage(ClusterViewSlot slot, bool overlay)
{
    if (NotReady(false)) {
        return std::nullopt;
    }
    if (slot == ClusterViewSlot::Main) {
        if (m_requestWidth == 0) {
            m_requestWidth = kDefaultWidth;
            m_requestHeight = kDefaultHeight;
        }
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
            image.overlayQuads);
    }
    return image;
}

void ClusterViewerWindow::Build(Renderer& renderer, EditorContext& ctx, Registry& registry)
{
    m_ctx = &ctx;
    m_registry = &registry;

    if (m_closeRequested) {
        m_closeRequested = false;
        Close();
    }
    if (!m_pendingPath.empty()) {
        const std::string path = std::move(m_pendingPath);
        m_pendingPath.clear();
        m_open = true;
        LoadMesh(renderer, path);
    }
    if (!m_open) {
        return;
    }
    PollFile(renderer);

    ImGui::SetNextWindowSize(ImVec2(1100.0f, 720.0f), ImGuiCond_FirstUseEver);
    const bool visible = ImGui::Begin("Cluster Viewer", &m_open);
    if (visible) {
        ScopedUniqueId idScope(0, "ClusterViewerWindow::Build");
        if (m_load.banner != ClusterViewerBanner::None) {
            ImGui::TextColored(IsHardFailure(m_load.banner) ? kRed : kOrange, "%s", m_load.text.c_str());
            if (m_load.offerCreateCopy) {
                ImGui::SameLine();
                if (ImGui::Button("Create Clustered Copy")) {
                    StartCloneFromViewer();
                }
            }
            DrawCloneStatus();
        }
        if (m_valid) {
            DrawToolbar();
            ImGui::Separator();

            const float fullWidth = ImGui::GetContentRegionAvail().x;
            const float maxControls = std::max(kMinPaneWidth, fullWidth - kSplitterThickness - kMinPaneWidth);
            m_controlsWidth = std::clamp(m_controlsWidth, kMinPaneWidth, maxControls);

            ImGui::BeginChild("ClusterViewerControls", ImVec2(m_controlsWidth, 0.0f), true);
            DrawControls();
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrab));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrabHovered));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrabActive));
            ImGui::Button("##ClusterViewerSplitter", ImVec2(kSplitterThickness, -1.0f));
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemActive()) {
                m_controlsWidth += ImGui::GetIO().MouseDelta.x;
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }

            ImGui::SameLine();
            ImGui::BeginChild("ClusterViewerViewport", ImVec2(0.0f, 0.0f), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            DrawViewport();
            ImGui::EndChild();
        }
    }
    ImGui::End();
    if (!m_open) {
        m_closeRequested = true; // this frame's draw data still references the view textures
    }
}

void ClusterViewerWindow::DrawToolbar()
{
    ScopedUniqueId idScope(1, "ClusterViewerWindow::Toolbar");
    static const char* kModes[] = { "Normal", "Clusters", "Cull result" };
    int mode = static_cast<int>(m_settings.drawMode);
    ImGui::BeginDisabled(!m_load.clusterModes);
    ImGui::PushItemWidth(140.0f);
    if (ImGui::Combo("Draw mode", &mode, kModes, static_cast<int>(std::size(kModes)))) {
        m_settings.drawMode = static_cast<ClusterViewerDrawMode>(std::clamp(mode, 0, 2));
    }
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        m_orbit.Frame(m_center, m_radius);
    }
    ImGui::SameLine();
    bool show = m_debug.show;
    if (ImGui::Checkbox("Debug camera", &show)) {
        m_debug.show = show;
        if (!show) {
            m_debug.selected = false;
        }
    }
}

void ClusterViewerWindow::DrawControls()
{
    DrawMeshInfo();
    ImGui::Separator();
    DrawCullControls();
    ImGui::Separator();
    DrawDebugCameraControls();
    ImGui::Separator();
    DrawHizControls();
    ImGui::Separator();
    DrawVerifyControls();
    ImGui::Separator();
    DrawStats();
}

void ClusterViewerWindow::DrawMeshInfo()
{
    ScopedUniqueId idScope(2, "ClusterViewerWindow::MeshInfo");
    ImGui::TextColored(kBlue, "Mesh");
    ImGui::TextDisabled("%s", m_assetPath.c_str());
    ImGui::Text("Vertices: %llu", static_cast<unsigned long long>(m_vertexCount));
    ImGui::Text("Triangles: %llu", static_cast<unsigned long long>(m_triangleCount));
    ImGui::Text("Clusters: %u", m_clusterCount);
    if (m_clusterCount > 0) {
        ImGui::Text("Avg triangles per cluster: %.1f",
            static_cast<double>(m_triangleCount) / static_cast<double>(m_clusterCount));
    }
}

void ClusterViewerWindow::DrawCullControls()
{
    ScopedUniqueId idScope(3, "ClusterViewerWindow::Culling");
    ImGui::TextColored(kBlue, "Culling");
    ImGui::BeginDisabled(!m_load.clusterModes);
    ImGui::Checkbox("Frustum", &m_settings.frustum);
    ImGui::Checkbox("Back-face cone", &m_settings.cone);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Correct for closed meshes only. Switches the viewer to back-face culling.");
    }
    ImGui::Checkbox("Hi-Z occlusion", &m_settings.occlusion);
    int style = static_cast<int>(m_settings.culledStyle);
    static const char* kStyles[] = { "Hidden", "Dim" };
    if (ImGui::Combo("Culled as", &style, kStyles, static_cast<int>(std::size(kStyles)))) {
        m_settings.culledStyle = static_cast<ClusterViewerCulledStyle>(std::clamp(style, 0, 1));
    }
    ImGui::EndDisabled();
    if (!m_load.clusterModes) {
        ImGui::TextDisabled("Cluster modes are unavailable for this mesh.");
    }
}

void ClusterViewerWindow::DrawDebugCameraControls()
{
    ScopedUniqueId idScope(4, "ClusterViewerWindow::DebugCamera");
    ImGui::TextColored(kBlue, "Debug camera");
    bool changed = false;
    const float posSpeed = std::max(0.01f, m_radius * 0.005f);

    float position[3] = { m_debug.transform.position.x, m_debug.transform.position.y, m_debug.transform.position.z };
    if (ImGui::DragFloat3("Position", position, posSpeed)) {
        m_debug.transform.position = Vec3(position[0], position[1], position[2]);
        changed = true;
    }
    const Vec3 euler = m_debug.transform.rotation.ToEulerDegrees();
    float rotation[3] = { euler.x, euler.y, euler.z };
    if (ImGui::DragFloat3("Pitch/Yaw/Roll", rotation, 0.5f)) {
        m_debug.transform.rotation = Quat::FromEulerDegrees(rotation[0], rotation[1], rotation[2]);
        changed = true;
    }
    changed |= ImGui::DragFloat("FOV", &m_debug.fovYDegrees, 0.1f, kDebugCameraMinFov, kDebugCameraMaxFov, "%.1f");
    changed |= ImGui::DragFloat("Near", &m_debug.nearZ, std::max(0.001f, m_debug.nearZ * 0.01f), kDebugCameraMinNear,
        kDebugCameraMaxNear, "%.4g");
    changed |= ImGui::DragFloat("Far", &m_debug.farZ, std::max(0.01f, m_debug.farZ * 0.01f),
        kDebugCameraMinNear * kDebugCameraFarOverNear, kDebugCameraMaxFar, "%.4g");
    changed |= ImGui::DragFloat("Aspect", &m_debug.aspect, 0.01f, kDebugCameraMinAspect, kDebugCameraMaxAspect, "%.3f");
    if (changed) {
        m_debug = ClampDebugCamera(m_debug);
    }

    if (ImGui::Button("Snap to view")) {
        SnapDebugCameraToView();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        ResetDebugCamera(true);
    }

    bool moveGizmo = m_settings.gizmoOperation == ClusterViewerGizmoOperation::Translate;
    ImGui::TextUnformatted("Gizmo");
    ImGui::SameLine();
    if (ImGui::RadioButton("Move", moveGizmo)) {
        m_settings.gizmoOperation = ClusterViewerGizmoOperation::Translate;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", !moveGizmo)) {
        m_settings.gizmoOperation = ClusterViewerGizmoOperation::Rotate;
    }

    ImGui::BeginDisabled(!m_load.clusterModes);
    ImGui::Checkbox("Camera preview", &m_settings.cameraPreview);
    ImGui::EndDisabled();
    ImGui::Checkbox("Show selected scene camera", &m_settings.showSceneCamera);
    const bool hasSceneCamera = SelectedSceneCamera().has_value();
    ImGui::BeginDisabled(!hasSceneCamera);
    if (ImGui::Button("Copy from selected scene camera")) {
        CopySceneCamera();
    }
    ImGui::EndDisabled();
    if (!hasSceneCamera) {
        ImGui::TextDisabled("Select a Camera entity in the Scene.");
    }
}

void ClusterViewerWindow::DrawHizControls()
{
    ScopedUniqueId idScope(5, "ClusterViewerWindow::Hiz");
    ImGui::TextColored(kBlue, "Hi-Z");
    ImGui::BeginDisabled(!m_load.clusterModes);
    if (ImGui::Button("Capture Hi-Z at debug camera")) {
        CaptureHizNow();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", HizStatusText().c_str());
    if (m_hiz) {
        float moved = 0.0f;
        float turned = 0.0f;
        bool sizeDiffers = false;
        if (HizIsStale(&moved, &turned, &sizeDiffers)) {
            if (sizeDiffers) {
                ImGui::TextColored(kOrange, "stale: size differs");
            } else {
                ImGui::TextColored(kOrange, "stale: debug camera moved by %.1f units / turned %.0f deg", moved, turned);
            }
        }
        if (m_run.hizCut) {
            ImGui::TextColored(kOrange, "engine would discard this pyramid (camera cut)");
        }
    }
    if (!m_hizError.empty()) {
        ImGui::TextColored(kRed, "%s", m_hizError.c_str());
    }
    ImGui::TextDisabled("Engine needs 2 clean builds; the viewer does not wait.");
}

void ClusterViewerWindow::DrawVerifyControls()
{
    ScopedUniqueId idScope(6, "ClusterViewerWindow::Verify");
    ImGui::TextColored(kBlue, "Verify");
    static const char* kWidths[] = { "640", "1280", "1920" };
    static constexpr std::uint32_t kWidthValues[] = { 640, 1280, 1920 };
    int widthIndex = 1;
    for (int i = 0; i < 3; ++i) {
        if (kWidthValues[i] == m_settings.encodeWidth) {
            widthIndex = i;
        }
    }
    ImGui::PushItemWidth(100.0f);
    if (ImGui::Combo("Size", &widthIndex, kWidths, 3)) {
        m_settings.encodeWidth = kWidthValues[std::clamp(widthIndex, 0, 2)];
    }
    ImGui::PopItemWidth();

    ImGui::BeginDisabled(!m_load.clusterModes);
    if (ImGui::Button("Verify with ID pass")) {
        VerifyNow();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Draws every cluster from the debug camera and compares what is seen with what the cull kept.\n"
                          "With the cone flag on, the check draws back-face culled, like the engine.");
    }

    if (!m_truthError.empty()) {
        ImGui::TextColored(kRed, "%s", m_truthError.c_str());
    }
    if (!m_truthValid) {
        return;
    }
    const bool stale = TruthIsStale();
    ImVec4 color = kGreen;
    const char* verdict = "no false culls";
    if (m_truth.falseCulls > 0) {
        const bool expected = m_truthVerdict == HizVerdictKind::HizStale;
        color = expected ? kOrange : kMagenta;
        verdict = expected ? "false culls (Hi-Z stale, expected)" : "false culls (BUG)";
    }
    ImGui::TextColored(stale ? kGrey : color, "%u %s%s", m_truth.falseCulls, verdict, stale ? " - stale" : "");
    ImGui::TextDisabled("seen %u, conservative %u, correct %u, invalid ids %llu", m_truth.seen, m_truth.conservative,
        m_truth.correct, static_cast<unsigned long long>(m_truthInvalidIds));
    ImGui::TextDisabled("pixels checked %llu, cull efficiency %.1f%%", static_cast<unsigned long long>(m_truthPixels),
        m_truth.cullEfficiency * 100.0);
    if (!m_truth.firstFalseCullIds.empty()) {
        std::string ids;
        for (const std::uint32_t id : m_truth.firstFalseCullIds) {
            ids += std::to_string(id) + " ";
        }
        ImGui::TextWrapped("first false culls: %s", ids.c_str());
    }
    if (stale) {
        ImGui::TextDisabled("The camera, flags, Hi-Z capture or size changed. Verify again.");
    }
}

void ClusterViewerWindow::DrawStats()
{
    ScopedUniqueId idScope(7, "ClusterViewerWindow::Stats");
    ImGui::TextColored(kBlue, "GPU cull report");
    if (m_settings.drawMode != ClusterViewerDrawMode::Cull) {
        ImGui::TextDisabled("Choose 'Cull result' to see what the debug camera culls.");
    }
    if (!m_runError.empty()) {
        ImGui::TextColored(kRed, "%s", m_runError.c_str());
    }
    if (!m_hasRun) {
        ImGui::TextDisabled("no run yet");
        return;
    }
    if (!m_stats.valid) {
        ImGui::TextColored(kRed, "counts are not nested: %s", m_stats.error.c_str());
    } else {
        ImGui::Text("Total clusters: %u", m_stats.total);
        ImGui::Text("GPU visible: %u (%.1f%%)", m_stats.visible, m_stats.visiblePercent);
        ImGui::Text("Culled by frustum: %u", m_stats.byFrustum);
        ImGui::Text("Culled by cone: %u", m_stats.byCone);
        ImGui::Text("Culled by Hi-Z: %u", m_stats.byOcclusion);
        ImGui::Text("Visible triangles: %llu", static_cast<unsigned long long>(m_stats.visibleTriangles));
    }
    if (m_run.hizCut) {
        ImGui::TextColored(kOrange, "engine would discard the pyramid (camera cut)");
    } else if (m_settings.occlusion && !m_run.occlusionApplied && m_settings.drawMode == ClusterViewerDrawMode::Cull) {
        ImGui::TextColored(kOrange, "no Hi-Z capture: occlusion is off");
    }
    ImGui::TextDisabled("Run time: %.2f ms", m_run.runMilliseconds);
}

void ClusterViewerWindow::DrawViewport()
{
    ScopedUniqueId idScope(8, "ClusterViewerWindow::Viewport");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1.0f || avail.y < 1.0f || !m_renderer) {
        CancelGizmo();
        return;
    }
    // The size is frozen while a mouse button is down, so a splitter or edge drag does not resize the target.
    if (m_requestWidth == 0 || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_requestWidth = Quantize(avail.x);
        m_requestHeight = Quantize(avail.y);
    }
    RenderMain(m_requestWidth, m_requestHeight);
    RenderPreview();
    if (m_mainTexture == UiTextureHandle::Invalid) {
        CancelGizmo();
        ImGui::TextDisabled("The view is not available.");
        if (!m_runError.empty()) {
            ImGui::TextColored(kRed, "%s", m_runError.c_str());
        }
        return;
    }

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + avail.x, min.y + avail.y);
    ImGui::Image(ToImTexture(m_mainTexture), avail);
    // An invisible button owns the mouse, so a drag in the image never moves the window.
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("##ClusterViewerImage", avail,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();

    const Vec2 imageMin(min.x, min.y);
    const Vec2 imageMax(max.x, max.y);
    const Mat4 viewProjection = OrbitViewProjection(ViewAspect());
    HandleViewportInput(viewProjection, imageMin, imageMax, hovered);

    std::vector<OverlaySegment2D> overlay;
    std::vector<OverlayQuad2D> quads;
    AppendOverlay(viewProjection, imageMin, imageMax, m_debugHovered, overlay, quads);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    DrawOverlayQuads(drawList, quads, imageMin, imageMax);
    DrawOverlaySegments(drawList, overlay, imageMin, imageMax);

    if (m_previewTexture != UiTextureHandle::Invalid && m_previewWidth > 0 && m_previewHeight > 0) {
        const float width = std::min(static_cast<float>(m_previewWidth), avail.x * 0.4f);
        const float height = width * static_cast<float>(m_previewHeight) / static_cast<float>(m_previewWidth);
        const ImVec2 p1(max.x - 8.0f, max.y - 8.0f);
        const ImVec2 p0(p1.x - width, p1.y - height);
        drawList->AddImage(ToImTexture(m_previewTexture), p0, p1);
        drawList->AddRect(p0, p1, IM_COL32(255, 220, 60, 255));
        drawList->AddText(ImVec2(p0.x + 4.0f, p0.y + 2.0f), IM_COL32(255, 220, 60, 255), "Debug camera");
    }
}

void ClusterViewerWindow::HandleViewportInput(const Mat4& viewProjection, Vec2 imageMin, Vec2 imageMax, bool hovered)
{
    const ImGuiIO& io = ImGui::GetIO();
    const bool debugValid = m_debug.show && DebugCameraIsValid(m_debug);
    if (!debugValid) {
        m_debug.selected = false;
    }

    bool gizmoUsing = false;
    {
        GizmoIdScope scope("ClusterViewerDebugCamera");
        gizmoUsing = IsGizmoUsing();
    }
    const bool gizmoBusy = gizmoUsing || IsGizmoOver();

    // The camera hit test runs before the orbit starts.
    m_debugHovered = false;
    if (hovered && debugValid && !gizmoBusy && !m_orbiting && !m_panning) {
        const CameraPickCandidate candidate{ 1, DebugFrustum() };
        m_debugHovered = PickCamera(std::span<const CameraPickCandidate>(&candidate, 1), viewProjection, imageMin,
            imageMax, Vec2(io.MousePos.x, io.MousePos.y)).has_value();
    }
    if (m_debugHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    if (hovered && !gizmoBusy && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (m_debugHovered) {
            m_debug.selected = true;
        } else {
            m_debug.selected = false;
            m_orbiting = true;
        }
    }
    if (hovered && !gizmoBusy && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        m_panning = true;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_orbiting = false;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
        m_panning = false;
    }
    if (gizmoBusy) {
        m_orbiting = false;
        m_panning = false;
    }
    if (m_orbiting) {
        m_orbit.Rotate(io.MouseDelta.x, io.MouseDelta.y);
    }
    if (m_panning) {
        m_orbit.Pan(io.MouseDelta.x, io.MouseDelta.y);
    }
    if (hovered && !gizmoBusy && io.MouseWheel != 0.0f) {
        m_orbit.Dolly(io.MouseWheel);
    }

    // The gizmo lives in the orbit image, so it uses the orbit projection (not the debug camera's).
    if (debugValid && m_debug.selected) {
        const GizmoOperation operation = m_settings.gizmoOperation == ClusterViewerGizmoOperation::Rotate
            ? GizmoOperation::Rotate
            : GizmoOperation::Translate;
        GizmoIdScope scope("ClusterViewerDebugCamera");
        ManipulateTransformGizmo(operation, m_orbit.View(), m_orbit.GizmoProjection(ViewAspect()), imageMin.x, imageMin.y,
            imageMax.x - imageMin.x, imageMax.y - imageMin.y, m_debug.transform, Mat4::Identity());
        m_debug.transform.scale = Vec3::One();
        m_gizmoDrawn = true;
        if (!DebugCameraIsValid(m_debug)) {
            m_debug = ClampDebugCamera(m_debug);
        }
    } else if (m_gizmoDrawn) {
        CancelGizmo();
    }
}

} // namespace gte
