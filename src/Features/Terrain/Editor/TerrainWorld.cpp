#include "TerrainWorld.h"

#include "../../../Editor/PresetSceneSupport.h"
#include "../../../Core/Logging.h"
#include "../../../ECS/Components/Camera.h"
#include "../../../ECS/Components/MeshRenderer.h"
#include "../../../ECS/Components/Name.h"
#include "../../../ECS/Components/Transform.h"
#include "../../../ECS/Registry.h"
#include "../../../ECS/TransformHierarchy.h"
#include "../../../Game/Game.h"
#include "../../../Profiling/ScopeTimer.h"
#include "../../../Renderer/Culling/ClusterCullMath.h"
#include "../../../Renderer/Renderer.h"
#include "../Gpu/TerrainClusterRecords.h"
#include "../TerrainErosion.h"
#include "../TerrainNode.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>
#include <variant>

namespace gte {

namespace {

constexpr const char* kLogCategory = "TerrainWorld";
constexpr int kPreviewQuads = 128;
constexpr float kCameraNear = 3.0f;           // Near plane high above the ground.
constexpr float kCameraNearMin = 0.15f;       // Near plane at ground level.
constexpr double kCameraNearPerClearance = 0.1; // Near plane grows with the height above ground.
constexpr float kCameraFarMargin = 1500.0f;
constexpr double kRampRebuildDelaySeconds = 0.3;
constexpr std::uint64_t kGpuCounterIntervalFrames = 12;
constexpr std::uint64_t kPreviewErosionLevel = 99;
constexpr double kMaxPoolBytes = 1.5 * 1024.0 * 1024.0 * 1024.0;

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point from) noexcept
{
    return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}

terrain::TerrainResult Failure(std::string message)
{
    terrain::TerrainResult result;
    result.ok = false;
    result.message = std::move(message);
    return result;
}

terrain::TerrainResult Success(std::string message, terrain::SettingsChange change = terrain::SettingsChange::None)
{
    terrain::TerrainResult result;
    result.message = std::move(message);
    result.change = change;
    return result;
}

std::int64_t AsInt(const fx::FeatureDebugValue& value) noexcept
{
    if (const auto* v = std::get_if<std::int64_t>(&value)) {
        return *v;
    }
    return 0;
}

std::string AsText(const fx::FeatureDebugValue& value)
{
    if (const auto* v = std::get_if<std::string>(&value)) {
        return *v;
    }
    return {};
}

std::uint64_t Positive(std::int64_t v) noexcept
{
    return v > 0 ? static_cast<std::uint64_t>(v) : 0u;
}

terrain::TerrainRuntimeOptions SanitizeRuntime(terrain::TerrainRuntimeOptions o) noexcept
{
    o.maxVisibleNodes = std::clamp(o.maxVisibleNodes, 0, 4000);
    o.maxUploadsPerTick = std::clamp(o.maxUploadsPerTick, 1, 128);
    o.streamingMaxInFlight = std::clamp(o.streamingMaxInFlight, 1, 16);
    o.unloadDelayFrames = std::clamp(o.unloadDelayFrames, 1, 3600);
    return o;
}

// Camera far plane for a view distance: a margin so the farthest node is not clipped.
float CameraFarFor(const terrain::TerrainSettings& s) noexcept
{
    return std::max(5000.0f, s.maxViewDistance + kCameraFarMargin);
}

} // namespace

TerrainWorld::TerrainWorld(ServicesFn services)
    : m_servicesFn(std::move(services))
{
}

TerrainWorld::~TerrainWorld()
{
    try {
        if (m_open) {
            CloseImpl("shutdown");
        }
    } catch (...) {
    }
}

bool TerrainWorld::Alive(Game& game) const
{
    return game.GetRegistry().IsAlive(m_entities.terrain);
}

// --- Settings ------------------------------------------------------------------------------------------------

terrain::TerrainResult TerrainWorld::SetSettings(const terrain::TerrainSettings& in)
{
    using terrain::SettingsChange;
    const terrain::TerrainSettings next = terrain::Sanitize(in);
    const SettingsChange change = terrain::ClassifyChange(m_settings, next);
    if (change == SettingsChange::None) {
        return Success("no change", change);
    }
    const terrain::TerrainSettings before = m_settings;
    m_settings = next;
    if (!m_open) {
        return Success("settings stored; they apply when the terrain opens", change);
    }

    try {
        if (terrain::HasChange(change, SettingsChange::Geometry)) {
            // Node layout and pool size changed: the scene is rebuilt, the camera stays where it is.
            terrain::TerrainResult reopened = ReopenKeepingCamera();
            reopened.change = change;
            if (!reopened.ok) {
                m_settings = before;
            }
            return reopened;
        }

        const TerrainWorldServices svc = m_servicesFn();
        m_streamer->SetSettings(next);
        if (terrain::HasChange(change, SettingsChange::Height)) {
            m_selector->SetSettings(next);
            m_pending.clear();
            m_previewDirty = true;
            if (m_previewWindow.autoCenter) {
                PickPreviewCenter();
            }
        } else {
            m_selector->SetOptimization(next);
        }
        if (terrain::HasChange(change, SettingsChange::Material)) {
            m_materialDirty = true;
            m_materialDirtySince = -1.0; // Stamped on the next Tick.
            m_previewDirty = true;
        }
        if (terrain::HasChange(change, SettingsChange::Optimization)) {
            if (std::string_view(TerrainCullModeName(before)) != std::string_view(TerrainCullModeName(next))) {
                SetTerrainCullMode(svc.gpuDrivenScene, next);
            }
            if (svc.game != nullptr) {
                ApplyCameraRange(*svc.game);
            }
        }
        return Success("applied", change);
    } catch (const std::exception& e) {
        m_lastError = e.what();
        return Failure(std::string("settings failed: ") + e.what());
    }
}

terrain::TerrainResult TerrainWorld::SetRuntimeOptions(const terrain::TerrainRuntimeOptions& options)
{
    m_runtime = SanitizeRuntime(options);
    if (m_open) {
        ApplySelectorBudget();
        ApplyStreamerConfig();
    }
    return Success(m_open ? "applied; the pool size changes on the next Open" : "stored");
}

void TerrainWorld::ApplySelectorBudget()
{
    if (!m_selector) {
        return;
    }
    terrain::LodBudget budget;
    budget.maxInView = m_runtime.maxVisibleNodes;
    if (m_pool && m_pool->Built()) {
        // Leave room in the pool for fallbacks and nodes that are about to be evicted.
        budget.maxTotal = static_cast<int>(m_pool->Layout().slotCount) * 8 / 10;
    }
    m_selector->SetBudget(budget);
}

