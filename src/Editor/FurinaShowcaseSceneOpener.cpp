#include "FurinaShowcaseSceneOpener.h"

#include "PresetFeatureSettings.h"
#include "PresetSceneSupport.h"
#include "ProjectRootPath.h"
#include "../Core/Plugins/RenderFeatureCompositor.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/DirectionalLight.h"
#include "../ECS/Components/ToonShading.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/TransformHierarchy.h"
#include "../Encoding/Utf8Path.h"
#include "../Game/Animation/SkeletalRigCache.h"
#include "../Game/Game.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <limits>
#include <span>
#include <utility>

namespace gte {

namespace {

constexpr const char* kLogCategory = "FurinaShowcase";

constexpr std::string_view kRootName = "Furina";
constexpr std::string_view kCameraName = "Main Camera";
constexpr std::string_view kSunName = "Sun";
constexpr const char* kModelFile = "Furina.gta";
constexpr const char* kClipFile = "ChatanyaraKuushanku_bassui260717a.gta";

// PMX hip bone: the Japanese name and the English name the importer keeps.
constexpr std::string_view kHipBoneName = "\xE4\xB8\x8B\xE5\x8D\x8A\xE8\xBA\xAB";
constexpr std::string_view kHipBoneEnglishName = "lower body";

fx::IFeatureDebugEndpoint* FindEndpoint(const FurinaShowcaseServices& services, std::string_view feature)
{
    return services.findEndpoint ? services.findEndpoint(feature) : nullptr;
}

// Switches `enable` registrations on, `disable` ones off and restores the saved settings. A failure is
// logged and the rest still runs. False when anything failed.
bool UndoSettings(const FurinaShowcaseServices& services, std::span<const std::string> enable,
    std::span<const std::string> disable, std::span<const SavedFeatureSettings> saved) noexcept
{
    bool complete = SetFeaturesEnabled(services.compositor, kLogCategory, enable, disable);
    for (const SavedFeatureSettings& entry : saved) {
        const bool restored = RestoreFeatureSettings(FindEndpoint(services, entry.feature), kLogCategory, entry.feature, entry.args);
        complete = restored && complete;
    }
    return complete;
}

// What one open call changed, so a failure can undo exactly that and nothing else.
struct SettingsChange {
    std::vector<SavedFeatureSettings> before; // Settings as they were when this call started.
    std::vector<std::string> switchedOn;
    std::vector<std::string> switchedOff;
};

// Sets the look of every feature found and switches features for the showcase. `next` holds the baseline of an
// active showcase, else the current settings become it. All changes go to `change`. False with `error` on failure.
bool ApplySettings(const FurinaShowcaseServices& services, bool haveBaseline, FurinaShowcaseState& next,
    SettingsChange& change, std::string& error)
{
    change.before.reserve(kFurinaShowcaseLooks.size());
    if (!haveBaseline) {
        next.savedSettings.reserve(kFurinaShowcaseLooks.size());
    }
    for (const FurinaFeatureLook& look : kFurinaShowcaseLooks) {
        fx::IFeatureDebugEndpoint* endpoint = FindEndpoint(services, look.feature);
        if (endpoint == nullptr) {
            continue;
        }
        std::vector<std::string_view> keys;
        std::vector<fx::FeatureDebugArg> values;
        for (const FurinaLookSetting& setting : look.settings) {
            keys.push_back(setting.key);
            values.push_back({ std::string(setting.key), std::string(setting.value) });
        }
        // Recorded before Set(), so an undo after a refusal writes back what was there anyway.
        change.before.push_back({ std::string(look.feature), SnapshotFeatureSettings(*endpoint, keys) });
        if (!haveBaseline) {
            next.savedSettings.push_back(change.before.back());
        }
        const fx::FeatureDebugResult applied = endpoint->Set(values);
        if (!applied.ok) {
            error = std::string(look.feature) + " settings were refused: " + applied.message;
            return false;
        }
    }

    if (services.compositor == nullptr) {
        error = "the render feature compositor is missing";
        return false;
    }
    const FurinaFeatureSwitch plan = PlanFurinaFeatureSwitch(services.compositor->DebugSnapshot());

    // Capacity first: the pushes below must not throw after a feature was switched.
    change.switchedOn.reserve(plan.turnOn.size());
    change.switchedOff.reserve(plan.turnOff.size());
    next.enabledByPreset.reserve(next.enabledByPreset.size() + plan.turnOn.size());
    next.disabledByPreset.reserve(next.disabledByPreset.size() + plan.turnOff.size());

    for (const std::string& name : plan.turnOn) {
        if (!TrySetFeatureEnabled(*services.compositor, name, true)) {
            error = "could not turn on the feature \"" + name + "\"";
            return false;
        }
        change.switchedOn.push_back(name);
        next.enabledByPreset.push_back(name);
    }
    for (const std::string& name : plan.turnOff) {
        if (!TrySetFeatureEnabled(*services.compositor, name, false)) {
            error = "could not turn off the feature \"" + name + "\"";
            return false;
        }
        change.switchedOff.push_back(name);
        next.disabledByPreset.push_back(name);
    }
    return true;
}

FurinaShowcaseReport Fail(std::string message)
{
    PresetLogError(kLogCategory, message);
    FurinaShowcaseReport report;
    report.message = std::move(message) + " The previous scene was kept.";
    return report;
}

FurinaShowcaseReport OpenImpl(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const FurinaShowcaseServices& services)
{
    if (services.state == nullptr || services.compositor == nullptr) {
        return Fail("The Furina showcase needs the render feature compositor.");
    }

    const std::filesystem::path projectDirectory = ResolveProjectRootDirectory();
    const std::filesystem::path modelPath = projectDirectory / kModelFile;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(modelPath, ec)) {
        return Fail("Furina.gta not found in Project/. Import Furina.pmx first.");
    }
    const std::string modelPathText = PathToUtf8(modelPath);

