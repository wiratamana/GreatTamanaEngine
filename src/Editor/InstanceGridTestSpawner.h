#pragma once

#include "../Core/GraphicsHandles.h"
#include "../ECS/Entity.h"
#include "../Math/Vec3.h"
#include "../Renderer/MeshHandle.h"
#include "../Renderer/PipelineHandle.h"

// Editor validation helper: spawns a square grid of instances that share one clustered indexed mesh.
// Used by the Hierarchy menu, POST /spawn_instance_grid and the Hi-Z demo scene.

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

class Game;
class Renderer;
class RenderSystem;

// Quads per side of the shared grid mesh.
inline constexpr std::uint32_t kInstanceGridQuadsPerSide = 32;

// Default grid centre depth: in front of the default camera.
inline constexpr float kInstanceGridDefaultDepth = 8.0f;

struct InstanceGridSpawnResult {
    bool success = false;
    std::string errorMessage;
    std::uint32_t instanceCount = 0; // New entities created; only meaningful on success.
    std::vector<Entity> entities;    // The new entities in creation order; only meaningful on success.
};

// Mesh and pipeline shared by every spawned grid. Owned by the editor layer; releases both on destruction.
// The owning Game and Renderer must outlive it.
class InstanceGridResources {
public:
    InstanceGridResources() = default;
    ~InstanceGridResources();
    InstanceGridResources(const InstanceGridResources&) = delete;
    InstanceGridResources& operator=(const InstanceGridResources&) = delete;

    // True when the mesh and pipeline still resolve in `renderSystem`.
    bool Resolves(RenderSystem& renderSystem) const;

    MeshHandle MeshRef() const noexcept { return m_mesh; }
    PipelineHandle PipelineRef() const noexcept { return m_pipeline; }
    std::uint32_t ClusterCount() const noexcept { return m_clusterCount; }

    // Builds the shared mesh (with its cluster table) and pipeline. Replaces any earlier pair.
    bool Build(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout, std::string* error);

    // Releases both resources after the GPU is idle. False when it was not: the handles are kept.
    bool Reset();

private:
    RenderSystem* m_owner = nullptr;
    Renderer* m_renderer = nullptr;
    MeshHandle m_mesh;
    PipelineHandle m_pipeline;
    std::uint32_t m_clusterCount = 0;
};

class InstanceGridTestSpawner {
public:
    // Spawns `instanceCount` named entities on a square grid, `spacing` apart, centred on `center`.
    // Refuses bad or oversized input; a failure removes the entities it made.
    static InstanceGridSpawnResult Spawn(Game& game, Renderer& renderer, InstanceGridResources& resources,
        std::uint32_t instanceCount, float spacing, DescriptorSetLayoutHandle sceneServicesSetLayout,
        const Vec3& center = Vec3{ 0.0f, 0.0f, kInstanceGridDefaultDepth });
};

} // namespace gte
