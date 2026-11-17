#pragma once

// Runs one /terrain/<action> HTTP command on the main thread against the terrain world.

#include "../../../Application/ModuleCommandBridge.h"
#include "../TerrainControl.h"

namespace gte {

ModuleCommandResult ExecuteTerrainCommand(terrain::ITerrainWorld& world, const ModuleCommandRequest& request);

} // namespace gte
