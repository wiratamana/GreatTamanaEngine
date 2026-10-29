#include "HizDemoSceneOpener.h"

#include "HizDemoLayout.h"
#include "HizDemoProfile.h"
#include "InstanceGridTestSpawner.h"
#include "../Core/Logging.h"
#include "../Core/Plugins/RenderFeatureCompositor.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/TransformHierarchy.h"
#include "../Game/Game.h"

#include <algorithm>
#include <array>
#include <exception>
#include <span>
#include <string>
#include <string_view>
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

bool TrySwitch(RenderFeatureCompositor& compositor, const std::string& name, bool enabled) noexcept
{
    try {
        return compositor.SetFeatureEnabled(name, enabled);
    } catch (...) {
        return false;
    }
}

// Switches `turnOn` features on, `turnOff` features off and sets `settings` back. A failure is logged and
// the rest still runs. False when anything failed.
bool Undo(const HizDemoServices& services, std::span<const std::string> turnOn, std::span<const std::string> turnOff,
    std::span<const fx::FeatureDebugArg> settings) noexcept
{
    bool complete = true;
    if (services.compositor == nullptr) {
        if (!turnOn.empty() || !turnOff.empty()) {
            complete = false;
            LogError("Cannot switch features back: the render feature compositor is missing.");
        }
    } else {
        for (const std::string& name : turnOn) {
            if (!TrySwitch(*services.compositor, name, true)) {
                complete = false;
                LogError("Could not turn the feature back on: ", name);
            }
        }
        for (const std::string& name : turnOff) {
            if (!TrySwitch(*services.compositor, name, false)) {
                complete = false;
                LogError("Could not turn the feature back off: ", name);
            }
        }
    }
    if (settings.empty()) {
        return complete;
    }
    if (services.gpuDrivenScene == nullptr) {
        LogError("Cannot restore the GpuDrivenScene settings: the feature is missing.");
        return false;
    }
    try {
        const fx::FeatureDebugResult restored = services.gpuDrivenScene->Set(settings);
        if (!restored.ok) {
            complete = false;
            LogError("GpuDrivenScene refused the saved settings: ", restored.message);
        }
    } catch (...) {
        complete = false;
        LogError("Restoring the GpuDrivenScene settings threw.");
    }
    return complete;
}

// Gives back the baseline the demo saved and forgets it. Safe to call twice. False when incomplete.
bool RestoreSettings(const HizDemoServices& services, HizDemoState& state) noexcept
{
    const bool complete = Undo(services, state.disabledFeatures, std::span<const std::string>{}, state.savedSettings);
    state.disabledFeatures.clear();
    state.savedSettings.clear();
    return complete;
}

// What one open call changed, so a failure can undo exactly that and nothing else.
struct SettingsChange {
    std::vector<fx::FeatureDebugArg> before; // GpuDrivenScene settings before the call.
    bool settingsSet = false;
    std::vector<std::string> turnedOff;
    std::vector<std::string> turnedOn;
};

// Undoes `change` on destruction unless released.
class ChangeGuard {
public:
    ChangeGuard(const HizDemoServices& services, const SettingsChange& change) noexcept : m_services(services), m_change(change) { }
    ~ChangeGuard()
    {
        if (m_armed) {
            Undo(m_services, m_change.turnedOff, m_change.turnedOn,
                m_change.settingsSet ? std::span<const fx::FeatureDebugArg>(m_change.before)
                                     : std::span<const fx::FeatureDebugArg>{});
        }
    }
    ChangeGuard(const ChangeGuard&) = delete;
    ChangeGuard& operator=(const ChangeGuard&) = delete;

    void Release() noexcept { m_armed = false; }

private:
    const HizDemoServices& m_services;
    const SettingsChange& m_change;
    bool m_armed = true;
};