void TerrainWorld::ApplyStreamerConfig()
{
    if (!m_streamer || !m_pool || !m_pool->Built()) {
        return;
    }
    terrain::StreamerConfig config;
    const std::size_t nodeBytes = static_cast<std::size_t>(m_pool->Layout().vertsPerSlot)
        * (sizeof(terrain::TerrainVertex) + sizeof(std::uint32_t));
    config.memoryBudgetBytes = nodeBytes * m_pool->Layout().slotCount * 95 / 100;
    config.maxInFlight = m_runtime.streamingMaxInFlight;
    config.unloadDelayFrames = m_runtime.unloadDelayFrames;
    m_streamer->SetConfig(config);
}

void TerrainWorld::ApplyCameraRange(Game& game)
{
    if (Camera* camera = game.GetRegistry().TryGetComponent<Camera>(m_entities.camera)) {
        camera->nearZ = kCameraNear;
        camera->farZ = CameraFarFor(m_settings);
    }
}

void TerrainWorld::AdaptCameraNear(Game& game)
{
    Registry& registry = game.GetRegistry();
    Camera* camera = registry.TryGetComponent<Camera>(m_entities.camera);
    const Transform* t = registry.TryGetComponent<Transform>(m_entities.camera);
    if (camera == nullptr || t == nullptr || !m_streamer) {
        return;
    }
    // Close to the ground the near plane shrinks so grass and rocks at arm's length are not clipped.
    const double ground = std::max(static_cast<double>(m_streamer->Field().Height(t->position.x, t->position.z, 16.0)), 0.0);
    const double clearance = std::max(static_cast<double>(t->position.y) - ground, 0.0);
    const float wanted = static_cast<float>(std::clamp(clearance * kCameraNearPerClearance, static_cast<double>(kCameraNearMin),
        static_cast<double>(kCameraNear)));
    if (std::abs(wanted - camera->nearZ) > 0.02f * camera->nearZ) {
        camera->nearZ = wanted;
    }
}

// --- Scene lifetime -----------------------------------------------------------------------------------------

terrain::TerrainResult TerrainWorld::Open(const terrain::TerrainOpenOptions& options)
{
    try {
        return OpenImpl(options);
    } catch (const std::exception& e) {
        m_lastError = e.what();
        return Failure(std::string("terrain open failed: ") + e.what());
    } catch (...) {
        return Failure("terrain open failed");
    }
}

terrain::TerrainResult TerrainWorld::Close()
{
    try {
        return CloseImpl("closed");
    } catch (const std::exception& e) {
        return Failure(std::string("terrain close failed: ") + e.what());
    }
}

std::pair<double, double> TerrainWorld::FindLandSpot() const
{
    // Nearest hilly land to the origin (height in a mountain band with some relief); else the nearest land.
    const terrain::TerrainHeightField& field = m_streamer->Field();
    const double lo = 0.08 * m_settings.heightScale;
    const double hi = 0.5 * m_settings.heightScale;
    double hillyDistance = 1.0e30;
    double landDistance = 1.0e30;
    std::pair<double, double> hilly{ 0.0, 0.0 };
    std::pair<double, double> land{ 0.0, 0.0 };
    for (int gz = -12; gz <= 12; ++gz) {
        for (int gx = -12; gx <= 12; ++gx) {
            const double x = gx * 1000.0;
            const double z = gz * 1000.0;
            const double d = x * x + z * z;
            if (d >= landDistance && d >= hillyDistance) {
                continue;
            }
            const double h = field.Height(x, z, 16.0);
            if (h < lo || h > hi) {
                continue;
            }
            if (d < landDistance) {
                landDistance = d;
                land = { x, z };
            }
            const double relief = std::abs(field.Height(x + 300.0, z, 16.0) - h) + std::abs(field.Height(x, z + 300.0, 16.0) - h);
            if (relief >= 40.0 && d < hillyDistance) {
                hillyDistance = d;
                hilly = { x, z };
            }
        }
    }
    return hillyDistance < 1.0e30 ? hilly : land;
}

terrain::TerrainResult TerrainWorld::SetCarve(std::shared_ptr<const terrain::ITerrainCarve> carve, std::span<const terrain::Rect2> dirty)
{
    m_carve = std::move(carve);
    if (!m_streamer) {
        return Success("carve stored for the next open");
    }
    const int invalidated = m_streamer->SetCarve(m_carve, dirty);
    if (m_livePreview) {
        m_previewDirty = true;
    }
    return Success("carve applied; " + std::to_string(invalidated) + " nodes rebuild");
}

std::optional<terrain::TerrainHeightProbe> TerrainWorld::ProbeHeight(double x, double z) const
{
    if (!m_streamer) {
        return std::nullopt;
    }
    const terrain::TerrainHeightField& field = m_streamer->Field();
    return terrain::TerrainHeightProbe{ field.BaseHeight(x, z, 2.0), field.Height(x, z, 2.0) };
}

terrain::SurfaceSnapshot TerrainWorld::SnapshotSurface(double minX, double minZ, double maxX, double maxZ) const
{
    if (!m_open || !m_streamer) {
        return {};
    }
    return terrain::CollectSurface(*m_streamer, terrain::MakeLayout(m_settings), minX, minZ, maxX, maxZ);
}

void TerrainWorld::SetOceanQuadVisible(bool visible)
{
    m_oceanQuadVisible = visible;
    const TerrainWorldServices svc = m_servicesFn();
    if (!m_open || svc.game == nullptr || m_entities.water == kInvalidEntity || !m_aux) {
        return;
    }
    if (visible) {
        SetMeshRenderer(*svc.game, m_entities.water, m_aux->WaterRenderer());
    } else {
        RemoveMeshRenderer(*svc.game, m_entities.water);
    }
}

void TerrainWorld::PickPreviewCenter()
{
    if (!m_streamer) {
        return;
    }
    const std::pair<double, double> spot = FindLandSpot();
    m_previewWindow.centerX = spot.first;
    m_previewWindow.centerZ = spot.second;
}

void TerrainWorld::SetMeshRenderer(Game& game, Entity entity, const MeshRenderer& renderer) const
{
    Registry& registry = game.GetRegistry();
    if (MeshRenderer* existing = registry.TryGetComponent<MeshRenderer>(entity)) {
        *existing = renderer;
    } else {
        registry.AddComponent<MeshRenderer>(entity, renderer);
    }
}

void TerrainWorld::RemoveMeshRenderer(Game& game, Entity entity) const
{
    Registry& registry = game.GetRegistry();
    if (registry.TryGetComponent<MeshRenderer>(entity) != nullptr) {
        registry.RemoveComponent<MeshRenderer>(entity);
    }
}