    Registry& registry = game.GetRegistry();

    // Old roots are listed before anything new exists, so the new scene is never part of them.
    const std::vector<Entity> oldRoots = GetChildren(registry, kInvalidEntity);

    CreatedEntityGuard created(registry, kLogCategory);

    const Entity model = game.CreateMeshEntityFromGtaFile(renderer, modelPathText, sceneServicesSetLayout);
    if (model == kInvalidEntity) {
        return Fail("Furina showcase: could not load \"" + modelPathText + "\".");
    }
    created.Add(model);
    SetEntityName(registry, model, kRootName);
    registry.AddComponent<ToonShading>(model);

    const SkinnedMeshData* skin = game.GetMeshInstantiationSystem().TryGetSkinnedMeshData(modelPathText);
    if (skin == nullptr) {
        return Fail("Furina.gta has no skeleton data to frame the camera with.");
    }
    FurinaShowcaseModel bounds = MeasureFurinaModel(*skin);
    Vec3 modelOrigin = Vec3::Zero();
    if (const Transform* modelTransform = registry.TryGetComponent<Transform>(model)) {
        modelOrigin = modelTransform->position;
        bounds.minY = modelOrigin.y + modelTransform->scale.y * bounds.minY;
        bounds.maxY = modelOrigin.y + modelTransform->scale.y * bounds.maxY;
        bounds.hipY = modelOrigin.y + modelTransform->scale.y * bounds.hipY;
    }
    const FurinaShowcaseLayout layout = ComputeFurinaShowcaseLayout(bounds);
    if (!layout.valid) {
        return Fail("Furina.gta has empty bind-pose bounds.");
    }

