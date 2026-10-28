#pragma once

// GPU side of the cluster viewer: runs the real cluster cull shader on one mesh and draws the survivors
// with one indirect-count draw. Every count and image is read back from the GPU.

#include "Culling/ClusterCullMath.h"
#include "MeshPreviewRenderer.h"
#include "../Assets/MeshCluster.h"
#include "../Core/CapturedPixels.h"
#include "../Core/UiTextureHandle.h"
#include "../Math/Mat4.h"
#include "../Math/Vec3.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte {

class Renderer;

// Hard limit of clusters per mesh; the real limit is MaxClusters().
inline constexpr std::uint32_t kViewerMaxClusters = 200000;

enum class ClusterViewSlot : std::uint8_t { Main, Preview };
enum class ClusterViewShade : std::uint8_t { Clay, Tint };

// Cull camera. Independent of the draw camera.
struct ClusterViewCull {
    ClusterCullFlags flags = ClusterCullFlags::None;
    Mat4 viewProjection = Mat4::Identity();
    Vec3 eye = Vec3::Zero();
};

struct ClusterViewFrame {
    Mat4 viewProjection = Mat4::Identity(); // draw camera, engine convention (flipY = true)
    int width = 1;
    int height = 1;
    std::array<std::uint8_t, 4> clearRgba{ 30, 32, 38, 255 };
    ClusterViewShade shade = ClusterViewShade::Clay;
    bool dimCulled = false; // draws every cluster flat dim, then the survivors on top
    ClusterViewCull cull;
};

// Counts reported by the GPU. Nothing here is computed from cluster data on the CPU.
struct ClusterViewCounts {
    std::uint32_t total = 0;            // clusters tested
    std::uint32_t passedFrustum = 0;    // survivors of the frustum test (total when the flag is off)
    std::uint32_t passedCone = 0;       // survivors of frustum, then cone
    std::uint32_t visible = 0;          // survivors of every applied test: what is drawn
    std::uint64_t visibleTriangles = 0; // sum of indexCount / 3 over the drawn commands
};

struct ClusterViewRender {
    bool ok = false;
    UiTextureHandle texture = UiTextureHandle::Invalid;
    ClusterViewCounts counts;
    bool occlusionApplied = false; // false when the flag was set but no usable Hi-Z capture exists
    bool hizCut = false;           // the capture was dropped because the cull pose differs from the capture pose
    double runMilliseconds = 0;    // wall time of the whole run, read-back included
    std::string note;              // why ok is false, empty when fine
};

struct ClusterViewCullResult {
    bool ok = false;
    std::vector<std::uint32_t> survivors; // cluster ids that passed every applied test
    bool occlusionApplied = false;
    std::string note;
};

struct ClusterViewMeshResult {
    bool meshSet = false;        // vertices and indices are on the GPU
    bool clustersUsable = false; // the cluster table passed every check
    bool retryable = false;      // the GPU was busy; the old mesh is kept and the call may be repeated
    std::string note;            // why the mesh or the table was refused, empty when fine
};

// Pose of the stored Hi-Z capture.
struct ClusterViewHizInfo {
    Mat4 viewProjection = Mat4::Identity();
    Vec3 eye = Vec3::Zero();
    int width = 0;
    int height = 0;
    std::uint64_t captureId = 0; // grows with every capture
};

// Owns the viewer pipelines, one mesh, its cluster table, the targets and the Hi-Z atlas.
// All runs are synchronous. The Renderer must outlive it.
class ClusterViewRenderer {
public:
    // Null (one error logged, reason in *whyNot) when the device lacks the indirect-draw features,
    // a shader file is missing or a pipeline cannot be built.
    static std::unique_ptr<ClusterViewRenderer> TryCreate(Renderer& renderer, std::string* whyNot);

    ~ClusterViewRenderer();

    ClusterViewRenderer(const ClusterViewRenderer&) = delete;
    ClusterViewRenderer& operator=(const ClusterViewRenderer&) = delete;

    // Uploads the mesh (GPU idle first) and keeps no CPU copy. A bad index list: meshSet = false.
    // A bad table: meshSet = true, clustersUsable = false, one synthesized cluster.
    ClusterViewMeshResult SetMesh(std::span<const MeshPreviewVertex> vertices,
        std::span<const std::uint32_t> indices, std::span<const MeshCluster> clusters);

    // Cull runs, draw into the slot target and read-back. Blocks the CPU: call it only when the frame changed.
    ClusterViewRender Render(ClusterViewSlot slot, const ClusterViewFrame& frame);
    // The cull part of Render only. Returns the surviving cluster ids and the counts.
    ClusterViewCullResult RunCull(const ClusterViewCull& cull);

    // Pixels of the slot target as last rendered. Empty when the slot was never rendered.
    std::optional<CapturedPixels> CaptureSlot(ClusterViewSlot slot);

    // Draws every cluster (no culling) with a flat id colour (id + 1 in 24 bits, background 0) and returns the pixels.
    std::optional<CapturedPixels> RenderIdPass(
        const Mat4& viewProjection, int width, int height, bool cullBackFaces);

    // Draws a depth-only pass of every cluster, builds the Hi-Z pyramid and keeps it on the GPU for later culls.
    std::optional<ClusterViewHizInfo> CaptureHiz(
        const Mat4& viewProjection, const Vec3& eye, int width, int height);
    std::optional<ClusterViewHizInfo> HizCapture() const noexcept;
    void ReleaseHiz();

    std::uint32_t ClusterCount() const noexcept;  // real clusters, 0 when only the synthesized one exists
    std::uint32_t MaxClusters() const noexcept;   // min(kViewerMaxClusters, device limits)

    // Frees the Preview slot and the id target (GPU idle first).
    void ReleaseTargets();

private:
    struct Impl;
    // Lets std::make_unique call the constructor while only TryCreate can make the key.
    struct CreateKey {
        explicit CreateKey() = default;
    };

public:
    ClusterViewRenderer(CreateKey, std::unique_ptr<Impl> impl) noexcept;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte
