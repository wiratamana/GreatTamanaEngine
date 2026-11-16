#pragma once

// Builds the height tile the Grass feature reads: drawn terrain height plus a submerged flag per texel.
// Pure CPU work on plain inputs, so it can run on a worker thread and be unit tested.

#include "../../Features/Grass/GrassTypes.h"
#include "../../Terrain/TerrainSurface.h"
#include "../../Water/WaterBodySet.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace gte {

struct GrassTileJob {
    std::uint32_t size = 0;      // Texels per side.
    float texelSize = 1.0f;      // Metres.
    double originX = 0.0;        // World xz of texel (0, 0); a multiple of the texel size.
    double originZ = 0.0;
    double centerX = 0.0;        // Camera xz the coverage is measured around.
    double centerZ = 0.0;
    double reach = 182.0;        // Metres around the camera that must be right.
    float waterMargin = 0.6f;    // Ground this far above a surface still counts as submerged.
    std::uint64_t revision = 1;
    terrain::SurfaceSnapshot surface;
    std::shared_ptr<const water::WaterBodySet> water; // Null = no water at all.
};

struct GrassTileBuild {
    std::shared_ptr<const GrassHeightTile> tile;
    double buildMs = 0.0;
    float coverageLevel0 = 0.0f; // Share of texels within `reach` taken from level-0 nodes.
    float coverageNodes = 0.0f;  // Share taken from any resident node.
    float submergedShare = 0.0f;
};

// Origin that puts a size x size tile around `center`, snapped so texels keep their world positions between rebuilds.
double SnapGrassTileOrigin(double center, std::uint32_t size, float texelSize) noexcept;

// World Y of the ocean in `water`, or a very low value when there is no ocean.
float GrassOceanLevel(const water::WaterBodySet* water) noexcept;

GrassTileBuild BuildGrassTile(const GrassTileJob& job);

// What the world looked like when a tile was built (or looks like now); compared to decide on a rebuild.
struct GrassTileSeen {
    std::uint64_t settingsKey = 0;
    std::uint64_t waterHash = 0;
    std::uint64_t carveHash = 0;
    std::uint64_t terrainHash = 0;
    std::uint64_t streamRevision = 0;
    int rebuilding = 0;
    // Resident level 0 and 1 nodes and which mesh each key held; a replaced mesh (same key) has new content.
    std::unordered_map<terrain::NodeKey, const terrain::NodeMesh*, terrain::NodeKeyHash> fineNodes;
    std::vector<std::shared_ptr<const terrain::NodeMesh>> pinned; // Keeps the addresses above from being reused.
};

// The submitted tile against the camera.
struct GrassTileView {
    bool haveTile = false;
    double originX = 0.0;
    double originZ = 0.0;
    double extent = 0.0;       // Metres per side.
    double reach = 182.0;      // Metres around the camera that must stay inside the tile.
    double cameraX = 0.0;
    double cameraZ = 0.0;
    double now = 0.0;
    double settleUntil = -1.0; // Until then any rebuilt node counts (a terrain edit is settling).
};

// Why the tile should be rebuilt now, if at all. Pure function of the two snapshots of the world.
std::optional<GrassTileReason> DecideGrassTileRebuild(const GrassTileView& view, const GrassTileSeen& last, const GrassTileSeen& now);

} // namespace gte
