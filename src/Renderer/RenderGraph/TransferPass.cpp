#include "TransferPass.h"

#include "BoundedOnceSet.h"
#include "PassKindDefaults.h"
#include "RenderGraph.h"
#include "../../Core/Logging.h"

#include <algorithm>
#include <cassert>
#include <mutex>
#include <utility>

namespace gte::rg {

namespace {

constexpr const char* kLogCategory = "TransferPass";

// Logs a message once per distinct text, so a bad chain declared every frame cannot flood the log.
void LogOnce(const std::string& message)
{
    static std::mutex mutex;
    static BoundedOnceSet<32> seen;
    const std::scoped_lock lock(mutex);
    switch (seen.Note(message)) {
    case BoundedOnceSet<32>::Result::First:
        GTE_LOG_ERROR(kLogCategory, message);
        break;
    case BoundedOnceSet<32>::Result::Overflow:
        GTE_LOG_ERROR(kLogCategory, "Further transfer pass errors are suppressed");
        break;
    default:
        break;
    }
}

} // namespace

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
    const VkCommandBuffer cmd = ctx.RawCommandBuffer();
    switch (record.op) {
    case Op::None:
        break;
    case Op::FillBuffer: {
        const VkBuffer buffer = ctx.resolveBuffer(record.dstBuffer);
        if (buffer != VK_NULL_HANDLE) {
            vkCmdFillBuffer(cmd, buffer, record.dstOffset, record.size, record.fillValue);
        }
        break;
    }
    case Op::CopyBuffer: {
        const VkBuffer src = ctx.resolveBuffer(record.srcBuffer);
        const VkBuffer dst = ctx.resolveBuffer(record.dstBuffer);
        if (src != VK_NULL_HANDLE && dst != VK_NULL_HANDLE) {
            const VkBufferCopy region{ record.srcOffset, record.dstOffset, record.size };
            vkCmdCopyBuffer(cmd, src, dst, 1, &region);
        }
        break;
    }
    case Op::CopyTexture: {
        const PassContext::ResolvedImage src = ctx.resolveColorImage(record.srcTexture);
        const PassContext::ResolvedImage dst = ctx.resolveColorImage(record.dstTexture);
        if (src.image != VK_NULL_HANDLE && dst.image != VK_NULL_HANDLE) {
            VkImageCopy region{};
            region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.extent = { std::min(src.extent.width, dst.extent.width), std::min(src.extent.height, dst.extent.height), 1 };
            vkCmdCopyImage(cmd, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        }
        break;
    }
    case Op::ClearTexture: {
        const PassContext::ResolvedImage dst = ctx.resolveColorImage(record.dstTexture);
        if (dst.image != VK_NULL_HANDLE) {
            VkClearColorValue color{};
            std::copy(record.clearColor.begin(), record.clearColor.end(), color.float32);
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(cmd, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
        }
        break;
    }
    }
}

TransferPass::TransferPass(RenderGraphBuilder& builder, StaticName name, ViewScope scope, RenderPassEvent event)
    : m_builder(builder)
    , m_name(name)
    , m_scope(scope)
    , m_event(event)
{
}

TransferPass::~TransferPass()
{
    if (!m_recorded) {
        assert(false && "TransferPass destroyed without Record()");
        LogOnce(std::string("Pass '") + m_name.value + "': chain ended without Record(): nothing was declared");
    }
}

void TransferPass::Set(const TransferRecord& record)
{
    m_duplicateOp = m_duplicateOp || m_record.op != TransferRecord::Op::None;
    m_record = record;
}

TransferPass& TransferPass::Fill(BufferHandle dst, std::uint32_t value, std::uint64_t offset, std::uint64_t size)
{
    Set(TransferRecord::Fill(dst, value, offset, size));
    return *this;
}

TransferPass& TransferPass::Copy(
    BufferHandle src, BufferHandle dst, std::uint64_t size, std::uint64_t srcOffset, std::uint64_t dstOffset)
{
    Set(TransferRecord::CopyBuffer(src, dst, size, srcOffset, dstOffset));
    return *this;
}

TransferPass& TransferPass::Copy(TextureHandle src, TextureHandle dst)
{
    Set(TransferRecord::CopyTexture(src, dst));
    return *this;
}

TransferPass& TransferPass::Clear(TextureHandle dst, const std::array<float, 4>& color)
{
    Set(TransferRecord::Clear(dst, color));
    return *this;
}

TransferPass& TransferPass::Readback(BufferHandle src, BufferHandle hostBuffer, std::uint64_t size)
{
    Set(TransferRecord::CopyBuffer(src, hostBuffer, size));
    m_keep = hostBuffer;
    return *this;
}

bool TransferPass::Record()
{
    if (m_recorded) {
        LogOnce(std::string("Pass '") + m_name.value + "': Record() called twice on one chain");
        return false;
    }
    m_recorded = true;

    if (m_duplicateOp) {
        LogOnce(std::string("Pass '") + m_name.value + "': one transfer pass carries one operation");
        return false;
    }
    if (const auto error = ValidateTransferRecord(m_record)) {
        LogOnce(std::string("Pass '") + m_name.value + "': " + *error);
        return false;
    }

    m_builder.AddRenderPass(
        m_name.value, DefaultPassKind(DeclarativePassType::Transfer), m_scope, RenderPassCategory::General,
        [record = m_record](RenderGraphBuilder::PassBuilder& pass) { DeclareTransfer(pass, record); },
        [record = m_record](PassContext& ctx) { ExecuteTransfer(ctx, record); },
        DefaultDrawKind(DeclarativePassType::Transfer), m_event);
    if (m_keep.IsValid()) {
        m_builder.KeepBufferOutput(m_keep);
    }
    return true;
}

} // namespace gte::rg