terrain::TerrainResult TerrainWorld::OpenImpl(const terrain::TerrainOpenOptions& options)
{
    if (m_open) {
        CloseImpl("reopened");
    }
    const TerrainWorldServices svc = m_servicesFn();
    if (svc.game == nullptr || svc.renderer == nullptr) {
        return Failure("the terrain world has no game or renderer yet");
    }
    if (svc.gpuDrivenScene == nullptr) {
        return Failure("the GpuDrivenScene feature is missing: the terrain needs its GPU culling");
    }
    if (!ClusterPathSupported(svc.renderer->IndirectDrawCaps())) {
        return Failure("this GPU lacks the indirect draw features the terrain culling needs");
    }
    Game& game = *svc.game;
    Renderer& renderer = *svc.renderer;
    Registry& registry = game.GetRegistry();

    const terrain::TerrainSettings settings = terrain::Sanitize(m_settings);
    m_settings = settings;
    m_runtime = SanitizeRuntime(m_runtime);
    const terrain::TerrainLayout world = terrain::MakeLayout(settings);

    const std::uint32_t slots = options.poolSlots != 0 ? options.poolSlots : terrain::TerrainPoolSlotsFor(m_runtime);
    std::string why;
    const std::optional<terrain::gpu::PoolLayout> layout = terrain::gpu::MakePoolLayout(settings.chunkQuads, slots, &why);
    if (!layout.has_value()) {
        return Failure("pool layout refused: " + why);
    }
    if (static_cast<double>(layout->VertexBytes() + layout->IndexBytes()) > kMaxPoolBytes) {
        return Failure("the pool would need more than 1.5 GiB; lower the visible node cap or use fewer slots");
    }

    // Everything below is undone if a step fails.
    bool committed = false;
    ScopeUndo undo([&]() noexcept {
        if (committed) {
            return;
        }
        m_streamer.reset();
        m_selector.reset();
        m_pool.reset();
        m_aux.reset();
    });

    TerrainPoolParams poolParams;
    poolParams.layout = *layout;
    poolParams.settings = settings;
    poolParams.worldBounds = terrain::gpu::PoolBounds(world.worldHalfExtent, terrain::MinHeight(settings), terrain::MaxHeight(settings));
    m_pool = std::make_unique<TerrainGpuPool>();
    std::string error;
    if (!m_pool->Build(game, renderer, svc.sceneServicesSetLayout, poolParams, &error)) {
        return Failure(error);
    }
    m_aux = std::make_unique<TerrainAuxMeshes>();
    if (!m_aux->Build(game, renderer, svc.sceneServicesSetLayout, kPreviewQuads, world.worldHalfExtent * 1.5, &error)) {
        return Failure(error);
    }

    m_selector = std::make_unique<terrain::LodSelector>(settings);
    m_streamer = std::make_unique<terrain::TerrainStreamer>(settings);
    if (m_baked) {
        m_streamer->SetNodeProvider(terrain::MakeBakedNodeProvider(m_baked));
    }
    if (m_carve) {
        m_streamer->SetCarve(m_carve, {});
    }
    ApplySelectorBudget();
    ApplyStreamerConfig();

    // Entities are listed old-first so the new scene is never part of what gets destroyed.
    const std::vector<Entity> oldRoots = GetChildren(registry, kInvalidEntity);
    CreatedEntityGuard created(registry, kLogCategory);

    const std::pair<double, double> spot = FindLandSpot();
    const double groundHere = m_streamer->Field().Height(spot.first, spot.second, 16.0);
    SceneEntities entities;

    entities.camera = registry.CreateEntity();
    created.Add(entities.camera);
    {
        Transform& t = registry.AddComponent<Transform>(entities.camera);
        t.position = Vec3(static_cast<float>(spot.first), static_cast<float>(std::max(groundHere, 0.0) + 400.0),
            static_cast<float>(spot.second - 1500.0));
        terrain::FlightPose pose;
        pose.pitchDownRadians = 0.21f;
        t.rotation = terrain::FlightRotation(pose);
        Camera& camera = registry.AddComponent<Camera>(entities.camera);
        camera.nearZ = kCameraNear;
        camera.farZ = CameraFarFor(settings);
        SetEntityName(registry, entities.camera, "Terrain Camera");
    }

    entities.sun = game.CreateDirectionalLightEntity();
    created.Add(entities.sun);
    if (Transform* t = registry.TryGetComponent<Transform>(entities.sun)) {
        t->rotation = Quat::FromEulerDegrees(35.0f, 40.0f, 0.0f);
    }
    SetEntityName(registry, entities.sun, "Terrain Sun");

    entities.terrain = registry.CreateEntity();
    created.Add(entities.terrain);
    registry.AddComponent<Transform>(entities.terrain);
    registry.AddComponent<MeshRenderer>(entities.terrain, m_pool->MakeMeshRenderer());
    SetEntityName(registry, entities.terrain, "Terrain");

    if (options.water) {
        entities.water = registry.CreateEntity();
        created.Add(entities.water);
        registry.AddComponent<Transform>(entities.water);
        if (m_oceanQuadVisible) {
            registry.AddComponent<MeshRenderer>(entities.water, m_aux->WaterRenderer());
        }
        SetEntityName(registry, entities.water, "Terrain Water");
    }

    entities.preview = registry.CreateEntity();
    created.Add(entities.preview);
    registry.AddComponent<Transform>(entities.preview);
    SetEntityName(registry, entities.preview, "Terrain Preview");

    TerrainFeatureState features;
    if (!ApplyTerrainFeatures(svc.compositor, svc.gpuDrivenScene, options.profile, settings, options.renderBudget, svc.findEndpoint, features, error)) {
        return Failure("render features: " + error);
    }

    // Past this point nothing can fail: the old scene goes and the new state is committed.
    DestroyEntitiesAndDescendants(registry, oldRoots);
    entities.all = created.Release();
    m_entities = std::move(entities);
    m_features = std::move(features);
    m_openOptions = options;
    m_open = true;
    m_livePreview = false;
    m_previewDirty = false;
    m_materialDirty = false;
    m_savedCamera.reset();
    m_pending.clear();
    m_pendingReplace.clear();
    m_slotOfKey.clear();
    m_keyOfSlot.assign(layout->slotCount, std::nullopt);
    m_wantEnabled.assign(layout->slotCount, 0);
    m_inViewKeys.clear();
    m_gpu = GpuCounters{};
    m_lastGpuCounterFrame = 0;
    m_stats = terrain::TerrainStats{};
    m_lastError.clear();
    if (m_previewWindow.autoCenter) {
        m_previewWindow.centerX = spot.first;
        m_previewWindow.centerZ = spot.second;
    }
    committed = true;
    GTE_LOG_INFO(kLogCategory,
        "terrain opened: " + std::to_string(layout->slotCount) + " slots, "
            + std::to_string((layout->VertexBytes() + layout->IndexBytes()) / (1024 * 1024)) + " MiB, profile "
            + terrain::TerrainProfileName(options.profile));
    return Success("terrain scene opened with " + std::to_string(layout->slotCount) + " node slots");
}

