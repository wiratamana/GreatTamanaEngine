#include "HizDemoLayout.h"

#include "../Core/InstanceGridLimits.h"

#include <algorithm>
#include <cmath>

namespace gte {

HizDemoParameters SanitizeHizDemoParameters(std::optional<std::uint32_t> count, std::optional<float> spacing)
{
    HizDemoParameters out;
    const float requestedSpacing = spacing.value_or(kHizDemoDefaultSpacing);
    if (!std::isfinite(requestedSpacing) || requestedSpacing < kMinSpawnGridSpacing
        || requestedSpacing > kMaxSpawnGridSpacing) {
        out.error = "spacing must be a number from 0.1 to 1000";
        return out;
    }
    const std::uint32_t requestedCount = count.value_or(kHizDemoDefaultCount);
    out.count = std::clamp<std::uint32_t>(requestedCount, 1u, kMaxSpawnGridInstances);
    out.countClamped = out.count != requestedCount;
    if (out.countClamped) {
        out.message = "count clamped to " + std::to_string(out.count);
    }
    out.spacing = requestedSpacing;
    out.valid = true;
    return out;
}

HizDemoLayout ComputeHizDemoLayout(
    std::uint32_t count, float spacing, const Vec3& fieldCentre, float fovYDegrees, float farZ)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kFitMargin = 1.1;
    constexpr double kWallDistanceFraction = 0.25;
    constexpr double kWallCoverMargin = 1.2;

    const std::uint32_t safeCount = std::max<std::uint32_t>(count, 1u);
    const double fovDegrees = (std::isfinite(fovYDegrees) && fovYDegrees > 1.0f && fovYDegrees < 170.0f) ? fovYDegrees : 60.0;
    const double tanHalfFov = std::tan(fovDegrees * kPi / 360.0);

    HizDemoLayout out;
    out.columns = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(safeCount))));
    out.rows = (safeCount + out.columns - 1) / out.columns;
    out.fieldCentre = fieldCentre;

    const double halfX = static_cast<double>(spacing) * (out.columns - 1) * 0.5 + kHizDemoInstanceHalfSize;
    const double halfY = static_cast<double>(spacing) * (out.rows - 1) * 0.5 + kHizDemoInstanceHalfSize;
    out.fieldHalfExtent = Vec3{ static_cast<float>(halfX), static_cast<float>(halfY), 0.0f };

    const double fitted = kFitMargin * std::max(halfY, halfX / kHizDemoReferenceAspect) / tanHalfFov;
    const double distance = std::clamp<double>(fitted, kHizDemoMinCameraDistance, kHizDemoMaxCameraDistance);
    out.cameraDistance = static_cast<float>(distance);
    out.cameraPosition = Vec3{ fieldCentre.x, fieldCentre.y, fieldCentre.z - out.cameraDistance };
    out.cameraFarZ = std::max(farZ, static_cast<float>(kFitMargin * (distance + kHizDemoFieldDepth)));

    const double wallDistance = kWallDistanceFraction * distance;
    const double wallHeight = 2.0 * wallDistance * tanHalfFov * kWallCoverMargin;
    const double wallWidth = wallHeight * kHizDemoMaxCoveredAspect;
    out.wallPosition = Vec3{ fieldCentre.x, fieldCentre.y, out.cameraPosition.z + static_cast<float>(wallDistance) };
    out.wallScale = Vec3{ static_cast<float>(wallWidth), static_cast<float>(wallHeight), kHizDemoWallDepth };

    const double visibleHalfY = distance * tanHalfFov;
    const double visibleHalfX = visibleHalfY * kHizDemoReferenceAspect;
    out.fieldFractionInView = static_cast<float>(
        std::min(halfX, visibleHalfX) / halfX * (std::min(halfY, visibleHalfY) / halfY));
    return out;
}

} // namespace gte
