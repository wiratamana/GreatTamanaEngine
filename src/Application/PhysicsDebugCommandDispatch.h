#pragma once
#include "PhysicsDebugCommandBridge.h"

namespace gte {

class Game;

// Runs one physics debug command on the main thread. Never throws.
PhysicsDebugCommandResult ExecutePhysicsDebugCommand(Game& game, const PhysicsDebugCommandRequest& request);

} // namespace gte
