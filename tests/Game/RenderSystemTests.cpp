// Unit tests for RenderSystem::CollectRenderables()/
// ResolveActiveCameraViewProjection() (src/Game/RenderSystem.h/.cpp) - the
// PURE data-collection/camera-resolution steps that turn ECS entities into
// DrawCommands and a view-projection matrix, using nothing but a Registry
// (no Renderer, no live Mesh/Pipeline, no GPU device at all - see the Tier 1
// rationale in AGENTS.md, "Testability & Regression Safety"). RenderSystem::
// Draw() itself (the non-pure half that actually resolves handles and calls
// Renderer::Submit()) is intentionally NOT tested here - it needs a live
// Renderer, same "Tier 2, not implemented yet" boundary as Buffer/
// RenderTexture/Pipeline (see tests/CMakeLists.txt).

#include "Game/RenderSystem.h"

#include "ECS/Components/Camera.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(RenderSystemTest, EmptyRegistryProducesNoDrawCommands)
{
    Registry registry;

    const std::vector<DrawCommand> commands = RenderSystem::CollectRenderables(registry);

    EXPECT_TRUE(commands.empty());
}

TEST(RenderSystemTest, EntityWithoutMeshRendererProducesNoDrawCommand)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity); // Transform alone is not renderable

    const std::vector<DrawCommand> commands = RenderSystem::CollectRenderables(registry);

    EXPECT_TRUE(commands.empty());
}

TEST(RenderSystemTest, EntityWithMeshRendererAndTransformUsesWorldMatrix)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();

    Transform& transform = registry.AddComponent<Transform>(entity);
    transform.position = Vec3{ 1.0f, 2.0f, 3.0f };

    const MeshHandle meshHandle{ 1, 1 };
    const PipelineHandle pipelineHandle{ 2, 1 };
    registry.AddComponent<MeshRenderer>(entity, MeshRenderer{ meshHandle, pipelineHandle });

    const std::vector<DrawCommand> commands = RenderSystem::CollectRenderables(registry);

    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].mesh, meshHandle);
    EXPECT_EQ(commands[0].pipeline, pipelineHandle);
    EXPECT_TRUE(ApproximatelyEqual(commands[0].model, transform.LocalToWorldMatrix()));
}

TEST(RenderSystemTest, EntityWithMeshRendererButNoTransformUsesIdentity)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();

    const MeshHandle meshHandle{ 1, 1 };
    const PipelineHandle pipelineHandle{ 2, 1 };
    registry.AddComponent<MeshRenderer>(entity, MeshRenderer{ meshHandle, pipelineHandle });

    const std::vector<DrawCommand> commands = RenderSystem::CollectRenderables(registry);

    ASSERT_EQ(commands.size(), 1u);
    EXPECT_TRUE(ApproximatelyEqual(commands[0].model, Mat4::Identity()));
}

TEST(RenderSystemTest, MultipleEntitiesEachProduceTheirOwnDrawCommand)
{
    Registry registry;

    const MeshHandle meshHandle{ 1, 1 };
    const PipelineHandle pipelineHandle{ 2, 1 };

    const float xPositions[3] = { -0.6f, 0.0f, 0.6f };
    for (const float x : xPositions) {
        const Entity entity = registry.CreateEntity();
        Transform& transform = registry.AddComponent<Transform>(entity);
        transform.position = Vec3{ x, 0.0f, 0.0f };
        registry.AddComponent<MeshRenderer>(entity, MeshRenderer{ meshHandle, pipelineHandle });
    }

    const std::vector<DrawCommand> commands = RenderSystem::CollectRenderables(registry);

    ASSERT_EQ(commands.size(), 3u);
    for (const DrawCommand& command : commands) {
        EXPECT_EQ(command.mesh, meshHandle);
        EXPECT_EQ(command.pipeline, pipelineHandle);
    }
}

