#pragma once

#include <cstdint>

namespace gte {

// Shadow feature tunables - plain data, no behavior. Always run through
// ShadowMath::SanitizeShadowSettings() before use; never read a raw field
// directly inside a render-graph pass body.
struct ShadowSettings {
    // Locked to whatever value this holds on the first frame Shadow.DepthPass
    // runs - the shadow map's render view cannot be resized later. Do not
    // expose a live Inspector slider for this field.
    std::uint32_t mapResolution = 2048;

    float orthoHalfExtentWorld = 20.0f; // Half-size of the light frustum box, world units.
    float nearZ = 0.1f;
    float farZ = 200.0f;
    float depthBias = 0.0025f;
    float strength = 0.6f; // 0 = no darkening, 1 = fully black in shadow.
};

} // namespace gte
