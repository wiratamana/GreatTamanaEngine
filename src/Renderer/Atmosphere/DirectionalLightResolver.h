#pragma once

#include "../../ECS/Registry.h"
#include "../../Math/Vec3.h"

namespace gte {

// Atmosphere Scattering + Aerial Perspective campaign, Phase 8
// (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md)
// - the ECS/Components/DirectionalLight.h resolution helper, mirroring
// RenderSystem::ResolveActiveCameraViewProjection()'s exact "first active
// one, in ComponentStorage order, ignore the rest, fall back to a sane
// default if none exists" pattern (see RenderSystem.h/.cpp). Deliberately
// NOT a method on RenderSystem itself - this campaign's atmosphere code is
// its own module (see AtmosphereLutRenderer.h's own class comment) - and
// deliberately Vulkan-header-free (only <Registry.h>/<Vec3.h>), so this
// stays genuinely Tier-1-testable with nothing but a Registry (see
// tests/Renderer/Atmosphere/DirectionalLightResolverTests.cpp), same
// precedent as AtmosphereMath.h/GpuTiming.h.
struct ResolvedDirectionalLight {
    // Normalized direction TOWARD the sun (the vector AtmosphereFrameUniforms::
    // sunDirection actually wants) - the negation of the resolved entity's
    // own forward vector (see DirectionalLight.h's own doc comment for why).
    Vec3 directionTowardSun = Vec3::Up();

    // Ready-to-use shader-space sun intensity (DirectionalLight::color *
    // a lux -> shader-scale conversion) - the unit conversion happens HERE,
    // inside this resolution helper, never inside DirectionalLight itself
    // (mirrors Camera::fovYDegrees's own "human units at the edge, internal
    // units at the point of use" convention - see Camera.h).
    Vec3 sunIlluminance = Vec3::One();
};

// Resolves the first entity (in ComponentStorage<DirectionalLight> order)
// with DirectionalLight::active == true, walking its FULL WORLD transform
// (ECS/TransformHierarchy.h's ComputeWorldTransform() - correct even for a
// parented Sun) to derive its direction. Falls back to the exact same
// hardcoded placeholder Phase 5 originally used (a fixed 45-degree-
// elevation sun, azimuth 0, warm-tinted color) when the Registry has no
// active DirectionalLight at all - never a crash/exception; a scene with
// no Sun entity must still render a plausible-looking sky, exactly as it
// always has.
ResolvedDirectionalLight ResolveActiveDirectionalLight(Registry& registry);

} // namespace gte
