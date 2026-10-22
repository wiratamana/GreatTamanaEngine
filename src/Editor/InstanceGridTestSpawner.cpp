#include "InstanceGridTestSpawner.h"

#include "../Assets/MeshClustering.h"
#include "../Core/InstanceGridLimits.h"
#include "../ECS/Components/MeshRenderer.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/EntityQuery.h"
#include "../ECS/Registry.h"
#include "../Game/Game.h"
#include "../Game/Instantiation/MeshClusterTableSelection.h"
#include "../Game/Instantiation/MeshVertexPacking.h"
#include "../Math/Vec3.h"
#include "../Renderer/Culling/ClusterCullMath.h"
#include "../Renderer/Culling/CullingTypes.h"
#include "../Renderer/Mesh.h"
#include "../Renderer/MeshVertex.h"
#include "../Renderer/Pipeline.h"
#include "../Renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <vector>

namespace gte {

namespace {

constexpr float kGridDepth = 8.0f; // In front of the default camera.

// Regular grid of kInstanceGridQuadsPerSide^2 quads in [-0.5, 0.5]^2, facing -Z, shared vertices.
MeshData MakeGridMeshData()
{
    constexpr std::uint32_t n = kInstanceGridQuadsPerSide;
    constexpr std::uint32_t stride = n + 1;

    MeshData data;
    data.positions.reserve(static_cast<std::size_t>(stride) * stride);
    for (std::uint32_t y = 0; y < stride; ++y) {
        for (std::uint32_t x = 0; x < stride; ++x) {
            data.positions.push_back(Vec3{ static_cast<float>(x) / n - 0.5f, static_cast<float>(y) / n - 0.5f, 0.0f });
        }
    }
    data.normals.assign(data.positions.size(), Vec3{ 0.0f, 0.0f, -1.0f });
    data.uvs.assign(data.positions.size(), Vec2{ 0.0f, 0.0f });

    data.indices.reserve(static_cast<std::size_t>(n) * n * 6);
    for (std::uint32_t y = 0; y < n; ++y) {
        for (std::uint32_t x = 0; x < n; ++x) {
            const std::uint32_t v00 = y * stride + x;
            const std::uint32_t v10 = v00 + 1;
            const std::uint32_t v01 = v00 + stride;
            const std::uint32_t v11 = v01 + 1;
            const std::uint32_t quad[6] = { v00, v10, v11, v11, v01, v00 };
            data.indices.insert(data.indices.end(), std::begin(quad), std::end(quad));
        }
    }
    return data;
}

} // namespace

InstanceGridResources::~InstanceGridResources()
{
    Reset();
}

bool InstanceGridResources::Resolves(RenderSystem& renderSystem) const
{
    if (m_owner != &renderSystem || !m_mesh.IsValid() || !m_pipeline.IsValid()) {
        return false;
    }
    const Mesh* mesh = renderSystem.TryGetMesh(m_mesh);
    return mesh != nullptr && mesh->HasClusters() && renderSystem.TryGetPipeline(m_pipeline) != nullptr;
}

void InstanceGridResources::Reset()
{
    if (m_owner != nullptr && (m_mesh.IsValid() || m_pipeline.IsValid())) {
        if (m_renderer != nullptr) {
            m_renderer->DrainGpuForResourceRelease();
        }
        m_owner->ReleaseMesh(m_mesh);
        m_owner->UnregisterPipeline(m_pipeline);
    }
    m_owner = nullptr;
    m_renderer = nullptr;
    m_mesh = MeshHandle{};
    m_pipeline = PipelineHandle{};
    m_clusterCount = 0;
}

bool InstanceGridResources::Build(
    Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout, std::string* error)
{
    Reset();
    const auto fail = [error](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    try {
        MeshData data = MakeGridMeshData();
        const MeshClusterReport report = BuildMeshClusters(data, {}, MeshClusterOptions{});
        if (!report.success) {
            return fail("grid clustering failed: " + report.message);
        }

        const std::vector<ClusterSliceRange> slices{
            { 0u, static_cast<std::uint32_t>(data.indices.size()), true } };
        std::string why;
        const std::optional<std::vector<MeshCluster>> table = SelectClusterTableForKeptSlices(
            data.clusters, slices, data.indices.size(), kMaxMeshClusters, &why);
        if (!table.has_value()) {
            return fail("grid cluster table rejected: " + why);
        }

        const std::vector<MeshVertex> vertices = PackMeshVertices(data.positions, data.normals);
        Mesh mesh = renderer.CreateMesh(vertices.data(), vertices.size() * sizeof(MeshVertex),
            static_cast<std::uint32_t>(vertices.size()), data.indices.data(),
            data.indices.size() * sizeof(std::uint32_t), static_cast<std::uint32_t>(data.indices.size()),
            "InstanceGridTestSpawner.Grid");
        mesh.SetLocalBounds(ComputeLocalAABB(data.positions));
        mesh.SetClusterTable(rg::GpuBuffer::CreateWithData(renderer.Device(), table->data(),
                                 table->size() * sizeof(MeshCluster), "MeshClusterTable", rg::GpuBufferUse::Storage),
            static_cast<std::uint32_t>(table->size()));

        Pipeline pipeline = renderer.CreatePipeline("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv",
            VertexLayout::PositionNormal, /*useMaterialTexture=*/false,
            "InstanceGridTestSpawner (Mesh.vert/Mesh.frag PositionNormal)", /*useInstanceBuffer=*/false,
            sceneServicesSetLayout);

        RenderSystem& renderSystem = game.GetRenderSystem();
        m_owner = &renderSystem;
        m_renderer = &renderer;
        m_clusterCount = static_cast<std::uint32_t>(table->size());
        m_mesh = renderSystem.RegisterMesh(std::move(mesh));
        m_pipeline = renderSystem.RegisterPipeline(std::move(pipeline));
        return true;
    } catch (const std::exception& e) {
        Reset();
        return fail(std::string("grid resources failed: ") + e.what());
    }
}

InstanceGridSpawnResult InstanceGridTestSpawner::Spawn(Game& game, Renderer& renderer,
    InstanceGridResources& resources, std::uint32_t instanceCount, float spacing,
    DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    InstanceGridSpawnResult result;
    const auto refuse = [&result](std::string message) {
        result.success = false;
        result.errorMessage = std::move(message);
        return result;
    };

    if (instanceCount == 0 || instanceCount > kMaxSpawnGridInstances) {
        return refuse("count must be an integer from 1 to " + std::to_string(kMaxSpawnGridInstances));
    }
    if (!std::isfinite(spacing) || spacing < kMinSpawnGridSpacing || spacing > kMaxSpawnGridSpacing) {
        return refuse("spacing must be a number from " + std::to_string(kMinSpawnGridSpacing) + " to "
            + std::to_string(kMaxSpawnGridSpacing));
    }

    RenderSystem& renderSystem = game.GetRenderSystem();
    if (!resources.Resolves(renderSystem)) {
        std::string error;
        if (!resources.Build(game, renderer, sceneServicesSetLayout, &error)) {
            return refuse(error);
        }
    }

    // The cluster path refuses a part above its limits, so refuse the grid instead of truncating it.
    const IndirectDrawCapsView caps = renderer.IndirectDrawCaps();
    if (ClusterPathSupported(caps)) {
        const ClusterLimits limits = ClusterLimitsFor(caps);
        const std::uint64_t workItems = static_cast<std::uint64_t>(instanceCount) * resources.ClusterCount();
        if (instanceCount > limits.maxInstances || workItems > limits.maxWorkItems) {
            return refuse("grid of " + std::to_string(instanceCount) + " instances exceeds the cluster draw limits");
        }
    }

    Registry& registry = game.GetRegistry();
    const std::uint32_t columns = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(instanceCount))));
    const std::uint32_t rows = (instanceCount + columns - 1) / columns;
    const float originX = -spacing * static_cast<float>(columns - 1) * 0.5f;
    const float originY = -spacing * static_cast<float>(rows - 1) * 0.5f;

    // One name scan per batch, not per entity: a batch is "InstanceGrid <batch> <index>".
    std::uint32_t batch = 1;
    while (IsEntityNameInUse(registry, "InstanceGrid " + std::to_string(batch) + " 0")) {
        ++batch;
    }
    const std::string namePrefix = "InstanceGrid " + std::to_string(batch) + " ";

    for (std::uint32_t i = 0; i < instanceCount; ++i) {
        const Entity entity = registry.CreateEntity();

        Transform& transform = registry.AddComponent<Transform>(entity);
        transform.position = Vec3{ originX + spacing * static_cast<float>(i % columns),
            originY + spacing * static_cast<float>(i / columns), kGridDepth };

        MeshRenderer& meshRenderer = registry.AddComponent<MeshRenderer>(entity);
        meshRenderer.mesh = resources.MeshRef();
        meshRenderer.pipeline = resources.PipelineRef();

        registry.AddComponent<Name>(entity, Name{ namePrefix + std::to_string(i) });
    }

    result.success = true;
    result.instanceCount = instanceCount;
    return result;
}

} // namespace gte
