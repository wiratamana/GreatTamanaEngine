#pragma once

// Interactive camera of the terrain scene: free-fly and orbit. Pure maths, frame-rate independent,
// no input or engine types (the editor turns keys and mouse into CameraInput).

#include "TerrainFlight.h"

#include <cstdint>
#include <string_view>

namespace gte::terrain {

// Script: the camera is left to the scripted flight (or stays where it is).
enum class CameraMode : std::uint8_t { Script, FreeFly, Orbit };

const char* CameraModeName(CameraMode mode) noexcept;
bool TryParseCameraMode(std::string_view name, CameraMode& out) noexcept;

// One frame of intent. Move axes are -1..1; look deltas are mouse pixels summed over the frame.
struct CameraInput {
    float moveForward = 0.0f;
    float moveRight = 0.0f;
    float moveUp = 0.0f;
    float lookDeltaX = 0.0f;
    float lookDeltaY = 0.0f;
    float wheel = 0.0f; // Notches; positive speeds up (free-fly) or zooms in (orbit).
    bool look = false;  // Look or orbit drag is held.
    bool boost = false;
};

// Adds the look and wheel sums, keeps the latest axes and flags.
void AccumulateInput(CameraInput& into, const CameraInput& frame) noexcept;

struct RigPose {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double yaw = 0.0;       // Radians about +Y; 0 looks along +Z.
    double pitchDown = 0.0; // Radians; positive looks toward the ground.
};

struct RigConfig {
    double lookRadiansPerPixel = 0.0025;
    double speedPerMetreOfHeight = 0.8; // Base free-fly speed grows with the height above ground.
    double minSpeed = 6.0;
    double maxSpeed = 3500.0;
    double boostFactor = 4.0;
    double accelerationRate = 9.0;      // 1/s of the velocity smoothing.
    double minClearance = 3.0;          // Metres above ground (and above sea level).
    double maxPitch = 1.50;             // Radians, free-fly looks up and down to this.
    double maxStepSeconds = 0.1;        // A long frame never throws the camera far.
};

// Unit vectors of a pose (left-handed, Y up): forward looks along the heading tilted down by the pitch.
void PoseBasis(const RigPose& pose, double forward[3], double right[3]) noexcept;

// Pose that looks along (fx, fy, fz) from (x, y, z).
RigPose PoseFromForward(double x, double y, double z, double fx, double fy, double fz) noexcept;

class CameraRig {
public:
    explicit CameraRig(const RigConfig& config = {}) : m_config(config) {}

    CameraMode Mode() const noexcept { return m_mode; }
    const RigPose& Pose() const noexcept { return m_pose; }
    const RigConfig& Config() const noexcept { return m_config; }

    // Takes over from `pose` (current camera). Orbit picks a target on the view ray.
    void Begin(CameraMode mode, const RigPose& pose, const GroundHeightFn& ground);
    void Place(const RigPose& pose) noexcept;
    void SetOrbitTarget(double x, double z, double distance, const GroundHeightFn& ground);

    // Moves the pose by one frame of input and returns it. Script mode returns the pose unchanged.
    RigPose Advance(const CameraInput& input, double deltaSeconds, const GroundHeightFn& ground);

    double SpeedMultiplier() const noexcept { return m_speedMultiplier; }
    double Speed() const noexcept { return m_speed; }  // Metres per second of the last frame.
    double OrbitDistance() const noexcept { return m_orbitDistance; }
    double TargetX() const noexcept { return m_targetX; }
    double TargetY() const noexcept { return m_targetY; }
    double TargetZ() const noexcept { return m_targetZ; }

private:
    void AdvanceFreeFly(const CameraInput& input, double dt, const GroundHeightFn& ground);
    void AdvanceOrbit(const CameraInput& input, double dt, const GroundHeightFn& ground);
    void PlaceOnOrbit(const GroundHeightFn& ground) noexcept;

    RigConfig m_config;
    CameraMode m_mode = CameraMode::Script;
    RigPose m_pose;
    double m_velocity[3] = { 0.0, 0.0, 0.0 };
    double m_speedMultiplier = 1.0;
    double m_speed = 0.0;
    double m_orbitDistance = 500.0;
    double m_targetX = 0.0;
    double m_targetY = 0.0;
    double m_targetZ = 0.0;
};

} // namespace gte::terrain
