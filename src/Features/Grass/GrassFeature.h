#pragma once

#include "GrassDebugEndpoint.h"
#include "GrassMath.h"
#include "GrassTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"
#include "../../Game/Lighting/CloudShadowMap.h"
#include "../../Game/Lighting/SunLightModulation.h"
#include "../../Game/Lighting/SunShadowReceiver.h"
#include "../../Game/Lighting/WindState.h"
#include "../../Math/Vec3.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace gte {

// Per-view parameter blocks and bookkeeping. One params object per pass: a block uploads once per frame slot.
struct GrassViewState {
    fx::GpuParams<GrassParamsGpu> initParams;
    fx::GpuParams<GrassParamsGpu> cullParams;
    fx::GpuParams<GrassShadowParamsGpu> cullShadowParams;
    std::array<fx::GpuParams<GrassParamsGpu>, kGrassLodCount> drawParams;
    std::array<std::uint64_t, 4> slotRevision{};   // Tile revision each frame-slot copy holds (0 = none).
    std::array<std::uint64_t, 4> slotGeneration{}; // Buffer generation that copy was uploaded into.
    GrassViewSnapshot snapshot;
};

// GPU-driven grass: a compute pass places and culls blades per 16 m chunk, three indirect draws (one per LOD) build
// each blade from its vertex index. Terrain height comes from a height tile the editor layer submits.
class GrassFeature final : public fx::Feature {
public:
    GrassFeature() = default;
    explicit GrassFeature(fx::FeatureHost& host);

    const char* Name() const override { return kGrassFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    // What the shaders use: the user's values after ordering and range rules.
    const GrassSettings& Settings() const noexcept { return m_settings; }

    // What the user asked for (each value only range-clamped); edit this, so a temporary conflict loses nothing.
    const GrassSettings& UserSettings() const noexcept { return m_user; }

    // Stores `candidate` as the user's values and derives Settings() from it. Main thread only.
    void ApplySettings(const GrassSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    GrassViewSnapshot Snapshot(rg::ViewScope scope) const;

    // Terrain height under the grass. Main thread only. Null clears it. Replaces a synthetic tile.
    // The tile must stay inside heightTileTexels (Settings()); a bigger one is refused when drawn.
    void SubmitHeightTile(std::shared_ptr<const GrassHeightTile> tile);
    std::shared_ptr<const GrassHeightTile> HeightTile() const noexcept { return m_tile; }

    // A made-up landscape that follows the camera; used until a real tile is submitted.
    void EnableSyntheticTile(const GrassSyntheticSpec& spec);
    void DisableSyntheticTile();
    bool SyntheticTileEnabled() const noexcept { return m_syntheticOn; }
    const GrassSyntheticSpec& SyntheticSpec() const noexcept { return m_syntheticSpec; }

    // Callbacks of the editor layer's tile provider, for the debug endpoint. Main thread; empty functions clear them.
    void SetTileSourceHooks(GrassTileReportFn report, GrassProbeFn probe)
    {
        m_tileReport = std::move(report);
        m_probe = std::move(probe);
    }
    const GrassTileReportFn& TileReportHook() const noexcept { return m_tileReport; }
    const GrassProbeFn& ProbeHook() const noexcept { return m_probe; }

    // Camera of the primary view as of the last frame (world metres).
    Vec3 LastEye() const noexcept { return m_lastEye; }

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void FrameStamp(fx::PreOpaqueFrame& f);
    void DrawPass(fx::PostOpaqueFrame& f);
    void UpdateSyntheticTile(const fx::View& view);
    void FillEnvironment(fx::PostOpaqueFrame& f, GrassFrameInputs& in, GrassViewSnapshot& snap);

    GrassSettings m_user;
    GrassSettings m_settings;
    std::shared_ptr<const GrassHeightTile> m_tile;
    bool m_syntheticOn = false;
    GrassSyntheticSpec m_syntheticSpec;
    std::uint64_t m_tileRevision = 0;
    GrassTileReportFn m_tileReport;
    GrassProbeFn m_probe;
    Vec3 m_lastEye;
    double m_time = 0.0;
    double m_ownOffset[2] = {};
    std::uint64_t m_clockFrame = ~std::uint64_t{ 0 };
    std::uint64_t m_newestFrame = 0;
    GrassDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<GrassViewState> m_views;
    fx::ComputeShader m_initShader{ "GrassInit" };
    fx::ComputeShader m_cullShader{ "GrassCull" };
    fx::FullscreenShader m_drawShader{ "Grass" };
    fx::HizPyramid m_hiz{ "Grass.Hiz" };
    fx::PersistentBuffer m_instances{ "Grass.Instances", fx::BufferUse::Storage };
    fx::PersistentBuffer m_commands{ "Grass.Commands", fx::BufferUse::Storage | fx::BufferUse::Indirect };
    fx::PersistentBuffer m_heights{ "Grass.Heights", fx::BufferUse::Storage | fx::BufferUse::HostWrite, 2 };
    fx::GpuReadback<GrassStatsGpu> m_stats{ "Grass.Stats" };
    fx::ChannelReader<WindState> m_wind{ kWindStateOwner, kWindStateChannel };
    fx::ChannelReader<SunLightModulation> m_sunLight{ kSunLightModulationOwner, kSunLightModulationChannel };
    fx::ChannelReader<SunShadowReceiver> m_shadowReceiver{ kSunShadowReceiverOwner, kSunShadowReceiverChannel };
    fx::ChannelReader<CloudShadowMap> m_cloudShadow{ kCloudShadowOwner, kCloudShadowChannel };
};

} // namespace gte
