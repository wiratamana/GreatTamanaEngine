#include "ClusterCandidates.h"

#include "../Renderer/Culling/ClusterCullMath.h"

#include <cmath>
#include <unordered_map>

namespace gte {

bool IsFiniteMatrix(const Mat4& m) noexcept
{
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(m(r, c))) {
                return false;
            }
        }
    }
    return true;
}

std::vector<fx::ClusterInstance> SelectClusterCandidates(std::span<const ClusterCandidateInput> inputs, bool capsPresent)
{
    std::vector<fx::ClusterInstance> result;
    if (!capsPresent) {
        return result;
    }

    const auto baseEligible = [](const ClusterCandidateInput& in) {
        return in.meshHasIndexBuffer && in.pipelineUntextured && !in.gpuSkinned && IsFiniteMatrix(in.world);
    };

    // A mesh that needs a synthesized table is worth a chain only with enough instances.
    std::unordered_map<fx::MeshToken, std::size_t, fx::MeshTokenHash> synthesizedCounts;
    for (const ClusterCandidateInput& in : inputs) {
        if (!in.meshHasClusters && in.meshSynthesizable && baseEligible(in)) {
            ++synthesizedCounts[in.mesh];
        }
    }

    result.reserve(inputs.size());
    for (const ClusterCandidateInput& in : inputs) {
        if (!baseEligible(in)) {
            continue;
        }
        if (!in.meshHasClusters) {
            if (!in.meshSynthesizable || synthesizedCounts[in.mesh] < kMinInstancesForSynthesizedClusters) {
                continue;
            }
        }
        fx::ClusterInstance instance;
        instance.entity = in.entity;
        instance.mesh = in.mesh;
        instance.world = in.world;
        instance.flags = ClusterInstanceFlagsFor(in.world, &instance.maxScale);
        result.push_back(instance);
    }
    return result;
}

} // namespace gte
