#pragma once

// The one surface the cluster viewer exposes to the HTTP bridge. No ImGui, no Vulkan.

#include "ViewportProjection.h"

#include "../Core/CapturedPixels.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte {

class Renderer;

// Conflict = state conflict (409). Unavailable = no editor or viewer in this build (503).
enum class ClusterViewerError : std::uint8_t { None, NotOpen, NoValidMesh, Unsupported, BadInput, Conflict, Unavailable };

// Which image a capture returns: the main view or the debug-camera preview.
enum class ClusterViewerImageTarget : std::uint8_t { View, DebugPreview };

struct ClusterViewerResult {
    bool ok = false;
    ClusterViewerError error = ClusterViewerError::None;
    std::string message;
};

struct ClusterViewerPickResult {
    bool ok = false;
    bool hit = false;
    ClusterViewerError error = ClusterViewerError::None;
    std::string message;
};

// One setting change of a Set() call.
struct KeyValue {
    std::string key;
    std::string value;
};

// A captured image and the 2D overlay (image pixels) drawn over it: translucent quads, then lines.
struct ClusterViewerImage {
    CapturedPixels pixels;
    std::vector<OverlaySegment2D> overlay;
    std::vector<OverlayQuad2D> overlayQuads;
};

struct ClusterViewerMeshReport {
    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
    std::uint32_t clusters = 0;
    bool clusterModes = false;
    std::string note;
};

struct ClusterViewerCullReport {
    bool ok = false;
    std::uint32_t total = 0;
    std::uint32_t passedFrustum = 0;
    std::uint32_t passedCone = 0;
    std::uint32_t visible = 0;
    std::uint32_t byFrustum = 0;
    std::uint32_t byCone = 0;
    std::uint32_t byOcclusion = 0;
    std::uint64_t visibleTriangles = 0;
    double runMilliseconds = 0;
    bool occlusionApplied = false;
    bool hizCut = false;
    bool nested = false;
    std::string error;
};

struct ClusterViewerHizReport {
    bool captured = false;
    bool stale = false;
    int width = 0;
    int height = 0;
};

struct ClusterViewerTruthReport {
    bool valid = false;
    bool stale = false;
    std::uint32_t seen = 0;
    std::uint32_t falseCulls = 0;
    std::uint32_t conservative = 0;
    std::uint32_t invalidIds = 0;
    std::uint64_t pixelsChecked = 0;
};

struct ClusterViewerDebugCameraReport {
    bool show = false;
    bool selected = false;
    float apexX = 0.0f; // 0..1 of the viewer image
    float apexY = 0.0f;
    bool screenVisible = false;
};

// Everything GET /cluster_viewer/state shows.
struct ClusterViewerReport {
    bool open = false;
    std::string path;
    bool valid = false;
    ClusterViewerMeshReport mesh;
    std::vector<KeyValue> settings;
    ClusterViewerCullReport cull;
    ClusterViewerHizReport hiz;
    ClusterViewerTruthReport truth;
    ClusterViewerDebugCameraReport debugCamera;
    bool clusterPathSupported = false;
};

class IClusterViewerControl {
public:
    virtual ~IClusterViewerControl() = default;

    // Opens or re-targets the window and loads the mesh before returning.
    virtual ClusterViewerResult Open(Renderer& renderer, const std::string& absolutePath) = 0;
    virtual ClusterViewerReport Report() const = 0;

    // All-or-nothing: one bad pair applies none.
    virtual ClusterViewerResult Set(std::span<const KeyValue> pairs) = 0;
    virtual ClusterViewerResult Verify() = 0;
    virtual ClusterViewerResult CaptureHiz() = 0;

    // Camera pick at a point (x, y in 0..1) of the viewer image.
    virtual ClusterViewerPickResult Pick(float x, float y) = 0;
    virtual std::optional<ClusterViewerImage> CaptureImage(ClusterViewerImageTarget target, bool overlay) = 0;
};

} // namespace gte