// Sets the GpuDrivenScene settings and switches features for `profile`; `next` holds the baseline of an active
// demo, else the current settings become it. All changes go to `change`. False with `error` on failure.
bool ApplySettings(const HizDemoServices& services, PresetProfile profile, bool haveBaseline, HizDemoState& next,
    SettingsChange& change, std::string& error)
{
    change.before = SnapshotSettings(*services.gpuDrivenScene);
    if (!haveBaseline) {
        next.savedSettings = change.before;
    }
    const std::array<fx::FeatureDebugArg, 5> demoSettings{ {
        { "enabled", "true" },
        { "mode", std::string(kDemoMode) },
        { "cone", "false" },
        { "collectStats", "true" },
        { "debugView", "Off" },
    } };
    const fx::FeatureDebugResult applied = services.gpuDrivenScene->Set(demoSettings);
    if (!applied.ok) {
        error = "GpuDrivenScene settings were refused: " + applied.message; // Set applies all or none.
        return false;
    }
    change.settingsSet = true;

    const bool geometryOnly = profile == PresetProfile::GeometryOnly;
    if (!geometryOnly && next.disabledFeatures.empty()) {
        return true;
    }
    if (services.compositor == nullptr) {
        error = "the render feature compositor is missing";
        return false;
    }
    const HizFeatureSwitch plan =
        PlanHizFeatureSwitch(profile, next.disabledFeatures, services.compositor->DebugSnapshot());

    // Capacity first: the pushes below must not throw after a feature was switched.
    change.turnedOn.reserve(plan.turnOn.size());
    change.turnedOff.reserve(plan.turnOff.size());
    next.disabledFeatures.reserve(next.disabledFeatures.size() + plan.turnOff.size());

    for (const std::string& name : plan.turnOn) {
        if (!TrySwitch(*services.compositor, name, true)) {
            error = "could not turn the feature \"" + name + "\" back on";
            return false;
        }
        change.turnedOn.push_back(name);
    }
    if (!geometryOnly) {
        next.disabledFeatures.clear();
    }
    for (const std::string& name : plan.turnOff) {
        if (!TrySwitch(*services.compositor, name, false)) {
            error = "could not turn off the feature \"" + name + "\"";
            return false;
        }
        change.turnedOff.push_back(name);
        if (std::find(next.disabledFeatures.begin(), next.disabledFeatures.end(), name) == next.disabledFeatures.end()) {
            next.disabledFeatures.push_back(name);
        }
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
                LogError("Removing a half-built demo failed: ", e.what());
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
    LogError(message);
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

    // An active demo hands its baseline over; nothing of it is touched until the old scene is gone.
    HizDemoState& state = *services.state;
    HizDemoState next;
    if (state.active) {
        next.savedSettings = state.savedSettings;
        next.disabledFeatures = state.disabledFeatures;
    }
    SettingsChange change;
    ChangeGuard changeGuard(services, change);
    std::string applyError;
    if (!ApplySettings(services, parameters.profile, state.active, next, change, applyError)) {
        return Fail("Hi-Z demo settings failed: " + applyError + ".");
    }

    // Every string is built before the old scene goes; after that only noexcept moves are left.
    HizDemoReport report;
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
    next.active = true;
    next.geometryOnly = parameters.profile == PresetProfile::GeometryOnly;

    DestroyEntitiesAndDescendants(registry, oldRoots);

    changeGuard.Release();
    next.created = created.Release();
    state = std::move(next);
    return report;
}

} // namespace

HizFeatureSwitch PlanHizFeatureSwitch(PresetProfile profile, std::span<const std::string> carriedOff,
    std::span<const RenderFeatureDebugEntry> features)
{
    HizFeatureSwitch plan;
    if (profile != PresetProfile::GeometryOnly) {
        plan.turnOn.assign(carriedOff.begin(), carriedOff.end());
        return plan;
    }
    for (const RenderFeatureDebugEntry& entry : features) {
        if (entry.enabled && IsGeometryOnlyFeature(entry.name)) {
            plan.turnOff.push_back(entry.name);
        }
    }
    return plan;
}

HizDemoReport OpenHizDemo(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const PresetParameters& parameters, const HizDemoServices& services) noexcept
try
{
    HizDemoReport report = OpenImpl(game, renderer, sceneServicesSetLayout, parameters, services);
    if (report.success) {
        LogInfo(report.message);
    }
    return report;
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
    const bool restored = RestoreSettings(services, state);
    DestroyEntitiesAndDescendants(game.GetRegistry(), state.created);
    state = HizDemoState{};
    message = restored ? "Closed the Hi-Z demo."
                       : "Closed the Hi-Z demo, but restoring the settings was incomplete (see the log).";
    LogInfo(message);
    return restored;
}
catch (const std::exception& e)
{
    LogError("Closing the Hi-Z demo failed: ", e.what());
    message = "Closing the Hi-Z demo failed (see the log).";
    return false;
}
catch (...)
{
    LogError("Closing the Hi-Z demo failed.");
    message = "Closing the Hi-Z demo failed (see the log).";
    return false;
}

bool ReleaseHizDemoSettings(const HizDemoServices& services) noexcept
{
    if (services.state == nullptr || !services.state->active) {
        return true;
    }
    const bool restored = RestoreSettings(services, *services.state);
    *services.state = HizDemoState{};
    return restored;
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
