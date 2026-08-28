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

// Display-only helper for OutputGroup::debugName below - the Editor's
// "Render Graph" panel (Panels/RenderGraphPanel.cpp) shows this pass/
// imported-buffer name verbatim, so it must stay a short, human-readable
// label ("Furina.gta#SkinGroup0"), never the full absolute on-disk path
// ("C:\Users\...\build\Project\Furina.gta#SkinGroup0") the caller actually
// hands Register() (see AnimationSystem::RegisterGpuSkinnedMesh()'s own
// `absoluteGtaPath` parameter, which is - and must stay - the full path,
// since it also doubles as this cache's own m_models lookup key).
//
// Same std::u8string round-trip MeshAssetGpuCatalog.cpp's own
// Utf8PathFromGamePath()/PathToUtf8() use - std::filesystem::path must be
// built from an explicit std::u8string, never straight from a plain
// std::string, or a non-ASCII (e.g. Japanese) path/filename gets
// reinterpreted through the OS's native/legacy codepage instead of UTF-8 on
// Windows and comes out corrupted.
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
    // AnimationSystem::Update() every frame this model is animated in GPU
    // mode. See this phase's own strategy document, Step 3.3.
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

    // Computed once, outside the loop below - see FileNameOnly()'s own doc
    // comment for why this (never absoluteGtaPath itself) is what feeds
    // OutputGroup::debugName.
    const std::string displayFileName = FileNameOnly(absoluteGtaPath);

    std::size_t groupIndex = 0;
    for (const MeshAssetPartGroup& group : groups) {
        OutputGroup outputGroup;
        outputGroup.isTextured = group.textured;
        outputGroup.vertexCount = vertexCount;
        // Phase 5 - a stable, persistent name for this group's compute pass/
        // imported buffer, living as long as this OutputGroup does (see
        // OutputGroup::debugName's own doc comment above for why this must
        // never be a per-frame temporary).
        outputGroup.debugName = displayFileName + "#SkinGroup" + std::to_string(groupIndex);
        ++groupIndex;

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
