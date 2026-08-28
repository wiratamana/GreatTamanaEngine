// Unit tests for Phase 1's pure, Vulkan-free-of-behavior render graph
// vocabulary (src/Renderer/RenderGraph/RenderGraphTypes.h) - handles,
// resource descriptors, and the ResourceAccess enum's pure helper
// functions. No live VkDevice/Renderer/Registry involved at all - see
// RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md, Step 3.3, and
// tests/Renderer/DrawStatsTests.cpp for the table-driven style this
// mirrors.

#include "Renderer/RenderGraph/RenderGraphTypes.h"

#include <gtest/gtest.h>

#include <string_view>

namespace gte::rg {
namespace {

// --- Handles -------------------------------------------------------------

TEST(RenderGraphHandleTest, DefaultConstructedTextureHandleIsInvalid)
{
    const TextureHandle handle;
    EXPECT_FALSE(handle.IsValid());
}

TEST(RenderGraphHandleTest, ExplicitlyConstructedTextureHandleIsValid)
{
    const TextureHandle handle{ 3, 1 };
    EXPECT_TRUE(handle.IsValid());
}

TEST(RenderGraphHandleTest, DefaultConstructedBufferHandleIsInvalid)
{
    const BufferHandle handle;
    EXPECT_FALSE(handle.IsValid());
}

TEST(RenderGraphHandleTest, ExplicitlyConstructedBufferHandleIsValid)
{
    const BufferHandle handle{ 7, 2 };
    EXPECT_TRUE(handle.IsValid());
}

TEST(RenderGraphHandleTest, DefaultConstructedPassHandleIsInvalid)
{
    const PassHandle handle;
    EXPECT_FALSE(handle.IsValid());
}

TEST(RenderGraphHandleTest, ExplicitlyConstructedPassHandleIsValid)
{
    const PassHandle handle{ 0, 5 }; // index 0 is a perfectly valid slot - only kInvalidIndex means invalid.
    EXPECT_TRUE(handle.IsValid());
}

TEST(RenderGraphHandleTest, TextureHandleEqualityComparesBothFields)
{
    const TextureHandle a{ 1, 2 };
    const TextureHandle b{ 1, 2 };
    const TextureHandle differentIndex{ 9, 2 };
    const TextureHandle differentGeneration{ 1, 9 };

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == differentIndex);
    EXPECT_FALSE(a == differentGeneration);
}

TEST(RenderGraphHandleTest, BufferHandleEqualityComparesBothFields)
{
    const BufferHandle a{ 4, 6 };
    const BufferHandle b{ 4, 6 };
    const BufferHandle differentIndex{ 5, 6 };
    const BufferHandle differentGeneration{ 4, 7 };

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == differentIndex);
    EXPECT_FALSE(a == differentGeneration);
}

TEST(RenderGraphHandleTest, PassHandleEqualityComparesBothFields)
{
    const PassHandle a{ 2, 3 };
    const PassHandle b{ 2, 3 };
    const PassHandle differentIndex{ 8, 3 };
    const PassHandle differentGeneration{ 2, 8 };

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == differentIndex);
    EXPECT_FALSE(a == differentGeneration);
}

// --- TextureDesc / BufferDesc value equality ------------------------------
//
// This is the exact behavior Phase 4's pool-matching logic will depend on
// ("does an already-pooled resource have a desc that equals this one?") -
// pinned here first, independently, before Phase 4 ever exists.

TEST(RenderGraphDescTest, IdenticalTextureDescsCompareEqual)
{
    const TextureDesc a{ 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM, true };
    const TextureDesc b{ 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM, true };
    EXPECT_TRUE(a == b);
}

TEST(RenderGraphDescTest, TextureDescsDifferingInWidthCompareUnequal)
{
    const TextureDesc a{ 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM, true };
    const TextureDesc b{ 1280, 1080, VK_FORMAT_R8G8B8A8_UNORM, true };
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, TextureDescsDifferingInHeightCompareUnequal)
{
    const TextureDesc a{ 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM, true };
    const TextureDesc b{ 1920, 720, VK_FORMAT_R8G8B8A8_UNORM, true };
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, TextureDescsDifferingInFormatCompareUnequal)
{
    const TextureDesc a{ 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM, true };
    const TextureDesc b{ 1920, 1080, VK_FORMAT_R8G8B8A8_SRGB, true };
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, TextureDescsDifferingInHasDepthCompareUnequal)
{
    const TextureDesc a{ 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM, true };
    const TextureDesc b{ 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM, false };
    EXPECT_FALSE(a == b);
}

