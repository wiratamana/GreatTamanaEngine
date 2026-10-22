#pragma once

#include "FeatureKit/ClusterDrawTypes.h"

#include <span>
#include <vector>

namespace gte {

// One draw command plus what Core knows about its mesh and pipeline.
struct ClusterCandidateInput {
    Entity entity;
    fx::MeshToken mesh;
    Mat4 world = Mat4::Identity();
    bool meshHasClusters = false;
    bool meshSynthesizable = false; // No table, but indices and bounds allow a one-cluster table.
    bool meshHasIndexBuffer = false;
    bool pipelineUntextured = false; // Position + normal vertex layout, no material set.
    bool gpuSkinned = false;
};

// Last frame's world matrix per entity, double buffered and indexed by entity slot. Fills only while
// someone needs the moved flag (occlusion culling); a new entity counts as moved.
class ClusterMovedTracker {
public:
    // Starts a frame: this frame's observations become the previous ones.
    void BeginFrame();
    // Records the matrix of `entity`. True when it is new or its matrix changed since the last frame.
    bool Observe(Entity entity, const Mat4& world);
    // Forgets everything, so the next frame treats every entity as moved.
    void Clear() noexcept;

private:
    struct Slot {
        Mat4 world = Mat4::Identity();
        std::uint32_t generation = 0; // 0 = empty.
    };
    // Entity slots above this are never tracked and always count as moved.
    static constexpr std::uint32_t kMaxTrackedIndex = 1u << 22;

    std::vector<Slot> m_previous;
    std::vector<Slot> m_current;
};

// True when every entry of `m` is finite.
bool IsFiniteMatrix(const Mat4& m) noexcept;

// Eligible inputs as cluster instances, in input order. Needs a table (or a synthesizable mesh with enough
// instances in the list), an index buffer, an untextured pipeline, no GPU skinning, a finite world matrix and
// `capsPresent`. With `moved` set, new or changed instances carry kClusterFlagMoved.
std::vector<fx::ClusterInstance> SelectClusterCandidates(
    std::span<const ClusterCandidateInput> inputs, bool capsPresent, ClusterMovedTracker* moved = nullptr);

} // namespace gte
