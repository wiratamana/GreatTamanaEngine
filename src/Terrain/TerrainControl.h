#pragma once

// Control surface of the live terrain world. The HTTP bridge and the editor panel talk only to this interface;
// the GPU details stay behind it. Every call is made on the main thread.

#include "TerrainCameraRig.h"
#include "TerrainFlight.h"
#include "TerrainSettings.h"
#include "TerrainTypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gte::terrain {

// How much of the fixed-cost render features stay on while the terrain scene is open.
enum class TerrainProfile {
    Full,         // Nothing is switched off.
    Lean,         // Volumetric light, SSGI and GTAO are off (the heavy ones on an iGPU).
    GeometryOnly, // Same set the Hi-Z demo switches off.
};

struct TerrainOpenOptions {
    TerrainProfile profile = TerrainProfile::Lean;
    std::uint32_t poolSlots = 0; // 0 = chosen from the settings (see TerrainPoolSlotsFor).
    bool water = true;           // A flat ocean quad at sea level.
    bool renderBudget = true;    // Lean and geometry-only profiles also lower shadow and aerial perspective quality.
};

// Tunables that are not part of the saved world.
struct TerrainRuntimeOptions {
    int maxVisibleNodes = 384;      // Cap of nodes in view; the LOD selector lowers detail to stay under it. 0 = no cap.
    int maxUploadsPerTick = 6;      // Node vertex writes per frame.
    bool collectGpuStats = true;    // Ask the culling feature for its counters.
    int streamingMaxInFlight = 4;   // Node jobs running at once.
    int unloadDelayFrames = 90;
    int bakeNodesPerSide = 16;      // Save bakes this many finest-level nodes per side around the camera.
};

// The square of land the live preview shows: 128 quads of `spacing` metres around the centre.
struct TerrainPreviewWindow {
    bool autoCenter = true;    // Pick a nearby hilly spot when the settings change the land.
    double centerX = 0.0;
    double centerZ = 0.0;
    double spacing = 4.0;      // 2..16 metres between preview vertices.
};

struct TerrainResult {
    bool ok = true;
    std::string message;
    SettingsChange change = SettingsChange::None; // What a SetSettings call needed.
};

// One resident node as the debug views see it (world space, metres).
struct TerrainNodeInfo {
    NodeKey key;
    double minX = 0.0;
    double minY = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    double maxZ = 0.0;
    bool enabled = false; // Drawn by the GPU path (not hidden by a finer or coarser node).
    bool inView = false;  // Wanted by the LOD selector this frame and inside the camera frustum.
};

// Camera placement request; every field is optional. `y` is metres above ground when heightAboveGround is set.
struct TerrainCameraPose {
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> z;
    std::optional<double> yawDegrees;       // 0 looks along +Z, 90 along +X.
    std::optional<double> pitchDownDegrees; // Positive looks toward the ground.
    std::optional<double> targetX;          // Orbit centre.
    std::optional<double> targetZ;
    std::optional<double> orbitDistance;
    bool heightAboveGround = false;
};

struct TerrainStats {
    bool open = false;
    bool livePreview = false;
    bool flightActive = false;
    bool debugAabb = false;
    std::uint64_t frame = 0;

    // Camera the LOD selector used.
    double cameraX = 0.0;
    double cameraY = 0.0;
    double cameraZ = 0.0;

    // Streaming (CPU).
    int nodesWanted = 0;
    int nodesInView = 0;
    int nodesResident = 0;
    int nodesPending = 0;
    int uploadQueue = 0;
    int nodesWrittenThisTick = 0;
    std::uint64_t builtTotal = 0;
    std::uint64_t evictedTotal = 0;
    double generationMsAverage = 0.0;
    double generationMsMax = 0.0;
    double generatedPerSecond = 0.0;
    double lodDistanceScale = 1.0; // 1 = full detail; below 1 the visible cap is lowering detail.

    // Pool (GPU memory).
    int slotsTotal = 0;
    int slotsUsed = 0;
    int slotsEnabled = 0;
    std::uint64_t trianglesEnabled = 0; // Triangles of the enabled nodes, before any GPU culling.
    double residentMbCpu = 0.0;
    double residentMbGpu = 0.0;         // Vertex, index and cluster table buffers of the pool.
    double uploadMbThisTick = 0.0;

    // Cost on the main thread.
    double tickCpuMs = 0.0;
    double tickCpuMsMax = 0.0;
    double selectMs = 0.0;
    double previewBuildMs = 0.0;       // Last live preview rebuild (generation plus upload).
    std::uint64_t previewRebuilds = 0;