// v2 regression test: this exact class of struct used to (v1) carry a
// `debugName` field compared by raw pointer identity - two structurally
// identical descs built from entirely separate, non-string-literal-folded
// runtime inputs must still compare equal, since TextureDesc/BufferDesc no
// longer carry ANY field that isn't a genuine determinant of physical-
// resource shareability. Building each field from a runtime function call
// (rather than a compile-time constant) defeats any compiler folding that
// could otherwise mask a reintroduced identity-based comparison.
TEST(RenderGraphDescTest, TextureDescsBuiltFromSeparateRuntimeInputsStillCompareEqualWhenStructurallyIdentical)
{
    auto makeWidth = [](int scale) { return static_cast<std::uint32_t>(640 * scale); };
    auto makeHeight = [](int scale) { return static_cast<std::uint32_t>(360 * scale); };

    TextureDesc a;
    a.width = makeWidth(2);
    a.height = makeHeight(2);
    a.format = VK_FORMAT_B8G8R8A8_UNORM;
    a.hasDepth = true;

    TextureDesc b;
    b.width = makeWidth(2);
    b.height = makeHeight(2);
    b.format = VK_FORMAT_B8G8R8A8_UNORM;
    b.hasDepth = true;

    EXPECT_TRUE(a == b);
}

TEST(RenderGraphDescTest, IdenticalBufferDescsCompareEqual)
{
    const BufferDesc a{ 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
    const BufferDesc b{ 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
    EXPECT_TRUE(a == b);
}

TEST(RenderGraphDescTest, BufferDescsDifferingInSizeCompareUnequal)
{
    const BufferDesc a{ 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
    const BufferDesc b{ 2048, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, BufferDescsDifferingInUsageCompareUnequal)
{
    const BufferDesc a{ 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT };
    const BufferDesc b{ 1024, VK_BUFFER_USAGE_INDEX_BUFFER_BIT };
    EXPECT_FALSE(a == b);
}

// --- IsWriteAccess() - a case for every enumerator ------------------------

TEST(RenderGraphResourceAccessTest, ColorAttachmentWriteIsAWrite)
{
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::ColorAttachmentWrite));
}

TEST(RenderGraphResourceAccessTest, DepthStencilAttachmentReadWriteIsAWrite)
{
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::DepthStencilAttachmentReadWrite));
}

TEST(RenderGraphResourceAccessTest, ShaderReadIsNotAWrite)
{
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::ShaderRead));
}

TEST(RenderGraphResourceAccessTest, TransferSrcIsNotAWrite)
{
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::TransferSrc));
}

TEST(RenderGraphResourceAccessTest, TransferDstIsAWrite)
{
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::TransferDst));
}

// Phase 5 of the compute-shader campaign
// (COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md) - the three new
// enumerators.
TEST(RenderGraphResourceAccessTest, ComputeShaderReadIsNotAWrite)
{
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::ComputeShaderRead));
}

TEST(RenderGraphResourceAccessTest, ComputeShaderWriteIsAWrite)
{
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::ComputeShaderWrite));
}

TEST(RenderGraphResourceAccessTest, IndirectCommandReadIsNotAWrite)
{
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::IndirectCommandRead));
}

// GPU Vertex Skinning campaign, Phase 3
// (GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md) - the
// fourth new enumerator.
TEST(RenderGraphResourceAccessTest, VertexBufferReadIsNotAWrite)
{
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::VertexBufferRead));
}

// --- ToString() - one assertion per enumerator, non-empty, non-null ------

TEST(RenderGraphResourceAccessTest, ToStringCoversEveryEnumeratorNonNullNonEmpty)
{
    const ResourceAccess values[] = {
        ResourceAccess::ColorAttachmentWrite,
        ResourceAccess::DepthStencilAttachmentReadWrite,
        ResourceAccess::ShaderRead,
        ResourceAccess::TransferSrc,
        ResourceAccess::TransferDst,
        ResourceAccess::ComputeShaderRead,
        ResourceAccess::ComputeShaderWrite,
        ResourceAccess::IndirectCommandRead,
        ResourceAccess::VertexBufferRead,
    };

    for (const ResourceAccess value : values) {
        const char* name = ToString(value);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::string_view(name).size(), 0u);
    }
}

