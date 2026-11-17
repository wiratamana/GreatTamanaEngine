#pragma once

// Main-thread executor of the /water/<action> HTTP routes. Actions arrive as "water.<name>".

#include "../../Application/TerrainCommandBridge.h"
#include "../../Features/Water/World/WaterControl.h"

namespace gte {

TerrainCommandResult ExecuteWaterCommand(water::IWaterWorld& world, const TerrainCommandRequest& request);

// True for actions that belong to the water executor.
inline bool IsWaterAction(const std::string& action) noexcept { return action.rfind("water.", 0) == 0; }

} // namespace gte
