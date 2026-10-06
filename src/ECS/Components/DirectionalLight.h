#pragma once

#include "../../Math/Vec3.h"

namespace gte {

// A directional light (sun). Plain data, same shape as Camera (ECS/Components/Camera.h):
// no direction field of its own - derived from its own entity's Transform rotation via
// Game/Lighting/DirectionalLightResolver.h's ResolveActiveDirectionalLight().
//
// Direction TOWARD the sun is the NEGATION of the entity's forward vector
// (transform.rotation.RotateVector(Vec3::Forward())) - get this sign wrong and the sky
// brightens on the wrong side of the world. See DirectionalLightResolver.h/.cpp.
struct DirectionalLight {
    // Linear color, PRE-intensity - illuminanceLux below is the actual brightness.
    Vec3 color = Vec3::One();

    // Illuminance in lux (100,000 ~= full daylight). Converted to shader-space
    // intensity by ResolveActiveDirectionalLight(), never inside this component.
    float illuminanceLux = 100000.0f;

    // First active DirectionalLight (ComponentStorage order) wins; every other one
    // this frame is ignored, mirroring Camera::active.
    bool active = true;
};

} // namespace gte
