#pragma once

// Runs one /terrain/<action> HTTP command on the main thread against the terrain world.

#include "../../Application/TerrainCommandBridge.h"
#include "../../Features/Terrain/TerrainControl.h"

namespace gte {

TerrainCommandResult ExecuteTerrainCommand(terrain::ITerrainWorld& world, const TerrainCommandRequest& request);

} // namespace gte
