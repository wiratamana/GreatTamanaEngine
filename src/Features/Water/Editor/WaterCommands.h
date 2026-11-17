#pragma once

// Main-thread executor of the /water/<action> HTTP routes. Actions arrive as "water.<name>".

#include "../../../Application/ModuleCommandBridge.h"
#include "../World/WaterControl.h"

namespace gte {

ModuleCommandResult ExecuteWaterCommand(water::IWaterWorld& world, const ModuleCommandRequest& request);

// True for actions that belong to the water executor.
inline bool IsWaterAction(const std::string& action) noexcept { return action.rfind("water.", 0) == 0; }

} // namespace gte