    // GPU culling (from the culling feature; valid when gpuCullValid). Counts lag about two frames.
    bool gpuCullValid = false;
    bool gpuPathActive = false;        // The pool entity is drawn by the GPU-driven path.
    std::string gpuCullMode;           // NoCull, Frustum or HiZ.
    std::string gpuOcclusion;          // Applied when Hi-Z really ran.
    std::uint64_t gpuTestedClusters = 0;  // Pool slots tested (inactive ones included).
    std::uint64_t gpuDrawnNodes = 0;      // Clusters that survived both culls.
    std::uint64_t gpuFrustumCulledNodes = 0; // Live nodes only: the inactive slots are subtracted.
    std::uint64_t gpuHiZCulledNodes = 0;
    std::uint64_t gpuTrianglesDrawn = 0;

    // Interactive camera (CameraMode name: script, free or orbit).
    std::string cameraMode = "script";
    double cameraYawDegrees = 0.0;
    double cameraPitchDownDegrees = 0.0;
    double cameraSpeed = 0.0;          // Metres per second of the last frame.
    double cameraFovDegrees = 60.0;    // Vertical field of view of the camera.
    double cameraAspect = 16.0 / 9.0;  // Width over height of the Game view.

    // Background save or load (src/Terrain/TerrainAssetTask.h). assetResult keeps the last finished text.
    bool assetBusy = false;
    std::string assetKind;             // "save", "load" or empty.
    double assetProgress = 0.0;        // 0..1 while busy.
    double assetElapsedMs = 0.0;
    std::string assetResult;

    std::string lastError;
};

// Slot count used when TerrainOpenOptions::poolSlots is 0: room for the visible cap, its prefetch ring and fallbacks.
inline std::uint32_t TerrainPoolSlotsFor(const TerrainRuntimeOptions& runtime) noexcept
{
    const int visible = runtime.maxVisibleNodes > 0 ? runtime.maxVisibleNodes : 512;
    return static_cast<std::uint32_t>(visible + visible / 2 + 32);
}

class ITerrainWorld {
public:
    virtual ~ITerrainWorld() = default;

    // --- Settings: height or geometry changes regenerate, material changes recolour, the rest flips flags. ---
    virtual const TerrainSettings& Settings() const = 0;
    virtual TerrainResult SetSettings(const TerrainSettings& settings) = 0;
    virtual const TerrainRuntimeOptions& RuntimeOptions() const = 0;
    virtual TerrainResult SetRuntimeOptions(const TerrainRuntimeOptions& options) = 0;

    // --- Scene ---
    virtual bool IsOpen() const = 0;
    virtual TerrainResult Open(const TerrainOpenOptions& options) = 0; // Replaces the current scene.
    virtual TerrainResult Close() = 0;

    // --- Live preview: one 129x129 vertex chunk, rebuilt at once on every SetSettings. ---
    virtual TerrainResult SetLivePreview(bool enabled) = 0;
    virtual bool LivePreview() const = 0;
    virtual TerrainResult SetPreviewWindow(const TerrainPreviewWindow& window) = 0;
    virtual TerrainPreviewWindow PreviewWindow() const = 0;

    // --- Debug ---
    virtual void SetDebugAabb(bool enabled) = 0; // Makes CollectNodes() report boxes; overlays draw them.
    virtual bool DebugAabb() const = 0;
    virtual void CollectNodes(std::vector<TerrainNodeInfo>& out) const = 0;
    virtual TerrainStats Stats() const = 0;

    // --- Asset file (.gta) ---
    virtual TerrainResult SaveAsset(const std::filesystem::path& path) = 0;
    virtual TerrainResult LoadAsset(const std::filesystem::path& path) = 0;

    // --- Scripted flight of the terrain camera (deterministic, frame-rate independent) ---
    virtual TerrainResult StartFlight(const FlightSpec& spec) = 0;
    virtual void StopFlight() = 0;
    virtual bool FlightActive() const = 0;

    // --- Interactive camera: free-fly or orbit. Input is summed per frame and used by the next Tick. ---
    virtual TerrainResult SetCameraMode(CameraMode mode) = 0;
    virtual CameraMode GetCameraMode() const = 0;
    virtual void FeedCameraInput(const CameraInput& input) = 0;
    virtual TerrainResult QueueCameraInput(const CameraInput& perFrame, int frames) = 0; // Scripted input for tests.
    virtual TerrainResult SetCameraPose(const TerrainCameraPose& pose) = 0;
};

// Stats as one JSON object (numbers and strings only), for the HTTP routes.
std::string TerrainStatsToJson(const TerrainStats& stats);

// Name of a profile for logs and routes ("full", "lean", "geometry_only"), and the inverse (false when unknown).
const char* TerrainProfileName(TerrainProfile profile) noexcept;
bool TryParseTerrainProfile(const std::string& name, TerrainProfile& out) noexcept;

} // namespace gte::terrain
