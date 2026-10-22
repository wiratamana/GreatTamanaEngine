#pragma once

#include "AtmosphereTypes.h"
#include "../../Math/Vec3.h"

namespace gte {

// World-unit convention: 1 world unit = 1 meter, so 1000 world units = 1 kilometer, the unit every
// atmosphere quantity is expressed in.
inline constexpr float kWorldUnitsPerKilometer = 1000.0f;

// Aerial perspective strength ceiling. Keeps strength * skyIntensity and rgb * strength finite in float.
inline constexpr float kMaxAerialPerspectiveStrength = 1.0e30f;

// Earth-like atmosphere parameters (the struct's default member values).
AtmosphereParametersGpu MakeDefaultEarthAtmosphereParameters();

// Outer boundary radius of the atmosphere, measured from the planet center.
float AtmosphereRadiusKm(const AtmosphereParametersGpu& params) noexcept;

// Converts an engine world-space position (left-handed, Y-up) to atmosphere-space kilometers. It does not add
// the planet radius. A non-positive worldUnitsPerKm returns Vec3::Zero().
Vec3 WorldPositionToAtmosphereSpaceKm(Vec3 worldPosition, float worldUnitsPerKm = kWorldUnitsPerKilometer) noexcept;

// Clamps every field into its valid range. A non-finite value becomes that field's default.
// Settings are stored only through this function. Idempotent.
AtmosphereSettings SanitizeAtmosphereSettings(const AtmosphereSettings& settings) noexcept;

} // namespace gte