terrain::TerrainResult TerrainWorld::CloseImpl(const char* reason)
{
    if (!m_open) {
        return Failure("the terrain is not open");
    }
    m_open = false;
    m_flight.Stop();
    m_rig = terrain::CameraRig{};
    m_pendingInput = terrain::CameraInput{};
    m_scriptedFrames = 0;
    const TerrainWorldServices svc = m_servicesFn();

    // Jobs first: they must not outlive the pool the results would go to.
    m_streamer.reset();
    m_selector.reset();
    m_pending.clear();
    m_pendingReplace.clear();
    m_slotOfKey.clear();
    m_keyOfSlot.clear();

    if (svc.game != nullptr) {
        Registry& registry = svc.game->GetRegistry();
        std::vector<Entity> alive;
        for (const Entity entity : m_entities.all) {
            if (registry.IsAlive(entity)) {
                alive.push_back(entity);
            }
        }
        DestroyEntitiesAndDescendants(registry, alive);
    }
    m_entities = SceneEntities{};

    const bool poolReleased = m_pool ? m_pool->Reset() : true;
    const bool auxReleased = m_aux ? m_aux->Reset() : true;
    m_pool.reset();
    m_aux.reset();
    const bool restored = RestoreTerrainFeatures(svc.compositor, svc.gpuDrivenScene, svc.findEndpoint, m_features);
    m_livePreview = false;
    m_savedCamera.reset();
    m_stats = terrain::TerrainStats{};
    GTE_LOG_INFO(kLogCategory, std::string("terrain closed: ") + reason);
    if (!poolReleased || !auxReleased) {
        return Failure("the terrain closed, but the GPU did not go idle, so its buffers were kept");
    }
    if (!restored) {
        return Failure("the terrain closed, but restoring the render features was incomplete (see the log)");
    }
    return Success("terrain closed");
}

terrain::TerrainResult TerrainWorld::ReopenKeepingCamera()
{
    const TerrainWorldServices svc = m_servicesFn();
    std::optional<std::pair<Vec3, Quat>> pose;
    if (svc.game != nullptr) {
        if (const Transform* t = svc.game->GetRegistry().TryGetComponent<Transform>(m_entities.camera)) {
            pose = std::make_pair(t->position, t->rotation);
        }
    }
    const bool wasPreview = m_livePreview;
    const terrain::CameraMode cameraMode = m_rig.Mode();
    const terrain::TerrainOpenOptions options = m_openOptions;
    CloseImpl("geometry changed");
    terrain::TerrainResult result = OpenImpl(options);
    if (result.ok && pose.has_value() && svc.game != nullptr) {
        if (Transform* t = svc.game->GetRegistry().TryGetComponent<Transform>(m_entities.camera)) {
            t->position = pose->first;
            t->rotation = pose->second;
        }
        if (cameraMode != terrain::CameraMode::Script) {
            SetCameraMode(cameraMode);
        }
        if (wasPreview) {
            SetLivePreview(true);
        }
    }
    return result;
}

// --- Live preview -------------------------------------------------------------------------------------------

terrain::TerrainResult TerrainWorld::SetLivePreview(bool enabled)
{
    if (!m_open) {
        return Failure("open the terrain first");
    }
    if (enabled == m_livePreview) {
        return Success(enabled ? "live preview already on" : "live preview already off");
    }
    const TerrainWorldServices svc = m_servicesFn();
    if (svc.game == nullptr) {
        return Failure("no game");
    }
    Game& game = *svc.game;
    if (enabled) {
        m_flight.Stop();
        RemoveMeshRenderer(game, m_entities.terrain);
        SetMeshRenderer(game, m_entities.preview, m_aux->PreviewRenderer());
        m_livePreview = true;
        m_previewDirty = true;
        if (m_previewWindow.autoCenter) {
            PickPreviewCenter();
        }
        FrameCameraOnPreview(game);
        return Success("live preview on");
    }
    RemoveMeshRenderer(game, m_entities.preview);
    SetMeshRenderer(game, m_entities.terrain, m_pool->MakeMeshRenderer());
    m_livePreview = false;
    if (m_savedCamera.has_value()) {
        if (Transform* t = game.GetRegistry().TryGetComponent<Transform>(m_entities.camera)) {
            t->position = m_savedCamera->first;
            t->rotation = m_savedCamera->second;
        }
        m_savedCamera.reset();
    }
    return Success("live preview off");
}

terrain::TerrainResult TerrainWorld::SetPreviewWindow(const terrain::TerrainPreviewWindow& window)
{
    terrain::TerrainPreviewWindow next = window;
    next.spacing = std::clamp(std::isfinite(next.spacing) ? next.spacing : 4.0, 2.0, 16.0);
    if (!std::isfinite(next.centerX) || !std::isfinite(next.centerZ)) {
        next.centerX = 0.0;
        next.centerZ = 0.0;
    }
    m_previewWindow = next;
    if (m_open && next.autoCenter && m_streamer) {
        PickPreviewCenter();
    }
    m_previewDirty = true;
    if (m_open && m_livePreview) {
        const TerrainWorldServices svc = m_servicesFn();
        if (svc.game != nullptr) {
            FrameCameraOnPreview(*svc.game);
        }
    }
    return Success("preview window set");
}

void TerrainWorld::FrameCameraOnPreview(Game& game)
{
    Transform* t = game.GetRegistry().TryGetComponent<Transform>(m_entities.camera);
    if (t == nullptr) {
        return;
    }
    if (!m_savedCamera.has_value()) {
        m_savedCamera = std::make_pair(t->position, t->rotation);
    }
    const double extent = kPreviewQuads * m_previewWindow.spacing;
    const double ground = m_streamer ? m_streamer->Field().Height(m_previewWindow.centerX, m_previewWindow.centerZ, 16.0) : 0.0;
    const double dy = 0.55 * extent + std::max(ground, 0.0) * 0.5;
    const double dz = 0.95 * extent;
    t->position = Vec3(static_cast<float>(m_previewWindow.centerX), static_cast<float>(std::max(ground, 0.0) + dy),
        static_cast<float>(m_previewWindow.centerZ - dz));
    terrain::FlightPose pose;
    pose.pitchDownRadians = static_cast<float>(std::atan2(dy, dz) * 0.8);
    t->rotation = terrain::FlightRotation(pose);
}

// --- Flight --------------------------------------------------------------------------------------------------

terrain::TerrainResult TerrainWorld::StartFlight(const terrain::FlightSpec& spec)
{
    if (!m_open) {
        return Failure("open the terrain first");
    }
    if (m_livePreview) {
        return Failure("turn the live preview off to fly");
    }
    m_rig.Begin(terrain::CameraMode::Script, m_rig.Pose(), {});
    m_flight.Start(spec);
    return Success("flight started");
}

