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

// A hand-authored, indexed, unit quad in the XY plane, facing -Z (a plain
// forward-facing normal is all this validation content needs - it is never
// meant to look like real game art, only to be a real, indexed,
// VertexLayout::PositionNormal Mesh so it can qualify for GPU-driven
// batching at all). Two triangles, 4 shared vertices - real vertex reuse via
// a real index buffer, exactly the shape PHASE0's Locked Design Decision 6/7
// requires (PrimitiveMeshGenerator's own built-in shapes are deliberately
// excluded precisely because they are NOT indexed).
void EnsureSharedMeshAndPipeline(Game& game, Renderer& renderer, VkDescriptorSetLayout sceneServicesSetLayout)
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
        "GpuDrivenBatchTestSpawner.Quad (validation content, render-pass-5 PHASE6)");

    std::vector<Vec3> positions;
    positions.reserve(4);
    for (const MeshVertex& v : vertices) {
        positions.push_back(Vec3{ v.position[0], v.position[1], v.position[2] });
    }
    mesh.SetLocalBounds(ComputeLocalAABB(positions));

    RenderSystem& renderSystem = game.GetRenderSystem();
    SharedMeshHandle() = renderSystem.RegisterMesh(std::move(mesh));

    // The EXACT SAME shader pair MeshAssetGpuCatalog::EnsureMeshPipeline()
    // already builds its own real untextured-mesh pipeline from (see this
    // file's own header comment) - not textured (Locked Design Decision 8),
    // not the PositionNormalInstanced variant (that sibling is resolved
    // automatically, per batch, by GpuDrivenBatchCache::
    // ResolveInstancedPipeline() - PHASE5).
    Pipeline pipeline = renderer.CreatePipeline("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv",
        VertexLayout::PositionNormal, /*useMaterialTexture=*/false,
        "GpuDrivenBatchTestSpawner (Mesh.vert/Mesh.frag PositionNormal, validation content)",
        /*useInstanceBuffer=*/false, sceneServicesSetLayout);
    SharedPipelineHandle() = renderSystem.RegisterPipeline(std::move(pipeline));
}

} // namespace

GpuDrivenBatchTestSpawnResult GpuDrivenBatchTestSpawner::Spawn(
    Game& game, Renderer& renderer, std::uint32_t instanceCount, VkDescriptorSetLayout sceneServicesSetLayout)
{
    GpuDrivenBatchTestSpawnResult result;

    if (instanceCount == 0) {
        result.success = false;
        result.errorMessage = "instanceCount must be >= 1";
        return result;
    }

    EnsureSharedMeshAndPipeline(game, renderer, sceneServicesSetLayout);

    Registry& registry = game.GetRegistry();

    // A simple left-to-right row, comfortably inside the engine's own
    // default Camera's frustum (Vec3{0,0,-5}, identity rotation, looking
    // down +Z - see Game::EnsureDefaultCameraExists()) without needing any
    // camera repositioning at all - mirrors PHASE5's own already-verified
    // manual verification harness's exact "6 entities, distinct world
    // positions" shape (see PHASE5_COMPLETION_REPORT.md's "Live
    // verification" section).
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
