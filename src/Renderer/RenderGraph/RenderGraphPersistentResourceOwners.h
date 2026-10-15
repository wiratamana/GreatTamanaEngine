#pragma once

// Owner identifiers for persistent textures requested by built-in engine features.
// Plugin and project features use their own descriptor name instead.

namespace gte::rg {

// Atmosphere LUT passes (AtmosphereLutRendererImpl.cpp) - shared GPU color textures reused every frame.
inline constexpr const char* kPersistentOwnerAtmosphere = "Atmosphere";

} // namespace gte::rg
