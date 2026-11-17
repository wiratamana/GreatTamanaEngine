#include "../../../Application/ProfilerScopeCatalog.h"

namespace gte {
namespace {

// Plain-English profiler rows of the terrain scopes.
const ScopeDescriptionAutoRegister kTick({ "TerrainWorld::Tick", "Terrain streaming",
    "Picks the terrain nodes the camera needs, writes new ones into the graphics memory pool and updates the culling table.",
    "If this is big, lower the visible node cap or the uploads per frame.", ScopeCategory::Simulation, false });

const ScopeDescriptionAutoRegister kBuildNode({ "Terrain.BuildNode", "Building a terrain node",
    "A worker thread makes the heights, erosion and vertices of one terrain node.", "", ScopeCategory::Simulation, false });

const ScopeDescriptionAutoRegister kBake({ "Terrain.Bake", "Baking terrain nodes",
    "A worker thread bakes one terrain node into the saved world data.", "", ScopeCategory::Simulation, false });

} // namespace
} // namespace gte
