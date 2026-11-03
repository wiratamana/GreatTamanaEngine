// Opens, and closes, the Furina showcase scene: the Furina model with the toon mark, one sun and one portrait
// camera, with the render features set for a clean toon image.
#pragma once

#include "FurinaShowcaseLayout.h"
#include "PresetFeatureSettings.h"
#include "../Core/FeatureKit/FeatureDebug.h"
#include "../Core/GraphicsHandles.h"
#include "../Core/Plugins/RenderFeatureDebugEntry.h"
#include "../ECS/Entity.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gte {

class Game;
class Renderer;
class RenderFeatureCompositor;
struct SkinnedMeshData;

// Registrations the showcase switches off: the heavy screen-space and atmosphere-composite passes.
inline constexpr std::array<std::string_view, 8> kFurinaShowcaseFeaturesOff{ "AtmosphereAerialVolume",
    "AtmosphereComposite", "Gtao.Occlusion", "Gtao.Composite", "Ssgi.Trace", "Ssgi.Composite",
    "VolumetricLight.Volume", "VolumetricLight.Composite" };

// Registration name prefixes the showcase switches on. A prefix with no registration is skipped.
inline constexpr std::array<std::string_view, 5> kFurinaShowcaseFeaturePrefixesOn{
    "Shadow", "SceneLighting", "ToonCharacter", "Bloom", "ToneMapping" };

// One feature debug key the showcase writes, as the text Set() takes.
struct FurinaLookSetting {
    std::string_view key;
    std::string_view value;
};

// Debug settings of one feature the showcase writes. The old values are saved first and restored on close.
struct FurinaFeatureLook {
    std::string_view feature;
    std::span<const FurinaLookSetting> settings;
};

inline constexpr std::array<FurinaLookSetting, 3> kFurinaToneMappingLook{
    { { "mode", "PbrNeutral" }, { "exposure", "1.4" }, { "saturation", "1.2" } } };
inline constexpr std::array<FurinaLookSetting, 2> kFurinaToonCharacterLook{ { { "enabled", "true" }, { "debugView", "0" } } };
// Threshold above the lit white hair, so only the gems glow.
inline constexpr std::array<FurinaLookSetting, 3> kFurinaBloomLook{
    { { "enabled", "true" }, { "threshold", "2.5" }, { "intensity", "0.5" } } };
inline constexpr std::array<FurinaFeatureLook, 3> kFurinaShowcaseLooks{ { { "ToneMapping", kFurinaToneMappingLook },
    { "ToonCharacter", kFurinaToonCharacterLook }, { "Bloom", kFurinaBloomLook } } };

// What the open scene changed, so closing it puts everything back.
struct FurinaShowcaseState {
    bool active = false;
    std::vector<Entity> created;                    // Model root, camera and sun.
    std::vector<std::string> enabledByPreset;       // Registrations the preset switched on.
    std::vector<std::string> disabledByPreset;      // Registrations the preset switched off.
    std::vector<SavedFeatureSettings> savedSettings; // Feature settings before the preset.
};

// Non-owning handles the opener works through. A feature the lookup cannot find keeps its settings.
struct FurinaShowcaseServices {
    FurinaShowcaseState* state = nullptr;
    RenderFeatureCompositor* compositor = nullptr;
    std::function<fx::IFeatureDebugEndpoint*(std::string_view)> findEndpoint; // By feature name; may be empty.
    std::function<void()> releasePrevious; // Called once the new scene is built, before any setting changes.
};

struct FurinaShowcaseReport {
    bool success = false;
    std::string message; // Status line on success; the error text (old scene kept) on failure.
    Entity model = kInvalidEntity;
    Entity camera = kInvalidEntity;
    Entity sun = kInvalidEntity;
    float modelHeight = 0.0f;
    float cameraDistance = 0.0f;
    bool animationStarted = false;
    std::uint32_t featuresSwitchedOn = 0;
    std::uint32_t featuresSwitchedOff = 0;
};

// Registrations to switch for the showcase, from the current feature list. Pure.
struct FurinaFeatureSwitch {
    std::vector<std::string> turnOn;
    std::vector<std::string> turnOff;
};
FurinaFeatureSwitch PlanFurinaFeatureSwitch(std::span<const RenderFeatureDebugEntry> features);

// Bind-pose bounds and hip bone height of a skinned model. The hip is NaN when the skeleton has no hip bone.
FurinaShowcaseModel MeasureFurinaModel(const SkinnedMeshData& skin) noexcept;

// Builds the new scene first and replaces the old one only when everything worked. Main thread only. Never throws.
FurinaShowcaseReport OpenFurinaShowcase(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const FurinaShowcaseServices& services) noexcept;

// Restores the settings and removes the entities the showcase made. False (with `message`) when it is not
// active or the settings restore was incomplete.
bool CloseFurinaShowcase(Game& game, const FurinaShowcaseServices& services, std::string& message) noexcept;

// Restores the settings only; for a scene that was replaced by something else. A no-op when not active.
bool ReleaseFurinaShowcaseSettings(const FurinaShowcaseServices& services) noexcept;

// The furina_showcase-specific keys of the open reply as a JSON object text.
std::string BuildFurinaShowcaseDetailsJson(const FurinaShowcaseReport& report);

} // namespace gte
