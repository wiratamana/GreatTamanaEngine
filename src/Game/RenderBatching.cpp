#include "RenderBatching.h"

#include <unordered_map>

namespace gte {

namespace {

// Local-only key/hash pair - deliberately NOT gte::GpuDrivenBatchKey (a
// Renderer-layer type, src/Renderer/Culling/GpuDrivenBatchCache.h): this
// function must stay Renderer-layer-independent (pure DrawCommand-only
// input), so it builds its own throwaway grouping key instead of reaching
// upward into a type this pure Tier-1 function has no real need to know
// about.
struct GroupKey {
    MeshHandle mesh;
    PipelineHandle pipeline;

    friend bool operator==(const GroupKey& a, const GroupKey& b) noexcept
    {
        return a.mesh == b.mesh && a.pipeline == b.pipeline;
    }
};

struct GroupKeyHash {
    std::size_t operator()(const GroupKey& key) const noexcept
    {
        const std::uint64_t meshBits =
            (static_cast<std::uint64_t>(key.mesh.index) << 32) | static_cast<std::uint64_t>(key.mesh.generation);
        const std::uint64_t pipelineBits = (static_cast<std::uint64_t>(key.pipeline.index) << 32)
            | static_cast<std::uint64_t>(key.pipeline.generation);
        // A standard 64-bit hash-combine (splitmix64-style constant) - only
        // needs to be a reasonable, well-distributed hash, never a
        // cryptographic one; std::unordered_map resolves any collision via
        // operator== regardless, so this can never produce an incorrect
        // grouping, only a slower one in a pathological case.
        return std::hash<std::uint64_t>{}(meshBits) ^ (std::hash<std::uint64_t>{}(pipelineBits) * 0x9E3779B97F4A7C15ULL);
    }
};

} // namespace

std::vector<RenderBatchGroup> GroupDrawCommandsByMeshAndPipeline(const std::vector<DrawCommand>& commands)
{
    std::vector<RenderBatchGroup> groups;
    std::unordered_map<GroupKey, std::size_t, GroupKeyHash> indexOfGroup;
    groups.reserve(commands.size());

    for (const DrawCommand& command : commands) {
        const GroupKey key{ command.mesh, command.pipeline };

        const auto found = indexOfGroup.find(key);
        if (found != indexOfGroup.end()) {
            groups[found->second].commands.push_back(command);
            continue;
        }

        indexOfGroup.emplace(key, groups.size());

        RenderBatchGroup group;
        group.mesh = command.mesh;
        group.pipeline = command.pipeline;
        group.commands.push_back(command);
        groups.push_back(std::move(group));
    }

    return groups;
}

bool IsGpuDrivenEligible(const RenderBatchGroup& group, bool hasIndexBuffer, VertexLayout vertexLayout,
    bool isGpuSkinned, std::size_t minInstancesForGpuDrivenBatch)
{
    if (group.commands.size() < minInstancesForGpuDrivenBatch) {
        return false;
    }
    if (!hasIndexBuffer) {
        return false;
    }
    if (vertexLayout != VertexLayout::PositionNormal) {
        return false;
    }
    if (isGpuSkinned) {
        return false;
    }
    return true;
}

} // namespace gte
