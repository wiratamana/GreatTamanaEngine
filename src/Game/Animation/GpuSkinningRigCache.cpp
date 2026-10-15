#include "GpuSkinningRigCache.h"

#include "../../Math/Mat4.h"
#include "../../Renderer/GpuSkinning/GpuSkinningTypes.h"
#include "../../Renderer/Renderer.h"
#include "../Instantiation/MeshAssetGpuCatalog.h"
#include "../Instantiation/MeshAssetPartGrouping.h"
#include "../RenderSystem.h"
#include "SkeletalRigCache.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace gte {

namespace {

// The file name only: the render graph panel shows this label, and a full path would leak the folder layout.
// Built through u8string so non-ASCII names survive on Windows.
std::string FileNameOnly(const std::string& absoluteUtf8Path)
{
    const std::filesystem::path path(
        std::u8string(reinterpret_cast<const char8_t*>(absoluteUtf8Path.data()), absoluteUtf8Path.size()));
    const std::u8string filenameU8 = path.filename().u8string();
    return std::string(reinterpret_cast<const char*>(filenameU8.data()), filenameU8.size());
}

} // namespace

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

MeshHandle GpuSkinningRigCache::GpuModelEntry::TryGetCpuMeshHandle(MeshHandle gpuMeshHandle) const
{
    for (const OutputGroup& group : outputGroups) {
        for (const OutputGroup::PartMeshBinding& binding : group.partMeshBindings) {
            if (binding.gpuMeshHandle == gpuMeshHandle) {
                return binding.cpuMeshHandle;
            }
        }
    }
    return kInvalidMeshHandle;
}

void GpuSkinningRigCache::Register(Renderer& renderer, RenderSystem& renderSystem, GpuSkinningPipelines& pipelines,
    const std::string& absoluteGtaPath, const SkinnedMeshData& data, const std::vector<MeshAssetPart>& parts)
{
    if (data.skeleton.bones.empty() || data.bindPositions.empty() || parts.empty()) {
        return;
    }

    const std::vector<MeshAssetPartGroup> groups = GroupMeshAssetPartsBySharedVertexBuffer(renderSystem, parts);
    if (groups.empty()) {
        return;
    }

    pipelines.EnsureInitialized(renderer);

    using rg::GpuBuffer;
    using rg::GpuBufferUse;
    const rg::DeviceRef device = renderer.Device();

    const std::vector<GpuBindPoseVertex> packedBindPose = PackBindPoseVertices(data.bindPositions, data.bindNormals);
    GpuBuffer bindPoseBuffer = GpuBuffer::CreateWithData(device, packedBindPose.data(),
        packedBindPose.size() * sizeof(GpuBindPoseVertex), "GpuSkinningBindPose", GpuBufferUse::Storage);

    const std::vector<GpuSkinWeights> packedSkinWeights = PackSkinWeights(data.skinWeights);
    GpuBuffer skinWeightsBuffer = GpuBuffer::CreateWithData(device, packedSkinWeights.data(),
        packedSkinWeights.size() * sizeof(GpuSkinWeights), "GpuSkinningWeights", GpuBufferUse::Storage);

    // The one input rewritten every animated frame.
    GpuBuffer boneMatricesBuffer(device, sizeof(Mat4) * data.skeleton.bones.size(), "GpuSkinningBoneMatrices",
        GpuBufferUse::Storage | GpuBufferUse::HostWrite);

    const bool anyTextured =
        std::any_of(groups.begin(), groups.end(), [](const MeshAssetPartGroup& group) { return group.textured; });
    std::optional<GpuBuffer> uvBuffer;
    if (anyTextured) {
        const std::vector<GpuUv> packedUvs = PackUvs(data.uvs);
        uvBuffer.emplace(GpuBuffer::CreateWithData(
            device, packedUvs.data(), packedUvs.size() * sizeof(GpuUv), "GpuSkinningUv", GpuBufferUse::Storage));
    }

    GpuModelEntry entry{ std::move(bindPoseBuffer), std::move(skinWeightsBuffer), std::move(uvBuffer),
        std::move(boneMatricesBuffer), {} };

    const std::uint32_t vertexCount = static_cast<std::uint32_t>(data.bindPositions.size());
    entry.outputGroups.reserve(groups.size());
    const std::string displayFileName = FileNameOnly(absoluteGtaPath);

    std::size_t groupIndex = 0;
    for (const MeshAssetPartGroup& group : groups) {
        OutputGroup outputGroup;
        outputGroup.isTextured = group.textured;
        outputGroup.vertexCount = vertexCount;
        outputGroup.debugName = displayFileName + "#SkinGroup" + std::to_string(groupIndex);
        ++groupIndex;

        const std::size_t outputSize = group.textured
            ? static_cast<std::size_t>(vertexCount) * sizeof(GpuSkinnedVertexPositionNormalUv)
            : static_cast<std::size_t>(vertexCount) * sizeof(GpuSkinnedVertexPositionNormal);
        outputGroup.outputVertexBuffer =
            GpuBuffer(device, outputSize, "GpuSkinningOutput", GpuBufferUse::Storage | GpuBufferUse::Vertex);

        const VkDescriptorSetLayout layout = group.textured ? pipelines.PositionNormalUvDescriptorSetLayout()
                                                             : pipelines.PositionNormalDescriptorSetLayout();
        outputGroup.descriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(layout));
        GpuSkinningPipelines::BindBuffers(outputGroup.descriptorSet, entry.bindPoseBuffer, entry.skinWeightsBuffer,
            entry.boneMatricesBuffer, outputGroup.outputVertexBuffer,
            group.textured && entry.uvBuffer.has_value() ? &*entry.uvBuffer : nullptr);
        outputGroup.descriptorSetHandle = RegisterDescriptorSet(outputGroup.descriptorSet.Native());

        // One GPU-skinned mesh per part, all sharing the group's output as their vertex buffer.
        for (const std::size_t partIndex : group.partIndices) {
            const MeshAssetPart& part = parts[partIndex];
            if (part.indices.empty()) {
                continue;
            }

            Mesh gpuMesh = renderer.CreateMeshFromSharedVertexBuffer(outputGroup.outputVertexBuffer, vertexCount,
                part.indices.data(), static_cast<uint64_t>(part.indices.size() * sizeof(std::uint32_t)),
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
