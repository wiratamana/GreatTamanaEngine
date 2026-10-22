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

void ClusterMovedTracker::BeginFrame()
{
    m_previous.swap(m_current);
    for (Slot& slot : m_current) {
        slot.generation = 0;
    }
}

bool ClusterMovedTracker::Observe(Entity entity, const Mat4& world)
{
    if (!entity.IsValid() || entity.index >= kMaxTrackedIndex) {
        return true;
    }
    if (m_current.size() <= entity.index) {
        m_current.resize(static_cast<std::size_t>(entity.index) + 1);
    }
    m_current[entity.index] = Slot{ world, entity.generation };
    if (entity.index >= m_previous.size()) {
        return true;
    }
    const Slot& before = m_previous[entity.index];
    return before.generation != entity.generation || InstanceMoved(before.world, world);
}

void ClusterMovedTracker::Clear() noexcept
{
    m_previous.clear();
    m_current.clear();
}

std::vector<fx::ClusterInstance> SelectClusterCandidates(
    std::span<const ClusterCandidateInput> inputs, bool capsPresent, ClusterMovedTracker* moved)
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
        if (moved != nullptr && moved->Observe(in.entity, in.world)) {
            instance.flags |= kClusterFlagMoved;
        }
        result.push_back(instance);
    }
    return result;
}

} // namespace gte
