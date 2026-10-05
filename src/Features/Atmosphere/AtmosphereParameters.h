#pragma once

#include "AtmosphereTypes.h"
#include "../../Math/Vec3.h"

namespace gte {

// This engine's world-unit convention, decided and documented here per this
// phase's own strategy document (Step 3.4): 1 world unit = 1 meter, matching
// PrimitiveMeshGenerator's own unit-size built-in shapes (a 1x1x1 Cube, a
// 0.5-radius Sphere - see src/Renderer/Primitives/PrimitiveMeshGenerator.h)
// exactly the way Unity's own default primitive sizes assume "1 unit = 1
// meter" too. 1000 world units (meters) == 1 kilometer, the unit every
// AtmosphereParametersGpu/AtmosphereMath.h quantity is expressed in.
inline constexpr float kWorldUnitsPerKilometer = 1000.0f;

// Returns AtmosphereParametersGpu populated with Earth-like defaults,
// transcribed verbatim from the cloned reference implementation
// (hoffstadt/pl-sky, _reference/pl-sky/src/app.c) - see
// task_manager/atmosphere-scattering-1/ATMOSPHERE_REFERENCE_NOTES.md,
// Section 1, for the exact citation of every value against the real
// reference source. AtmosphereParametersGpu's own default member
// initializers already carry these exact numbers (so a plain
// `AtmosphereParametersGpu{}` is already Earth-like) - this function exists
// as the one explicit, discoverable call site every later phase should call
// rather than silently relying on default-construction, and as the natural
// extension point for a future "load a different planet's atmosphere"
// variant.
AtmosphereParametersGpu MakeDefaultEarthAtmosphereParameters();

// The atmosphere's outer boundary radius from the planet's center - derived
// from planetRadiusKm + atmosphereThicknessKm (see AtmosphereTypes.h's own
// "DEVIATION FROM THIS PHASE'S OWN STRATEGY DOCUMENT" note on why the struct
// itself stores thickness, not this derived radius, directly).
float AtmosphereRadiusKm(const AtmosphereParametersGpu& params) noexcept;

// Converts a raw engine world-space position (this engine's own coordinate
// convention - see src/Math/MathTypes.h - left-handed, Y-up, Z-forward) into
// ATMOSPHERE-SPACE kilometers, per the kWorldUnitsPerKilometer convention
// above. Deliberately does NOT also fold in planetRadiusKm/an "up" offset
// here - composing "how far above the virtual planet surface is the camera,
// and in which direction is up" is AtmosphereFrameUniforms' job (Phase 5),
// which needs a per-scene notion of where the planet's surface/center
// actually sit that does not exist anywhere in the engine yet. A
// non-positive worldUnitsPerKm degrades to Vec3::Zero() rather than dividing
// by zero/producing NaN or Inf, matching this codebase's own
// "degrade gracefully" convention (see AGENTS.md).
Vec3 WorldPositionToAtmosphereSpaceKm(Vec3 worldPosition, float worldUnitsPerKm = kWorldUnitsPerKilometer) noexcept;

} // namespace gte
