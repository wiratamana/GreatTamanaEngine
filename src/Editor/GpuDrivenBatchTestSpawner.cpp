#include "GpuDrivenBatchTestSpawner.h"

#include "../ECS/Components/MeshRenderer.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/EntityQuery.h"
#include "../ECS/Registry.h"
#include "../Game/Game.h"
#include "../Renderer/Culling/CullingTypes.h"
#include "../Renderer/Mesh.h"
#include "../Renderer/MeshHandle.h"
#include "../Math/Vec3.h"
#include "../Renderer/Pipeline.h"
#include "../Renderer/PipelineHandle.h"
#include "../Renderer/Renderer.h"

#include <vector>

namespace gte {

namespace {

// Lazily built exactly once per process - see GpuDrivenBatchTestSpawner.h's
// own header comment for why plain function-local statics are safe here
// (exactly one Game/RenderSystem instance ever exists per process lifetime,
// mirroring Application.cpp's own BatchNamePool() "static ... pool;"
// precedent).
MeshHandle& SharedMeshHandle()
{
    static MeshHandle handle;
    return handle;
}

PipelineHandle& SharedPipelineHandle()
{
    static PipelineHandle handle;
    return handle;
}

// Builds the shared indexed unit quad (PositionNormal, facing -Z) and its pipeline once.
// Indexed so the instances qualify for GPU-driven batching.
void EnsureSharedMeshAndPipeline(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    if (SharedMeshHandle().IsValid() && SharedPipelineHandle().IsValid()) {
        return;
    }

    const MeshVertex vertices[4] = {
        { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
        { { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
        { { 0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
        { { -0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
    };
    const std::uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };

    Mesh mesh = renderer.CreateMesh(vertices, sizeof(vertices), 4, indices, sizeof(indices), 6,
        "GpuDrivenBatchTestSpawner.Quad");

    std::vector<Vec3> positions;
    positions.reserve(4);
    for (const MeshVertex& v : vertices) {
        positions.push_back(Vec3{ v.position[0], v.position[1], v.position[2] });
    }
    mesh.SetLocalBounds(ComputeLocalAABB(positions));

    RenderSystem& renderSystem = game.GetRenderSystem();
    SharedMeshHandle() = renderSystem.RegisterMesh(std::move(mesh));

    // Untextured mesh shaders; the instanced variant is resolved per batch by GpuDrivenBatchCache.
    Pipeline pipeline = renderer.CreatePipeline("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv",
        VertexLayout::PositionNormal, /*useMaterialTexture=*/false,
        "GpuDrivenBatchTestSpawner (Mesh.vert/Mesh.frag PositionNormal, validation content)",
        /*useInstanceBuffer=*/false, sceneServicesSetLayout);
    SharedPipelineHandle() = renderSystem.RegisterPipeline(std::move(pipeline));
}

} // namespace

GpuDrivenBatchTestSpawnResult GpuDrivenBatchTestSpawner::Spawn(
    Game& game, Renderer& renderer, std::uint32_t instanceCount, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    GpuDrivenBatchTestSpawnResult result;

    if (instanceCount == 0) {
        result.success = false;
        result.errorMessage = "instanceCount must be >= 1";
        return result;
    }

    EnsureSharedMeshAndPipeline(game, renderer, sceneServicesSetLayout);

    Registry& registry = game.GetRegistry();

    // A left-to-right row inside the default camera's frustum.
    constexpr float kSpacing = 1.5f;
    constexpr float kDepth = 8.0f;
    const float startX = -kSpacing * (static_cast<float>(instanceCount) - 1.0f) / 2.0f;

    for (std::uint32_t i = 0; i < instanceCount; ++i) {
        const Entity entity = registry.CreateEntity();

        Transform& transform = registry.AddComponent<Transform>(entity);
        transform.position = Vec3{ startX + kSpacing * static_cast<float>(i), 0.0f, kDepth };

        MeshRenderer& meshRenderer = registry.AddComponent<MeshRenderer>(entity);
        meshRenderer.mesh = SharedMeshHandle();
        meshRenderer.pipeline = SharedPipelineHandle();

        const std::string uniqueName = MakeUniqueEntityName(registry, "GpuDrivenTestBatch");
        registry.AddComponent<Name>(entity, Name{ uniqueName });
    }

    result.success = true;
    result.instanceCount = instanceCount;
    return result;
}

} // namespace gte
