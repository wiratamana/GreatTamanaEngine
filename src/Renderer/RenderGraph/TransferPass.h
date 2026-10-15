#pragma once

#include "RenderGraphBuilder.h"
#include "RenderGraphHandles.h"
#include "StaticName.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace gte::rg {

struct PassContext;

// Size sentinel for Fill: "to the end of the buffer".
inline constexpr std::uint64_t kWholeBuffer = ~std::uint64_t{ 0 };

// One transfer operation, as plain data. Declaring and recording it are separate pure steps.
struct TransferRecord {
    enum class Op : std::uint8_t { None, FillBuffer, CopyBuffer, CopyTexture, ClearTexture };

    Op op = Op::None;
    BufferHandle srcBuffer;
    BufferHandle dstBuffer;
    TextureHandle srcTexture;
    TextureHandle dstTexture;
    std::uint64_t size = kWholeBuffer;
    std::uint64_t srcOffset = 0;
    std::uint64_t dstOffset = 0;
    std::uint32_t fillValue = 0;
    std::array<float, 4> clearColor{};

    static TransferRecord Fill(BufferHandle dst, std::uint32_t value, std::uint64_t offset = 0,
        std::uint64_t size = kWholeBuffer);
    static TransferRecord CopyBuffer(BufferHandle src, BufferHandle dst, std::uint64_t size,
        std::uint64_t srcOffset = 0, std::uint64_t dstOffset = 0);
    static TransferRecord CopyTexture(TextureHandle src, TextureHandle dst);
    static TransferRecord Clear(TextureHandle dst, const std::array<float, 4>& color);
};

// Why `record` cannot be declared, or nullopt. Fill offsets and sizes are 4-byte multiples.
std::optional<std::string> ValidateTransferRecord(const TransferRecord& record);

// Declares the reads and writes of a validated record (the graph plans the barriers).
void DeclareTransfer(RenderGraphBuilder::PassBuilder& pass, const TransferRecord& record);

// Records the copy, fill or clear. Textures must be 2D color images created with the transfer usage bits.
void ExecuteTransfer(PassContext& ctx, const TransferRecord& record);

// One transfer pass: Fill, Copy, Clear or Readback, then Record().
//   TransferPass(builder, "Batch.ResetCount", scope, event).Fill(count, 0).Record();
class TransferPass {
public:
    TransferPass(RenderGraphBuilder& builder, StaticName name, ViewScope scope, RenderPassEvent event);
    ~TransferPass();

    TransferPass(const TransferPass&) = delete;
    TransferPass& operator=(const TransferPass&) = delete;
    TransferPass(TransferPass&&) = delete;
    TransferPass& operator=(TransferPass&&) = delete;

    TransferPass& Fill(BufferHandle dst, std::uint32_t value, std::uint64_t offset = 0, std::uint64_t size = kWholeBuffer);
    TransferPass& Copy(BufferHandle src, BufferHandle dst, std::uint64_t size, std::uint64_t srcOffset = 0,
        std::uint64_t dstOffset = 0);
    TransferPass& Copy(TextureHandle src, TextureHandle dst);
    TransferPass& Clear(TextureHandle dst, const std::array<float, 4>& color);

    // Copies `size` bytes of a graph buffer into a host-readable buffer and keeps that buffer alive.
    TransferPass& Readback(BufferHandle src, BufferHandle hostBuffer, std::uint64_t size);

    // Declares the pass. False (and one error in the log) when the record is invalid or set twice.
    bool Record();

private:
    void Set(const TransferRecord& record);

    RenderGraphBuilder& m_builder;
    StaticName m_name;
    ViewScope m_scope;
    RenderPassEvent m_event;
    TransferRecord m_record;
    BufferHandle m_keep;
    bool m_duplicateOp = false;
    bool m_recorded = false;
};

} // namespace gte::rg