void TerrainWorld::TickFlight(const TerrainTickContext& context, Game& game)
{
    if (!m_flight.Active()) {
        return;
    }
    const terrain::TerrainHeightField& field = m_streamer->Field();
    const terrain::FlightPose pose = m_flight.Advance(context.deltaSeconds, [&field](double x, double z) {
        return static_cast<double>(field.Height(x, z, 16.0));
    });
    if (Transform* t = game.GetRegistry().TryGetComponent<Transform>(m_entities.camera)) {
        t->position = Vec3(static_cast<float>(pose.x), static_cast<float>(pose.y), static_cast<float>(pose.z));
        t->rotation = terrain::FlightRotation(pose);
    }
}

// --- Interactive camera --------------------------------------------------------------------------------------

terrain::GroundHeightFn TerrainWorld::GroundFn() const
{
    if (!m_streamer) {
        return {};
    }
    const terrain::TerrainHeightField* field = &m_streamer->Field();
    return [field](double x, double z) { return static_cast<double>(field->Height(x, z, 16.0)); };
}

terrain::RigPose TerrainWorld::CameraPoseNow(Game& game) const
{
    const Transform* t = game.GetRegistry().TryGetComponent<Transform>(m_entities.camera);
    if (t == nullptr) {
        return m_rig.Pose();
    }
    const Vec3 forward = t->rotation.RotateVector(Vec3::Forward());
    return terrain::PoseFromForward(t->position.x, t->position.y, t->position.z, forward.x, forward.y, forward.z);
}

void TerrainWorld::WriteCameraPose(Game& game, const terrain::RigPose& pose) const
{
    Transform* t = game.GetRegistry().TryGetComponent<Transform>(m_entities.camera);
    if (t == nullptr) {
        return;
    }
    t->position = Vec3(static_cast<float>(pose.x), static_cast<float>(pose.y), static_cast<float>(pose.z));
    terrain::FlightPose rotation;
    rotation.yawRadians = static_cast<float>(pose.yaw);
    rotation.pitchDownRadians = static_cast<float>(pose.pitchDown);
    t->rotation = terrain::FlightRotation(rotation);
}

terrain::TerrainResult TerrainWorld::SetCameraMode(terrain::CameraMode mode)
{
    if (!m_open) {
        return Failure("open the terrain first");
    }
    const TerrainWorldServices svc = m_servicesFn();
    if (svc.game == nullptr) {
        return Failure("no game");
    }
    if (mode != terrain::CameraMode::Script) {
        m_flight.Stop(); // The scripted flight and a hand-driven camera cannot share the transform.
    }
    m_rig.Begin(mode, CameraPoseNow(*svc.game), GroundFn());
    m_pendingInput = terrain::CameraInput{};
    return Success(std::string("camera mode ") + terrain::CameraModeName(mode));
}

terrain::TerrainResult TerrainWorld::QueueCameraInput(const terrain::CameraInput& perFrame, int frames)
{
    if (!m_open) {
        return Failure("open the terrain first");
    }
    if (m_rig.Mode() == terrain::CameraMode::Script) {
        return Failure("set the camera mode to free or orbit first");
    }
    m_scriptedInput = perFrame;
    m_scriptedFrames = std::clamp(frames, 1, 3600);
    return Success("input queued for " + std::to_string(m_scriptedFrames) + " frames");
}

terrain::TerrainResult TerrainWorld::SetCameraPose(const terrain::TerrainCameraPose& request)
{
    if (!m_open) {
        return Failure("open the terrain first");
    }
    const TerrainWorldServices svc = m_servicesFn();
    if (svc.game == nullptr) {
        return Failure("no game");
    }
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const terrain::GroundHeightFn ground = GroundFn();
    terrain::RigPose pose = CameraPoseNow(*svc.game);
    pose.x = request.x.value_or(pose.x);
    pose.z = request.z.value_or(pose.z);
    if (request.y.has_value()) {
        pose.y = request.heightAboveGround ? std::max(ground ? ground(pose.x, pose.z) : 0.0, 0.0) + *request.y : *request.y;
    }
    if (request.yawDegrees.has_value()) {
        pose.yaw = *request.yawDegrees * kDegToRad;
    }
    if (request.pitchDownDegrees.has_value()) {
        pose.pitchDown = std::clamp(*request.pitchDownDegrees, -89.0, 89.0) * kDegToRad;
    }
    m_flight.Stop();
    m_rig.Begin(m_rig.Mode(), pose, ground);
    if (m_rig.Mode() == terrain::CameraMode::Orbit && (request.targetX || request.targetZ || request.orbitDistance)) {
        m_rig.SetOrbitTarget(request.targetX.value_or(m_rig.TargetX()), request.targetZ.value_or(m_rig.TargetZ()),
            request.orbitDistance.value_or(m_rig.OrbitDistance()), ground);
    }
    WriteCameraPose(*svc.game, m_rig.Pose());
    return Success("camera placed");
}

void TerrainWorld::TickCamera(const TerrainTickContext& context, Game& game)
{
    terrain::CameraInput input = m_pendingInput;
    m_pendingInput = terrain::CameraInput{};
    if (m_scriptedFrames > 0) {
        --m_scriptedFrames;
        terrain::AccumulateInput(input, m_scriptedInput);
    }
    if (m_rig.Mode() == terrain::CameraMode::Script || m_flight.Active()) {
        return;
    }
    const terrain::GroundHeightFn ground = GroundFn();
    // Something else moved the camera (an Inspector edit, the preview framing): continue from there.
    const terrain::RigPose now = CameraPoseNow(game);
    const terrain::RigPose& mine = m_rig.Pose();
    if (std::abs(now.x - mine.x) + std::abs(now.y - mine.y) + std::abs(now.z - mine.z) > 0.5) {
        m_rig.Begin(m_rig.Mode(), now, ground);
    }
    WriteCameraPose(game, m_rig.Advance(input, context.deltaSeconds, ground));
}

// --- Per-frame work -----------------------------------------------------------------------------------------

