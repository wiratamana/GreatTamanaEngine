#pragma once

// Mailbox between the Terrain panel (ImGui, draws after the frame was built) and the terrain world
// (changes GPU buffers). The panel only writes `request`; the host applies it at the start of the next frame
// and fills `snapshot` for the panel and the view overlays. No ImGui and no GPU types here.

#include "../../../Math/Mat4.h"
#include "../TerrainControl.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

struct TerrainUiRequest {
    std::optional<terrain::TerrainSettings> settings;
    std::optional<terrain::TerrainRuntimeOptions> runtime;
    std::optional<terrain::TerrainOpenOptions> open;
    bool close = false;
    std::optional<bool> livePreview;
    std::optional<bool> debugAabb;
    std::optional<std::string> savePath;
    std::optional<std::string> loadPath;
    std::optional<terrain::CameraMode> cameraMode;
    std::optional<terrain::FlightSpec> flight;
    bool stopFlight = false;
    terrain::CameraInput cameraInput; // Summed over the frames since the host last took it.
    bool hasCameraInput = false;

    bool Empty() const noexcept
    {
        return !settings && !runtime && !open && !close && !livePreview && !debugAabb && !savePath && !loadPath && !cameraMode
            && !flight && !stopFlight && !hasCameraInput;
    }
};

struct TerrainUiSnapshot {
    bool valid = false;
    bool open = false;
    terrain::TerrainStats stats;
    terrain::TerrainSettings settings;
    terrain::TerrainRuntimeOptions runtime;
    terrain::CameraMode cameraMode = terrain::CameraMode::Script;
    bool livePreview = false;
    bool debugAabb = false;
    std::string message;      // Outcome of the last action.
    bool messageIsError = false;
    std::string busy;         // Not empty while a background save or load runs (the panel shows it).
    bool hasViewProjection = false;
    Mat4 viewProjection = Mat4::Identity(); // Game camera, flipY, absolute world space.
    std::vector<terrain::TerrainNodeInfo> nodes; // Filled while the bounding-box overlay is on.
};

struct TerrainUiBridge {
    TerrainUiRequest request;
    TerrainUiSnapshot snapshot;

    // View debug state owned by the UI.
    bool overlayColorByState = true; // Node boxes: by draw state (true) or by LOD level (false).
    bool lookCaptured = false;       // A camera drag started over the Game image and is still held.

    // Last finished background save/load text that was already shown in the snapshot message.
    std::string lastAssetResult;
};

// Applies the pending request to the world (main thread, once per frame, before the world's Tick).
void ApplyTerrainUiRequest(terrain::ITerrainWorld& world, TerrainUiBridge& bridge);

// Copies the world state into the snapshot (main thread, once per frame, after the Tick).
void PublishTerrainSnapshot(terrain::ITerrainWorld& world, TerrainUiBridge& bridge, const Mat4* gameViewProjection);

} // namespace gte
