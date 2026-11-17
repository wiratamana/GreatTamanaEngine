#pragma once

// Scripted camera flight over the world: a pure function of elapsed time, so benchmark runs repeat exactly.

#include "../../Math/Quat.h"

#include <functional>

namespace gte::terrain {

enum class FlightKind { Orbit, Line };

struct FlightSpec {
    FlightKind kind = FlightKind::Orbit;
    double centerX = 0.0;          // Orbit centre, or the line start (metres).
    double centerZ = 0.0;
    double radius = 3000.0;        // Orbit only.
    double headingDegrees = 0.0;   // Line only: 0 looks along +Z, 90 along +X.
    double speed = 120.0;          // Metres per second along the path.
    double altitudeAboveGround = 250.0;
    double minAltitude = 40.0;     // The camera never goes below this height above sea level.
    double lookDownDegrees = 10.0; // Positive looks toward the ground.
    double fixedStepSeconds = 0.0; // Above 0, every Advance() moves time by exactly this (frame-rate independent).
    double durationSeconds = 0.0;  // 0 = endless.
};

struct FlightPose {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yawRadians = 0.0f;       // Rotation about +Y; 0 looks along +Z.
    float pitchDownRadians = 0.0f; // Positive looks toward the ground.
    bool finished = false;
};

// Ground height in metres at a world position.
using GroundHeightFn = std::function<double(double x, double z)>;

// Camera rotation that looks along the pose heading, tilted down by the pose pitch.
Quat FlightRotation(const FlightPose& pose) noexcept;

// Sanitizes a spec: finite values, positive speed and radius, bounded angles.
FlightSpec SanitizeFlight(const FlightSpec& spec) noexcept;

// Horizontal path position and heading at distance `travelled` along the path.
struct PathSample {
    double x = 0.0;
    double z = 0.0;
    double headingX = 0.0; // Unit direction of travel.
    double headingZ = 1.0;
};
PathSample SamplePath(const FlightSpec& spec, double travelled) noexcept;

class FlightController {
public:
    void Start(const FlightSpec& spec) noexcept;
    void Stop() noexcept { m_active = false; }
    bool Active() const noexcept { return m_active; }
    double Elapsed() const noexcept { return m_elapsed; }
    const FlightSpec& Spec() const noexcept { return m_spec; }

    // Moves time on by `realDeltaSeconds` (or the fixed step) and returns the new pose. Inactive: last pose.
    FlightPose Advance(double realDeltaSeconds, const GroundHeightFn& ground);

private:
    FlightSpec m_spec;
    bool m_active = false;
    bool m_haveHeight = false;
    double m_elapsed = 0.0;
    double m_height = 0.0;
    FlightPose m_last;
};

} // namespace gte::terrain