void TerrainWorld::Tick(const TerrainTickContext& context)
{
    try {
        PollAssetTask();
    } catch (const std::exception& e) {
        m_assetResult = e.what();
        m_assetTask.Reset();
    }
    if (!m_open) {
        return;
    }
    GTE_PROFILE_SCOPE("TerrainWorld::Tick");
    const Clock::time_point start = Clock::now();
    try {
        const TerrainWorldServices svc = m_servicesFn();
        if (svc.game == nullptr || svc.renderer == nullptr) {
            return;
        }
        if (!Alive(*svc.game)) {
            CloseImpl("the terrain scene was replaced");
            return;
        }
        m_stats.frame = context.frameIndex;
        TickFlight(context, *svc.game);
        TickCamera(context, *svc.game);
        AdaptCameraNear(*svc.game);
        {
            const terrain::RigPose pose = CameraPoseNow(*svc.game);
            m_stats.cameraYawDegrees = pose.yaw * 180.0 / 3.14159265358979323846;
            m_stats.cameraPitchDownDegrees = pose.pitchDown * 180.0 / 3.14159265358979323846;
            m_stats.cameraX = pose.x;
            m_stats.cameraY = pose.y;
            m_stats.cameraZ = pose.z;
            m_stats.cameraAspect = static_cast<double>(context.aspect);
            if (const Camera* camera = svc.game->GetRegistry().TryGetComponent<Camera>(m_entities.camera)) {
                m_stats.cameraFovDegrees = static_cast<double>(camera->fovYDegrees);
            }
        }
        TickMaterial(context, *svc.game);
        if (context.gpuFrame.has_value()) {
            if (m_livePreview) {
                TickPreview(context, *svc.game, *context.gpuFrame);
            } else {
                TickStreaming(context, *svc.game, *context.gpuFrame);
            }
        }
        if (context.frameIndex >= m_lastGpuCounterFrame + kGpuCounterIntervalFrames) {
            m_lastGpuCounterFrame = context.frameIndex;
            if (m_runtime.collectGpuStats) {
                RefreshGpuCounters();
            }
        }
    } catch (const std::exception& e) {
        if (m_lastError != e.what()) {
            GTE_LOG_ERROR(kLogCategory, std::string("tick failed: ") + e.what());
        }
        m_lastError = e.what();
    }
    m_stats.tickCpuMs = MsSince(start);
    m_stats.tickCpuMsMax = std::max(m_stats.tickCpuMsMax * 0.995, m_stats.tickCpuMs);
}

void TerrainWorld::TickMaterial(const TerrainTickContext& context, Game& game)
{
    if (!m_materialDirty) {
        return;
    }
    if (m_materialDirtySince < 0.0) {
        m_materialDirtySince = context.nowSeconds;
    }
    // A slider drag changes the colours every frame; the ramp is rebuilt (GPU drain) once it settles.
    if (context.nowSeconds - m_materialDirtySince < kRampRebuildDelaySeconds) {
        return;
    }
    m_materialDirty = false;
    std::string error;
    if (!m_pool->ReplaceRamp(m_settings, &error)) {
        m_lastError = "colour ramp: " + error;
        return;
    }
    if (!m_livePreview) {
        SetMeshRenderer(game, m_entities.terrain, m_pool->MakeMeshRenderer());
    }
}

void TerrainWorld::TickPreview(const TerrainTickContext& context, Game& game, const OffscreenFrameTicket& ticket)
{
    static_cast<void>(context);
    static_cast<void>(game);
    m_aux->NotePreviewRead(ticket);
    if (!m_previewDirty) {
        return;
    }
    const Clock::time_point start = Clock::now();
    const terrain::TerrainHeightField& field = m_streamer->Field();
    const double spacing = m_previewWindow.spacing;
    const double half = 0.5 * kPreviewQuads * spacing;

    terrain::GridBuildSpec spec;
    spec.quads = kPreviewQuads;
    spec.spacing = spacing;
    spec.originX = std::floor((m_previewWindow.centerX - half) / spacing) * spacing;
    spec.originZ = std::floor((m_previewWindow.centerZ - half) / spacing) * spacing;
    spec.erode = true;
    spec.erosionSeed = terrain::ErosionSeed(m_settings.seed, static_cast<std::int32_t>(spec.originX / spacing),
        static_cast<std::int32_t>(spec.originZ / spacing), static_cast<std::int32_t>(kPreviewErosionLevel));
    const terrain::NodeMesh mesh = terrain::BuildGridMesh(field, spec);
    if (m_aux->UploadPreview(mesh, ticket)) {
        m_previewDirty = false;
        m_stats.previewBuildMs = MsSince(start);
        ++m_stats.previewRebuilds;
    }
}

