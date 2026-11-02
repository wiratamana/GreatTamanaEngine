#pragma once
#include "../../Assets/SkeletonData.h"
#include "../../Physics/DynamicChainDefinition.h"
#include "../../Physics/DynamicChainDetection.h"
#include "../../Physics/ModelColliderDefinition.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

// Names and PMX limits of one chain joint, kept for the physics debug toolbox.
struct JointDebugInfo {
    std::string rigidBodyName;
    std::string pmxJointName;
    float pmxRotLowerRad[3] = { 0.0f, 0.0f, 0.0f };
    float pmxRotUpperRad[3] = { 0.0f, 0.0f, 0.0f };
    bool pmxJointFound = false;
};

// Per-model cache of detected dynamic chains, owned by PhysicsSystem and keyed by the .gta path.
class DynamicChainRigCache {
public:
    struct ModelEntry {
        std::vector<DynamicChainDefinition> chains;
        // Private copy of the model skeleton.
        SkeletonData skeleton;
        DynamicChainDetectionDiagnostics diagnostics;
        // Static-body colliders in bind-pose-relative form, shared by every chain of the model.
        std::vector<ModelColliderDefinition> colliders;
        // One vector per chain, index-aligned with that chain's joints.
        std::vector<std::vector<JointDebugInfo>> jointDebugInfo;
        // Rigid body name per collider, index-aligned with `colliders`.
        std::vector<std::string> colliderRigidBodyNames;
    };

    void Register(const std::string& absoluteGtaPath, ModelEntry entry)
    {
        m_cache.insert_or_assign(absoluteGtaPath, std::move(entry));
    }

    const ModelEntry* TryGet(const std::string& absoluteGtaPath) const
    {
        const auto found = m_cache.find(absoluteGtaPath);
        return found != m_cache.end() ? &found->second : nullptr;
    }

    // Live edits of cached joint settings apply to every entity spawned from the same model path.
    ModelEntry* TryGetMutable(const std::string& absoluteGtaPath)
    {
        const auto found = m_cache.find(absoluteGtaPath);
        return found != m_cache.end() ? &found->second : nullptr;
    }

private:
    std::unordered_map<std::string, ModelEntry> m_cache;
};

} // namespace gte
