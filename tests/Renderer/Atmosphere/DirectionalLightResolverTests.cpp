// Unit tests for the Atmosphere Scattering + Aerial Perspective campaign's
// Phase 8 DirectionalLight resolution helper
// (src/Renderer/Atmosphere/DirectionalLightResolver.h/.cpp) - mirrors
// tests/Game/RenderSystemTests.cpp's own
// ResolveActiveCameraViewProjection() tests exactly: a Registry-only test,
// no Renderer/GPU/live Vulkan device involved at all.

#include "Renderer/Atmosphere/DirectionalLightResolver.h"

#include "ECS/Components/DirectionalLight.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "ECS/TransformHierarchy.h"
#include "Math/Quat.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(DirectionalLightResolverTest, EmptyRegistryFallsBackToThePlaceholderSun)
{
    Registry registry;

    const ResolvedDirectionalLight resolved = ResolveActiveDirectionalLight(registry);

    const Vec3 expectedDirection = Normalize(Vec3(0.70710678f, 0.70710678f, 0.0f));
    const Vec3 expectedIlluminance = Vec3(1.0f, 0.95f, 0.85f) * 3.0f;
    EXPECT_TRUE(ApproximatelyEqual(resolved.directionTowardSun, expectedDirection));
    EXPECT_TRUE(ApproximatelyEqual(resolved.sunIlluminance, expectedIlluminance));
}

TEST(DirectionalLightResolverTest, SkipsInactiveLightAndFallsBackToPlaceholder)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    DirectionalLight& light = registry.AddComponent<DirectionalLight>(entity);
    light.active = false;

    const ResolvedDirectionalLight resolved = ResolveActiveDirectionalLight(registry);

    const Vec3 expectedDirection = Normalize(Vec3(0.70710678f, 0.70710678f, 0.0f));
    EXPECT_TRUE(ApproximatelyEqual(resolved.directionTowardSun, expectedDirection));
}

// A non-trivial rotation regression check (per this phase's own strategy
// document's explicit instruction) - a DirectionalLight entity whose
// Transform is rotated 90 degrees around the world Y (yaw) axis faces
// +X instead of the default +Z, so the direction TOWARD the sun must be
// the negation of +X, i.e. -X.
TEST(DirectionalLightResolverTest, ResolvesDirectionAsNegationOfEntityForwardForNonTrivialRotation)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    Transform& transform = registry.AddComponent<Transform>(entity);
    transform.rotation = Quat::FromEulerDegrees(0.0f, 90.0f, 0.0f);
    registry.AddComponent<DirectionalLight>(entity);

    const ResolvedDirectionalLight resolved = ResolveActiveDirectionalLight(registry);

    const Vec3 forward = transform.rotation.RotateVector(Vec3::Forward());
    EXPECT_TRUE(ApproximatelyEqual(resolved.directionTowardSun, Normalize(-forward)));
    // Sanity-check the rotation itself actually did something non-trivial,
    // so this test would have caught a sign error either way.
    EXPECT_TRUE(ApproximatelyEqual(forward, Vec3::Right(), 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(resolved.directionTowardSun, Vec3::Left(), 1e-4f));
}

TEST(DirectionalLightResolverTest, UsesFirstActiveLightAndAppliesColorAndIlluminanceScale)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity); // Identity rotation -> forward is +Z.
    DirectionalLight& light = registry.AddComponent<DirectionalLight>(entity);
    light.color = Vec3(0.5f, 0.25f, 1.0f);
    light.illuminanceLux = 100000.0f; // The exact reference value the fallback also implicitly uses.

    const ResolvedDirectionalLight resolved = ResolveActiveDirectionalLight(registry);

    // Identity rotation's forward is +Z, so the direction toward the sun is -Z.
    EXPECT_TRUE(ApproximatelyEqual(resolved.directionTowardSun, Vec3::Back()));
    // At the reference illuminance, the shader-space scale collapses to
    // exactly 3.0 (matching the fallback's own overall magnitude), so this
    // is a color multiplied by 3.0.
    EXPECT_TRUE(ApproximatelyEqual(resolved.sunIlluminance, light.color * 3.0f, 1e-4f));
}

TEST(DirectionalLightResolverTest, FollowsParentTransformWhenParented)
{
    Registry registry;

    const Entity parent = registry.CreateEntity();
    Transform& parentTransform = registry.AddComponent<Transform>(parent);
    parentTransform.rotation = Quat::FromEulerDegrees(0.0f, 90.0f, 0.0f);

    const Entity lightEntity = registry.CreateEntity();
    Transform& lightTransform = registry.AddComponent<Transform>(lightEntity);
    lightTransform.parent = parent; // Identity LOCAL rotation - inherits the parent's world rotation.
    registry.AddComponent<DirectionalLight>(lightEntity);

    const ResolvedDirectionalLight resolved = ResolveActiveDirectionalLight(registry);

    const Vec3 expectedForward = parentTransform.rotation.RotateVector(Vec3::Forward());
    EXPECT_TRUE(ApproximatelyEqual(resolved.directionTowardSun, Normalize(-expectedForward), 1e-4f));
}

TEST(DirectionalLightResolverTest, IlluminanceOfZeroLuxProducesZeroSunIlluminance)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    DirectionalLight& light = registry.AddComponent<DirectionalLight>(entity);
    light.illuminanceLux = 0.0f;

    const ResolvedDirectionalLight resolved = ResolveActiveDirectionalLight(registry);

    EXPECT_TRUE(ApproximatelyEqual(resolved.sunIlluminance, Vec3::Zero()));
}

} // namespace
} // namespace gte