void TerrainWorld::TickStreaming(const TerrainTickContext& context, Game& game, const OffscreenFrameTicket& ticket)
{
    using terrain::NodeKey;
    const terrain::gpu::SlotFrame frame{ ticket.serial, ticket.completedSerial };
    Registry& registry = game.GetRegistry();

    const std::optional<Mat4> viewProjection =
        RenderSystem::ResolveActiveCameraEyeRelativeViewProjection(registry, context.aspect);
    if (!viewProjection.has_value()) {
        return;
    }
    const Vec3 eye = RenderSystem::ResolveActiveCameraWorldPosition(registry);
    terrain::LodView view;
    view.cameraX = eye.x;
    view.cameraY = eye.y;
    view.cameraZ = eye.z;
    view.frustum = terrain::ExtractFrustumPlanes(viewProjection->Data());

    const Clock::time_point selectStart = Clock::now();
    terrain::LodSelectionStats selection;
    m_selector->Select(view, m_wanted, &selection);
    m_stats.selectMs = MsSince(selectStart);

    m_update.ready.clear();
    m_update.rebuilt.clear();
    m_update.evicted.clear();
    m_streamer->Update(m_wanted, context.frameIndex, context.nowSeconds, m_update);

    // Evictions first: their slots go back to the pool (hidden before they can be reused).
    // The measured height range is kept, else the node flips wanted/unwanted and rebuilds in a loop.
    for (const NodeKey& key : m_update.evicted) {
        if (const auto it = m_slotOfKey.find(key); it != m_slotOfKey.end()) {
            m_pool->Release(it->second, frame);
            m_keyOfSlot[it->second].reset();
            m_slotOfKey.erase(it);
        }
        std::erase_if(m_pending, [&key](const std::shared_ptr<const terrain::NodeMesh>& n) { return n->key == key; });
        std::erase_if(m_pendingReplace, [&key](const std::shared_ptr<const terrain::NodeMesh>& n) { return n->key == key; });
    }
    // New geometry for a node the pool already holds goes to a fresh slot; the old one is released once that is written.
    for (const std::shared_ptr<const terrain::NodeMesh>& node : m_update.rebuilt) {
        m_selector->SetNodeHeightRange(node->key, node->boundsMin.y, node->boundsMax.y);
        auto& queue = m_slotOfKey.contains(node->key) ? m_pendingReplace : m_pending;
        const auto same = std::find_if(queue.begin(), queue.end(),
            [&node](const std::shared_ptr<const terrain::NodeMesh>& p) { return p->key == node->key; });
        if (same != queue.end()) {
            *same = node;
        } else {
            queue.push_back(node);
        }
    }
    for (const std::shared_ptr<const terrain::NodeMesh>& node : m_update.ready) {
        m_selector->SetNodeHeightRange(node->key, node->boundsMin.y, node->boundsMax.y);
        // A recoloured copy of a node that already has a slot changes nothing the pool holds (colour is the ramp).
        if (m_slotOfKey.contains(node->key)) {
            continue;
        }
        const auto same = std::find_if(m_pending.begin(), m_pending.end(),
            [&node](const std::shared_ptr<const terrain::NodeMesh>& p) { return p->key == node->key; });
        if (same != m_pending.end()) {
            *same = node;
        } else {
            m_pending.push_back(node);
        }
    }

    // Slot writes, bounded per frame; the nearest and visible nodes go first.
    int written = 0;
    if (!m_pending.empty()) {
        if (m_pending.size() > 1) {
            std::unordered_map<NodeKey, double, terrain::NodeKeyHash> priority;
            priority.reserve(m_wanted.size());
            for (const terrain::WantedNode& w : m_wanted) {
                priority.emplace(w.key, w.distance + (w.inView ? 0.0 : 1.0e6));
            }
            const auto rank = [&priority](const std::shared_ptr<const terrain::NodeMesh>& n) {
                const auto it = priority.find(n->key);
                return it != priority.end() ? it->second : 1.0e30;
            };
            std::sort(m_pending.begin(), m_pending.end(),
                [&rank](const auto& a, const auto& b) { return rank(a) < rank(b); });
        }
        std::size_t used = 0;
        while (used < m_pending.size() && written < m_runtime.maxUploadsPerTick) {
            const terrain::NodeMesh& node = *m_pending[used];
            const std::optional<std::uint32_t> slot = m_pool->WriteNode(node, frame);
            if (!slot.has_value()) {
                break; // Pool full, or every free slot is still in flight: retry next frame.
            }
            m_slotOfKey[node.key] = *slot;
            m_keyOfSlot[*slot] = node.key;
            ++used;
            ++written;
        }
        m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(used));
    }
    std::size_t replaced = 0;
    while (replaced < m_pendingReplace.size() && written < m_runtime.maxUploadsPerTick) {
        const terrain::NodeMesh& node = *m_pendingReplace[replaced];
        const std::optional<std::uint32_t> slot = m_pool->WriteNode(node, frame);
        if (!slot.has_value()) {
            break;
        }
        if (const auto old = m_slotOfKey.find(node.key); old != m_slotOfKey.end()) {
            m_pool->Release(old->second, frame);
            m_keyOfSlot[old->second].reset();
            old->second = *slot;
        } else {
            m_slotOfKey[node.key] = *slot;
        }
        m_keyOfSlot[*slot] = node.key;
        ++replaced;
        ++written;
    }
    m_pendingReplace.erase(m_pendingReplace.begin(), m_pendingReplace.begin() + static_cast<std::ptrdiff_t>(replaced));

    // Which slots the cull may draw: the draw list, holes filled by ready parents or children.
    m_streamer->BuildDrawList(m_wanted, m_drawList);
    std::fill(m_wantEnabled.begin(), m_wantEnabled.end(), static_cast<std::uint8_t>(0));
    m_seabedCulled = 0;
    for (const terrain::DrawNode& node : m_drawList) {
        if (const auto it = m_slotOfKey.find(node.key); it != m_slotOfKey.end()) {
            if (static_cast<double>(m_pool->SlotBox(it->second).max[1]) < m_seabedCullBelow) {
                ++m_seabedCulled; // Hidden by deep opaque water: not worth drawing.
                continue;
            }
            m_wantEnabled[it->second] = 1;
        }
    }
    for (std::uint32_t slot = 0; slot < m_wantEnabled.size(); ++slot) {
        const bool want = m_wantEnabled[slot] != 0;
        const bool is = m_pool->IsEnabled(slot);
        if (want && !is) {
            m_pool->Enable(slot);
        } else if (!want && is) {
            m_pool->Disable(slot);
        }
    }
    m_pool->Flush(frame);

    if (m_debugAabb) {
        m_inViewKeys.clear();
        for (const terrain::WantedNode& w : m_wanted) {
            if (w.inView) {
                m_inViewKeys.insert(w.key);
            }
        }
    }

    const terrain::StreamerStats streamer = m_streamer->Stats();
    const TerrainPoolStats pool = m_pool->Stats();
    m_stats.cameraX = eye.x;
    m_stats.cameraY = eye.y;
    m_stats.cameraZ = eye.z;
    m_stats.nodesWanted = static_cast<int>(m_wanted.size());
    m_stats.nodesInView = selection.inView;
    m_stats.nodesResident = streamer.resident;
    m_stats.nodesPending = streamer.pending;
    m_stats.uploadQueue = static_cast<int>(m_pending.size() + m_pendingReplace.size());
    m_stats.nodesRebuilding = streamer.rebuilding;
    m_stats.carveDirtyNodes = streamer.carveLastDirtyNodes;
    m_stats.carveRebuildMs = streamer.carveRebuildMs;
    m_stats.carveHash = m_streamer->CarveHash();
    m_stats.oceanQuadVisible = m_oceanQuadVisible;
    m_stats.seabedCulledNodes = m_seabedCulled;
    m_stats.nodesWrittenThisTick = written;
    m_stats.builtTotal = streamer.builtTotal;
    m_stats.evictedTotal = streamer.evictedTotal;
    m_stats.generationMsAverage = streamer.buildMsAverage;
    m_stats.generationMsMax = streamer.buildMsMax;
    m_stats.generatedPerSecond = streamer.generatedPerSecond;
    m_stats.lodDistanceScale = selection.distanceScale;
    m_stats.slotsTotal = static_cast<int>(pool.slots);
    m_stats.slotsUsed = static_cast<int>(pool.used);
    m_stats.slotsEnabled = static_cast<int>(pool.enabled);
    m_stats.trianglesEnabled = static_cast<std::uint64_t>(pool.enabled) * (m_pool->Layout().indicesPerSlot / 3);
    m_stats.residentMbCpu = static_cast<double>(streamer.bytesResident) / (1024.0 * 1024.0);
    m_stats.residentMbGpu = static_cast<double>(pool.vertexBytes + pool.indexBytes + pool.tableBytes) / (1024.0 * 1024.0);
    m_stats.uploadMbThisTick = static_cast<double>(written) * m_pool->Layout().vertsPerSlot * terrain::gpu::kPoolVertexBytes
        / (1024.0 * 1024.0);
    if (selection.overBudget && m_lastError.empty()) {
        m_lastError = "the visible node cap is lower than the detail floor allows";
    }
}

