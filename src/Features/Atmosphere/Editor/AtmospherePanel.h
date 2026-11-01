#pragma once

namespace gte {

struct AtmosphereSettings;
struct AtmosphereAerialSnapshot;

// Draws the Atmosphere settings into the current window. True when a value changed.
// `effective` is what the aerial perspective used last frame.
bool BuildAtmospherePanel(AtmosphereSettings& edit, const AtmosphereAerialSnapshot& effective);

} // namespace gte
