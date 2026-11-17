#include "TerrainFlight.h"

#include <algorithm>
#include <cmath>

namespace gte::terrain {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHeightFollowRate = 2.0; // 1/s of the exponential height filter.
constexpr double kLookAheadSeconds[3] = { 0.5, 1.5, 3.0 };

double FiniteOr(double v, double fallback) noexcept
{
    return std::isfinite(v) ? v : fallback;
}

} // namespace

Quat FlightRotation(const FlightPose& pose) noexcept
{
    constexpr float kRadToDeg = static_cast<float>(180.0 / kPi);
    return Quat::FromEulerDegrees(pose.pitchDownRadians * kRadToDeg, pose.yawRadians * kRadToDeg, 0.0f);
}

FlightSpec SanitizeFlight(const FlightSpec& in) noexcept
{
    FlightSpec s = in;
    s.centerX = FiniteOr(s.centerX, 0.0);
    s.centerZ = FiniteOr(s.centerZ, 0.0);
    s.radius = std::clamp(FiniteOr(s.radius, 3000.0), 50.0, 1.0e6);
    s.headingDegrees = std::fmod(FiniteOr(s.headingDegrees, 0.0), 360.0);
    s.speed = std::clamp(FiniteOr(s.speed, 120.0), 1.0, 5000.0);
    s.altitudeAboveGround = std::clamp(FiniteOr(s.altitudeAboveGround, 250.0), 2.0, 20000.0);
    s.minAltitude = std::clamp(FiniteOr(s.minAltitude, 40.0), 0.0, 20000.0);
    s.lookDownDegrees = std::clamp(FiniteOr(s.lookDownDegrees, 10.0), -80.0, 80.0);
    s.fixedStepSeconds = std::clamp(FiniteOr(s.fixedStepSeconds, 0.0), 0.0, 1.0);
    s.durationSeconds = std::clamp(FiniteOr(s.durationSeconds, 0.0), 0.0, 1.0e7);
    return s;
}

PathSample SamplePath(const FlightSpec& spec, double travelled) noexcept
{
    PathSample p;
    if (spec.kind == FlightKind::Orbit) {
        const double angle = travelled / spec.radius;
        p.x = spec.centerX + spec.radius * std::sin(angle);
        p.z = spec.centerZ + spec.radius * std::cos(angle);
        p.headingX = std::cos(angle);
        p.headingZ = -std::sin(angle);
    } else {
        const double h = spec.headingDegrees * kPi / 180.0;
        p.headingX = std::sin(h);
        p.headingZ = std::cos(h);
        p.x = spec.centerX + p.headingX * travelled;
        p.z = spec.centerZ + p.headingZ * travelled;
    }
    return p;
}

void FlightController::Start(const FlightSpec& spec) noexcept
{
    m_spec = SanitizeFlight(spec);
    m_active = true;
    m_haveHeight = false;
    m_elapsed = 0.0;
    m_last = FlightPose{};
}

FlightPose FlightController::Advance(double realDeltaSeconds, const GroundHeightFn& ground)
{
    if (!m_active) {
        return m_last;
    }
    const double dt = m_spec.fixedStepSeconds > 0.0 ? m_spec.fixedStepSeconds : std::max(0.0, FiniteOr(realDeltaSeconds, 0.0));
    m_elapsed += dt;
    if (m_spec.durationSeconds > 0.0 && m_elapsed >= m_spec.durationSeconds) {
        m_elapsed = m_spec.durationSeconds;
        m_active = false;
        m_last.finished = true;
    }

    const double travelled = m_elapsed * m_spec.speed;
    const PathSample here = SamplePath(m_spec, travelled);

    // Highest ground under the current position and a few seconds ahead, so the camera climbs before a ridge.
    double groundMax = ground ? ground(here.x, here.z) : 0.0;
    for (const double ahead : kLookAheadSeconds) {
        const PathSample p = SamplePath(m_spec, travelled + ahead * m_spec.speed);
        groundMax = std::max(groundMax, ground ? ground(p.x, p.z) : 0.0);
    }
    const double target = std::max(groundMax, 0.0) + m_spec.altitudeAboveGround;
    const double floorY = m_spec.minAltitude;
    if (!m_haveHeight) {
        m_height = std::max(target, floorY);
        m_haveHeight = true;
    } else {
        m_height += (target - m_height) * (1.0 - std::exp(-kHeightFollowRate * dt));
        m_height = std::max(m_height, floorY);
    }

    m_last.x = here.x;
    m_last.z = here.z;
    m_last.y = m_height;
    m_last.yawRadians = static_cast<float>(std::atan2(here.headingX, here.headingZ));
    m_last.pitchDownRadians = static_cast<float>(m_spec.lookDownDegrees * kPi / 180.0);
    return m_last;
}

} // namespace gte::terrain