void TerrainWorld::RefreshGpuCounters()
{
    const TerrainWorldServices svc = m_servicesFn();
    if (svc.gpuDrivenScene == nullptr) {
        m_gpu = GpuCounters{};
        return;
    }
    const fx::FeatureDebugResult state = svc.gpuDrivenScene->GetState();
    GpuCounters next;
    next.valid = state.ok;
    std::int64_t claimed = 0;
    std::int64_t parts = 0;
    for (const fx::FeatureDebugField& field : state.fields) {
        const std::string& key = field.key;
        if (key == "mode") {
            next.mode = AsText(field.value);
        } else if (key == "game.occlusion") {
            next.occlusion = AsText(field.value);
        } else if (key == "game.claimedEntities") {
            claimed = AsInt(field.value);
        } else if (key == "game.parts") {
            parts = AsInt(field.value);
        } else if (key == "game.testedClusters") {
            next.tested = Positive(AsInt(field.value));
        } else if (key == "game.visibleClusters") {
            next.visible = Positive(AsInt(field.value));
        } else if (key == "game.reasons.frustum") {
            next.frustum = Positive(AsInt(field.value));
        } else if (key == "game.reasons.pyramid") {
            next.pyramid = Positive(AsInt(field.value));
        }
    }
    next.pathActive = claimed >= 1 && parts >= 1;
    m_gpu = std::move(next);
}

// --- Queries -------------------------------------------------------------------------------------------------

terrain::TerrainStats TerrainWorld::Stats() const
{
    terrain::TerrainStats stats = m_stats;
    stats.open = m_open;
    stats.livePreview = m_livePreview;
    stats.flightActive = m_flight.Active();
    stats.debugAabb = m_debugAabb;
    stats.lastError = m_lastError;
    {
        const terrain::AssetTaskStatus asset = m_assetTask.Status();
        stats.assetBusy = asset.state == terrain::AssetTaskState::Running;
        stats.assetKind = asset.kind == terrain::AssetTaskKind::Save ? "save" : asset.kind == terrain::AssetTaskKind::Load ? "load" : "";
        stats.assetProgress = static_cast<double>(asset.progress);
        stats.assetElapsedMs = asset.elapsedMs;
        stats.assetResult = m_assetResult;
    }
    stats.gpuCullValid = m_gpu.valid;
    stats.gpuPathActive = m_gpu.pathActive;
    stats.gpuCullMode = m_gpu.mode;
    stats.gpuOcclusion = m_gpu.occlusion;
    stats.gpuTestedClusters = m_gpu.tested;
    stats.gpuDrawnNodes = m_gpu.visible;
    // Unused slots are rejected by the frustum test too, so they are taken out of the count.
    const std::uint64_t idle = stats.slotsTotal > stats.slotsEnabled ? static_cast<std::uint64_t>(stats.slotsTotal - stats.slotsEnabled) : 0;
    stats.gpuFrustumCulledNodes = m_gpu.frustum > idle ? m_gpu.frustum - idle : 0;
    stats.gpuHiZCulledNodes = m_gpu.pyramid;
    stats.cameraMode = terrain::CameraModeName(m_rig.Mode());
    stats.cameraSpeed = m_rig.Mode() == terrain::CameraMode::Script ? (m_flight.Active() ? m_flight.Spec().speed : 0.0) : m_rig.Speed();
    if (m_pool && m_pool->Built()) {
        stats.gpuTrianglesDrawn = m_gpu.visible * (m_pool->Layout().indicesPerSlot / 3);
    }
    return stats;
}

void TerrainWorld::CollectNodes(std::vector<terrain::TerrainNodeInfo>& out) const
{
    out.clear();
    if (!m_open || !m_debugAabb || !m_pool) {
        return;
    }
    out.reserve(m_slotOfKey.size());
    for (const auto& [key, slot] : m_slotOfKey) {
        const terrain::gpu::BoxF& box = m_pool->SlotBox(slot);
        terrain::TerrainNodeInfo info;
        info.key = key;
        info.minX = box.min[0];
        info.minY = box.min[1];
        info.minZ = box.min[2];
        info.maxX = box.max[0];
        info.maxY = box.max[1];
        info.maxZ = box.max[2];
        info.enabled = m_pool->IsEnabled(slot);
        info.inView = m_inViewKeys.contains(key);
        out.push_back(info);
    }
}

terrain::TerrainResult TerrainWorld::SaveAsset(const std::filesystem::path& path)
{
    if (!m_open || !m_streamer) {
        return Failure("open the terrain before saving it");
    }
    // Bakes a square of finest-level nodes around the camera; the world outside stays procedural.
    const terrain::TerrainLayout layout = terrain::MakeLayout(m_settings);
    const std::int32_t side = std::clamp(m_runtime.bakeNodesPerSide, 1, 64);
    const std::int32_t half = static_cast<std::int32_t>(layout.worldHalfExtent / layout.nodeSizeLevel0);
    const std::int32_t cx = static_cast<std::int32_t>(std::floor(m_stats.cameraX / layout.nodeSizeLevel0));
    const std::int32_t cz = static_cast<std::int32_t>(std::floor(m_stats.cameraZ / layout.nodeSizeLevel0));
    terrain::SaveRegion region;
    region.firstX = std::clamp(cx - side / 2, -half, half - side);
    region.firstZ = std::clamp(cz - side / 2, -half, half - side);
    region.countX = static_cast<std::uint32_t>(side);
    region.countZ = static_cast<std::uint32_t>(side);
    if (!m_assetTask.StartSave(m_settings, region, path)) {
        return Failure("another save or load is still running");
    }
    return Success("saving " + std::to_string(side * side) + " nodes to " + path.string() + " in the background");
}

terrain::TerrainResult TerrainWorld::LoadAsset(const std::filesystem::path& path)
{
    if (!m_assetTask.StartLoad(path)) {
        return Failure("another save or load is still running");
    }
    return Success("loading " + path.string() + " in the background");
}

void TerrainWorld::PollAssetTask()
{
    if (m_assetTask.Busy()) {
        return;
    }
    const terrain::AssetTaskStatus status = m_assetTask.Status();
    if (status.state == terrain::AssetTaskState::Idle) {
        return;
    }
    char timing[48];
    std::snprintf(timing, sizeof(timing), " (%.0f ms)", status.elapsedMs);
    if (status.state == terrain::AssetTaskState::Done && status.kind == terrain::AssetTaskKind::Load) {
        std::optional<terrain::BakedTerrain> loaded = m_assetTask.TakeLoaded();
        if (loaded.has_value()) {
            const terrain::TerrainSettings settings = loaded->settings;
            const std::uint32_t countX = loaded->countX;
            m_baked = std::make_shared<const terrain::BakedTerrain>(std::move(*loaded));
            const terrain::TerrainResult applied = SetSettings(settings);
            if (m_streamer) {
                m_streamer->SetNodeProvider(terrain::MakeBakedNodeProvider(m_baked));
            }
            m_assetResult = applied.ok ? "loaded " + status.path + " (" + std::to_string(countX) + " nodes wide)" + timing
                                       : applied.message;
            return;
        }
    }
    m_assetResult = status.message + (status.state == terrain::AssetTaskState::Done ? timing : "");
    m_assetTask.Reset();
}

} // namespace gte
