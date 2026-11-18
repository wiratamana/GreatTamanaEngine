#pragma once

// Orchestrator of the Big World demo. It opens the terrain, adds water on it, switches the sky and grass
// features on, and keeps the three systems in agreement. All work is spread over frames; nothing waits.

#include "BigWorldPlan.h"
#include "BigWorldPorts.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte::bigworld {

enum class Phase {
    Idle,
    OpeningTerrain,
    WaitingForNodes,
    AddingWater,
    WaitingForWater,
    EnablingFeatures,
    WaitingForGrass,
    Ready,
    Failed,
};

const char* PhaseName(Phase phase) noexcept;

// Expected end of a demo whose scene another scene replaced; hosts may log it below warning level.
inline constexpr const char* kSceneReplacedNote = "the terrain scene was replaced";

// Above this camera ground altitude (metres), SDSM cascade texels exceed the grass shadow limit by design.
inline constexpr double kGrassShadowCastMaxAltitude = 15.0;

struct BigWorldHealth {
    TerrainFacts terrain;
    WaterFacts water;
    GrassHealth grass;
    bool cloudsAvailable = false;
    bool cloudsEnabled = false;
    bool sunAvailable = false;
    bool sunAboveHorizon = true;
    double sunElevationDegrees = 0.0;
};

struct BigWorldState {
    Phase phase = Phase::Idle;
    std::string status = "closed";
    std::string error;                   // Why the last open failed.
    std::string info;                    // Benign state note (for example camera above grass shadow cast range).
    std::vector<std::string> warnings;   // Things that work but look wrong.
    BigWorldPlan plan;
    BigWorldHealth health;
    std::vector<std::uint32_t> waterBodyIds; // Bodies the demo made; Close() removes them.
    bool terrainOwned = false;
    double secondsInPhase = 0.0;
    double secondsTotal = 0.0;
};

class BigWorldDemo {
public:
    explicit BigWorldDemo(BigWorldPorts ports) noexcept : m_ports(ports) {}

    // Starts the demo, or restarts it when it is open. The terrain opens now (one frame of work); the rest
    // runs in Tick(). Fails at once when a port is missing or the terrain refuses.
    PortResult Open(const BigWorldPlan& plan, double nowSeconds);

    // Undoes everything the demo changed: feature settings, water bodies it made, the terrain it opened.
    PortResult Close();

    // Moves the state machine on; cheap when nothing is due.
    void Tick(double nowSeconds);

    // Reads fresh facts now, for a state report.
    void Refresh(double nowSeconds);

    const BigWorldState& State() const noexcept { return m_state; }
    bool Active() const noexcept { return m_state.phase != Phase::Idle && m_state.phase != Phase::Failed; }
    bool Ready() const noexcept { return m_state.phase == Phase::Ready; }

private:
    bool PortsPresent(std::string& missing) const;
    void Enter(Phase phase, std::string status);
    void Fail(const std::string& message);
    void Cleanup();
    void ReadHealth();
    void CheckAgreement();
    bool Step();
    void StepWaitingForNodes();
    void StepAddingWater();
    void StepWaitingForWater();
    void StepEnablingFeatures();
    void StepWaitingForGrass();
    bool PhaseTimedOut(double limitSeconds) const noexcept;
    void LoseTerrain();

    BigWorldPorts m_ports;
    BigWorldState m_state;
    std::vector<std::string> m_phaseWarnings;
    std::vector<std::string> m_agreementWarnings;
    double m_now = 0.0;
    double m_phaseStart = 0.0;
    double m_started = 0.0;
    double m_lastPoll = -1e9;
    int m_carveAppliesBefore = 0;
    bool m_waterAdded = false;
};

// The state as one JSON object (numbers, bools and strings only), for the HTTP route.
std::string BigWorldStateToJson(const BigWorldState& state);

} // namespace gte::bigworld
