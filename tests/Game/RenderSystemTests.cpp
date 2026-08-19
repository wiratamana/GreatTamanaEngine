// Unit tests for RenderSystem::CollectRenderables() (src/Game/RenderSystem.h/
// .cpp) - the PURE data-collection step that turns ECS entities into
// DrawCommands, using nothing but a Registry (no Renderer, no live Mesh/
// Pipeline, no GPU device at all - see the Tier 1 rationale in AGENTS.md,
// "Testability & Regression Safety"). RenderSystem::Draw() itself (the
// non-pure half that actually resolves handles and calls Renderer::Submit())
// is intentionally NOT tested here - it needs a live Renderer, same "Tier 2,
// not implemented yet" boundary as Buffer/RenderTexture/Pipeline (see
// tests/CMakeLists.txt).

#include "Game/RenderSystem.h"

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

} // namespace
} // namespace gte
