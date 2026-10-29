#pragma once

#include "../Core/FeatureKit/FeatureDebug.h"
#include "../ECS/Entity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// What the Hi-Z demo scene changed, so closing it can put everything back. Lives in EditorContext.
struct HizDemoState {
    bool active = false;
    bool geometryOnly = true;                          // The profile that was opened.
    std::vector<Entity> created;                       // Every entity the demo made.
    std::vector<std::string> disabledFeatures;         // Features the profile turned off.
    std::vector<fx::FeatureDebugArg> savedSettings;    // GpuDrivenScene settings before the demo.
};

// Hi-Z demo actions a menu click asks the host to run on the main thread.
enum class HizDemoMenuAction : std::uint8_t { None, Open, Close, SceneReplaced };

// Outcome of a menu action, shown once by the menu bar (`fresh` is cleared when it is read).
struct HizDemoMenuResult {
    bool fresh = false;
    bool isError = false;
    std::string message;
};

} // namespace gte
