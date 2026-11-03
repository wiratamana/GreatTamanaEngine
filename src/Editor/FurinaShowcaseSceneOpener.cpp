#include "FurinaShowcaseSceneOpener.h"

#include "PresetFeatureSettings.h"
#include "ProjectRootPath.h"
#include "../Core/Logging.h"
#include "../Core/Plugins/RenderFeatureCompositor.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/DirectionalLight.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/ToonShading.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/TransformHierarchy.h"
#include "../Encoding/Utf8Path.h"
#include "../Game/Animation/SkeletalRigCache.h"
#include "../Game/Game.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <limits>
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

// ToneMapping keys the showcase rewrites, and the values it sets.
constexpr std::array<std::string_view, 3> kToneMappingKeys{ "mode", "exposure", "saturation" };

std::string EntityText(Entity entity)
{
    return std::to_string(entity.index) + "/" + std::to_string(entity.generation);
}

// Logs an error without ever throwing (callers run in noexcept paths).
void LogError(std::string_view what, std::string_view detail = {}) noexcept
{
    try {
        GTE_LOG_ERROR(kLogCategory, std::string(what).append(detail));
    } catch (...) {
    }
}

void LogInfo(const std::string& message) noexcept
{
    try {
        GTE_LOG_INFO(kLogCategory, message);
    } catch (...) {
    }
}

// Switches `enable` registrations on, `disable` ones off and sets the ToneMapping settings back. A failure is
// logged and the rest still runs. False when anything failed.
bool UndoSettings(const FurinaShowcaseServices& services, std::span<const std::string> enable,
    std::span<const std::string> disable, std::span<const fx::FeatureDebugArg> toneMapping) noexcept
{
    bool complete = true;
    if (services.compositor == nullptr) {
        if (!enable.empty() || !disable.empty()) {
            complete = false;
            LogError("Cannot switch features back: the render feature compositor is missing.");
        }
    } else {
        for (const std::string& name : enable) {
            if (!TrySetFeatureEnabled(*services.compositor, name, true)) {
                complete = false;
                LogError("Could not turn the feature back on: ", name);
            }
        }
        for (const std::string& name : disable) {
            if (!TrySetFeatureEnabled(*services.compositor, name, false)) {
                complete = false;
                LogError("Could not turn the feature back off: ", name);
            }
        }
    }
    if (toneMapping.empty()) {
        return complete;
    }
    if (services.toneMapping == nullptr) {
        LogError("Cannot restore the ToneMapping settings: the feature is missing.");
        return false;
    }
    try {
        const fx::FeatureDebugResult restored = services.toneMapping->Set(toneMapping);
        if (!restored.ok) {
            complete = false;
            LogError("ToneMapping refused the saved settings: ", restored.message);
        }
    } catch (...) {
        complete = false;
        LogError("Restoring the ToneMapping settings threw.");
    }
    return complete;
}

// What one open call changed, so a failure can undo exactly that and nothing else.
struct SettingsChange {
    std::vector<fx::FeatureDebugArg> toneBefore;
    bool toneSet = false;
    std::vector<std::string> switchedOn;
    std::vector<std::string> switchedOff;
};

// Undoes `change` on destruction unless released.
class ChangeGuard {
public:
    ChangeGuard(const FurinaShowcaseServices& services, const SettingsChange& change) noexcept
        : m_services(services), m_change(change)
    {
    }
    ~ChangeGuard()
    {
        if (m_armed) {
            UndoSettings(m_services, m_change.switchedOff, m_change.switchedOn,
                m_change.toneSet ? std::span<const fx::FeatureDebugArg>(m_change.toneBefore)
                                 : std::span<const fx::FeatureDebugArg>{});
        }
    }
    ChangeGuard(const ChangeGuard&) = delete;
    ChangeGuard& operator=(const ChangeGuard&) = delete;

    void Release() noexcept { m_armed = false; }

private:
    const FurinaShowcaseServices& m_services;
    const SettingsChange& m_change;
    bool m_armed = true;
};

