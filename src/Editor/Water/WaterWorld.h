#pragma once

// The live water world of the editor: owns the bodies, hands the carve to the terrain (debounced) and the bodies
// and look to the Water render feature. Every call is made on the main thread.
// Tick() runs once per frame:  waterWorld.Tick(nowSeconds);

#include "../../Core/FeatureKit/FeatureDebug.h"
#include "../../Features/Water/World/WaterControl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gte {

// Non-owning handles, resolved fresh on every use.
struct WaterWorldServices {
    terrain::ITerrainWorld* terrain = nullptr;
    fx::IFeatureDebugEndpoint* waterFeature = nullptr; // The "Water" render feature; null when it is not built in.
};

class WaterWorld final : public water::IWaterWorld {
public:
    using ServicesFn = std::function<WaterWorldServices()>;

    explicit WaterWorld(ServicesFn services);

    void Tick(double nowSeconds);

    // IWaterWorld
    const water::WaterSettings& Settings() const override { return m_settings; }
    water::WaterResult SetSettings(const water::WaterSettings& settings) override;
    std::shared_ptr<const water::WaterBodySet> Bodies() const override { return m_set; }
    water::WaterResult AddBody(const water::WaterBody& body) override;
    water::WaterResult UpdateBody(const water::WaterBody& body) override;
    water::WaterResult RemoveBody(std::uint32_t id) override;
    water::WaterResult ClearBodies() override;
    water::WaterResult MovePoint(std::uint32_t id, int index, double x, double z, std::optional<float> surfaceY, std::optional<float> width) override;
    water::WaterResult InsertPoint(std::uint32_t id, int index, double x, double z) override;
    water::WaterResult RemovePoint(std::uint32_t id, int index) override;
    void FlushCarve() override { m_flushRequested = true; }
    void SetCarveDebounce(double seconds) override { m_debounce = seconds < 0.0 ? 0.0 : seconds; }
    double CarveDebounce() const override { return m_debounce; }
    water::WaterResult SaveFile(const std::filesystem::path& path) override;
    water::WaterResult LoadFile(const std::filesystem::path& path) override;
    std::optional<terrain::TerrainHeightProbe> TerrainHeight(double x, double z) const override;
    water::WaterProbeResult Probe(double x, double z) const override;
    water::WaterStats Stats() const override;

private:
    // Rebuilds the snapshot from `bodies`; refuses (and changes nothing) when the body `id` (0 = any) would be dropped.
    water::WaterResult Commit(std::vector<water::WaterBody> bodies, const water::WaterSettings& settings, std::uint32_t id,
        const std::string& message);
    water::WaterBody* FindBody(std::vector<water::WaterBody>& bodies, std::uint32_t id) const;
    water::WaterResult EditBody(std::uint32_t id, const std::function<bool(water::WaterBody&, std::string&)>& edit);
    bool PushToFeature(const WaterWorldServices& services);
    void PushCarve(const WaterWorldServices& services);

    ServicesFn m_services;
    water::WaterSettings m_settings;
    std::vector<water::WaterBody> m_bodies;
    std::shared_ptr<const water::WaterBodySet> m_set;
    std::shared_ptr<const water::WaterBodySet> m_appliedCarveSet; // What the terrain was given last.
    std::uint32_t m_nextId = 1;
    std::uint64_t m_revision = 0;
    double m_now = 0.0;
    double m_lastEdit = 0.0;
    double m_lastFeatureTry = -1.0e9;
    double m_debounce = 0.3;
    bool m_featureDirty = true;
    bool m_carvePending = false;
    bool m_flushRequested = false;
    bool m_featureConnected = false;
    bool m_quadHidden = false;
    double m_seabedCullBelow = -1.0e30;
    int m_carveApplies = 0;
    int m_carveRects = 0;
    int m_featurePushes = 0;
    std::string m_lastError;
};

} // namespace gte
