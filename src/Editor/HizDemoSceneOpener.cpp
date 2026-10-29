#include "HizDemoSceneOpener.h"

#include "HizDemoLayout.h"
#include "InstanceGridTestSpawner.h"
#include "../Core/Logging.h"
#include "../Core/Plugins/RenderFeatureCompositor.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/TransformHierarchy.h"
#include "../Game/Game.h"

#include <algorithm>
#include <exception>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace gte {

namespace {

constexpr const char* kLogCategory = "HizDemo";

constexpr std::string_view kCameraName = "Main Camera";
constexpr std::string_view kSunName = "HizDemo Sun";
constexpr std::string_view kWallName = "HizDemo Wall";

constexpr float kSunPitchDegrees = 50.0f;
constexpr float kSunYawDegrees = 30.0f;

// GpuDrivenScene keys the demo reads back and rewrites.
constexpr std::array<std::string_view, 5> kSettingKeys{ "enabled", "mode", "cone", "collectStats", "debugView" };
constexpr std::string_view kDemoMode = "Frustum";

std::string EntityText(Entity entity)
{
    return std::to_string(entity.index) + "/" + std::to_string(entity.generation);
}

std::string ValueText(const fx::FeatureDebugValue& value)
{
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else {
                return std::to_string(v);
            }
        },
        value);
}

std::vector<fx::FeatureDebugArg> SnapshotSettings(const fx::IFeatureDebugEndpoint& endpoint)
{
    const fx::FeatureDebugResult state = endpoint.GetState();
    std::vector<fx::FeatureDebugArg> saved;
    for (const fx::FeatureDebugField& field : state.fields) {
        if (std::find(kSettingKeys.begin(), kSettingKeys.end(), field.key) != kSettingKeys.end()) {
            saved.push_back({ field.key, ValueText(field.value) });
        }
    }
    return saved;
}

// Puts back what the demo changed and forgets it. Safe to call twice.
void RestoreSettings(const HizDemoServices& services, HizDemoState& state) noexcept
{
    try {
        if (services.compositor != nullptr) {
            for (const std::string& name : state.disabledFeatures) {
                services.compositor->SetFeatureEnabled(name, true);
            }
        }
        if (services.gpuDrivenScene != nullptr && !state.savedSettings.empty()) {
            services.gpuDrivenScene->Set(state.savedSettings);
        }
    } catch (const std::exception& e) {
        GTE_LOG_ERROR(kLogCategory, std::string("Restoring the demo settings failed: ") + e.what());
    }
    state.disabledFeatures.clear();
    state.savedSettings.clear();
}

// Applies the demo settings to `next`; restores them on destruction unless released.
class SettingsGuard {
public:
    SettingsGuard(const HizDemoServices& services, HizDemoState& next) noexcept : m_services(services), m_next(next) { }
    ~SettingsGuard()
    {
        if (m_armed) {
            RestoreSettings(m_services, m_next);
        }
    }
    SettingsGuard(const SettingsGuard&) = delete;
    SettingsGuard& operator=(const SettingsGuard&) = delete;

    void Release() noexcept { m_armed = false; }

private:
    const HizDemoServices& m_services;
    HizDemoState& m_next;
    bool m_armed = true;
};

