#include "BigWorldDemo.h"

#include <charconv>
#include <cmath>

namespace gte::bigworld {

namespace {

constexpr double kPollWaitingSeconds = 0.1;
constexpr double kPollReadySeconds = 0.5;
constexpr double kSeaLevelTolerance = 0.05;
constexpr double kNoOceanBelow = -1.0e5; // The grass tile reports a very low sea level when there is no ocean.
constexpr int kMaxStepsPerTick = 8;

std::string Escape(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

std::string Quote(const std::string& text)
{
    return "\"" + Escape(text) + "\"";
}

const char* Bool(bool value) noexcept
{
    return value ? "true" : "false";
}

std::string Number(double value)
{
    if (!std::isfinite(value)) {
        return "0";
    }
    char buffer[40];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, 12);
    return ec == std::errc{} ? std::string(buffer, end) : std::string("0");
}

PortResult Refuse(std::string message)
{
    PortResult r;
    r.ok = false;
    r.message = std::move(message);
    return r;
}

} // namespace

const char* PhaseName(Phase phase) noexcept
{
    switch (phase) {
    case Phase::Idle: return "Idle";
    case Phase::OpeningTerrain: return "OpeningTerrain";
    case Phase::WaitingForNodes: return "WaitingForNodes";
    case Phase::AddingWater: return "AddingWater";
    case Phase::WaitingForWater: return "WaitingForWater";
    case Phase::EnablingFeatures: return "EnablingFeatures";
    case Phase::WaitingForGrass: return "WaitingForGrass";
    case Phase::Ready: return "Ready";
    case Phase::Failed: return "Failed";
    }
    return "Idle";
}

bool BigWorldDemo::PortsPresent(std::string& missing) const
{
    if (m_ports.terrain == nullptr) {
        missing = "terrain";
    } else if (m_ports.water == nullptr) {
        missing = "water";
    } else if (m_ports.features == nullptr) {
        missing = "feature settings";
    }
    return missing.empty();
}

void BigWorldDemo::Enter(Phase phase, std::string status)
{
    m_state.phase = phase;
    m_state.status = std::move(status);
    m_phaseStart = m_now;
}

void BigWorldDemo::Fail(const std::string& message)
{
    Cleanup();
    m_state.error = message;
    Enter(Phase::Failed, "failed: " + message);
}

bool BigWorldDemo::PhaseTimedOut(double limitSeconds) const noexcept
{
    return m_now - m_phaseStart > limitSeconds;
}

PortResult BigWorldDemo::Open(const BigWorldPlan& plan, double nowSeconds)
{
    m_now = nowSeconds;
    if (Active()) {
        Close();
    }
    m_state = BigWorldState{};
    m_phaseWarnings.clear();
    m_agreementWarnings.clear();
    m_lastPoll = -1e9;
    m_waterAdded = false;
    m_carveAppliesBefore = 0;
    m_started = nowSeconds;

    std::string missing;
    if (!PortsPresent(missing)) {
        const std::string message = "the " + missing + " system is not available";
        Fail(message);
        return Refuse(message);
    }

    m_state.plan = SanitizeBigWorldPlan(plan);
    const BigWorldPlan& p = m_state.plan;
    Enter(Phase::OpeningTerrain, "opening the terrain");

    TerrainOpenRequest open;
    open.profile = p.terrainProfile;
    open.renderBudget = p.renderBudget;
    open.flatOceanQuad = p.flatOceanQuad;
    PortResult opened = m_ports.terrain->Open(open);
    if (!opened.ok) {
        Fail("terrain: " + opened.message);
        return Refuse(m_state.error);
    }
    m_state.terrainOwned = true;

    CameraPlacement camera;
    camera.x = p.spotX;
    camera.z = p.spotZ;
    camera.heightAboveGround = p.heightAboveGround;
    camera.headingDegrees = p.headingDegrees;
    camera.lookDownDegrees = p.lookDownDegrees;
    camera.freeCamera = p.freeCamera;
    const PortResult placed = m_ports.terrain->PlaceCamera(camera);
    if (!placed.ok) {
        Fail("camera: " + placed.message);
        return Refuse(m_state.error);
    }
    Enter(Phase::WaitingForNodes, "streaming the terrain around the camera");
    PortResult ok;
    ok.message = m_state.status;
    return ok;
}

PortResult BigWorldDemo::Close()
{
    if (m_state.phase == Phase::Idle) {
        PortResult r;
        r.message = "the Big World is not open";
        return r;
    }
    Cleanup();
    m_state.error.clear();
    m_state.info.clear();
    m_state.warnings.clear();
    m_phaseWarnings.clear();
    m_agreementWarnings.clear();
    Enter(Phase::Idle, "closed");
    PortResult r;
    r.message = "the Big World is closed";
    return r;
}

void BigWorldDemo::Cleanup()
{
    m_agreementWarnings.clear();
    m_state.info.clear();
    if (m_ports.features != nullptr) {
        const PortResult restored = m_ports.features->RestoreAll();
        if (!restored.ok) {
            m_phaseWarnings.push_back("restore: " + restored.message);
        }
    }
    if (m_ports.water != nullptr) {
        for (const std::uint32_t id : m_state.waterBodyIds) {
            const PortResult removed = m_ports.water->RemoveBody(id);
            if (!removed.ok) {
                m_phaseWarnings.push_back("water body " + std::to_string(id) + ": " + removed.message);
            }
        }
    }
    m_state.waterBodyIds.clear();
    if (m_state.terrainOwned && m_ports.terrain != nullptr && m_ports.terrain->Facts().open) {
        const PortResult closed = m_ports.terrain->Close();
        if (!closed.ok) {
            m_phaseWarnings.push_back("terrain close: " + closed.message);
        }
    }
    m_state.terrainOwned = false;
    m_state.warnings = m_phaseWarnings;
}

void BigWorldDemo::ReadHealth()
{
    BigWorldHealth& h = m_state.health;
    if (m_ports.terrain != nullptr) {
        h.terrain = m_ports.terrain->Facts();
    }
    if (m_ports.water != nullptr) {
        h.water = m_ports.water->Facts();
    }
    if (m_ports.features == nullptr) {
        return;
    }
    const FeatureState f = m_ports.features->GetState();
    h.grass = f.grass;
    h.cloudsAvailable = f.cloudsAvailable;
    h.cloudsEnabled = f.cloudsEnabled;
    h.sunAvailable = f.sunAvailable;
    h.sunAboveHorizon = f.sunAboveHorizon;
    h.sunElevationDegrees = f.sunElevationDegrees;
}

void BigWorldDemo::CheckAgreement()
{
    m_agreementWarnings.clear();
    m_state.info.clear();
    const BigWorldHealth& h = m_state.health;
    if (!h.water.featureConnected) {
        m_agreementWarnings.push_back("the water feature is not connected to the water world");
    }
    if (m_ports.terrain != nullptr && h.water.hasOcean) {
        const std::optional<double> ground = m_ports.terrain->GroundHeight(m_state.plan.spotX, m_state.plan.spotZ);
        if (ground && *ground < h.water.oceanLevel) {
            m_agreementWarnings.push_back("the spot is under the sea (ground " + Number(*ground) + " m): no grass grows here");
        }
    }
    if (h.sunAvailable && !h.sunAboveHorizon) {
        m_agreementWarnings.push_back("the sun is not above the horizon (elevation " + Number(h.sunElevationDegrees) + " deg)");
    }
    if (m_state.plan.tweaks.empty() || !h.grass.available) {
        return;
    }
    if (h.grass.status != "Ok" && h.grass.tilePresent) {
        m_agreementWarnings.push_back("grass does not draw (status " + h.grass.status + ")");
    }
    if (!h.grass.tilePresent) {
        m_agreementWarnings.push_back("grass has no height tile");
        return;
    }
    const bool grassHasOcean = h.grass.seaLevel > kNoOceanBelow;
    if (h.water.hasOcean != grassHasOcean) {
        m_agreementWarnings.push_back(h.water.hasOcean ? "the sea is on but grass knows no ocean"
                                                       : "grass knows an ocean but the water world has none");
    } else if (grassHasOcean && std::abs(h.water.oceanLevel - h.grass.seaLevel) > kSeaLevelTolerance) {
        m_agreementWarnings.push_back("ocean level " + Number(h.water.oceanLevel) + " differs from the grass sea level "
            + Number(h.grass.seaLevel));
    }
    if (h.grass.status != "Ok") {
        return;
    }
    if (!h.grass.receivesSunShadow || h.grass.shadowCascades <= 0) {
        m_agreementWarnings.push_back("grass gets no sun shadow");
    }
    if (h.grass.castCascades <= 0) {
        const double altitude = h.terrain.cameraAboveGround.value_or(m_state.plan.heightAboveGround);
        if (altitude < kGrassShadowCastMaxAltitude) {
            m_agreementWarnings.push_back("grass casts no sun shadow");
        } else {
            m_state.info = "grass shadow casting is inactive above " + Number(kGrassShadowCastMaxAltitude)
                + " m ground altitude (camera " + Number(altitude) + " m)";
        }
    }
    if (h.cloudsEnabled && !h.grass.receivesCloudShadow) {
        m_agreementWarnings.push_back("grass gets no cloud shadow");
    }
}

void BigWorldDemo::LoseTerrain()
{
    if (m_state.phase == Phase::Ready) {
        Cleanup();
        m_phaseWarnings.push_back(kSceneReplacedNote);
        m_state.warnings = m_phaseWarnings;
        Enter(Phase::Idle, "the scene was replaced; the Big World is closed");
        return;
    }
    Fail("the terrain closed while the demo was opening");
}

void BigWorldDemo::Refresh(double nowSeconds)
{
    m_now = nowSeconds;
    ReadHealth();
    if (m_state.phase == Phase::Ready) {
        CheckAgreement();
    } else {
        m_state.info.clear();
    }
    m_state.warnings = m_phaseWarnings;
    m_state.warnings.insert(m_state.warnings.end(), m_agreementWarnings.begin(), m_agreementWarnings.end());
    const bool idle = m_state.phase == Phase::Idle;
    m_state.secondsInPhase = idle ? 0.0 : m_now - m_phaseStart;
    m_state.secondsTotal = idle ? 0.0 : m_now - m_started;
}

void BigWorldDemo::StepWaitingForNodes()
{
    const TerrainFacts& t = m_state.health.terrain;
    if (t.nodesPending == 0 && t.uploadQueue == 0 && t.nodesResident >= m_state.plan.minResidentNodes) {
        Enter(Phase::AddingWater, "adding water");
    } else if (PhaseTimedOut(m_state.plan.nodeWaitSeconds)) {
        Fail("terrain streaming did not finish in " + Number(m_state.plan.nodeWaitSeconds) + " s (resident "
            + std::to_string(t.nodesResident) + ", pending " + std::to_string(t.nodesPending) + ")");
    }
}

void BigWorldDemo::StepAddingWater()
{
    const BigWorldPlan& p = m_state.plan;
    m_carveAppliesBefore = m_state.health.water.carveApplies;
    if (p.lake) {
        LakeRequest request;
        request.outline = LakeOutline(p);
        request.bankWidth = p.lakeBankWidth;
        const PortResult lake = m_ports.water->AddLake(request);
        if (!lake.ok) {
            Fail("lake: " + lake.message);
            return;
        }
        m_state.waterBodyIds.push_back(lake.id);
        m_waterAdded = true;
    }
    if (p.river) {
        RiverRequest river;
        river.width = p.riverWidth;
        river.points = RiverPath(p);
        const PortResult added = m_ports.water->AddRiver(river);
        if (!added.ok) {
            m_phaseWarnings.push_back("river: " + added.message);
        } else {
            m_state.waterBodyIds.push_back(added.id);
            m_waterAdded = true;
        }
    }
    Enter(m_waterAdded ? Phase::WaitingForWater : Phase::EnablingFeatures,
        m_waterAdded ? "waiting for the water carve" : "switching the sky and grass on");
}

void BigWorldDemo::StepWaitingForWater()
{
    const WaterFacts& w = m_state.health.water;
    const bool carved = w.carveApplies > m_carveAppliesBefore && !w.carvePending && m_state.health.terrain.nodesRebuilding == 0;
    if (carved && w.featureConnected) {
        Enter(Phase::EnablingFeatures, "switching the sky and grass on");
    } else if (PhaseTimedOut(m_state.plan.waterWaitSeconds)) {
        m_phaseWarnings.push_back("the water carve did not settle in " + Number(m_state.plan.waterWaitSeconds) + " s");
        Enter(Phase::EnablingFeatures, "switching the sky and grass on");
    }
}

void BigWorldDemo::StepEnablingFeatures()
{
    for (const FeatureTweak& tweak : m_state.plan.tweaks) {
        if (!m_ports.features->Has(tweak.feature)) {
            if (tweak.required) {
                Fail("feature " + tweak.feature + " is not available");
                return;
            }
            m_phaseWarnings.push_back("feature " + tweak.feature + " is not available");
            continue;
        }
        const PortResult saved = m_ports.features->Snapshot(tweak.feature, tweak.restoreKeys);
        if (!saved.ok) {
            Fail(tweak.feature + ": " + saved.message);
            return;
        }
        const PortResult set = m_ports.features->Set(tweak.feature, tweak.values);
        if (!set.ok) {
            Fail(tweak.feature + ": " + set.message);
            return;
        }
    }
    if (m_state.plan.waitForGrass) {
        Enter(Phase::WaitingForGrass, "waiting for the first grass frame");
    } else {
        Enter(Phase::Ready, "ready");
    }
}

void BigWorldDemo::StepWaitingForGrass()
{
    const GrassHealth& g = m_state.health.grass;
    if (g.status == "Ok" && g.tilePresent) {
        Enter(Phase::Ready, "ready");
    } else if (PhaseTimedOut(m_state.plan.grassWaitSeconds)) {
        m_phaseWarnings.push_back("grass did not draw in " + Number(m_state.plan.grassWaitSeconds) + " s (status "
            + (g.available ? g.status : std::string("unavailable")) + ")");
        Enter(Phase::Ready, "ready, without grass");
    }
}

bool BigWorldDemo::Step()
{
    const Phase before = m_state.phase;
    switch (before) {
    case Phase::WaitingForNodes: StepWaitingForNodes(); break;
    case Phase::AddingWater: StepAddingWater(); break;
    case Phase::WaitingForWater: StepWaitingForWater(); break;
    case Phase::EnablingFeatures: StepEnablingFeatures(); break;
    case Phase::WaitingForGrass: StepWaitingForGrass(); break;
    case Phase::Idle:
    case Phase::OpeningTerrain:
    case Phase::Ready:
    case Phase::Failed: break;
    }
    return m_state.phase != before;
}

void BigWorldDemo::Tick(double nowSeconds)
{
    if (m_state.phase == Phase::Idle || m_state.phase == Phase::Failed) {
        return;
    }
    m_now = nowSeconds;
    const double interval = m_state.phase == Phase::Ready ? kPollReadySeconds : kPollWaitingSeconds;
    if (m_now - m_lastPoll < interval) {
        return;
    }
    m_lastPoll = m_now;
    Refresh(nowSeconds);
    if (!m_state.health.terrain.open) {
        LoseTerrain();
        return;
    }
    const Phase beforeSteps = m_state.phase;
    for (int i = 0; i < kMaxStepsPerTick && Step(); ++i) {
        if (m_state.phase == Phase::Failed || m_state.phase == Phase::Idle) {
            break;
        }
    }
    if (beforeSteps != Phase::Ready && m_state.phase == Phase::Ready) {
        CheckAgreement();
    }
    m_state.warnings = m_phaseWarnings;
    m_state.warnings.insert(m_state.warnings.end(), m_agreementWarnings.begin(), m_agreementWarnings.end());
    m_state.secondsInPhase = m_now - m_phaseStart;
}

std::string BigWorldStateToJson(const BigWorldState& s)
{
    const BigWorldHealth& h = s.health;
    std::string j = "{\"success\":true";
    j += ",\"phase\":" + Quote(PhaseName(s.phase));
    j += ",\"status\":" + Quote(s.status);
    j += ",\"error\":" + Quote(s.error);
    j += ",\"info\":" + Quote(s.info);
    j += std::string(",\"open\":") + Bool(s.phase != Phase::Idle && s.phase != Phase::Failed);
    j += std::string(",\"ready\":") + Bool(s.phase == Phase::Ready);
    j += ",\"secondsInPhase\":" + Number(s.secondsInPhase);
    j += ",\"secondsTotal\":" + Number(s.secondsTotal);
    j += ",\"quality\":" + Quote(QualityName(s.plan.quality));
    j += ",\"warnings\":[";
    for (std::size_t i = 0; i < s.warnings.size(); ++i) {
        j += (i ? "," : "") + Quote(s.warnings[i]);
    }
    j += "],\"waterBodyIds\":[";
    for (std::size_t i = 0; i < s.waterBodyIds.size(); ++i) {
        j += (i ? "," : "") + std::to_string(s.waterBodyIds[i]);
    }
    j += "],\"plan\":{\"spotX\":" + Number(s.plan.spotX) + ",\"spotZ\":" + Number(s.plan.spotZ)
        + ",\"heightAboveGround\":" + Number(s.plan.heightAboveGround) + ",\"headingDegrees\":" + Number(s.plan.headingDegrees)
        + ",\"lookDownDegrees\":" + Number(s.plan.lookDownDegrees) + ",\"terrainProfile\":" + Quote(s.plan.terrainProfile)
        + ",\"lake\":" + Bool(s.plan.lake)
        + ",\"river\":" + Bool(s.plan.river) + ",\"cloudCoverage\":" + Number(s.plan.cloudCoverage)
        + ",\"cloudShadowRangeKm\":" + Number(s.plan.cloudShadowRangeKm)
        + ",\"cloudShadowSoftness\":" + Number(s.plan.cloudShadowSoftness)
        + ",\"cloudSunRays\":" + Number(s.plan.cloudSunRays)
        + ",\"cloudSunRaysContrast\":" + Number(s.plan.cloudSunRaysContrast)
        + ",\"cloudSunRaysDecay\":" + Number(s.plan.cloudSunRaysDecay)
        + ",\"cloudSunRaysLength\":" + Number(s.plan.cloudSunRaysLength)
        + ",\"bloomThreshold\":" + Number(s.plan.bloomThreshold)
        + ",\"bloomIntensity\":" + Number(s.plan.bloomIntensity) + "}";
    j += std::string(",\"terrain\":{\"available\":") + Bool(h.terrain.available) + ",\"open\":" + Bool(h.terrain.open)
        + ",\"nodesResident\":" + std::to_string(h.terrain.nodesResident) + ",\"nodesPending\":" + std::to_string(h.terrain.nodesPending)
        + ",\"uploadQueue\":" + std::to_string(h.terrain.uploadQueue) + ",\"nodesRebuilding\":" + std::to_string(h.terrain.nodesRebuilding)
        + ",\"lastError\":" + Quote(h.terrain.lastError) + "}";
    j += std::string(",\"water\":{\"available\":") + Bool(h.water.available) + ",\"featureConnected\":" + Bool(h.water.featureConnected)
        + ",\"bodyCount\":" + std::to_string(h.water.bodyCount) + ",\"hasOcean\":" + Bool(h.water.hasOcean)
        + ",\"oceanLevel\":" + Number(h.water.oceanLevel) + ",\"carvePending\":" + Bool(h.water.carvePending)
        + ",\"carveApplies\":" + std::to_string(h.water.carveApplies) + ",\"lastError\":" + Quote(h.water.lastError) + "}";
    j += std::string(",\"grass\":{\"available\":") + Bool(h.grass.available) + ",\"status\":" + Quote(h.grass.status)
        + ",\"tilePresent\":" + Bool(h.grass.tilePresent) + ",\"seaLevel\":" + Number(h.grass.seaLevel)
        + ",\"tileRevision\":" + std::to_string(h.grass.tileRevision) + ",\"blades\":[" + std::to_string(h.grass.blades[0]) + ","
        + std::to_string(h.grass.blades[1]) + "," + std::to_string(h.grass.blades[2]) + "]"
        + ",\"receivesSunShadow\":" + Bool(h.grass.receivesSunShadow)
        + ",\"receivesCloudShadow\":" + Bool(h.grass.receivesCloudShadow)
        + ",\"castCascades\":" + std::to_string(h.grass.castCascades)
        + ",\"castBlades\":" + std::to_string(h.grass.castBlades) + "}";
    j += std::string(",\"clouds\":{\"available\":") + Bool(h.cloudsAvailable) + ",\"enabled\":" + Bool(h.cloudsEnabled) + "}";
    j += "}";
    return j;
}

} // namespace gte::bigworld
