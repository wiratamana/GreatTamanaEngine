#include "FurinaShowcaseLayout.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

// Elevation angle of the screen position `fromTop` (0 top edge, 1 bottom edge) relative to the view axis.
double ScreenAngle(double fromTop, double tanHalfFov) noexcept
{
    return std::atan((1.0 - 2.0 * fromTop) * tanHalfFov);
}

} // namespace

float ResolveFurinaHipHeight(float minY, float maxY, float hipY) noexcept
{
    if (std::isfinite(hipY) && hipY > minY && hipY < maxY) {
        return hipY;
    }
    return minY + kFurinaShowcaseFallbackHipFraction * (maxY - minY);
}

FurinaShowcaseLayout ComputeFurinaShowcaseLayout(const FurinaShowcaseModel& model) noexcept
{
    FurinaShowcaseLayout out;
    if (!std::isfinite(model.minY) || !std::isfinite(model.maxY) || !(model.maxY - model.minY > 1.0e-4f)) {
        return out;
    }

    const double top = model.maxY;
    const double hips = ResolveFurinaHipHeight(model.minY, model.maxY, model.hipY);
    const double tanHalfFov = std::tan(kFurinaShowcaseFovYDegrees * kDegToRad * 0.5);
    const double pitchUp = kFurinaShowcaseCameraPitchUpDegrees * kDegToRad;

    // Both points sit at their screen rows. With tan(a) = (y - cameraY) / distance, subtracting the two
    // equations gives the distance, and either one then gives the camera height.
    const double tanTop = std::tan(pitchUp + ScreenAngle(kFurinaShowcaseHatTopFromTop, tanHalfFov));
    const double tanHips = std::tan(pitchUp + ScreenAngle(kFurinaShowcaseHipsFromTop, tanHalfFov));
    const double distance = (top - hips) / (tanTop - tanHips);
    const double cameraY = top - distance * tanTop;
    if (!std::isfinite(distance) || !std::isfinite(cameraY) || distance <= 0.0) {
        return out;
    }

    out.cameraDistance = static_cast<float>(distance);
    out.cameraPosition = Vec3{ 0.0f, static_cast<float>(cameraY), -out.cameraDistance };
    out.cameraPitchEulerDegrees = -kFurinaShowcaseCameraPitchUpDegrees;
    out.nearZ = std::max(0.01f, 0.05f * out.cameraDistance);
    out.farZ = std::max(1000.0f, 100.0f * out.cameraDistance);

    // Light travels away from the sun: down by the elevation, and toward +X so the sun sits on the camera's left.
    out.sunPitchEulerDegrees = kFurinaShowcaseSunElevationDegrees;
    out.sunYawEulerDegrees = kFurinaShowcaseSunLeftOfCameraDegrees;
    out.valid = true;
    return out;
}

} // namespace gte
