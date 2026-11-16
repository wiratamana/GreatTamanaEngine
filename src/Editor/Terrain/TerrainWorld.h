#pragma once

// The live terrain world of the editor: opens the scene, streams nodes into the GPU pool, keeps the culling
// feature and camera in step with the settings, and answers the control interface.
// Tick() is called once per frame on the main thread after the game update:
//     terrainWorld.Tick({ aspect, engineContext.gpuFrame, nowSeconds, deltaSeconds, frameIndex });

#include "TerrainAuxMeshes.h"
#include "TerrainFeatureProfile.h"
#include "TerrainGpuPool.h"
#include "../../Core/GraphicsHandles.h"
#include "../../ECS/Entity.h"
#include "../../Renderer/OffscreenFrameTicket.h"
#include "../../Terrain/TerrainAsset.h"
#include "../../Terrain/TerrainAssetTask.h"
#include "../../Terrain/TerrainControl.h"
#include "../../Terrain/TerrainFlight.h"
#include "../../Terrain/TerrainLod.h"
#include "../../Terrain/TerrainStreamer.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gte {

class Game;
class Renderer;
class RenderFeatureCompositor;

// Non-owning handles the world works through; resolved fresh on every use.
struct TerrainWorldServices {
    Game* game = nullptr;
    Renderer* renderer = nullptr;
    DescriptorSetLayoutHandle sceneServicesSetLayout;
    RenderFeatureCompositor* compositor = nullptr;
    fx::IFeatureDebugEndpoint* gpuDrivenScene = nullptr;
    FeatureEndpointLookup findEndpoint; // Any render feature by name (Shadow, Atmosphere ...).
};

struct TerrainTickContext {
    float aspect = 16.0f / 9.0f;                                  // Width over height of the game view.
    std::optional<OffscreenFrameTicket> gpuFrame = OffscreenFrameTicket{}; // Empty: no GPU write this frame.
    double nowSeconds = 0.0;                                      // Any monotonic clock.
    double deltaSeconds = 0.0;
    std::uint64_t frameIndex = 0;
};

class TerrainWorld final : public terrain::ITerrainWorld {
public:
    using ServicesFn = std::function<TerrainWorldServices()>;

    explicit TerrainWorld(ServicesFn services);
    ~TerrainWorld() override;
    TerrainWorld(const TerrainWorld&) = delete;
    TerrainWorld& operator=(const TerrainWorld&) = delete;

    // Per-frame work: flight, LOD selection, streaming, slot writes (bounded), cluster table update, live preview.
    void Tick(const TerrainTickContext& context);

    // ITerrainWorld
    const terrain::TerrainSettings& Settings() const override { return m_settings; }
    terrain::TerrainResult SetSettings(const terrain::TerrainSettings& settings) override;
    const terrain::TerrainRuntimeOptions& RuntimeOptions() const override { return m_runtime; }
    terrain::TerrainResult SetRuntimeOptions(const terrain::TerrainRuntimeOptions& options) override;
    bool IsOpen() const override { return m_open; }
    terrain::TerrainResult Open(const terrain::TerrainOpenOptions& options) override;
    terrain::TerrainResult Close() override;
    terrain::TerrainResult SetLivePreview(bool enabled) override;
    bool LivePreview() const override { return m_livePreview; }
    terrain::TerrainResult SetPreviewWindow(const terrain::TerrainPreviewWindow& window) override;
    terrain::TerrainPreviewWindow PreviewWindow() const override { return m_previewWindow; }
    void SetDebugAabb(bool enabled) override { m_debugAabb = enabled; }
    bool DebugAabb() const override { return m_debugAabb; }
    void CollectNodes(std::vector<terrain::TerrainNodeInfo>& out) const override;
    terrain::TerrainStats Stats() const override;
    terrain::TerrainResult SaveAsset(const std::filesystem::path& path) override;
    terrain::TerrainResult LoadAsset(const std::filesystem::path& path) override;
    terrain::TerrainResult StartFlight(const terrain::FlightSpec& spec) override;
    void StopFlight() override { m_flight.Stop(); }
    bool FlightActive() const override { return m_flight.Active(); }
    terrain::TerrainResult SetCameraMode(terrain::CameraMode mode) override;
    terrain::CameraMode GetCameraMode() const override { return m_rig.Mode(); }
    void FeedCameraInput(const terrain::CameraInput& input) override { terrain::AccumulateInput(m_pendingInput, input); }
    terrain::TerrainResult QueueCameraInput(const terrain::CameraInput& perFrame, int frames) override;
    terrain::TerrainResult SetCameraPose(const terrain::TerrainCameraPose& pose) override;
    terrain::TerrainResult SetCarve(std::shared_ptr<const terrain::ITerrainCarve> carve, std::span<const terrain::Rect2> dirty) override;
    std::optional<terrain::TerrainHeightProbe> ProbeHeight(double x, double z) const override;
    terrain::SurfaceSnapshot SnapshotSurface(double minX, double minZ, double maxX, double maxZ) const override;
    void SetOceanQuadVisible(bool visible) override;
    bool OceanQuadVisible() const override { return m_oceanQuadVisible; }
    void SetSeabedCullBelow(double y) override { m_seabedCullBelow = y; }

private:
    struct SceneEntities {
        Entity camera = kInvalidEntity;
        Entity sun = kInvalidEntity;
        Entity terrain = kInvalidEntity;
        Entity water = kInvalidEntity;
        Entity preview = kInvalidEntity;
        std::vector<Entity> all;
    };

