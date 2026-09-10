#pragma once

#include "../../Math/Vec3.h"

namespace gte {

// Atmosphere Scattering + Aerial Perspective campaign, Phase 8
// (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md)
// - the engine's first light component. Mirrors Camera's own shape exactly
// (see ECS/Components/Camera.h): plain data, no behavior beyond what a
// small pure-math resolution helper needs, an `active` bool with the same
// "first active one wins, ComponentStorage order" resolution convention
// (see Renderer/Atmosphere/DirectionalLightResolver.h's
// ResolveActiveDirectionalLight()).
//
// Direction is DELIBERATELY NOT a field here - exactly like Camera never
// stores its own eye/target/up, this component is meant to sit on a
// Transform-bearing entity ("Sun") and have its direction derived from
// that Transform's own rotation, the same "edited exactly like any other
// entity" philosophy Camera already established. The entity's forward
// vector (`transform.rotation.RotateVector(Vec3::Forward())`) is the
// direction the sun's light actually TRAVELS IN (i.e. shines toward);
// AtmosphereFrameUniforms::sunDirection instead wants the direction
// TOWARD the sun (the vector a viewer on the ground would look along to
// see it) - simply the NEGATION of that forward vector. Get this sign
// wrong and the sky brightens on the wrong side of the world from where
// the Sun entity visually points - see
// DirectionalLightResolver.h/.cpp and its own Tier-1 test for the exact
// resolution and a worked, non-trivial-rotation regression check.
struct DirectionalLight {
    // Linear color, PRE-intensity (illuminanceLux below is the actual
    // brightness multiplier) - matches Camera::fovYDegrees's own "human-
    // facing edge of the API" philosophy: this stays a plain tint, never
    // baked-in intensity, so a user can change color/brightness
    // independently in the Inspector.
    Vec3 color = Vec3::One();

    // Illuminance in LUX - a physically-plausible full-daylight default
    // (real-world direct sunlight is roughly 100,000 lux). Converted into
    // AtmosphereFrameUniforms::sunIlluminance's own shader-space intensity
    // units by DirectionalLightResolver.h's ResolveActiveDirectionalLight()
    // - never inside this component itself, mirroring
    // Camera::fovYDegrees's own "degrees at the edges, radians internally"
    // convention (see Camera.h).
    float illuminanceLux = 100000.0f;

    // Only one DirectionalLight is ever "the" active sun a frame's
    // atmosphere resolves through - DirectionalLightResolver picks the
    // first entity (in ComponentStorage<DirectionalLight> order) with
    // active == true, and ignores every other one in the Registry that
    // frame, exactly mirroring Camera::active's own documented "first
    // active wins" rule (see Camera.h and RenderSystem::
    // ResolveActiveCameraViewProjection()).
    bool active = true;
};

} // namespace gte
