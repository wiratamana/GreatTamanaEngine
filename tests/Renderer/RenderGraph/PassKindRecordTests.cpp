// Tier-1 tests for the pure records of the declarative pass kinds (transfer, mesh, indirect draw) and their
// defaults. Passes are declared on a bare builder or a bare PassRecord: no device, nothing executes.

#include "Renderer/RenderGraph/IndirectDrawPass.h"
#include "Renderer/RenderGraph/MeshPass.h"
#include "Renderer/RenderGraph/PassKindDefaults.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/TransferPass.h"

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace gte::rg {
namespace {

const BufferHandle kBufferA{ 1, 1 };
const BufferHandle kBufferB{ 2, 1 };
const BufferHandle kBufferC{ 3, 1 };
const BufferHandle kBufferD{ 4, 1 };
const TextureHandle kTexA{ 1, 1 };
const TextureHandle kTexB{ 2, 1 };

bool Contains(const std::optional<std::string>& text, const char* needle)
{
    return text.has_value() && text->find(needle) != std::string::npos;
}

// --- TransferRecord ------------------------------------------------------------

TEST(TransferRecordTest, FillIsValidAndDeclaresOneTransferDstWrite)
{
    const TransferRecord record = TransferRecord::Fill(kBufferA, 0, 0, 4);
    EXPECT_FALSE(ValidateTransferRecord(record).has_value());

    PassRecord pass;
    RenderGraphBuilder::PassBuilder builder(pass);
    DeclareTransfer(builder, record);

    EXPECT_TRUE(pass.reads.empty());
    ASSERT_EQ(pass.writes.size(), 1u);
    EXPECT_EQ(pass.writes[0].kind, ResourceKind::Buffer);
    EXPECT_EQ(pass.writes[0].buffer, kBufferA);
    EXPECT_EQ(pass.writes[0].access, ResourceAccess::TransferDst);
}

TEST(TransferRecordTest, FillOffsetsAndSizesMustBeMultiplesOfFour)
{
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord::Fill(kBufferA, 0, 2, 4)), "multiples of 4"));
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord::Fill(kBufferA, 0, 0, 6)), "multiples of 4"));
    EXPECT_FALSE(ValidateTransferRecord(TransferRecord::Fill(kBufferA, 0)).has_value()); // Whole buffer.
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord::Fill(kBufferA, 0, 0, 0)), "zero"));
}

TEST(TransferRecordTest, BufferCopyDeclaresASourceReadAndADestinationWrite)
{
    const TransferRecord record = TransferRecord::CopyBuffer(kBufferA, kBufferB, 16);
    EXPECT_FALSE(ValidateTransferRecord(record).has_value());

    PassRecord pass;
    RenderGraphBuilder::PassBuilder builder(pass);
    DeclareTransfer(builder, record);

    ASSERT_EQ(pass.reads.size(), 1u);
    EXPECT_EQ(pass.reads[0].buffer, kBufferA);
    EXPECT_EQ(pass.reads[0].access, ResourceAccess::TransferSrc);
    ASSERT_EQ(pass.writes.size(), 1u);
    EXPECT_EQ(pass.writes[0].buffer, kBufferB);
    EXPECT_EQ(pass.writes[0].access, ResourceAccess::TransferDst);
}

TEST(TransferRecordTest, BufferCopyRefusesInvalidSameAndUnsizedBuffers)
{
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord::CopyBuffer(BufferHandle{}, kBufferB, 4)), "invalid"));
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord::CopyBuffer(kBufferA, kBufferA, 4)), "same"));
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord::CopyBuffer(kBufferA, kBufferB, 0)), "explicit"));
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord::CopyBuffer(kBufferA, kBufferB, kWholeBuffer)), "explicit"));
}

TEST(TransferRecordTest, TextureCopyDeclaresATransferSrcReadAndATransferDstWrite)
{
    const TransferRecord record = TransferRecord::CopyTexture(kTexA, kTexB);
    EXPECT_FALSE(ValidateTransferRecord(record).has_value());

    PassRecord pass;
    RenderGraphBuilder::PassBuilder builder(pass);
    DeclareTransfer(builder, record);

    ASSERT_EQ(pass.reads.size(), 1u);
    EXPECT_EQ(pass.reads[0].texture, kTexA);
    EXPECT_EQ(pass.reads[0].access, ResourceAccess::TransferSrc);
    ASSERT_EQ(pass.writes.size(), 1u);
    EXPECT_EQ(pass.writes[0].texture, kTexB);
    EXPECT_EQ(pass.writes[0].access, ResourceAccess::TransferDst);
}

