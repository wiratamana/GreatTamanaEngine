#pragma once

// Main-thread executor of the /water/<action> HTTP routes. Actions arrive as "water.<name>".

#include "../../../Application/ModuleCommandBridge.h"
#include "../World/WaterControl.h"

namespace gte {

ModuleCommandResult ExecuteWaterCommand(water::IWaterWorld& world, const ModuleCommandRequest& request);

} // namespace gte