// Sets the ToneMapping look and switches features for the showcase. `next` holds the baseline of an active
// showcase, else the current settings become it. All changes go to `change`. False with `error` on failure.
bool ApplySettings(const FurinaShowcaseServices& services, bool haveBaseline, FurinaShowcaseState& next,
    SettingsChange& change, std::string& error)
{
    if (services.toneMapping != nullptr) {
        change.toneBefore = SnapshotFeatureSettings(*services.toneMapping, kToneMappingKeys);
        if (!haveBaseline) {
            next.savedToneMapping = change.toneBefore;
        }
        const std::array<fx::FeatureDebugArg, 3> look{ {
            { "mode", "PbrNeutral" },
            { "exposure", "0.9" },
            { "saturation", "1.2" },
        } };
        const fx::FeatureDebugResult applied = services.toneMapping->Set(look);
        if (!applied.ok) {
            error = "ToneMapping settings were refused: " + applied.message;
            return false;
        }
        change.toneSet = true;
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

// Destroys everything made so far unless released.
class CreatedGuard {
public:
    explicit CreatedGuard(Registry& registry) noexcept : m_registry(registry) { }
    ~CreatedGuard()
    {
        if (m_armed && !m_created.empty()) {
            try {
                DestroyEntitiesAndDescendants(m_registry, m_created);
            } catch (const std::exception& e) {
                LogError("Removing a half-built showcase failed: ", e.what());
            }
        }
    }
    CreatedGuard(const CreatedGuard&) = delete;
    CreatedGuard& operator=(const CreatedGuard&) = delete;

    void Add(Entity entity) { m_created.push_back(entity); }
    std::vector<Entity> Release() noexcept
    {
        m_armed = false;
        return std::move(m_created);
    }

private:
    Registry& m_registry;
    std::vector<Entity> m_created;
    bool m_armed = true;
};

void SetName(Registry& registry, Entity entity, std::string_view name)
{
    if (Name* existing = registry.TryGetComponent<Name>(entity)) {
        existing->value = std::string(name);
    } else {
        registry.AddComponent<Name>(entity, Name{ std::string(name) });
    }
}

FurinaShowcaseReport Fail(std::string message)
{
    LogError(message);
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

    CreatedGuard created(registry);

    const Entity model = game.CreateMeshEntityFromGtaFile(renderer, modelPathText, sceneServicesSetLayout);
    if (model == kInvalidEntity) {
        return Fail("Furina showcase: could not load \"" + modelPathText + "\".");
    }
    created.Add(model);
    SetName(registry, model, kRootName);
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
    SetName(registry, camera, kCameraName);

    const Entity sun = game.CreateDirectionalLightEntity();
    created.Add(sun);
    if (Transform* sunTransform = registry.TryGetComponent<Transform>(sun)) {
        sunTransform->rotation = Quat::FromEulerDegrees(layout.sunPitchEulerDegrees, layout.sunYawEulerDegrees, 0.0f);
    }
    if (DirectionalLight* sunLight = registry.TryGetComponent<DirectionalLight>(sun)) {
        sunLight->color = layout.sunColor;
    }
    SetName(registry, sun, kSunName);

    // The scene exists; a replaced preset hands its settings back before the baseline is read.
    if (services.releasePrevious) {
        services.releasePrevious();
    }

    FurinaShowcaseState& state = *services.state;
    FurinaShowcaseState next;
    if (state.active) {
        next.enabledByPreset = state.enabledByPreset;
        next.disabledByPreset = state.disabledByPreset;
        next.savedToneMapping = state.savedToneMapping;
    }
    SettingsChange change;
    ChangeGuard changeGuard(services, change);
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
        LogInfo(report.message);
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
    const bool restored = UndoSettings(services, state.disabledByPreset, state.enabledByPreset, state.savedToneMapping);
    DestroyEntitiesAndDescendants(game.GetRegistry(), state.created);
    state = FurinaShowcaseState{};
    message = restored ? "Closed the Furina showcase."
                       : "Closed the Furina showcase, but restoring the settings was incomplete (see the log).";
    LogInfo(message);
    return restored;
}
catch (const std::exception& e)
{
    LogError("Closing the Furina showcase failed: ", e.what());
    message = "Closing the Furina showcase failed (see the log).";
    return false;
}
catch (...)
{
    LogError("Closing the Furina showcase failed.");
    message = "Closing the Furina showcase failed (see the log).";
    return false;
}

bool ReleaseFurinaShowcaseSettings(const FurinaShowcaseServices& services) noexcept
{
    if (services.state == nullptr || !services.state->active) {
        return true;
    }
    FurinaShowcaseState& state = *services.state;
    const bool restored = UndoSettings(services, state.disabledByPreset, state.enabledByPreset, state.savedToneMapping);
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
