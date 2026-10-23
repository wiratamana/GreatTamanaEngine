#include "ClusterViewerWindowInternal.h"

#include "CameraFrustumOverlay.h"
#include "CameraPick.h"
#include "ClusterStatusText.h"
#include "ClusterToolUiState.h"
#include "EditorContext.h"
#include "ImGuiUniqueId.h"
#include "MeshClusterBuildRunner.h"
#include "TransformGizmo.h"
#include "../Assets/MeshClusterClone.h"
#include "../ECS/Registry.h"
#include "../Math/MathTypes.h"
#include "../Renderer/Culling/ClusterCullMath.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gte {

using namespace viewer_detail;

void ClusterViewerWindow::StartCloneFromViewer()
{
    const std::filesystem::path destination = ChooseClusteredCloneDestination(m_absolutePath);
    if (destination.empty()) {
        m_ctx->clusterTool.lastCloneStatus = "No free file name for the copy.";
        return;
    }
    StartCloneFromUi(m_ctx->clusterTool, m_ctx->clusterBuildRunner, m_absolutePath, destination);
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
    case ClusterStatusTone::Good: ImGui::TextColored(kGreen, "%s", status.text.c_str()); break;
    case ClusterStatusTone::Bad: ImGui::TextColored(kRed, "%s", status.text.c_str()); break;
    }
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
    if (!visible || !m_valid) {
        CancelGizmo();
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
        static_cast<void>(Verify());
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
    if (!ActiveRunError().empty()) {
        ImGui::TextColored(kRed, "%s", ActiveRunError().c_str());
    }
    if (!m_previewError.empty()) {
        ImGui::TextColored(kRed, "%s", m_previewError.c_str());
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
        if (!ActiveRunError().empty()) {
            ImGui::TextColored(kRed, "%s", ActiveRunError().c_str());
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