    const Entity camera = registry.CreateEntity();
    created.Add(camera);
    Transform& cameraTransform = registry.AddComponent<Transform>(camera);
    cameraTransform.position = Vec3{ modelOrigin.x + layout.cameraPosition.x, layout.cameraPosition.y,
        modelOrigin.z + layout.cameraPosition.z };
    cameraTransform.rotation = Quat::FromEulerDegrees(layout.cameraPitchEulerDegrees, 0.0f, 0.0f);
    Camera& cameraComponent = registry.AddComponent<Camera>(camera);
    cameraComponent.fovYDegrees = layout.fovYDegrees;
    cameraComponent.nearZ = layout.nearZ;
    cameraComponent.farZ = layout.farZ;
    SetEntityName(registry, camera, kCameraName);

    const Entity sun = game.CreateDirectionalLightEntity();
    created.Add(sun);
    if (Transform* sunTransform = registry.TryGetComponent<Transform>(sun)) {
        sunTransform->rotation = Quat::FromEulerDegrees(layout.sunPitchEulerDegrees, layout.sunYawEulerDegrees, 0.0f);
    }
    if (DirectionalLight* sunLight = registry.TryGetComponent<DirectionalLight>(sun)) {
        sunLight->color = layout.sunColor;
    }
    SetEntityName(registry, sun, kSunName);

    // The scene exists; a replaced preset hands its settings back before the baseline is read.
    if (services.releasePrevious) {
        services.releasePrevious();
    }

    FurinaShowcaseState& state = *services.state;
    FurinaShowcaseState next;
    if (state.active) {
        next.enabledByPreset = state.enabledByPreset;
        next.disabledByPreset = state.disabledByPreset;
        next.savedSettings = state.savedSettings;
    }
    SettingsChange change;
    ScopeUndo changeGuard([&]() noexcept {
        UndoSettings(services, change.switchedOff, change.switchedOn, change.before);
    });
    std::string applyError;
    if (!ApplySettings(services, state.active, next, change, applyError)) {
        return Fail("Furina showcase settings failed: " + applyError + ".");
    }

    // Every string is built before the old scene goes; after that only noexcept moves are left.
    FurinaShowcaseReport report;
    report.success = true;
    report.model = model;
    report.camera = camera;
    report.sun = sun;
    report.modelHeight = bounds.maxY - bounds.minY;
    report.cameraDistance = layout.cameraDistance;
    report.featuresSwitchedOn = static_cast<std::uint32_t>(change.switchedOn.size());
    report.featuresSwitchedOff = static_cast<std::uint32_t>(change.switchedOff.size());
    report.message = "Opened the Furina showcase scene.";

    DestroyEntitiesAndDescendants(registry, oldRoots);

    // The clip is optional: without it the model stays in its bind pose.
    const std::filesystem::path clipPath = projectDirectory / kClipFile;
    if (std::filesystem::is_regular_file(clipPath, ec)) {
        report.animationStarted = game.PlayAnimationOnEntity(model, PathToUtf8(clipPath));
    }

    changeGuard.Release();
    next.active = true;
    next.created = created.Release();
    state = std::move(next);
    return report;
}

} // namespace

FurinaFeatureSwitch PlanFurinaFeatureSwitch(std::span<const RenderFeatureDebugEntry> features)
{
    const auto startsWithAny = [](std::string_view name, std::span<const std::string_view> prefixes) {
        return std::any_of(prefixes.begin(), prefixes.end(),
            [name](std::string_view prefix) { return name.starts_with(prefix); });
    };
    FurinaFeatureSwitch plan;
    for (const RenderFeatureDebugEntry& entry : features) {
        const bool listedOff = std::find(kFurinaShowcaseFeaturesOff.begin(), kFurinaShowcaseFeaturesOff.end(),
                                   std::string_view(entry.name))
            != kFurinaShowcaseFeaturesOff.end();
        if (listedOff) {
            if (entry.enabled) {
                plan.turnOff.push_back(entry.name);
            }
        } else if (!entry.enabled && startsWithAny(entry.name, kFurinaShowcaseFeaturePrefixesOn)) {
            plan.turnOn.push_back(entry.name);
        }
    }
    return plan;
}