TEST(TransferRecordTest, ClearDeclaresOneTransferDstWriteAndKeepsTheColor)
{
    const std::array<float, 4> color{ 0.25f, 0.5f, 0.75f, 1.0f };
    const TransferRecord record = TransferRecord::Clear(kTexA, color);
    EXPECT_FALSE(ValidateTransferRecord(record).has_value());
    EXPECT_EQ(record.clearColor, color);

    PassRecord pass;
    RenderGraphBuilder::PassBuilder builder(pass);
    DeclareTransfer(builder, record);
    EXPECT_TRUE(pass.reads.empty());
    ASSERT_EQ(pass.writes.size(), 1u);
    EXPECT_EQ(pass.writes[0].access, ResourceAccess::TransferDst);
}

TEST(TransferRecordTest, EmptyRecordIsRefusedAndDeclaresNothing)
{
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord{}), "no operation"));

    PassRecord pass;
    RenderGraphBuilder::PassBuilder builder(pass);
    DeclareTransfer(builder, TransferRecord{});
    EXPECT_TRUE(pass.reads.empty());
    EXPECT_TRUE(pass.writes.empty());
}

TEST(TransferRecordTest, TextureCopyAndClearRefuseInvalidAndSameTextures)
{
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord::CopyTexture(TextureHandle{}, kTexB)), "invalid"));
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord::CopyTexture(kTexA, kTexA)), "same"));
    EXPECT_TRUE(Contains(ValidateTransferRecord(TransferRecord::Clear(TextureHandle{}, {})), "invalid"));
}

// --- MeshPass ------------------------------------------------------------------

TEST(MeshPassRecordTest, NeedsAtLeastOneAttachment)
{
    EXPECT_TRUE(Contains(ValidateMeshPassRecord(MeshPassRecord{}), "attachment"));

    MeshPassRecord depthOnly;
    depthOnly.depth = kTexA;
    EXPECT_FALSE(ValidateMeshPassRecord(depthOnly).has_value());
}

TEST(MeshPassRecordTest, DeclaresColorAndDepthWritesWithTheirClears)
{
    MeshPassRecord record;
    record.color = kTexA;
    record.depth = kTexB;
    record.clearColor = std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f };
    record.clearDepth = 1.0f;

    PassRecord pass;
    RenderGraphBuilder::PassBuilder builder(pass);
    DeclareMeshPass(builder, record);

    ASSERT_EQ(pass.colorAttachments.size(), 1u);
    EXPECT_EQ(pass.colorAttachments[0].handle, kTexA);
    ASSERT_EQ(pass.writes.size(), 2u);
    EXPECT_EQ(pass.writes[1].texture, kTexB);
    EXPECT_EQ(pass.writes[1].access, ResourceAccess::DepthStencilAttachmentReadWrite);
    ASSERT_TRUE(pass.depthClearValue.has_value());
    EXPECT_FLOAT_EQ(*pass.depthClearValue, 1.0f);
}

TEST(MeshPassRecordTest, DepthOnlyDeclaresNoColorAttachment)
{
    MeshPassRecord record;
    record.depth = kTexB;

    PassRecord pass;
    RenderGraphBuilder::PassBuilder builder(pass);
    DeclareMeshPass(builder, record);
    EXPECT_TRUE(pass.colorAttachments.empty());
    EXPECT_EQ(pass.writes.size(), 1u);
}

TEST(MeshPassTest, DrawDeclaresAGraphicsPassOnlyWithAnAttachmentAndADrawFunction)
{
    RenderGraphBuilder builder;
    EXPECT_FALSE(MeshPass(builder, "Test.NoTarget", ViewScope::Primary, RenderPassEvent::PreOpaques).Draw([] {}));
    EXPECT_FALSE(MeshPass(builder, "Test.NoDraw", ViewScope::Primary, RenderPassEvent::PreOpaques)
                     .Depth(kTexA, 1.0f)
                     .Draw(nullptr));
    EXPECT_EQ(builder.DeclaredPassCount(), 0u);

    EXPECT_TRUE(MeshPass(builder, "Test.Mesh", ViewScope::Primary, RenderPassEvent::PreOpaques)
                    .Depth(kTexA, 1.0f)
                    .Draw([] {}));
    EXPECT_EQ(builder.DeclaredPassCount(), 1u);
    EXPECT_EQ(builder.PassEventAt(0), RenderPassEvent::PreOpaques);
}

// --- IndirectDrawRecord --------------------------------------------------------

IndirectDrawRecord ValidIndirect()
{
    IndirectDrawRecord record;
    record.target = kTexA;
    record.indirect = kBufferA;
    record.count = kBufferB;
    record.instances = kBufferC;
    record.visible = kBufferD;
    record.maxDrawCount = 8;
    return record;
}

