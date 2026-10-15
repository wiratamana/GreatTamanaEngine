#pragma once

#include "Time.h"

namespace gte {

// Per-frame state the simulation layer needs. Owned by Application, advanced once per frame and
// passed to Game::Update() by const reference. Add fields one justified field at a time.
struct EngineContext {
    Time time;
};

} // namespace gte
