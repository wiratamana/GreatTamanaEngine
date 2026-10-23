#pragma once

// Floating window that shows the clusters of one mesh and simulates cluster culling with a debug camera.
// Every number and image comes from the GPU through ClusterViewRenderer. Call Reset() before the UI backend shuts down.

#include "ClusterViewerControl.h"
#include "ClusterViewerLoad.h"
#include "ClusterViewerSettings.h"
#include "ClusterViewerStats.h"
#include "ClusterViewerTruth.h"
#include "OrbitCamera.h"
#include "../Renderer/ClusterViewRenderer.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gte {

class Registry;
class Renderer;
struct EditorContext;

class ClusterViewerWindow final : public IClusterViewerControl {
public:
    ClusterViewerWindow();
    ~ClusterViewerWindow() override;

    ClusterViewerWindow(const ClusterViewerWindow&) = delete;
    ClusterViewerWindow& operator=(const ClusterViewerWindow&) = delete;

    // Opens or re-targets the window; the mesh loads in the next Build().
    void Open(const std::string& absoluteGtaPath);

    // Per frame; a no-op while closed.
    void Build(Renderer& renderer, EditorContext& ctx, Registry& registry);

    // Closes the window and frees the renderer and every derived result (GPU idle first). Safe to repeat.
    void Reset();

    ClusterViewerResult Open(Renderer& renderer, const std::string& absolutePath) override;
    ClusterViewerReport Report() const override;
    ClusterViewerResult Set(std::span<const KeyValue> pairs) override;
    ClusterViewerResult Verify() override;
    ClusterViewerResult CaptureHiz() override;
    ClusterViewerPickResult Pick(float x, float y) override;
    std::optional<ClusterViewerImage> CaptureImage(ClusterViewSlot slot, bool overlay) override;

private:
    // Everything one slot render depends on; the slot re-renders only when it changes.
    struct FrameKey {
        std::array<float, 16> viewProjection{};
        std::array<float, 16> cullViewProjection{};
        std::array<float, 3> eye{};
        int width = 0;
        int height = 0;
        std::uint32_t drawMode = 0;
        std::uint32_t flags = 0;
        bool dim = false;
        std::uint64_t hizCaptureId = 0;
        std::uint64_t dataGeneration = 0;

        friend bool operator==(const FrameKey&, const FrameKey&) noexcept = default;
    };

    // Identity of the file the mesh was loaded from.
    struct FileIdentity {
        bool ok = false;
        std::filesystem::file_time_type writeTime{};
        std::uintmax_t size = 0;

        friend bool operator==(const FileIdentity&, const FileIdentity&) noexcept = default;
    };

    // Loading and lifetime.
    bool EnsureRenderer(Renderer& renderer);
    void LoadMesh(Renderer& renderer, const std::string& path);
    void PollFile(Renderer& renderer);
    void ClearDerived();
    void Close();
    static FileIdentity StatFile(const std::filesystem::path& path);

    // Camera and flags.
    ClusterCullFlags CurrentFlags() const noexcept;
    float ViewAspect() const noexcept;
    Mat4 OrbitViewProjection(float aspect) const;
    FrustumInput DebugFrustum() const;
    std::optional<FrustumInput> SelectedSceneCamera() const;
    bool CopySceneCamera();
    void ResetDebugCamera(bool keepShow);
    void SnapDebugCameraToView();
    void ApplyOrbitRequest(const ClusterViewerOrbitRequest& request);
    std::pair<int, int> VerifySize() const;
    void AppendOverlay(const Mat4& viewProjection, Vec2 rectMin, Vec2 rectMax, bool debugHovered,
        std::vector<OverlaySegment2D>& out) const;

    // GPU work. Every call is synchronous and runs only when its frame key changed.
    void RenderMain(int width, int height);
    void RenderPreview();
    ClusterViewerResult VerifyNow();
    ClusterViewerResult CaptureHizNow();
    bool TruthIsStale() const;
    std::string HizStatusText() const;
    // True when the stored capture no longer matches the debug camera or the capture size.
    bool HizIsStale(float* moved = nullptr, float* turnedDegrees = nullptr, bool* sizeDiffers = nullptr) const;
    // Why a request cannot run now; nullopt when it can.
    std::optional<ClusterViewerResult> NotReady(bool needClusterModes) const;
    void StartCloneFromViewer();
    FrameKey MakeKey(const ClusterViewFrame& frame, std::uint32_t drawMode) const;

    // UI.
    void DrawToolbar();
    void DrawControls();
    void DrawMeshInfo();
    void DrawCullControls();
    void DrawDebugCameraControls();
    void DrawHizControls();
    void DrawVerifyControls();
    void DrawStats();
    void DrawViewport();
    void HandleViewportInput(const Mat4& viewProjection, Vec2 imageMin, Vec2 imageMax, bool hovered);

    // State.
    bool m_open = false;
    bool m_closeRequested = false; // the window was closed this frame; GPU objects are freed at the next Build()
    std::string m_pendingPath;
    EditorContext* m_ctx = nullptr;
    Registry* m_registry = nullptr;

    std::unique_ptr<ClusterViewRenderer> m_renderer;
    bool m_rendererFailed = false;
    std::string m_rendererWhyNot;

    // Loaded mesh: only counts and bounds stay on the CPU.
    std::string m_assetPath;
    std::filesystem::path m_absolutePath;
    FileIdentity m_identity;
    std::chrono::steady_clock::time_point m_lastStat{};
    bool m_hasLoaded = false;
    bool m_valid = false;
    ClusterViewerLoadDecision m_load;
    std::uint64_t m_vertexCount = 0;
    std::uint64_t m_triangleCount = 0;
    std::uint32_t m_clusterCount = 0;
    std::string m_meshNote;
    Vec3 m_center = Vec3::Zero();
    float m_radius = 1.0f;
    std::uint64_t m_dataGeneration = 0;

    // Cameras and settings; they survive a reload of the same asset.
    OrbitCamera m_orbit;
    ClusterViewerSettings m_settings;
    DebugCameraState m_debug;

    // Main slot.
    FrameKey m_mainKey;
    bool m_mainKeyValid = false;
    UiTextureHandle m_mainTexture = UiTextureHandle::Invalid;
    bool m_hasRun = false;
    ClusterViewRender m_run;
    ClusterViewerStats m_stats;
    std::string m_runError;
    int m_requestWidth = 0;
    int m_requestHeight = 0;

    // Preview slot.
    FrameKey m_previewKey;
    bool m_previewKeyValid = false;
    bool m_previewActive = false;
    UiTextureHandle m_previewTexture = UiTextureHandle::Invalid;
    int m_previewWidth = 0;
    int m_previewHeight = 0;

    // Hi-Z capture status (the pyramid itself lives on the GPU).
    std::optional<ClusterViewHizInfo> m_hiz;
    Vec3 m_hizForward = Vec3::Forward();
    std::chrono::steady_clock::time_point m_hizTime{};
    std::string m_hizError;

    // Verify result.
    bool m_truthValid = false;
    ClusterTruthResult m_truth;
    std::uint64_t m_truthInvalidIds = 0;
    std::uint64_t m_truthPixels = 0;
    HizVerdictKind m_truthVerdict = HizVerdictKind::Clean;
    VerifyKey m_truthKey;
    std::string m_truthError;

    // Input state.
    float m_controlsWidth = 320.0f;
    bool m_orbiting = false;
    bool m_panning = false;
    bool m_gizmoDrawn = false;
    bool m_debugHovered = false;
    std::string m_cloneStatus;
};

} // namespace gte