TEST(IndirectDrawRecordTest, ValidRecordDeclaresTargetsAndTheFourBufferReads)
{
    const IndirectDrawRecord record = ValidIndirect();
    EXPECT_FALSE(ValidateIndirectDrawRecord(record).has_value());

    PassRecord pass;
    RenderGraphBuilder::PassBuilder builder(pass);
    DeclareIndirectDraw(builder, record);

    ASSERT_EQ(pass.colorAttachments.size(), 1u);
    EXPECT_EQ(pass.colorAttachments[0].handle, kTexA);
    ASSERT_EQ(pass.writes.size(), 2u);
    EXPECT_EQ(pass.writes[1].access, ResourceAccess::DepthStencilAttachmentReadWrite);
    ASSERT_EQ(pass.reads.size(), 4u);
    EXPECT_EQ(pass.reads[0].access, ResourceAccess::IndirectCommandRead);
    EXPECT_EQ(pass.reads[0].buffer, kBufferA);
    EXPECT_EQ(pass.reads[1].access, ResourceAccess::IndirectCommandRead);
    EXPECT_EQ(pass.reads[1].buffer, kBufferB);
    EXPECT_EQ(pass.reads[2].access, ResourceAccess::VertexShaderStorageRead);
    EXPECT_EQ(pass.reads[2].buffer, kBufferC);
    EXPECT_EQ(pass.reads[3].access, ResourceAccess::VertexShaderStorageRead);
    EXPECT_EQ(pass.reads[3].buffer, kBufferD);
}

TEST(IndirectDrawRecordTest, ClearValuesDefaultToNoneAndAreDeclaredWhenSet)
{
    IndirectDrawRecord record = ValidIndirect();
    EXPECT_FALSE(record.clearColor.has_value());
    EXPECT_FALSE(record.clearDepth.has_value());

    PassRecord plain;
    RenderGraphBuilder::PassBuilder plainBuilder(plain);
    DeclareIndirectDraw(plainBuilder, record);
    EXPECT_FALSE(plain.colorClearValue.has_value());
    EXPECT_FALSE(plain.depthClearValue.has_value());

    record.clearColor = std::array<float, 4>{ 0.1f, 0.2f, 0.3f, 1.0f };
    record.clearDepth = 1.0f;
    PassRecord cleared;
    RenderGraphBuilder::PassBuilder clearedBuilder(cleared);
    DeclareIndirectDraw(clearedBuilder, record);
    ASSERT_TRUE(cleared.colorClearValue.has_value());
    EXPECT_FLOAT_EQ((*cleared.colorClearValue)[2], 0.3f);
    ASSERT_TRUE(cleared.depthClearValue.has_value());
    EXPECT_FLOAT_EQ(*cleared.depthClearValue, 1.0f);
}

TEST(IndirectDrawRecordTest, EveryMissingPartIsNamed)
{
    IndirectDrawRecord record = ValidIndirect();
    record.target = TextureHandle{};
    EXPECT_TRUE(Contains(ValidateIndirectDrawRecord(record), "target"));

    record = ValidIndirect();
    record.indirect = BufferHandle{};
    EXPECT_TRUE(Contains(ValidateIndirectDrawRecord(record), "indirect"));

    record = ValidIndirect();
    record.instances = BufferHandle{};
    EXPECT_TRUE(Contains(ValidateIndirectDrawRecord(record), "instance"));

    record = ValidIndirect();
    record.visible = BufferHandle{};
    EXPECT_TRUE(Contains(ValidateIndirectDrawRecord(record), "visible"));

    record = ValidIndirect();
    record.count = BufferHandle{};
    EXPECT_TRUE(Contains(ValidateIndirectDrawRecord(record), "count"));

    record = ValidIndirect();
    record.maxDrawCount = 0;
    EXPECT_TRUE(Contains(ValidateIndirectDrawRecord(record), "maxDrawCount"));
}

// --- Defaults per kind ---------------------------------------------------------

TEST(PassKindDefaultsTest, ComputeAndTransferShareTheComputeKindAndTheOthersAreGraphics)
{
    EXPECT_EQ(DefaultPassKind(DeclarativePassType::Compute), PassKind::Compute);
    EXPECT_EQ(DefaultPassKind(DeclarativePassType::Transfer), PassKind::Compute);
    EXPECT_EQ(DefaultPassKind(DeclarativePassType::Fullscreen), PassKind::Graphics);
    EXPECT_EQ(DefaultPassKind(DeclarativePassType::Mesh), PassKind::Graphics);
    EXPECT_EQ(DefaultPassKind(DeclarativePassType::IndirectDraw), PassKind::Graphics);
}

TEST(PassKindDefaultsTest, OnlyFullscreenIsLabelledAsAQuad)
{
    EXPECT_EQ(DefaultDrawKind(DeclarativePassType::Fullscreen), RenderPassDrawKind::DrawQuad);
    EXPECT_EQ(DefaultDrawKind(DeclarativePassType::Mesh), RenderPassDrawKind::DrawMesh);
    EXPECT_EQ(DefaultDrawKind(DeclarativePassType::IndirectDraw), RenderPassDrawKind::DrawMesh);
    EXPECT_EQ(DefaultDrawKind(DeclarativePassType::Compute), RenderPassDrawKind::DrawMesh);
    EXPECT_EQ(DefaultDrawKind(DeclarativePassType::Transfer), RenderPassDrawKind::DrawMesh);
}

} // namespace
} // namespace gte::rg
