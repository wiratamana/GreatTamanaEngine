#include "TransferPass.h"

#include "RenderGraph.h"

#include <algorithm>
#include <string>

namespace gte::rg {

namespace {

constexpr const char* kLogCategory = "TransferPass";

} // namespace

TransferRecord TransferRecord::Readback(BufferHandle src, BufferHandle hostBuffer, std::uint64_t size)
{
    TransferRecord record = CopyBuffer(src, hostBuffer, size);
    record.hostRead = true;
    return record;
}

TransferRecord TransferRecord::Fill(BufferHandle dst, std::uint32_t value, std::uint64_t offset, std::uint64_t size)
{
    TransferRecord record;
    record.op = Op::FillBuffer;
    record.dstBuffer = dst;
    record.fillValue = value;
    record.dstOffset = offset;
    record.size = size;
    return record;
}

TransferRecord TransferRecord::CopyBuffer(
    BufferHandle src, BufferHandle dst, std::uint64_t size, std::uint64_t srcOffset, std::uint64_t dstOffset)
{
    TransferRecord record;
    record.op = Op::CopyBuffer;
    record.srcBuffer = src;
    record.dstBuffer = dst;
    record.size = size;
    record.srcOffset = srcOffset;
    record.dstOffset = dstOffset;
    return record;
}

TransferRecord TransferRecord::CopyTexture(TextureHandle src, TextureHandle dst)
{
    TransferRecord record;
    record.op = Op::CopyTexture;
    record.srcTexture = src;
    record.dstTexture = dst;
    return record;
}

TransferRecord TransferRecord::Clear(TextureHandle dst, const std::array<float, 4>& color)
{
    TransferRecord record;
    record.op = Op::ClearTexture;
    record.dstTexture = dst;
    record.clearColor = color;
    return record;
}

std::optional<std::string> ValidateTransferRecord(const TransferRecord& record)
{
    using Op = TransferRecord::Op;
    switch (record.op) {
    case Op::None:
        return std::string("no operation was set");
    case Op::FillBuffer:
        if (!record.dstBuffer.IsValid()) {
            return std::string("Fill: the destination buffer is invalid");
        }
        if (record.size == 0) {
            return std::string("Fill: the size is zero");
        }
        if (record.dstOffset % 4 != 0 || (record.size != kWholeBuffer && record.size % 4 != 0)) {
            return std::string("Fill: offset and size must be multiples of 4");
        }
        return std::nullopt;
    case Op::CopyBuffer:
        if (!record.srcBuffer.IsValid() || !record.dstBuffer.IsValid()) {
            return std::string("Copy: a buffer handle is invalid");
        }
        if (record.srcBuffer == record.dstBuffer) {
            return std::string("Copy: source and destination are the same buffer");
        }
        if (record.size == 0 || record.size == kWholeBuffer) {
            return std::string("Copy: the size must be explicit and non-zero");
        }
        return std::nullopt;
    case Op::CopyTexture:
        if (!record.srcTexture.IsValid() || !record.dstTexture.IsValid()) {
            return std::string("Copy: a texture handle is invalid");
        }
        if (record.srcTexture == record.dstTexture) {
            return std::string("Copy: source and destination are the same texture");
        }
        return std::nullopt;
    case Op::ClearTexture:
        if (!record.dstTexture.IsValid()) {
            return std::string("Clear: the texture handle is invalid");
        }
        return std::nullopt;
    }
    return std::string("unknown operation");
}

void DeclareTransfer(RenderGraphBuilder::PassBuilder& pass, const TransferRecord& record)
{
    using Op = TransferRecord::Op;
    switch (record.op) {
    case Op::None:
        break;
    case Op::FillBuffer:
        pass.WriteBuffer(record.dstBuffer, ResourceAccess::TransferDst);
        break;
    case Op::CopyBuffer:
        pass.ReadBuffer(record.srcBuffer, ResourceAccess::TransferSrc);
        pass.WriteBuffer(record.dstBuffer, ResourceAccess::TransferDst);
        break;
    case Op::CopyTexture:
        pass.ReadTexture(record.srcTexture, ResourceAccess::TransferSrc, /*isDepthResource=*/false);
        pass.WriteTexture(record.dstTexture, ResourceAccess::TransferDst, /*isDepthResource=*/false);
        break;
    case Op::ClearTexture:
        pass.WriteTexture(record.dstTexture, ResourceAccess::TransferDst, /*isDepthResource=*/false);
        break;
    }
}

void ExecuteTransfer(PassContext& ctx, const TransferRecord& record)
{
    using Op = TransferRecord::Op;
    const VkCommandBuffer cmd = RecordingAccess::Native(ctx);
    const auto unresolved = [&ctx](const char* what) {
        ctx.ReportOnce(kLogCategory, std::string("transfer skipped: ") + what + " did not resolve");
    };
    switch (record.op) {
    case Op::None:
        break;
    case Op::FillBuffer: {
        const VkBuffer buffer = ctx.resolveBuffer(record.dstBuffer);
        if (buffer == VK_NULL_HANDLE) {
            unresolved("the fill buffer");
            break;
        }
        vkCmdFillBuffer(cmd, buffer, record.dstOffset, record.size, record.fillValue);
        break;
    }
    case Op::CopyBuffer: {
        const VkBuffer src = ctx.resolveBuffer(record.srcBuffer);
        const VkBuffer dst = ctx.resolveBuffer(record.dstBuffer);
        if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) {
            unresolved(src == VK_NULL_HANDLE ? "the source buffer" : "the destination buffer");
            break;
        }
        const VkBufferCopy region{ record.srcOffset, record.dstOffset, record.size };
        vkCmdCopyBuffer(cmd, src, dst, 1, &region);
        if (record.hostRead) {
            // Make the copy visible to the host read that follows the frame's fence wait.
            VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = dst;
            barrier.offset = record.dstOffset;
            barrier.size = record.size;
            VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmd, &dependency);
        }
        break;
    }
    case Op::CopyTexture: {
        const PassContext::ResolvedImage src = ctx.resolveColorImage(record.srcTexture);
        const PassContext::ResolvedImage dst = ctx.resolveColorImage(record.dstTexture);
        if (src.image == VK_NULL_HANDLE || dst.image == VK_NULL_HANDLE) {
            unresolved(src.image == VK_NULL_HANDLE ? "the source texture" : "the destination texture");
            break;
        }
        VkImageCopy region{};
        region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.extent = { std::min(src.extent.width, dst.extent.width), std::min(src.extent.height, dst.extent.height), 1 };
        vkCmdCopyImage(cmd, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        break;
    }
    case Op::ClearTexture: {
        const PassContext::ResolvedImage dst = ctx.resolveColorImage(record.dstTexture);
        if (dst.image == VK_NULL_HANDLE) {
            unresolved("the clear target");
            break;
        }
        VkClearColorValue color{};
        std::copy(record.clearColor.begin(), record.clearColor.end(), color.float32);
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(cmd, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
        break;
    }
    }
}

} // namespace gte::rg
