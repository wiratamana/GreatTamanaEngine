#pragma once

#include "../Core/GraphicsHandles.h"

// Editor validation helper: spawns a row of instances sharing one untextured indexed mesh and pipeline,
// which forms a GPU-driven batch. Used by the Hierarchy menu and POST /spawn_gpu_driven_test_batch.

#include <cstdint>
#include <string>

namespace gte {

class Game;
class Renderer;

struct GpuDrivenBatchTestSpawnResult {
    bool success = false;
    std::string errorMessage;
    std::uint32_t instanceCount = 0; // New entities created; only meaningful on success.
};

class GpuDrivenBatchTestSpawner {
public:
    // Spawns `instanceCount` named entities in a row; fails (no entities) only for a count of 0.
    // `sceneServicesSetLayout` is the layout of Core's scene-services descriptor set.
    static GpuDrivenBatchTestSpawnResult Spawn(
        Game& game, Renderer& renderer, std::uint32_t instanceCount, DescriptorSetLayoutHandle sceneServicesSetLayout);
};

} // namespace gte