    // Counters read back from the culling feature.
    struct GpuCounters {
        bool valid = false;
        bool pathActive = false;
        std::string mode;
        std::string occlusion;
        std::uint64_t tested = 0;
        std::uint64_t visible = 0;
        std::uint64_t frustum = 0;
        std::uint64_t pyramid = 0;
    };

    terrain::TerrainResult OpenImpl(const terrain::TerrainOpenOptions& options);
    terrain::TerrainResult CloseImpl(const char* reason);
    terrain::TerrainResult ReopenKeepingCamera();
    void ApplySelectorBudget();
    void ApplyStreamerConfig();
    void ApplyCameraRange(Game& game);
    bool Alive(Game& game) const;

    void TickStreaming(const TerrainTickContext& context, Game& game, const OffscreenFrameTicket& ticket);
    void TickPreview(const TerrainTickContext& context, Game& game, const OffscreenFrameTicket& ticket);
    void TickMaterial(const TerrainTickContext& context, Game& game);
    void TickFlight(const TerrainTickContext& context, Game& game);
    void TickCamera(const TerrainTickContext& context, Game& game);
    terrain::GroundHeightFn GroundFn() const;
    terrain::RigPose CameraPoseNow(Game& game) const;
    void WriteCameraPose(Game& game, const terrain::RigPose& pose) const;
    void RefreshGpuCounters();
    void PollAssetTask();

    void SetMeshRenderer(Game& game, Entity entity, const MeshRenderer& renderer) const;
    void RemoveMeshRenderer(Game& game, Entity entity) const;
    void FrameCameraOnPreview(Game& game);
    void PickPreviewCenter();
    std::pair<double, double> FindLandSpot() const;

    ServicesFn m_servicesFn;
    std::shared_ptr<const terrain::BakedTerrain> m_baked; // Serves nodes inside the loaded asset region.
    std::shared_ptr<const terrain::ITerrainCarve> m_carve; // Survives reopen; applied to every new streamer.
    bool m_oceanQuadVisible = true;
    double m_seabedCullBelow = -1.0e30;
    int m_seabedCulled = 0;

    terrain::TerrainSettings m_settings = terrain::Sanitize(terrain::TerrainSettings{});
    terrain::TerrainRuntimeOptions m_runtime;
    terrain::TerrainOpenOptions m_openOptions;
    terrain::TerrainPreviewWindow m_previewWindow;

    bool m_open = false;
    bool m_livePreview = false;
    bool m_debugAabb = false;
    bool m_previewDirty = false;
    bool m_materialDirty = false;
    double m_materialDirtySince = 0.0;
    std::optional<std::pair<Vec3, Quat>> m_savedCamera; // Camera pose before the preview took over.

    SceneEntities m_entities;
    TerrainFeatureState m_features;
    terrain::FlightController m_flight;
    terrain::CameraRig m_rig;
    terrain::CameraInput m_pendingInput;  // Summed since the last Tick.
    terrain::CameraInput m_scriptedInput; // Repeated every frame while m_scriptedFrames > 0.
    int m_scriptedFrames = 0;
    std::unique_ptr<TerrainGpuPool> m_pool;
    std::unique_ptr<TerrainAuxMeshes> m_aux;
    std::unique_ptr<terrain::LodSelector> m_selector;
    std::unique_ptr<terrain::TerrainStreamer> m_streamer;

    // Per-frame scratch, reused so a steady frame allocates nothing.
    std::vector<terrain::WantedNode> m_wanted;
    std::vector<terrain::DrawNode> m_drawList;
    terrain::StreamerUpdate m_update;
    std::vector<std::shared_ptr<const terrain::NodeMesh>> m_pending; // Ready on the CPU, not yet in a slot.
    std::vector<std::shared_ptr<const terrain::NodeMesh>> m_pendingReplace; // Rebuilt geometry waiting for a free slot.
    std::unordered_map<terrain::NodeKey, std::uint32_t, terrain::NodeKeyHash> m_slotOfKey;
    std::vector<std::optional<terrain::NodeKey>> m_keyOfSlot;
    std::vector<std::uint8_t> m_wantEnabled;
    std::unordered_set<terrain::NodeKey, terrain::NodeKeyHash> m_inViewKeys;

    GpuCounters m_gpu;
    std::uint64_t m_lastGpuCounterFrame = 0;
    std::uint64_t m_lastTick = 0;

    terrain::TerrainStats m_stats;
    std::string m_lastError;
    std::string m_assetResult;
    terrain::TerrainAssetTask m_assetTask; // Last member: its thread is joined first.
};

} // namespace gte
