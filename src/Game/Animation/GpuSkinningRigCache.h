#pragma once

#include "../../Core/GraphicsHandles.h" // DescriptorSetHandle
#include "../../Renderer/Compute/ComputeDescriptorSet.h"
#include "../../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../../Renderer/MeshHandle.h"
#include "../../Renderer/RenderGraph/GpuBuffer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

class Renderer;
class RenderSystem;
struct SkinnedMeshData;
struct MeshAssetPart;

// GPU resources of every GPU-skinned model: registered once per model, looked up every frame, never evicted.
class GpuSkinningRigCache {
public:
    // One skinning output shared by a group of mesh parts that share one CPU-side vertex buffer.
    struct OutputGroup {
        // Written by the skinning compute pass, read as the vertex buffer of the group's meshes below.
        rg::GpuBuffer outputVertexBuffer;

        // Bound once at registration; the buffers have a fixed identity for the model's lifetime.
        ComputeDescriptorSet descriptorSet;

        // Engine handle of `descriptorSet`, registered once.
        DescriptorSetHandle descriptorSetHandle = DescriptorSetHandle::Invalid;

        // True: output uses the position-normal-uv layout; false: position-normal.
        bool isTextured = false;

        // The model's full vertex count; every group's output covers every vertex.
        std::uint32_t vertexCount = 0;

        // Stable label "<file>#SkinGroup<N>" used as pass and resource name; static for the process lifetime.
        std::string debugName;

        // Each CPU-mode part mesh and its GPU-skinned counterpart; the runtime switch swaps between them.
        struct PartMeshBinding {
            MeshHandle cpuMeshHandle;
            MeshHandle gpuMeshHandle;
        };
        std::vector<PartMeshBinding> partMeshBindings;
    };

    // All GPU resources of one registered model.
    struct GpuModelEntry {
        // Uploaded once at registration.
        rg::GpuBuffer bindPoseBuffer;
        rg::GpuBuffer skinWeightsBuffer;
        // Present only when at least one output group is textured.
        std::optional<rg::GpuBuffer> uvBuffer;

        // Rewritten every frame the model animates on the GPU; mutable because TryGet() returns a const entry.
        mutable rg::GpuBuffer boneMatricesBuffer;

        std::vector<OutputGroup> outputGroups;

        // The GPU-skinned counterpart of a CPU-mode mesh, or kInvalidMeshHandle.
        MeshHandle TryGetGpuMeshHandle(MeshHandle cpuMeshHandle) const;

        // The CPU-mode counterpart of a GPU-skinned mesh, or kInvalidMeshHandle.
        MeshHandle TryGetCpuMeshHandle(MeshHandle gpuMeshHandle) const;
    };

    // Builds the model's GPU resources. No-op for a boneless or empty model, or one with no resolvable parts.
    // Initializes `pipelines` on first use.
    void Register(Renderer& renderer, RenderSystem& renderSystem, GpuSkinningPipelines& pipelines,
        const std::string& absoluteGtaPath, const SkinnedMeshData& data, const std::vector<MeshAssetPart>& parts);

    const GpuModelEntry* TryGet(const std::string& absoluteGtaPath) const;

private:
    std::unordered_map<std::string, GpuModelEntry> m_models;
};

} // namespace gte
