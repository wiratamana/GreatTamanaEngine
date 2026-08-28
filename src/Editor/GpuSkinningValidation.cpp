#include "GpuSkinningValidation.h"

#include "../Animation/VertexSkinning.h"
#include "../Game/Instantiation/MeshAssetPartGrouping.h"
#include "../Renderer/ComputeDispatch.h"
#include "../Renderer/GpuSkinning/GpuSkinningTypes.h"
#include "../Renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

namespace gte {

namespace {

double VectorLength(float x, float y, float z)
{
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    const double dz = static_cast<double>(z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

std::string ToDiagnosticString(const GpuSkinningValidationResult& result)
{
    if (!result.succeeded) {
        return "GPU Skinning Validation: FAILED - " + result.failureReason;
    }

    char buffer[640];
    std::snprintf(buffer, sizeof(buffer),
        "GPU Skinning Validation: %zu vertices (%s)\n"
        "  max position delta:  %.8f\n"
        "  mean position delta: %.8f\n"
        "  max normal delta:    %.8f\n"
        "  mean normal delta:   %.8f\n"
        "  epsilon:              %.8f\n"
        "  vertices exceeding epsilon: %zu / %zu",
        result.vertexCount, result.textured ? "textured" : "untextured", result.maxPositionDelta,
        result.meanPositionDelta, result.maxNormalDelta, result.meanNormalDelta, result.epsilon,
        result.verticesExceedingEpsilon, result.vertexCount);
    return std::string(buffer);
}

GpuSkinningValidationResult ValidateGpuSkinningAgainstCpuOracle(Renderer& renderer, GpuSkinningPipelines& pipelines,
    const std::vector<Vec3>& bindPositions, const std::vector<Vec3>& bindNormals,
    const std::vector<VertexSkinWeights>& skinWeights, const std::vector<Vec2>& uvs,
    const std::vector<Mat4>& skinningMatrices, double epsilon)
{
    GpuSkinningValidationResult result;
    result.epsilon = epsilon;
    result.vertexCount = bindPositions.size();
    result.textured = !uvs.empty();

    if (bindPositions.empty()) {
        result.failureReason = "bindPositions is empty - nothing to validate.";
        return result;
    }
    if (result.textured && uvs.size() != bindPositions.size()) {
        result.failureReason = "uvs.size() does not match bindPositions.size() - refusing to read out of bounds.";
        return result;
    }
    if (skinningMatrices.empty()) {
        result.failureReason = "skinningMatrices is empty - nothing to skin against.";
        return result;
    }

    pipelines.EnsureInitialized(renderer);

    // --- Step 1: the CPU oracle - see AGENTS.md's "the CPU path stays the
    // oracle, never modified to 'agree' with the GPU path" rule. -----------
    std::vector<Vec3> cpuPositions;
    std::vector<Vec3> cpuNormals;
    SkinVertices(bindPositions, bindNormals, skinWeights, skinningMatrices, cpuPositions, cpuNormals);

    // --- Step 2: pack + upload the GPU-side inputs, exactly per Phase 1's
    // own Pack*() functions - never a hand-rolled second copy of this
    // padding logic. --------------------------------------------------------
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(bindPositions.size());

    const std::vector<GpuBindPoseVertex> packedBindPose = PackBindPoseVertices(bindPositions, bindNormals);
    Buffer bindPoseBuffer = renderer.CreateDeviceLocalBuffer(packedBindPose.data(),
        static_cast<VkDeviceSize>(packedBindPose.size() * sizeof(GpuBindPoseVertex)),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "GpuSkinningValidationBindPose");

    const std::vector<GpuSkinWeights> packedWeights = PackSkinWeights(skinWeights);
    Buffer skinWeightsBuffer = renderer.CreateDeviceLocalBuffer(packedWeights.data(),
        static_cast<VkDeviceSize>(packedWeights.size() * sizeof(GpuSkinWeights)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "GpuSkinningValidationWeights");

    // Bone matrices need no repacking at all (Mat4::Data() is already
    // GLSL-column-major - see Phase 1's own strategy document, Step 3.3) -
    // a one-shot upload is enough for this validation run (production's own
    // GpuSkinningRigCache re-uploads this every frame; this tool only ever
    // needs it once, for one fixed pose).
    Buffer boneMatricesBuffer = renderer.CreateDeviceLocalBuffer(skinningMatrices.data(),
        static_cast<VkDeviceSize>(skinningMatrices.size() * sizeof(Mat4)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        "GpuSkinningValidationBoneMatrices");

    std::optional<Buffer> uvBuffer;
    if (result.textured) {
        const std::vector<GpuUv> packedUvs = PackUvs(uvs);
        uvBuffer.emplace(renderer.CreateDeviceLocalBuffer(packedUvs.data(),
            static_cast<VkDeviceSize>(packedUvs.size() * sizeof(GpuUv)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            "GpuSkinningValidationUv"));
    }

    // STORAGE_BUFFER (the compute kernel's real output binding) +
    // VERTEX_BUFFER (mirroring production's own combined-usage buffer - see
    // GpuResourceFactory::CreateGpuSkinningTargetBuffer()) + TRANSFER_SRC_BIT
    // (this validation tool's OWN extra requirement, to read the result
    // back to the CPU afterward - never part of the production buffer's own
    // usage flags, since production never reads it back on the CPU).
    const VkDeviceSize outputElementStride =
        result.textured ? sizeof(GpuSkinnedVertexPositionNormalUv) : sizeof(GpuSkinnedVertexPositionNormal);
    Buffer outputBuffer = renderer.CreateStructuredBuffer(outputElementStride, vertexCount, BufferMemoryUsage::GpuOnly,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "GpuSkinningValidationOutput");

    // --- Step 3: descriptor set, per Phase 1's binding table (+ Phase 2's
    // own binding-4 UV addition for the textured variant). ------------------
    const VkDescriptorSetLayout layout = result.textured ? pipelines.PositionNormalUvDescriptorSetLayout()
                                                          : pipelines.PositionNormalDescriptorSetLayout();
    ComputeDescriptorSet descriptorSet(renderer.AllocateComputeDescriptorSet(layout));

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();

    std::vector<ComputeDescriptorWrite> writes;
    writes.push_back(ComputeDescriptorWrite::StorageBuffer(0, bindPoseBuffer.Native()));
    writes.push_back(ComputeDescriptorWrite::StorageBuffer(1, skinWeightsBuffer.Native()));
    writes.push_back(ComputeDescriptorWrite::StorageBuffer(2, boneMatricesBuffer.Native()));
    writes.push_back(ComputeDescriptorWrite::StorageBuffer(3, outputBuffer.Native()));
    if (result.textured && uvBuffer.has_value()) {
        writes.push_back(ComputeDescriptorWrite::StorageBuffer(4, uvBuffer->Native()));
    }
    descriptorSet.Rewrite(context.device, writes);

    // --- Step 4: dispatch (SELF-CONTAINED - NOT through the full
    // RenderGraph, per Phase 6 v2's own Step 3.1) + readback, both inside
    // ONE Renderer::ImmediateSubmit() command buffer. ------------------------
    const VkDeviceSize outputSizeBytes = outputBuffer.Size();
    Buffer readbackBuffer = renderer.CreateBuffer(
        outputSizeBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferMemoryUsage::GpuToCpu, "GpuSkinningValidationReadback");

    const ComputePipeline& pipeline =
        result.textured ? pipelines.PositionNormalUvPipeline() : pipelines.PositionNormalPipeline();
    const VkDescriptorSet rawDescriptorSet = descriptorSet.Native();
    const VkBuffer rawOutputBuffer = outputBuffer.Native();
    const VkBuffer rawReadbackBuffer = readbackBuffer.Native();

    renderer.ImmediateSubmit([&](VkCommandBuffer cmd) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Native());
        vkCmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Layout(), 0, 1, &rawDescriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, pipeline.Layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(vertexCount), &vertexCount);

        const std::uint32_t groupCountX = ComputeGroupCount(vertexCount, kSkinningLocalSizeX);
        vkCmdDispatch(cmd, groupCountX, 1, 1);

        // The compute shader's own write must fully complete (and become
        // visible) before the copy below reads it - a real buffer memory
        // barrier, never assumed-safe-by-coincidence.
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = rawOutputBuffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
            1, &barrier, 0, nullptr);

        VkBufferCopy copyRegion{};
        copyRegion.size = outputSizeBytes;
        vkCmdCopyBuffer(cmd, rawOutputBuffer, rawReadbackBuffer, 1, &copyRegion);
    });
    // ImmediateSubmit() blocks until the GPU finishes - readbackBuffer's own
    // mapped memory is guaranteed valid/current by the time control returns
    // here.

    // --- Step 5: compare, vertex-for-vertex, against the CPU oracle. -------
    const void* rawReadback = readbackBuffer.MappedData();
    if (rawReadback == nullptr) {
        result.failureReason = "Readback buffer was not host-mapped (unexpected BufferMemoryUsage) - cannot compare.";
        return result;
    }

    double positionDeltaSum = 0.0;
    double normalDeltaSum = 0.0;
    double maxPositionDelta = 0.0;
    double maxNormalDelta = 0.0;
    std::size_t exceeding = 0;

    for (std::size_t i = 0; i < result.vertexCount; ++i) {
        float gpuPositionX = 0.0f;
        float gpuPositionY = 0.0f;
        float gpuPositionZ = 0.0f;
        float gpuNormalX = 0.0f;
        float gpuNormalY = 0.0f;
        float gpuNormalZ = 0.0f;

        if (result.textured) {
            const auto* vertices = static_cast<const GpuSkinnedVertexPositionNormalUv*>(rawReadback);
            const GpuSkinnedVertexPositionNormalUv& v = vertices[i];
            gpuPositionX = v.positionX;
            gpuPositionY = v.positionY;
            gpuPositionZ = v.positionZ;
            gpuNormalX = v.normalX;
            gpuNormalY = v.normalY;
            gpuNormalZ = v.normalZ;
        } else {
            const auto* vertices = static_cast<const GpuSkinnedVertexPositionNormal*>(rawReadback);
            const GpuSkinnedVertexPositionNormal& v = vertices[i];
            gpuPositionX = v.positionX;
            gpuPositionY = v.positionY;
            gpuPositionZ = v.positionZ;
            gpuNormalX = v.normalX;
            gpuNormalY = v.normalY;
            gpuNormalZ = v.normalZ;
        }

        const double positionDelta = VectorLength(
            gpuPositionX - cpuPositions[i].x, gpuPositionY - cpuPositions[i].y, gpuPositionZ - cpuPositions[i].z);
        const double normalDelta =
            VectorLength(gpuNormalX - cpuNormals[i].x, gpuNormalY - cpuNormals[i].y, gpuNormalZ - cpuNormals[i].z);

        positionDeltaSum += positionDelta;
        normalDeltaSum += normalDelta;
        maxPositionDelta = std::max(maxPositionDelta, positionDelta);
        maxNormalDelta = std::max(maxNormalDelta, normalDelta);
        if (positionDelta > epsilon || normalDelta > epsilon) {
            ++exceeding;
        }
    }

    result.maxPositionDelta = maxPositionDelta;
    result.meanPositionDelta = positionDeltaSum / static_cast<double>(result.vertexCount);
    result.maxNormalDelta = maxNormalDelta;
    result.meanNormalDelta = normalDeltaSum / static_cast<double>(result.vertexCount);
    result.verticesExceedingEpsilon = exceeding;
    result.succeeded = true;
    return result;
}

GpuSkinningGroupingParityResult ValidateGpuSkinningGroupingParity(RenderSystem& renderSystem,
    const GpuSkinningRigCache::GpuModelEntry& gpuEntry, const std::vector<MeshAssetPart>& parts)
{
    GpuSkinningGroupingParityResult result;

    const std::vector<MeshAssetPartGroup> cpuGroups = GroupMeshAssetPartsBySharedVertexBuffer(renderSystem, parts);
    result.cpuGroupCount = cpuGroups.size();
    result.gpuGroupCount = gpuEntry.outputGroups.size();
    result.succeeded = (result.cpuGroupCount == result.gpuGroupCount);

    if (!result.succeeded) {
        result.failureReason = "CPU grouping produced " + std::to_string(result.cpuGroupCount)
            + " distinct shared-vertex-buffer group(s), but the GPU model entry has "
            + std::to_string(result.gpuGroupCount)
            + " OutputGroup(s) - these must match exactly (see GPU_SKINNING_PHASE4_PER_MODEL_RESOURCE_MANAGEMENT_STRATEGY_v1.md).";
    }

    return result;
}

} // namespace gte
