#pragma once

#include "../Math/Vec3.h"

// Pure layout of the Hi-Z demo scene: a flat field of grid instances, a camera in front of it and a wall
// between them that hides the whole field. No registry, no GPU.

#include <cstdint>
#include <optional>
#include <string>

namespace gte {

inline constexpr std::uint32_t kHizDemoDefaultCount = 50000;
inline constexpr float kHizDemoDefaultSpacing = 2.0f;

// Aspect the field is fitted for, and the widest aspect the wall still covers.
inline constexpr float kHizDemoReferenceAspect = 16.0f / 9.0f;
inline constexpr float kHizDemoMaxCoveredAspect = 2.5f;

// Half size of one grid instance (a unit quad sheet) and the depth the field is assumed to span.
inline constexpr float kHizDemoInstanceHalfSize = 0.5f;
inline constexpr float kHizDemoFieldDepth = 1.0f;

// Camera distance bounds: the wall must stay clear of the near plane, and the layout must stay finite.
inline constexpr float kHizDemoMinCameraDistance = 4.0f;
inline constexpr float kHizDemoMaxCameraDistance = 1.0e6f;

inline constexpr float kHizDemoWallDepth = 0.2f;

struct HizDemoParameters {
    bool valid = false;
    std::string error;   // Set when !valid.
    std::string message; // "count clamped to N" when the count was changed, else empty.
    std::uint32_t count = kHizDemoDefaultCount;
    float spacing = kHizDemoDefaultSpacing;
    bool countClamped = false;
};

// Defaults for unset values; count clamped to 1..kMaxSpawnGridInstances; spacing outside the grid
// spawner's range (or not finite) is invalid.
HizDemoParameters SanitizeHizDemoParameters(std::optional<std::uint32_t> count, std::optional<float> spacing);

struct HizDemoLayout {
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    Vec3 fieldCentre;
    Vec3 fieldHalfExtent;     // Includes the instance size; z is 0.
    float cameraDistance = 0.0f;
    Vec3 cameraPosition;      // Rotation is identity: the camera looks along +Z.
    float cameraFarZ = 0.0f;
    Vec3 wallPosition;
    Vec3 wallScale;
    float fieldFractionInView = 0.0f; // Share of the field area inside the frustum at the reference aspect.
};

// `count` and `spacing` must already be sanitized. `farZ` is the camera's default far plane.
HizDemoLayout ComputeHizDemoLayout(
    std::uint32_t count, float spacing, const Vec3& fieldCentre, float fovYDegrees, float farZ);

} // namespace gte
