#pragma once

// One process-wide mutex for engine state that hot reload mutates outside the ECS registry: the component type registry, the editor window
// registry (RegisterWindow/UnregisterWindow) and the loaded-assembly list with its registration ledger.

#include <mutex>

namespace gte {

std::mutex& GetHotReloadEngineStateMutex();

} // namespace gte
