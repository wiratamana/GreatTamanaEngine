#include "BigWorldAdapters.h"

#include "../../Terrain/Editor/TerrainModule.h"
#include "../../Water/Editor/WaterModule.h"
#include "../../Water/World/WaterEdit.h"

#include <algorithm>
#include <exception>
#include <memory>

namespace gte {

namespace {

using bigworld::PortResult;

PortResult Fail(std::string message)
{
    PortResult r;
    r.ok = false;
    r.message = std::move(message);
    return r;
}

terrain::ITerrainWorld* TerrainOf(const IEditorModuleDirectory& modules)
{
    TerrainModule* const module = modules.Find<TerrainModule>(kTerrainModuleName);
    return module != nullptr ? module->World() : nullptr;
}

water::IWaterWorld* WaterOf(const IEditorModuleDirectory& modules)
{
    WaterModule* const module = modules.Find<WaterModule>(kWaterModuleName);
    return module != nullptr ? module->World() : nullptr;
}

water::HeightFn GroundOf(const water::IWaterWorld& world)
{
    return [&world](double x, double z) {
        const auto h = world.TerrainHeight(x, z);
        return h ? static_cast<float>(h->baseHeight) : 0.0f;
    };
}

PortResult FromWater(const water::WaterResult& r)
{
    PortResult out;
    out.ok = r.ok;
    out.message = r.message;
    out.id = r.id;
    return out;
}

} // namespace

PortResult TerrainPortAdapter::Open(const bigworld::TerrainOpenRequest& request)
{
    terrain::ITerrainWorld* const world = TerrainOf(m_modules);
    if (world == nullptr) {
        return Fail("the terrain world is not available");
    }
    terrain::TerrainOpenOptions options;
    if (!terrain::TryParseTerrainProfile(request.profile, options.profile)) {
        return Fail("unknown terrain profile " + request.profile);
    }
    options.water = request.flatOceanQuad;
    options.renderBudget = request.renderBudget;
    const terrain::TerrainResult opened = world->Open(options);
    return opened.ok ? PortResult{ true, opened.message, 0 } : Fail(opened.message);
}

PortResult TerrainPortAdapter::PlaceCamera(const bigworld::CameraPlacement& placement)
{
    terrain::ITerrainWorld* const world = TerrainOf(m_modules);
    if (world == nullptr) {
        return Fail("the terrain world is not available");
    }
    const terrain::TerrainResult mode = world->SetCameraMode(placement.freeCamera ? terrain::CameraMode::FreeFly : terrain::CameraMode::Script);
    if (!mode.ok) {
        return Fail(mode.message);
    }
    terrain::TerrainCameraPose pose;
    pose.x = placement.x;
    pose.z = placement.z;
    pose.y = placement.heightAboveGround;
    pose.heightAboveGround = true;
    pose.yawDegrees = placement.headingDegrees;
    pose.pitchDownDegrees = placement.lookDownDegrees;
    const terrain::TerrainResult placed = world->SetCameraPose(pose);
    return placed.ok ? PortResult{ true, placed.message, 0 } : Fail(placed.message);
}

PortResult TerrainPortAdapter::Close()
{
    terrain::ITerrainWorld* const world = TerrainOf(m_modules);
    if (world == nullptr) {
        return Fail("the terrain world is not available");
    }
    const terrain::TerrainResult closed = world->Close();
    return closed.ok ? PortResult{ true, closed.message, 0 } : Fail(closed.message);
}

bigworld::TerrainFacts TerrainPortAdapter::Facts() const
{
    bigworld::TerrainFacts facts;
    const terrain::ITerrainWorld* const world = TerrainOf(m_modules);
    if (world == nullptr) {
        return facts;
    }
    const terrain::TerrainStats stats = world->Stats();
    facts.available = true;
    facts.open = world->IsOpen();
    facts.nodesResident = stats.nodesResident;
    facts.nodesPending = stats.nodesPending;
    facts.uploadQueue = stats.uploadQueue;
    facts.nodesRebuilding = stats.nodesRebuilding;
    facts.lastError = stats.lastError;
    return facts;
}

std::optional<double> TerrainPortAdapter::GroundHeight(double x, double z) const
{
    const terrain::ITerrainWorld* const world = TerrainOf(m_modules);
    if (world == nullptr || !world->IsOpen()) {
        return std::nullopt;
    }
    const std::optional<terrain::TerrainHeightProbe> probe = world->ProbeHeight(x, z);
    return probe ? std::optional<double>(probe->carvedHeight) : std::nullopt;
}

PortResult WaterPortAdapter::AddLake(const bigworld::LakeRequest& request)
{
    water::IWaterWorld* const world = WaterOf(m_modules);
    if (world == nullptr) {
        return Fail("the water world is not available");
    }
    if (request.outline.size() < 3) {
        return Fail("the lake outline needs at least 3 corners");
    }
    water::WaterBody lake = water::MakeBoxLake("Big World Lake", 0.0, 0.0, 10.0, 10.0, 0.0f);
    lake.polygon.clear();
    for (const bigworld::PlanPoint& p : request.outline) {
        lake.polygon.push_back({ p.x, p.z });
    }
    lake.bankWidth = static_cast<float>(request.bankWidth);
    lake.level = water::SuggestLakeLevel(lake, GroundOf(*world));
    return FromWater(world->AddBody(lake));
}

PortResult WaterPortAdapter::AddRiver(const bigworld::RiverRequest& request)
{
    water::IWaterWorld* const world = WaterOf(m_modules);
    if (world == nullptr) {
        return Fail("the water world is not available");
    }
    std::vector<water::RiverPoint> points;
    for (const bigworld::PlanPoint& p : request.points) {
        points.push_back({ p.x, p.z, 0.0f, static_cast<float>(request.width) });
    }
    water::WaterBody river = water::MakeRiverBody("Big World River", std::move(points));
    water::AutoRiverLevels(river, GroundOf(*world), 1.0f);
    return FromWater(world->AddBody(river));
}

PortResult WaterPortAdapter::RemoveBody(std::uint32_t id)
{
    water::IWaterWorld* const world = WaterOf(m_modules);
    if (world == nullptr) {
        return Fail("the water world is not available");
    }
    return FromWater(world->RemoveBody(id));
}

bigworld::WaterFacts WaterPortAdapter::Facts() const
{
    bigworld::WaterFacts facts;
    const water::IWaterWorld* const world = WaterOf(m_modules);
    if (world == nullptr) {
        return facts;
    }
    const water::WaterStats stats = world->Stats();
    facts.available = true;
    facts.featureConnected = stats.featureConnected;
    facts.bodyCount = stats.bodyCount;
    facts.carvePending = stats.carvePending;
    facts.carveApplies = stats.carveApplies;
    facts.lastError = stats.lastError;
    const std::shared_ptr<const water::WaterBodySet> set = world->Bodies();
    for (const water::WaterBody& body : set->Bodies()) {
        if (body.kind == water::WaterBodyKind::Ocean) {
            facts.hasOcean = true;
            facts.oceanLevel = static_cast<double>(body.level);
            break;
        }
    }
    return facts;
}

bool FeaturePortAdapter::Has(std::string_view feature) const
{
    return m_modules.FindEndpoint(feature) != nullptr;
}

PortResult FeaturePortAdapter::Snapshot(std::string_view feature, std::span<const std::string> keys)
{
    const fx::IFeatureDebugEndpoint* const endpoint = m_modules.FindEndpoint(feature);
    if (endpoint == nullptr) {
        return Fail("feature " + std::string(feature) + " is not available");
    }
    std::vector<std::string_view> names(keys.begin(), keys.end());
    SavedFeatureSettings saved;
    saved.feature = std::string(feature);
    saved.args = SnapshotFeatureSettings(*endpoint, names);
    m_saved.push_back(std::move(saved));
    return {};
}

PortResult FeaturePortAdapter::Set(std::string_view feature, std::span<const bigworld::KeyValue> values)
{
    fx::IFeatureDebugEndpoint* const endpoint = m_modules.FindEndpoint(feature);
    if (endpoint == nullptr) {
        return Fail("feature " + std::string(feature) + " is not available");
    }
    std::vector<fx::FeatureDebugArg> args;
    for (const bigworld::KeyValue& kv : values) {
        args.push_back({ kv.key, kv.value });
    }
    try {
        const fx::FeatureDebugResult result = endpoint->Set(args);
        if (!result.ok) {
            return Fail(result.message + (result.badKey.empty() ? std::string() : " (key " + result.badKey + ")"));
        }
    } catch (const std::exception& e) {
        return Fail(e.what());
    }
    return {};
}

PortResult FeaturePortAdapter::RestoreAll()
{
    bool allOk = true;
    for (auto it = m_saved.rbegin(); it != m_saved.rend(); ++it) {
        allOk = RestoreFeatureSettings(m_modules.FindEndpoint(it->feature), "BigWorld", it->feature, it->args) && allOk;
    }
    m_saved.clear();
    return allOk ? PortResult{} : Fail("some feature settings could not be restored (see the log)");
}

std::vector<std::optional<std::string>> FeaturePortAdapter::Read(
    std::string_view feature, std::span<const std::string_view> keys) const
{
    std::vector<std::optional<std::string>> out;
    const fx::IFeatureDebugEndpoint* const endpoint = m_modules.FindEndpoint(feature);
    if (endpoint == nullptr) {
        return out;
    }
    const fx::FeatureDebugResult state = endpoint->GetState();
    out.reserve(keys.size());
    for (const std::string_view key : keys) {
        const auto found = std::find_if(state.fields.begin(), state.fields.end(),
            [&](const fx::FeatureDebugField& f) { return f.key == key; });
        if (found == state.fields.end()) {
            out.emplace_back(std::nullopt);
        } else {
            out.emplace_back(FeatureDebugValueText(found->value));
        }
    }
    return out;
}

} // namespace gte