TEST(RenderGraphResourceAccessTest, ToStringProducesDistinctNamesForDistinctEnumerators)
{
    EXPECT_STREQ(ToString(ResourceAccess::ColorAttachmentWrite), "ColorAttachmentWrite");
    EXPECT_STREQ(ToString(ResourceAccess::DepthStencilAttachmentReadWrite), "DepthStencilAttachmentReadWrite");
    EXPECT_STREQ(ToString(ResourceAccess::ShaderRead), "ShaderRead");
    EXPECT_STREQ(ToString(ResourceAccess::TransferSrc), "TransferSrc");
    EXPECT_STREQ(ToString(ResourceAccess::TransferDst), "TransferDst");
    EXPECT_STREQ(ToString(ResourceAccess::ComputeShaderRead), "ComputeShaderRead");
    EXPECT_STREQ(ToString(ResourceAccess::ComputeShaderWrite), "ComputeShaderWrite");
    EXPECT_STREQ(ToString(ResourceAccess::IndirectCommandRead), "IndirectCommandRead");
    EXPECT_STREQ(ToString(ResourceAccess::VertexBufferRead), "VertexBufferRead");
}

// --- PassRecord / ResourceUsage - basic plain-data sanity -----------------
//
// Not compared for equality (see RenderGraphTypes.h's own comment on
// PassRecord::name) - just confirms the shape holds the fields later
// phases need and defaults sanely. ResourceUsage's ForTexture()/ForBuffer()
// tagged-union shape (grown in Phase 2, see RenderGraphTypes.h's own
// comment on ResourceUsage and RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md)
// is covered here rather than in RenderGraphBuilderTests.cpp since
// ResourceUsage itself still lives in, and is owned by, this Phase 1 file.

TEST(RenderGraphPassRecordTest, DefaultConstructedPassRecordIsEmptyAndNotCulled)
{
    const PassRecord record;
    EXPECT_EQ(record.name, nullptr);
    EXPECT_TRUE(record.reads.empty());
    EXPECT_TRUE(record.writes.empty());
    EXPECT_FALSE(record.isCulled);
    EXPECT_FALSE(static_cast<bool>(record.execute));
}

TEST(RenderGraphResourceUsageTest, ForTextureSetsKindAndTextureFields)
{
    const ResourceUsage usage = ResourceUsage::ForTexture(TextureHandle{ 5, 2 }, ResourceAccess::ShaderRead);
    EXPECT_EQ(usage.kind, ResourceKind::Texture);
    EXPECT_EQ(usage.texture, (TextureHandle{ 5, 2 }));
    EXPECT_EQ(usage.access, ResourceAccess::ShaderRead);
}

TEST(RenderGraphResourceUsageTest, ForBufferSetsKindAndBufferFields)
{
    const ResourceUsage usage = ResourceUsage::ForBuffer(BufferHandle{ 3, 1 }, ResourceAccess::TransferDst);
    EXPECT_EQ(usage.kind, ResourceKind::Buffer);
    EXPECT_EQ(usage.buffer, (BufferHandle{ 3, 1 }));
    EXPECT_EQ(usage.access, ResourceAccess::TransferDst);
}

TEST(RenderGraphPassRecordTest, ReadsAndWritesCanBeAppendedIndependently)
{
    PassRecord record;
    record.name = "TestPass";
    record.reads.push_back(ResourceUsage::ForTexture(TextureHandle{ 1, 1 }, ResourceAccess::ShaderRead));
    record.writes.push_back(ResourceUsage::ForTexture(TextureHandle{ 2, 1 }, ResourceAccess::ColorAttachmentWrite));

    ASSERT_EQ(record.reads.size(), 1u);
    ASSERT_EQ(record.writes.size(), 1u);
    EXPECT_EQ(record.reads[0].kind, ResourceKind::Texture);
    EXPECT_EQ(record.reads[0].texture, (TextureHandle{ 1, 1 }));
    EXPECT_EQ(record.reads[0].access, ResourceAccess::ShaderRead);
    EXPECT_EQ(record.writes[0].kind, ResourceKind::Texture);
    EXPECT_EQ(record.writes[0].texture, (TextureHandle{ 2, 1 }));
    EXPECT_EQ(record.writes[0].access, ResourceAccess::ColorAttachmentWrite);
}

} // namespace
} // namespace gte::rg