TEST(RenderSystemTest, EntityWithParentUsesComposedWorldMatrix)
{
    Registry registry;

    const Entity parent = registry.CreateEntity();
    Transform& parentTransform = registry.AddComponent<Transform>(parent);
    parentTransform.position = Vec3{ 5.0f, 0.0f, 0.0f };

    const Entity child = registry.CreateEntity();
    Transform& childTransform = registry.AddComponent<Transform>(child);
    childTransform.position = Vec3{ 1.0f, 0.0f, 0.0f };
    childTransform.parent = parent;

    const MeshHandle meshHandle{ 1, 1 };
    const PipelineHandle pipelineHandle{ 2, 1 };
    registry.AddComponent<MeshRenderer>(child, MeshRenderer{ meshHandle, pipelineHandle });

    const std::vector<DrawCommand> commands = RenderSystem::CollectRenderables(registry);

    ASSERT_EQ(commands.size(), 1u);
    const Mat4 expected = parentTransform.LocalToWorldMatrix() * childTransform.LocalToWorldMatrix();
    EXPECT_TRUE(ApproximatelyEqual(commands[0].model, expected));
    EXPECT_FALSE(ApproximatelyEqual(commands[0].model, childTransform.LocalToWorldMatrix()));
}

TEST(RenderSystemTest, ResolveActiveCameraViewProjectionWithNoCameraReturnsIdentity)
{
    Registry registry;

    const Mat4 viewProjection = RenderSystem::ResolveActiveCameraViewProjection(registry, 16.0f / 9.0f);

    EXPECT_TRUE(ApproximatelyEqual(viewProjection, Mat4::Identity()));
}

TEST(RenderSystemTest, ResolveActiveCameraViewProjectionSkipsInactiveCamera)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    registry.AddComponent<Transform>(entity);
    Camera& camera = registry.AddComponent<Camera>(entity);
    camera.active = false;

    const Mat4 viewProjection = RenderSystem::ResolveActiveCameraViewProjection(registry, 16.0f / 9.0f);

    EXPECT_TRUE(ApproximatelyEqual(viewProjection, Mat4::Identity()));
}

TEST(RenderSystemTest, ResolveActiveCameraViewProjectionUsesActiveCameraAndItsTransform)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    Transform& transform = registry.AddComponent<Transform>(entity);
    transform.position = Vec3{ 0.0f, 0.0f, -5.0f };
    Camera& camera = registry.AddComponent<Camera>(entity);
    camera.fovYDegrees = 60.0f;
    camera.nearZ = 0.1f;
    camera.farZ = 100.0f;

    const float aspect = 16.0f / 9.0f;
    const Mat4 viewProjection = RenderSystem::ResolveActiveCameraViewProjection(registry, aspect);

    const Mat4 expected = camera.ProjectionMatrix(aspect) * Camera::ViewMatrix(transform);
    EXPECT_TRUE(ApproximatelyEqual(viewProjection, expected));
    EXPECT_FALSE(ApproximatelyEqual(viewProjection, Mat4::Identity()));
}

TEST(RenderSystemTest, ResolveActiveCameraViewProjectionUsesIdentityTransformWhenCameraHasNone)
{
    Registry registry;
    const Entity entity = registry.CreateEntity();
    Camera& camera = registry.AddComponent<Camera>(entity); // No Transform on this entity.

    const float aspect = 1.5f;
    const Mat4 viewProjection = RenderSystem::ResolveActiveCameraViewProjection(registry, aspect);

    const Mat4 expected = camera.ProjectionMatrix(aspect) * Camera::ViewMatrix(Transform{});
    EXPECT_TRUE(ApproximatelyEqual(viewProjection, expected));
}

TEST(RenderSystemTest, ResolveActiveCameraViewProjectionFollowsParentTransform)
{
    Registry registry;

    const Entity parent = registry.CreateEntity();
    Transform& parentTransform = registry.AddComponent<Transform>(parent);
    parentTransform.position = Vec3{ 0.0f, 0.0f, -5.0f };

    const Entity cameraEntity = registry.CreateEntity();
    Transform& cameraTransform = registry.AddComponent<Transform>(cameraEntity);
    cameraTransform.parent = parent; // Camera itself has an identity LOCAL transform.
    Camera& camera = registry.AddComponent<Camera>(cameraEntity);

    const float aspect = 16.0f / 9.0f;
    const Mat4 viewProjection = RenderSystem::ResolveActiveCameraViewProjection(registry, aspect);

    // The camera's WORLD transform should equal its parent's (identity
    // local offset composed on top) - i.e. exactly as if the camera itself
    // sat at the parent's position/rotation directly.
    const Mat4 expected = camera.ProjectionMatrix(aspect) * Camera::ViewMatrix(parentTransform);
    EXPECT_TRUE(ApproximatelyEqual(viewProjection, expected));
}

} // namespace
} // namespace gte
