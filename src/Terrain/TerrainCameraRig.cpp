#include "TerrainCameraRig.h"

#include <algorithm>
#include <cmath>

namespace gte::terrain {

namespace {

constexpr double kPi = 3.14159265358979323846;

double Finite(double v, double fallback) noexcept
{
    return std::isfinite(v) ? v : fallback;
}

double GroundAt(const GroundHeightFn& ground, double x, double z)
{
    return ground ? Finite(ground(x, z), 0.0) : 0.0;
}

// Lowest allowed camera height at x, z: above the land and above the sea.
double FloorAt(const GroundHeightFn& ground, double x, double z, double clearance)
{
    return std::max(GroundAt(ground, x, z), 0.0) + clearance;
}

} // namespace

const char* CameraModeName(CameraMode mode) noexcept
{
    switch (mode) {
    case CameraMode::Script: return "script";
    case CameraMode::FreeFly: return "free";
    case CameraMode::Orbit: return "orbit";
    }
    return "script";
}

bool TryParseCameraMode(std::string_view name, CameraMode& out) noexcept
{
    for (const CameraMode mode : { CameraMode::Script, CameraMode::FreeFly, CameraMode::Orbit }) {
        if (name == CameraModeName(mode)) {
            out = mode;
            return true;
        }
    }
    return false;
}

void AccumulateInput(CameraInput& into, const CameraInput& frame) noexcept
{
    into.lookDeltaX += frame.lookDeltaX;
    into.lookDeltaY += frame.lookDeltaY;
    into.wheel += frame.wheel;
    into.moveForward = frame.moveForward;
    into.moveRight = frame.moveRight;
    into.moveUp = frame.moveUp;
    into.look = frame.look;
    into.boost = frame.boost;
}

void PoseBasis(const RigPose& pose, double forward[3], double right[3]) noexcept
{
    const double cp = std::cos(pose.pitchDown);
    forward[0] = std::sin(pose.yaw) * cp;
    forward[1] = -std::sin(pose.pitchDown);
    forward[2] = std::cos(pose.yaw) * cp;
    right[0] = std::cos(pose.yaw);
    right[1] = 0.0;
    right[2] = -std::sin(pose.yaw);
}

RigPose PoseFromForward(double x, double y, double z, double fx, double fy, double fz) noexcept
{
    RigPose pose;
    pose.x = x;
    pose.y = y;
    pose.z = z;
    const double horizontal = std::hypot(fx, fz);
    pose.yaw = horizontal > 1.0e-9 ? std::atan2(fx, fz) : 0.0;
    pose.pitchDown = std::atan2(-fy, std::max(horizontal, 1.0e-9));
    return pose;
}

void CameraRig::Place(const RigPose& pose) noexcept
{
    m_pose = pose;
    m_velocity[0] = m_velocity[1] = m_velocity[2] = 0.0;
}

void CameraRig::Begin(CameraMode mode, const RigPose& pose, const GroundHeightFn& ground)
{
    m_mode = mode;
    Place(pose);
    if (mode != CameraMode::Orbit) {
        return;
    }
    // The orbit centre is the point the camera looks at, estimated from its height and tilt.
    const double above = std::max(pose.y - GroundAt(ground, pose.x, pose.z), 10.0);
    const double distance = std::clamp(above / std::max(std::sin(pose.pitchDown), 0.25), 50.0, 30000.0);
    double forward[3];
    double right[3];
    PoseBasis(pose, forward, right);
    m_orbitDistance = distance;
    m_targetX = pose.x + forward[0] * distance;
    m_targetY = pose.y + forward[1] * distance;
    m_targetZ = pose.z + forward[2] * distance;
}

void CameraRig::SetOrbitTarget(double x, double z, double distance, const GroundHeightFn& ground)
{
    m_targetX = Finite(x, m_targetX);
    m_targetZ = Finite(z, m_targetZ);
    m_targetY = std::max(GroundAt(ground, m_targetX, m_targetZ), 0.0);
    m_orbitDistance = std::clamp(Finite(distance, m_orbitDistance), 10.0, 100000.0);
    m_pose.pitchDown = std::clamp(m_pose.pitchDown, 0.05, m_config.maxPitch);
    PlaceOnOrbit(ground);
}

void CameraRig::PlaceOnOrbit(const GroundHeightFn& ground) noexcept
{
    double forward[3];
    double right[3];
    PoseBasis(m_pose, forward, right);
    m_pose.x = m_targetX - forward[0] * m_orbitDistance;
    m_pose.y = m_targetY - forward[1] * m_orbitDistance;
    m_pose.z = m_targetZ - forward[2] * m_orbitDistance;
    m_pose.y = std::max(m_pose.y, FloorAt(ground, m_pose.x, m_pose.z, m_config.minClearance));
}

RigPose CameraRig::Advance(const CameraInput& input, double deltaSeconds, const GroundHeightFn& ground)
{
    const double dt = std::clamp(Finite(deltaSeconds, 0.0), 0.0, m_config.maxStepSeconds);
    if (m_mode == CameraMode::FreeFly) {
        AdvanceFreeFly(input, dt, ground);
    } else if (m_mode == CameraMode::Orbit) {
        AdvanceOrbit(input, dt, ground);
    }
    return m_pose;
}

void CameraRig::AdvanceFreeFly(const CameraInput& input, double dt, const GroundHeightFn& ground)
{
    if (input.look) {
        m_pose.yaw = std::remainder(m_pose.yaw + Finite(input.lookDeltaX, 0.0) * m_config.lookRadiansPerPixel, 2.0 * kPi);
        m_pose.pitchDown = std::clamp(
            m_pose.pitchDown + Finite(input.lookDeltaY, 0.0) * m_config.lookRadiansPerPixel, -m_config.maxPitch, m_config.maxPitch);
    }
    m_speedMultiplier = std::clamp(m_speedMultiplier * std::pow(1.25, Finite(input.wheel, 0.0)), 0.05, 50.0);

    const double above = std::max(m_pose.y - GroundAt(ground, m_pose.x, m_pose.z), 1.0);
    m_speed = std::clamp(above * m_config.speedPerMetreOfHeight, m_config.minSpeed, m_config.maxSpeed) * m_speedMultiplier
        * (input.boost ? m_config.boostFactor : 1.0);

    double forward[3];
    double right[3];
    PoseBasis(m_pose, forward, right);
    double wish[3];
    const double f = Finite(input.moveForward, 0.0);
    const double r = Finite(input.moveRight, 0.0);
    const double u = Finite(input.moveUp, 0.0);
    wish[0] = forward[0] * f + right[0] * r;
    wish[1] = forward[1] * f + u;
    wish[2] = forward[2] * f + right[2] * r;
    const double length = std::sqrt(wish[0] * wish[0] + wish[1] * wish[1] + wish[2] * wish[2]);
    const double scale = length > 1.0 ? m_speed / length : m_speed;

    // Exponential smoothing: the result does not depend on the frame rate.
    const double blend = 1.0 - std::exp(-m_config.accelerationRate * dt);
    for (int i = 0; i < 3; ++i) {
        m_velocity[i] += (wish[i] * scale - m_velocity[i]) * blend;
    }
    m_pose.x += m_velocity[0] * dt;
    m_pose.y += m_velocity[1] * dt;
    m_pose.z += m_velocity[2] * dt;

    const double floorY = FloorAt(ground, m_pose.x, m_pose.z, m_config.minClearance);
    if (m_pose.y < floorY) {
        m_pose.y = floorY;
        m_velocity[1] = std::max(m_velocity[1], 0.0);
    }
}

void CameraRig::AdvanceOrbit(const CameraInput& input, double dt, const GroundHeightFn& ground)
{
    if (input.look) {
        m_pose.yaw = std::remainder(m_pose.yaw + Finite(input.lookDeltaX, 0.0) * m_config.lookRadiansPerPixel, 2.0 * kPi);
        m_pose.pitchDown = std::clamp(
            m_pose.pitchDown + Finite(input.lookDeltaY, 0.0) * m_config.lookRadiansPerPixel, 0.05, m_config.maxPitch);
    }
    m_orbitDistance = std::clamp(m_orbitDistance * std::pow(0.85, Finite(input.wheel, 0.0)), 10.0, 100000.0);

    // Keys pan the orbit centre over the ground; the speed follows the zoom.
    m_speed = std::clamp(m_orbitDistance * 0.6, m_config.minSpeed, m_config.maxSpeed) * (input.boost ? m_config.boostFactor : 1.0);
    const double flatForwardX = std::sin(m_pose.yaw);
    const double flatForwardZ = std::cos(m_pose.yaw);
    const double wishX = flatForwardX * Finite(input.moveForward, 0.0) + std::cos(m_pose.yaw) * Finite(input.moveRight, 0.0);
    const double wishZ = flatForwardZ * Finite(input.moveForward, 0.0) - std::sin(m_pose.yaw) * Finite(input.moveRight, 0.0);
    const double length = std::hypot(wishX, wishZ);
    const double scale = length > 1.0 ? m_speed / length : m_speed;
    const double blend = 1.0 - std::exp(-m_config.accelerationRate * dt);
    m_velocity[0] += (wishX * scale - m_velocity[0]) * blend;
    m_velocity[2] += (wishZ * scale - m_velocity[2]) * blend;
    m_targetX += m_velocity[0] * dt;
    m_targetZ += m_velocity[2] * dt;
    m_targetY = std::max(GroundAt(ground, m_targetX, m_targetZ), 0.0);
    PlaceOnOrbit(ground);
}

} // namespace gte::terrain
