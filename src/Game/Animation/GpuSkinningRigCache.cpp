#include "GpuSkinningRigCache.h"

#include "../../Math/Mat4.h"
#include "../../Renderer/GpuSkinning/GpuSkinningTypes.h"
#include "../../Renderer/Renderer.h"
#include "../Instantiation/MeshAssetGpuCatalog.h"
#include "../Instantiation/MeshAssetPartGrouping.h"
#include "../RenderSystem.h"
#include "SkeletalRigCache.h"

#include <algorithm>
#include <utility>

namespace gte {

MeshHandle GpuSkinningRigCache::GpuModelEntry::TryGetGpuMeshHandle(MeshHandle cpuMeshHandle) const
{
    for (const OutputGroup& group : outputGroups) {
        for (const OutputGroup::PartMeshBinding& binding : group.partMeshBindings) {
            if (binding.cpuMeshHandle == cpuMeshHandle) {
                return binding.gpuMeshHandle;
            }
        }
    }
    return kInvalidMeshHandle;
}

void GpuSkinningRigCache::Register(Renderer& renderer, RenderSystem& renderSystem, GpuSkinningPipelines& pipelines,
    const std::string& absoluteGtaPath, const SkinnedMeshData& data, const std::vector<MeshAssetPart>& parts)
{
    if (data.skeleton.bones.empty() || data.bindPositions.empty() || parts.empty()) {
        return; // Boneless/riggless/empty model - nothing to register (mirrors SkeletalRigCache/AnimationSystem's own convention).
    }

    const std::vector<MeshAssetPartGroup> groups = GroupMeshAssetPartsBySharedVertexBuffer(renderSystem, parts);
    if (groups.empty()) {
        return; // None of `parts`' handles resolved to a live Mesh - nothing to build against.
    }

    pipelines.EnsureInitialized(renderer);

    // GpuModelEntry is deliberately NOT default-constructible (Buffer has
    // no default constructor - see Buffer.h) - every GpuModelEntry must be
    // built as a single, fully-formed aggregate-initialization. Compute
    // every per-model buffer as a plain local first, then construct `entry`
    // from them in one step below.
    const std::vector<GpuBindPoseVertex> packedBindPose = PackBindPoseVertices(data.bindPositions, data.bindNormals);
    Buffer bindPoseBuffer = renderer.CreateDeviceLocalBuffer(packedBindPose.data(),
        static_cast<VkDeviceSize>(packedBindPose.size() * sizeof(GpuBindPoseVertex)),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "GpuSkinningBindPose");

    const std::vector<GpuSkinWeights> packedSkinWeights = PackSkinWeights(data.skinWeights);
    Buffer skinWeightsBuffer = renderer.CreateDeviceLocalBuffer(packedSkinWeights.data(),
        static_cast<VkDeviceSize>(packedSkinWeights.size() * sizeof(GpuSkinWeights)),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "GpuSkinningWeights");

    // The one genuinely per-frame-rewritten input - sized here, uploaded by
    // a future Phase 5 every frame this model is animated in GPU mode. See
    // this phase's own strategy document, Step 3.3.
    Buffer boneMatricesBuffer = renderer.CreateStructuredBuffer(sizeof(Mat4),
        static_cast<std::uint32_t>(data.skeleton.bones.size()), BufferMemoryUsage::CpuToGpu, 0,
        "GpuSkinningBoneMatrices");

    const bool anyTextured =
        std::any_of(groups.begin(), groups.end(), [](const MeshAssetPartGroup& group) { return group.textured; });
    std::optional<Buffer> uvBuffer;
    if (anyTextured) {
        const std::vector<GpuUv> packedUvs = PackUvs(data.uvs);
        uvBuffer.emplace(renderer.CreateDeviceLocalBuffer(packedUvs.data(),
            static_cast<VkDeviceSize>(packedUvs.size() * sizeof(GpuUv)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            "GpuSkinningUv"));
    }

    GpuModelEntry entry{ std::move(bindPoseBuffer), std::move(skinWeightsBuffer), std::move(uvBuffer),
        std::move(boneMatricesBuffer), {} };

    const std::uint32_t vertexCount = static_cast<std::uint32_t>(data.bindPositions.size());
    const VkDevice device = renderer.GetVulkanContextInfo().device;

    entry.outputGroups.reserve(groups.size());

    for (const MeshAssetPartGroup& group : groups) {
        OutputGroup outputGroup;
        outputGroup.isTextured = group.textured;
        outputGroup.vertexCount = vertexCount;

        const VkDeviceSize outputSize = group.textured
            ? static_cast<VkDeviceSize>(vertexCount) * sizeof(GpuSkinnedVertexPositionNormalUv)
            : static_cast<VkDeviceSize>(vertexCount) * sizeof(GpuSkinnedVertexPositionNormal);

        outputGroup.outputVertexBuffer = std::make_shared<Buffer>(
            renderer.CreateGpuSkinningTargetBuffer(outputSize, "GpuSkinningOutput"));

        const VkDescriptorSetLayout layout = group.textured ? pipelines.PositionNormalUvDescriptorSetLayout()
                                                             : pipelines.PositionNormalDescriptorSetLayout();
        outputGroup.descriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(layout));

        // Bound once, per Phase 1's binding table (bind pose / skin
        // weights / bone matrices / output, + UV at binding 4 for a
        // textured group - see GpuSkinningTypes.h) - never re-Rewrite()'d
        // afterward (see this class's own OutputGroup::descriptorSet doc
        // comment).
        std::vector<ComputeDescriptorWrite> writes;
        writes.push_back(ComputeDescriptorWrite::StorageBuffer(0, entry.bindPoseBuffer.Native()));
        writes.push_back(ComputeDescriptorWrite::StorageBuffer(1, entry.skinWeightsBuffer.Native()));
        writes.push_back(ComputeDescriptorWrite::StorageBuffer(2, entry.boneMatricesBuffer.Native()));
        writes.push_back(ComputeDescriptorWrite::StorageBuffer(3, outputGroup.outputVertexBuffer->Native()));
        if (group.textured && entry.uvBuffer.has_value()) {
            writes.push_back(ComputeDescriptorWrite::StorageBuffer(4, entry.uvBuffer->Native()));
        }
        outputGroup.descriptorSet.Rewrite(device, writes);

        // Build a GPU-skinned Mesh PER PART in this group, all sharing this
        // group's own output vertex buffer - mirrors the CPU path's own
        // "several textured parts share one vertex buffer, each with its
        // own index buffer/range" shape exactly (see
        // Renderer::CreateMeshFromSharedVertexBuffer()). A part whose
        // MeshAssetPart::indices is empty (e.g. built before that field
        // existed) is simply skipped - never crashes, mirrors this whole
        // module's "degrade gracefully" convention.
        for (const std::size_t partIndex : group.partIndices) {
            const MeshAssetPart& part = parts[partIndex];
            if (part.indices.empty()) {
                continue;
            }

            Mesh gpuMesh = renderer.CreateMeshFromSharedVertexBuffer(outputGroup.outputVertexBuffer, vertexCount,
                part.indices.data(), static_cast<VkDeviceSize>(part.indices.size() * sizeof(std::uint32_t)),
                static_cast<std::uint32_t>(part.indices.size()), "GpuSkinnedMesh");
            const MeshHandle gpuHandle = renderSystem.RegisterMesh(std::move(gpuMesh));
            outputGroup.partMeshBindings.push_back(OutputGroup::PartMeshBinding{ part.mesh, gpuHandle });
        }

        entry.outputGroups.push_back(std::move(outputGroup));
    }

    m_models.insert_or_assign(absoluteGtaPath, std::move(entry));
}

const GpuSkinningRigCache::GpuModelEntry* GpuSkinningRigCache::TryGet(const std::string& absoluteGtaPath) const
{
    const auto found = m_models.find(absoluteGtaPath);
    return found != m_models.end() ? &found->second : nullptr;
}

} // namespace gte
