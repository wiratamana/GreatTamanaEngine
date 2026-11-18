#include "BigWorldAdapters.h"

#include "../../Terrain/Editor/TerrainModule.h"
#include "../../Water/Editor/WaterModule.h"
#include "../../Water/World/WaterEdit.h"
#include "../../../Core/Core.h"
#include "../../../Core/Plugins/RenderFeatureCompositor.h"
#include "../../../Game/Game.h"
#include "../../../Game/Lighting/DirectionalLightResolver.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>

namespace gte {

namespace {

using bigworld::PortResult;

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

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

const fx::FeatureDebugValue* FindField(const fx::FeatureDebugResult& state, std::string_view key) noexcept
{
    const auto it = std::find_if(state.fields.begin(), state.fields.end(),
        [key](const fx::FeatureDebugField& f) { return f.key == key; });
    return it == state.fields.end() ? nullptr : &it->value;
}

bool AsBool(const fx::FeatureDebugValue* value, bool fallback = false) noexcept
{
    if (value == nullptr) {
        return fallback;
    }
    if (const bool* b = std::get_if<bool>(value)) {
        return *b;
    }
    if (const std::string* s = std::get_if<std::string>(value)) {
        return *s == "true" || *s == "1";
    }
    return fallback;
}

std::int64_t AsInt(const fx::FeatureDebugValue* value, std::int64_t fallback = 0) noexcept
{
    if (value == nullptr) {
        return fallback;
    }
    if (const std::int64_t* n = std::get_if<std::int64_t>(value)) {
        return *n;
    }
    if (const double* d = std::get_if<double>(value)) {
        return std::isfinite(*d) ? static_cast<std::int64_t>(*d) : fallback;
    }
    if (const std::string* s = std::get_if<std::string>(value)) {
        const auto parsed = fx::ParseDebugFloat(*s);
        return parsed ? static_cast<std::int64_t>(*parsed) : fallback;
    }
    return fallback;
}

double AsDouble(const fx::FeatureDebugValue* value, double fallback = 0.0) noexcept
{
    if (value == nullptr) {
        return fallback;
    }
    if (const double* d = std::get_if<double>(value)) {
        return std::isfinite(*d) ? *d : fallback;
    }
    if (const std::int64_t* n = std::get_if<std::int64_t>(value)) {
        return static_cast<double>(*n);
    }
    if (const std::string* s = std::get_if<std::string>(value)) {
        const auto parsed = fx::ParseDebugFloat(*s);
        return parsed.value_or(fallback);
    }
    return fallback;
}

std::string AsString(const fx::FeatureDebugValue* value)
{
    if (value != nullptr) {
        if (const std::string* s = std::get_if<std::string>(value)) {
            return *s;
        }
    }
    return {};
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
    if (facts.open) {
        if (const std::optional<terrain::TerrainHeightProbe> probe = world->ProbeHeight(stats.cameraX, stats.cameraZ)) {
            facts.cameraAboveGround = stats.cameraY - probe->carvedHeight;
        }
    }
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

bigworld::FeatureState FeaturePortAdapter::GetState() const
{
    bigworld::FeatureState out;
    if (const fx::IFeatureDebugEndpoint* const grass = m_modules.FindEndpoint("Grass")) {
        const fx::FeatureDebugResult s = grass->GetState();
        out.grass.available = true;
        out.grass.status = AsString(FindField(s, "game.status"));
        out.grass.tilePresent = AsBool(FindField(s, "tile.present"));
        out.grass.seaLevel = AsDouble(FindField(s, "tile.seaLevel"), -1.0e6);
        out.grass.tileRevision = AsInt(FindField(s, "tile.revision"));
        out.grass.blades[0] = AsInt(FindField(s, "game.stats.accepted0"));
        out.grass.blades[1] = AsInt(FindField(s, "game.stats.accepted1"));
        out.grass.blades[2] = AsInt(FindField(s, "game.stats.accepted2"));
        out.grass.shadowCascades = AsInt(FindField(s, "game.shadow.cascades"));
        out.grass.receivesSunShadow = out.grass.shadowCascades > 0;
        out.grass.receivesCloudShadow = AsBool(FindField(s, "game.shadow.cloud"));
        out.grass.castCascades = AsInt(FindField(s, "game.shadow.castCascades"));
        out.grass.castBlades = AsInt(FindField(s, "game.shadow.castBlades"));
    }
    if (const fx::IFeatureDebugEndpoint* const clouds = m_modules.FindEndpoint("VolumetricCloud")) {
        const fx::FeatureDebugResult s = clouds->GetState();
        out.cloudsAvailable = true;
        out.cloudsEnabled = AsBool(FindField(s, "enabled"));
    }
    if (const fx::IFeatureDebugEndpoint* const vl = m_modules.FindEndpoint("VolumetricLight")) {
        const fx::FeatureDebugResult s = vl->GetState();
        out.volumetricLightAvailable = true;
        out.volumetricLightEnabled = AsBool(FindField(s, "enabled"));
        out.volumetricLightDisabledByProfile = AsString(FindField(s, "game.status")) == "PassDisabled";
    }
    if (m_core != nullptr) {
        if (const RenderFeatureCompositor* const compositor = m_core->GetRenderFeatureCompositor()) {
            bool disabled = false;
            for (const RenderFeatureDebugEntry& entry : compositor->DebugSnapshot()) {
                if (entry.name == "VolumetricLight.Volume" || entry.name == "VolumetricLight.Composite") {
                    out.volumetricLightAvailable = true;
                    if (!entry.enabled || !entry.enabledPrimary) {
                        disabled = true;
                    }
                }
            }
            out.volumetricLightDisabledByProfile = disabled;
        }
    }
    if (m_core != nullptr) {
        const ResolvedDirectionalLight sun = ResolveActiveDirectionalLight(m_core->GetGame().GetRegistry());
        const double y = std::clamp(static_cast<double>(sun.directionTowardSun.y), -1.0, 1.0);
        out.sunAvailable = true;
        out.sunElevationDegrees = std::asin(y) * kRadToDeg;
        out.sunAboveHorizon = y > 0.0;
    } else if (const fx::IFeatureDebugEndpoint* const shadow = m_modules.FindEndpoint("Shadow")) {
        const fx::FeatureDebugResult s = shadow->GetState();
        if (const fx::FeatureDebugValue* const elev = FindField(s, "game.sunElevationDegrees")) {
            out.sunAvailable = true;
            out.sunElevationDegrees = AsDouble(elev);
            out.sunAboveHorizon = out.sunElevationDegrees > 0.0;
        }
    }
    return out;
}

} // namespace gte
