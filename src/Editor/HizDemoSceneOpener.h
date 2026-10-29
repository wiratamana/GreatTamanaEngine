#pragma once

#include "HizDemoState.h"
#include "../Core/EditorCapabilities.h"
#include "../Core/FeatureKit/FeatureDebug.h"
#include "../Core/GraphicsHandles.h"
#include "../Core/Plugins/RenderFeatureDebugEntry.h"
#include "../ECS/Entity.h"

// Opens, and closes, the Hi-Z demo scene: a camera, a sun, a wall in front of a large field of grid instances,
// with the culling settings of the GpuDrivenScene feature set for a comparison run.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gte {

class Game;
class Renderer;
class RenderFeatureCompositor;
class InstanceGridResources;

// Non-owning handles the opener works through. `compositor` may be null only for the "full" profile.
struct HizDemoServices {
    InstanceGridResources* grid = nullptr;
    HizDemoState* state = nullptr;
    RenderFeatureCompositor* compositor = nullptr;
    fx::IFeatureDebugEndpoint* gpuDrivenScene = nullptr;
};

struct HizDemoReport {
    bool success = false;
    std::string message; // Status line on success; the error text (old scene kept) on failure.
    std::string profile;
    std::uint32_t instanceCount = 0;
    bool countClamped = false;
    std::uint32_t clustersPerInstance = 0;
    std::uint64_t workItems = 0;
    Entity camera = kInvalidEntity;
    Entity sun = kInvalidEntity;
    Entity wall = kInvalidEntity;
    std::string mode;
    bool collectStats = false;
};

// Feature switches an open needs for `profile`.
struct HizFeatureSwitch {
    std::vector<std::string> turnOn;  // Features an earlier demo turned off that this profile wants back on.
    std::vector<std::string> turnOff; // Still-enabled fixed-cost features this profile turns off.
};

// `carriedOff` lists the features an already active demo turned off. Pure.
HizFeatureSwitch PlanHizFeatureSwitch(PresetProfile profile, std::span<const std::string> carriedOff,
    std::span<const RenderFeatureDebugEntry> features);

// Builds the new scene first and replaces the old one only when everything worked. Main thread only. Never throws.
HizDemoReport OpenHizDemo(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const PresetParameters& parameters, const HizDemoServices& services) noexcept;

// Restores the settings and removes the entities the demo made. False (with `message`) when it is not
// active or the settings restore was incomplete.
bool CloseHizDemo(Game& game, const HizDemoServices& services, std::string& message) noexcept;

// Restores the settings only; for a scene that was replaced by something else. A no-op when not active.
// False when the restore was incomplete (each failure is logged).
bool ReleaseHizDemoSettings(const HizDemoServices& services) noexcept;

// The hiz_demo-specific keys of the open reply as a JSON object text.
std::string BuildHizDemoDetailsJson(const HizDemoReport& report);

} // namespace gte
