#pragma once

#include "HizDemoState.h"
#include "../Core/EditorCapabilities.h"
#include "../Core/FeatureKit/FeatureDebug.h"
#include "../Core/GraphicsHandles.h"
#include "../ECS/Entity.h"

// Opens, and closes, the Hi-Z demo scene: a camera, a sun, a wall in front of a large field of grid instances,
// with the culling settings of the GpuDrivenScene feature set for a comparison run.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gte {

class Game;
class Renderer;
class RenderFeatureCompositor;
class InstanceGridResources;

// The one list of fixed-cost features the "geometry_only" profile turns off: a feature matches when its
// name starts with one of these. Scripts read it from GET /hiz_demo/geometry_only_features.
inline constexpr std::array<std::string_view, 6> kGeometryOnlyFeaturePrefixes{
    "Shadow", "VolumetricLight", "Atmosphere", "Ssgi", "Gtao", "ToneMapping" };

// True when `featureName` starts with a prefix of kGeometryOnlyFeaturePrefixes.
bool IsGeometryOnlyFeature(std::string_view featureName) noexcept;

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

// Builds the new scene first and replaces the old one only when everything worked. Main thread only. Never throws.
HizDemoReport OpenHizDemo(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const PresetParameters& parameters, const HizDemoServices& services) noexcept;

// Restores the settings and removes the entities the demo made. False (with `message`) when it is not active.
bool CloseHizDemo(Game& game, const HizDemoServices& services, std::string& message) noexcept;

// Restores the settings only; for a scene that was replaced by something else. A no-op when not active.
void ReleaseHizDemoSettings(const HizDemoServices& services) noexcept;

// The hiz_demo-specific keys of the open reply, as a JSON object text:
// profile, instanceCount, countClamped, clustersPerInstance, workItems, entities{camera,sun,wall},
// gpuDrivenScene{mode,collectStats}, restoresOnClose. Entities are "index/generation".
std::string BuildHizDemoDetailsJson(const HizDemoReport& report);

} // namespace gte