// Sets the GpuDrivenScene settings, then switches the profile's features off. False with `error` on failure.
bool ApplySettings(const HizDemoServices& services, PresetProfile profile, HizDemoState& next, std::string& error)
{
    next.savedSettings = SnapshotSettings(*services.gpuDrivenScene);
    const std::array<fx::FeatureDebugArg, 5> demoSettings{ {
        { "enabled", "true" },
        { "mode", std::string(kDemoMode) },
        { "cone", "false" },
        { "collectStats", "true" },
        { "debugView", "Off" },
    } };
    const fx::FeatureDebugResult applied = services.gpuDrivenScene->Set(demoSettings);
    if (!applied.ok) {
        next.savedSettings.clear(); // Set applies all or none, so there is nothing to put back.
        error = "GpuDrivenScene settings were refused: " + applied.message;
        return false;
    }

    if (profile != PresetProfile::GeometryOnly) {
        return true;
    }
    for (const RenderFeatureDebugEntry& entry : services.compositor->DebugSnapshot()) {
        if (!entry.enabled || !IsGeometryOnlyFeature(entry.name)) {
            continue;
        }
        if (!services.compositor->SetFeatureEnabled(entry.name, false)) {
            error = "could not turn off the feature \"" + entry.name + "\"";
            return false;
        }
        next.disabledFeatures.push_back(entry.name);
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
                GTE_LOG_ERROR(kLogCategory, std::string("Removing a half-built demo failed: ") + e.what());
            }
        }
    }
    CreatedGuard(const CreatedGuard&) = delete;
    CreatedGuard& operator=(const CreatedGuard&) = delete;

    void Add(Entity entity) { m_created.push_back(entity); }
    void Add(const std::vector<Entity>& entities) { m_created.insert(m_created.end(), entities.begin(), entities.end()); }
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

HizDemoReport Fail(std::string message)
{
    GTE_LOG_ERROR(kLogCategory, message);
    HizDemoReport report;
    report.message = std::move(message) + " The previous scene was kept.";
    return report;
}

HizDemoReport OpenImpl(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const PresetParameters& parameters, const HizDemoServices& services)
{
    if (services.grid == nullptr || services.state == nullptr || services.gpuDrivenScene == nullptr) {
        return Fail("The Hi-Z demo needs the GpuDrivenScene feature and the editor grid resources.");
    }
    if (parameters.profile == PresetProfile::GeometryOnly && services.compositor == nullptr) {
        return Fail("The geometry_only profile needs the render feature compositor.");
    }
    const HizDemoParameters sanitized = SanitizeHizDemoParameters(parameters.count, parameters.spacing);
    if (!sanitized.valid) {
        return Fail(sanitized.error + ".");
    }

    Registry& registry = game.GetRegistry();
    const Camera cameraDefaults;
    const Vec3 fieldCentre{ 0.0f, 0.0f, kInstanceGridDefaultDepth };
    const HizDemoLayout layout = ComputeHizDemoLayout(
        sanitized.count, sanitized.spacing, fieldCentre, cameraDefaults.fovYDegrees, cameraDefaults.farZ);

    // Old roots are listed before anything new exists, so the new scene is never part of them.
    const std::vector<Entity> oldRoots = GetChildren(registry, kInvalidEntity);

    CreatedGuard created(registry);
    HizDemoReport report;

    const Entity camera = registry.CreateEntity();
    created.Add(camera);
    registry.AddComponent<Transform>(camera).position = layout.cameraPosition;
    registry.AddComponent<Camera>(camera).farZ = layout.cameraFarZ;
    SetName(registry, camera, kCameraName);

    const Entity sun = game.CreateDirectionalLightEntity();
    created.Add(sun);
    if (Transform* sunTransform = registry.TryGetComponent<Transform>(sun)) {
        sunTransform->rotation = Quat::FromEulerDegrees(kSunPitchDegrees, kSunYawDegrees, 0.0f);
    }
    SetName(registry, sun, kSunName);

    const Entity wall = game.CreatePrimitiveEntity(renderer, PrimitiveType::Cube, sceneServicesSetLayout);
    created.Add(wall);
    if (Transform* wallTransform = registry.TryGetComponent<Transform>(wall)) {
        wallTransform->position = layout.wallPosition;
        wallTransform->scale = layout.wallScale;
    }
    SetName(registry, wall, kWallName);

    InstanceGridSpawnResult field = InstanceGridTestSpawner::Spawn(game, renderer, *services.grid, sanitized.count,
        sanitized.spacing, sceneServicesSetLayout, fieldCentre);
    if (!field.success) {
        return Fail("Hi-Z demo field failed: " + field.errorMessage);
    }
    created.Add(field.entities);

    // A demo that is still active gives its settings back first, so the new snapshot sees the originals.
    HizDemoState& state = *services.state;
    if (state.active) {
        RestoreSettings(services, state);
        state = HizDemoState{};
    }

    HizDemoState next;
    SettingsGuard settings(services, next);
    std::string applyError;
    if (!ApplySettings(services, parameters.profile, next, applyError)) {
        return Fail("Hi-Z demo settings failed: " + applyError + ".");
    }

    // Everything worked: the old scene goes in one pass.
    DestroyEntitiesAndDescendants(registry, oldRoots);

    settings.Release();
    next.active = true;
    next.geometryOnly = parameters.profile == PresetProfile::GeometryOnly;
    next.created = created.Release();
    state = std::move(next);

    report.success = true;
    report.profile = std::string(PresetProfileName(parameters.profile));
    report.instanceCount = sanitized.count;
    report.countClamped = sanitized.countClamped;
    report.clustersPerInstance = services.grid->ClusterCount();
    report.workItems = static_cast<std::uint64_t>(sanitized.count) * report.clustersPerInstance;
    report.camera = camera;
    report.sun = sun;
    report.wall = wall;
    report.mode = std::string(kDemoMode);
    report.collectStats = true;
    report.message = "Opened the Hi-Z demo scene with " + std::to_string(sanitized.count) + " instances.";
    if (sanitized.countClamped) {
        report.message += " " + sanitized.message + ".";
    }
    if (parameters.profile == PresetProfile::GeometryOnly) {
        report.message += " Fixed-cost features are off until the demo is closed.";
    }
    GTE_LOG_INFO(kLogCategory, report.message);
    return report;
}

} // namespace