FurinaShowcaseModel MeasureFurinaModel(const SkinnedMeshData& skin) noexcept
{
    FurinaShowcaseModel model;
    model.minY = std::numeric_limits<float>::infinity();
    model.maxY = -std::numeric_limits<float>::infinity();
    model.hipY = std::numeric_limits<float>::quiet_NaN();
    for (const Vec3& position : skin.bindPositions) {
        model.minY = std::min(model.minY, position.y);
        model.maxY = std::max(model.maxY, position.y);
    }
    for (const Bone& bone : skin.skeleton.bones) {
        if (bone.name == kHipBoneName || bone.englishName == kHipBoneEnglishName) {
            model.hipY = bone.position.y;
            break;
        }
    }
    return model;
}

FurinaShowcaseReport OpenFurinaShowcase(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const FurinaShowcaseServices& services) noexcept
try
{
    FurinaShowcaseReport report = OpenImpl(game, renderer, sceneServicesSetLayout, services);
    if (report.success) {
        PresetLogInfo(kLogCategory, report.message);
    }
    return report;
}
catch (const std::exception& e)
{
    // GPU upload errors surface as exceptions; the caller must get a failure and the old scene.
    return Fail(std::string("Furina showcase open failed: ") + e.what());
}
catch (...)
{
    return Fail("Furina showcase open failed.");
}

bool CloseFurinaShowcase(Game& game, const FurinaShowcaseServices& services, std::string& message) noexcept
try
{
    if (services.state == nullptr || !services.state->active) {
        message = "The Furina showcase is not active.";
        return false;
    }
    FurinaShowcaseState& state = *services.state;
    const bool restored = UndoSettings(services, state.disabledByPreset, state.enabledByPreset, state.savedSettings);
    DestroyEntitiesAndDescendants(game.GetRegistry(), state.created);
    state = FurinaShowcaseState{};
    message = restored ? "Closed the Furina showcase."
                       : "Closed the Furina showcase, but restoring the settings was incomplete (see the log).";
    PresetLogInfo(kLogCategory, message);
    return restored;
}
catch (const std::exception& e)
{
    PresetLogError(kLogCategory, "Closing the Furina showcase failed: ", e.what());
    message = "Closing the Furina showcase failed (see the log).";
    return false;
}
catch (...)
{
    PresetLogError(kLogCategory, "Closing the Furina showcase failed.");
    message = "Closing the Furina showcase failed (see the log).";
    return false;
}

bool ReleaseFurinaShowcaseSettings(const FurinaShowcaseServices& services) noexcept
{
    if (services.state == nullptr || !services.state->active) {
        return true;
    }
    FurinaShowcaseState& state = *services.state;
    const bool restored = UndoSettings(services, state.disabledByPreset, state.enabledByPreset, state.savedSettings);
    state = FurinaShowcaseState{};
    return restored;
}

std::string BuildFurinaShowcaseDetailsJson(const FurinaShowcaseReport& report)
{
    const auto flag = [](bool value) { return value ? "true" : "false"; };
    std::string json = "{\"entities\":{\"model\":\"" + EntityText(report.model) + "\",\"camera\":\""
        + EntityText(report.camera) + "\",\"sun\":\"" + EntityText(report.sun) + "\"}";
    json += ",\"modelHeight\":" + std::to_string(report.modelHeight);
    json += ",\"cameraDistance\":" + std::to_string(report.cameraDistance);
    json += std::string(",\"animationStarted\":") + flag(report.animationStarted);
    json += ",\"featuresSwitchedOn\":" + std::to_string(report.featuresSwitchedOn);
    json += ",\"featuresSwitchedOff\":" + std::to_string(report.featuresSwitchedOff);
    json += ",\"restoresOnClose\":true}";
    return json;
}

} // namespace gte