bool IsGeometryOnlyFeature(std::string_view featureName) noexcept
{
    return std::any_of(kGeometryOnlyFeaturePrefixes.begin(), kGeometryOnlyFeaturePrefixes.end(),
        [featureName](std::string_view prefix) { return featureName.starts_with(prefix); });
}

HizDemoReport OpenHizDemo(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const PresetParameters& parameters, const HizDemoServices& services) noexcept
try
{
    return OpenImpl(game, renderer, sceneServicesSetLayout, parameters, services);
}
catch (const std::exception& e)
{
    // GPU upload errors surface as exceptions; the caller must get a failure and the old scene.
    return Fail(std::string("Hi-Z demo open failed: ") + e.what());
}
catch (...)
{
    return Fail("Hi-Z demo open failed.");
}

bool CloseHizDemo(Game& game, const HizDemoServices& services, std::string& message) noexcept
try
{
    if (services.state == nullptr || !services.state->active) {
        message = "The Hi-Z demo is not active.";
        return false;
    }
    HizDemoState& state = *services.state;
    RestoreSettings(services, state);
    DestroyEntitiesAndDescendants(game.GetRegistry(), state.created);
    state = HizDemoState{};
    message = "Closed the Hi-Z demo.";
    GTE_LOG_INFO(kLogCategory, message);
    return true;
}
catch (...)
{
    message = "Closing the Hi-Z demo failed.";
    return false;
}

void ReleaseHizDemoSettings(const HizDemoServices& services) noexcept
{
    if (services.state == nullptr || !services.state->active) {
        return;
    }
    RestoreSettings(services, *services.state);
    *services.state = HizDemoState{};
}

std::string BuildHizDemoDetailsJson(const HizDemoReport& report)
{
    const auto flag = [](bool value) { return value ? "true" : "false"; };
    std::string json = "{\"profile\":\"" + report.profile + "\"";
    json += ",\"instanceCount\":" + std::to_string(report.instanceCount);
    json += std::string(",\"countClamped\":") + flag(report.countClamped);
    json += ",\"clustersPerInstance\":" + std::to_string(report.clustersPerInstance);
    json += ",\"workItems\":" + std::to_string(report.workItems);
    json += ",\"entities\":{\"camera\":\"" + EntityText(report.camera) + "\",\"sun\":\"" + EntityText(report.sun)
        + "\",\"wall\":\"" + EntityText(report.wall) + "\"}";
    json += ",\"gpuDrivenScene\":{\"mode\":\"" + report.mode + "\",\"collectStats\":" + flag(report.collectStats) + "}";
    json += ",\"restoresOnClose\":true}";
    return json;
}

} // namespace gte
