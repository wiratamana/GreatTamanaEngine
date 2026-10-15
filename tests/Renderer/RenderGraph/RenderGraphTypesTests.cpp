// Tier-1 tests for the render graph vocabulary in RenderGraphTypes.h: handles, descriptors and pure helpers.

#include "Renderer/RenderGraph/RenderGraphTypes.h"
#include "Renderer/RenderGraph/RenderGraphPersistentResourceCache.h"

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

// --- TextureDesc / BufferDesc value equality (pool matching depends on it) ---

TEST(RenderGraphDescTest, IdenticalTextureDescsCompareEqual)
{
    const TextureDesc a{ 1920, 1080, PixelFormat::RGBA8_UNorm, true };
    const TextureDesc b{ 1920, 1080, PixelFormat::RGBA8_UNorm, true };
    EXPECT_TRUE(a == b);
}

TEST(RenderGraphDescTest, TextureDescsDifferingInWidthCompareUnequal)
{
    const TextureDesc a{ 1920, 1080, PixelFormat::RGBA8_UNorm, true };
    const TextureDesc b{ 1280, 1080, PixelFormat::RGBA8_UNorm, true };
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, TextureDescsDifferingInHeightCompareUnequal)
{
    const TextureDesc a{ 1920, 1080, PixelFormat::RGBA8_UNorm, true };
    const TextureDesc b{ 1920, 720, PixelFormat::RGBA8_UNorm, true };
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, TextureDescsDifferingInFormatCompareUnequal)
{
    const TextureDesc a{ 1920, 1080, PixelFormat::RGBA8_UNorm, true };
    const TextureDesc b{ 1920, 1080, PixelFormat::BGRA8_UNorm, true };
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, TextureDescsDifferingInHasDepthCompareUnequal)
{
    const TextureDesc a{ 1920, 1080, PixelFormat::RGBA8_UNorm, true };
    const TextureDesc b{ 1920, 1080, PixelFormat::RGBA8_UNorm, false };
    EXPECT_FALSE(a == b);
}

// --- TextureUsage / TextureDesc::usage ---

TEST(RenderGraphTextureUsageTest, DefaultIsNone)
{
    EXPECT_EQ(TextureUsage::None, TextureUsage{});
}

TEST(RenderGraphTextureUsageTest, OrCombinesDistinctBits)
{
    const TextureUsage combined = TextureUsage::Storage | TextureUsage::TransferSrc;
    EXPECT_TRUE(HasFlag(combined, TextureUsage::Storage));
    EXPECT_TRUE(HasFlag(combined, TextureUsage::TransferSrc));
    EXPECT_FALSE(HasFlag(combined, TextureUsage::TransferDst));
    EXPECT_FALSE(HasFlag(combined, TextureUsage::Sampled));
}

TEST(RenderGraphTextureUsageTest, OrAssignMutatesInPlace)
{
    TextureUsage usage = TextureUsage::Sampled;
    usage |= TextureUsage::Storage;
    EXPECT_TRUE(HasFlag(usage, TextureUsage::Sampled));
    EXPECT_TRUE(HasFlag(usage, TextureUsage::Storage));
}

TEST(RenderGraphTextureUsageTest, HasFlagIsFalseForNoneAgainstAnyRealBit)
{
    EXPECT_FALSE(HasFlag(TextureUsage::None, TextureUsage::Storage));
    EXPECT_FALSE(HasFlag(TextureUsage::None, TextureUsage::Sampled));
    EXPECT_FALSE(HasFlag(TextureUsage::None, TextureUsage::TransferSrc));
    EXPECT_FALSE(HasFlag(TextureUsage::None, TextureUsage::TransferDst));
}

// Descs differing only in `usage` must compare unequal, so the pool never hands back a non-storage image for a storage request.
TEST(RenderGraphDescTest, TextureDescsDifferingOnlyInUsageCompareUnequal)
{
    TextureDesc a{ 1920, 1080, PixelFormat::RGBA8_UNorm, true };
    a.usage = TextureUsage::None;
    TextureDesc b{ 1920, 1080, PixelFormat::RGBA8_UNorm, true };
    b.usage = TextureUsage::Storage;
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, TextureDescsWithIdenticalUsageCompareEqual)
{
    TextureDesc a{ 1920, 1080, PixelFormat::RGBA8_UNorm, true };
    a.usage = TextureUsage::Storage;
    TextureDesc b{ 1920, 1080, PixelFormat::RGBA8_UNorm, true };
    b.usage = TextureUsage::Storage;
    EXPECT_TRUE(a == b);
}

// Every field of a default-constructed TextureDesc.
TEST(RenderGraphDescTest, DefaultConstructedTextureDescHasExpectedDefaults)
{
    const TextureDesc desc{};
    EXPECT_EQ(desc.width, 0u);
    EXPECT_EQ(desc.height, 0u);
    EXPECT_EQ(desc.format, PixelFormat::Undefined);
    EXPECT_FALSE(desc.hasDepth);
    EXPECT_EQ(desc.usage, TextureUsage::None);
}

// Descs built from separate runtime inputs must compare equal: no identity-based field may take part in equality.
TEST(RenderGraphDescTest, TextureDescsBuiltFromSeparateRuntimeInputsStillCompareEqualWhenStructurallyIdentical)
{
    auto makeWidth = [](int scale) { return static_cast<std::uint32_t>(640 * scale); };
    auto makeHeight = [](int scale) { return static_cast<std::uint32_t>(360 * scale); };

    TextureDesc a;
    a.width = makeWidth(2);
    a.height = makeHeight(2);
    a.format = PixelFormat::BGRA8_UNorm;
    a.hasDepth = true;

    TextureDesc b;
    b.width = makeWidth(2);
    b.height = makeHeight(2);
    b.format = PixelFormat::BGRA8_UNorm;
    b.hasDepth = true;

    EXPECT_TRUE(a == b);
}

TEST(RenderGraphDescTest, IdenticalBufferDescsCompareEqual)
{
    const BufferDesc a{ 1024, BufferUsage::VertexBuffer };
    const BufferDesc b{ 1024, BufferUsage::VertexBuffer };
    EXPECT_TRUE(a == b);
}

TEST(RenderGraphDescTest, BufferDescsDifferingInSizeCompareUnequal)
{
    const BufferDesc a{ 1024, BufferUsage::VertexBuffer };
    const BufferDesc b{ 2048, BufferUsage::VertexBuffer };
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, BufferDescsDifferingInUsageCompareUnequal)
{
    const BufferDesc a{ 1024, BufferUsage::VertexBuffer };
    const BufferDesc b{ 1024, BufferUsage::IndexBuffer };
    EXPECT_FALSE(a == b);
}

// TextureDesc also describes 3D volumes, arrays and cubes - every shape field takes part in equality, so
// two requests only share a pooled resource when they are physically identical.

TEST(RenderGraphDescTest, DefaultTextureDescIsA2DTexture)
{
    const TextureDesc desc;
    EXPECT_EQ(desc.dimension, TextureDimension::Tex2D);
    EXPECT_EQ(desc.depth, 1u);
    EXPECT_EQ(desc.arrayLayers, 1u);
}

TEST(RenderGraphDescTest, IdenticalVolumeDescsCompareEqual)
{
    TextureDesc a{ 32, 32, PixelFormat::RGBA16_Float };
    a.dimension = TextureDimension::Tex3D;
    a.depth = 32;
    const TextureDesc b = a;
    EXPECT_TRUE(a == b);
}

TEST(RenderGraphDescTest, TextureDescsDifferingInDimensionCompareUnequal)
{
    TextureDesc a{ 32, 32, PixelFormat::RGBA16_Float };
    TextureDesc b = a;
    b.dimension = TextureDimension::Tex3D;
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, VolumeDescsDifferingInDepthCompareUnequal)
{
    TextureDesc a{ 32, 32, PixelFormat::RGBA16_Float };
    a.dimension = TextureDimension::Tex3D;
    a.depth = 32;
    TextureDesc b = a;
    b.depth = 16;
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, ArrayDescsDifferingInLayerCountCompareUnequal)
{
    TextureDesc a{ 1024, 1024, PixelFormat::D32_Float, true };
    a.dimension = TextureDimension::Array2D;
    a.arrayLayers = 4;
    TextureDesc b = a;
    b.arrayLayers = 6;
    EXPECT_FALSE(a == b);
}

TEST(RenderGraphDescTest, ArrayAndCubeDescsCompareUnequal)
{
    TextureDesc a{ 512, 512, PixelFormat::RGBA8_UNorm };
    a.dimension = TextureDimension::Array2D;
    a.arrayLayers = 6;
    TextureDesc b = a;
    b.dimension = TextureDimension::Cube;
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

TEST(RenderGraphResourceAccessTest, VertexBufferReadIsNotAWrite)
{
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::VertexBufferRead));
}

TEST(RenderGraphResourceAccessTest, VertexShaderStorageReadIsNotAWrite)
{
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::VertexShaderStorageRead));
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
        ResourceAccess::VertexShaderStorageRead,
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
    EXPECT_STREQ(ToString(ResourceAccess::VertexShaderStorageRead), "VertexShaderStorageRead");
}

// --- BindingStageLabel() - one case per enumerator, non-empty, non-null ---

TEST(RenderGraphResourceAccessTest, BindingStageLabelCoversEveryEnumeratorNonNullNonEmpty)
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
        ResourceAccess::VertexShaderStorageRead,
    };

    for (const ResourceAccess value : values) {
        const char* label = BindingStageLabel(value);
        ASSERT_NE(label, nullptr);
        EXPECT_GT(std::string_view(label).size(), 0u);
    }
}

TEST(RenderGraphResourceAccessTest, BindingStageLabelMatchesDocumentedStringPerEnumerator)
{
    EXPECT_STREQ(BindingStageLabel(ResourceAccess::ColorAttachmentWrite), "Color Attachment");
    EXPECT_STREQ(BindingStageLabel(ResourceAccess::DepthStencilAttachmentReadWrite), "Depth Test");
    EXPECT_STREQ(BindingStageLabel(ResourceAccess::ShaderRead), "Pixel Shader");
    EXPECT_STREQ(BindingStageLabel(ResourceAccess::TransferSrc), "Transfer Source");
    EXPECT_STREQ(BindingStageLabel(ResourceAccess::TransferDst), "Transfer Destination");
    EXPECT_STREQ(BindingStageLabel(ResourceAccess::ComputeShaderRead), "Compute Shader");
    EXPECT_STREQ(BindingStageLabel(ResourceAccess::ComputeShaderWrite), "Compute Shader");
    EXPECT_STREQ(BindingStageLabel(ResourceAccess::IndirectCommandRead), "Indirect Command");
    EXPECT_STREQ(BindingStageLabel(ResourceAccess::VertexBufferRead), "Vertex Input");
    EXPECT_STREQ(BindingStageLabel(ResourceAccess::VertexShaderStorageRead), "Vertex Shader");
}

// --- IsResolvedViewMissingItsSampler() - Safety Net #2's pure predicate ---

TEST(RenderGraphIsResolvedViewMissingItsSamplerTest, FalseWhenViewIsNull)
{
    const VkSampler sampler = reinterpret_cast<VkSampler>(static_cast<std::uintptr_t>(0x1234));
    EXPECT_FALSE(IsResolvedViewMissingItsSampler(VK_NULL_HANDLE, sampler));
    EXPECT_FALSE(IsResolvedViewMissingItsSampler(VK_NULL_HANDLE, VK_NULL_HANDLE));
}

TEST(RenderGraphIsResolvedViewMissingItsSamplerTest, FalseWhenViewAndSamplerAreBothNonNull)
{
    const VkImageView view = reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x5678));
    const VkSampler sampler = reinterpret_cast<VkSampler>(static_cast<std::uintptr_t>(0x1234));
    EXPECT_FALSE(IsResolvedViewMissingItsSampler(view, sampler));
}

TEST(RenderGraphIsResolvedViewMissingItsSamplerTest, TrueWhenViewIsNonNullAndSamplerIsNull)
{
    const VkImageView view = reinterpret_cast<VkImageView>(static_cast<std::uintptr_t>(0x5678));
    EXPECT_TRUE(IsResolvedViewMissingItsSampler(view, VK_NULL_HANDLE));
}

// --- PassRecord / ResourceUsage - plain-data defaults and tagged-union constructors ---

TEST(RenderGraphPassRecordTest, DefaultConstructedPassRecordIsEmptyAndNotCulled)
{
    const PassRecord record;
    EXPECT_EQ(record.name, nullptr);
    EXPECT_TRUE(record.reads.empty());
    EXPECT_TRUE(record.writes.empty());
    EXPECT_FALSE(record.isCulled);
    EXPECT_EQ(record.kind, PassKind::Graphics);
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

// A per-layer array/cube usage is still a Texture usage - the layer index is the only extra field.
TEST(RenderGraphResourceUsageTest, ForTextureLayerSetsKindTextureAndLayerIndex)
{
    const ResourceUsage usage =
        ResourceUsage::ForTextureLayer(TextureHandle{ 9, 4 }, 2u, ResourceAccess::DepthStencilAttachmentReadWrite);
    EXPECT_EQ(usage.kind, ResourceKind::Texture);
    EXPECT_EQ(usage.texture, (TextureHandle{ 9, 4 }));
    ASSERT_TRUE(usage.arrayLayerIndex.has_value());
    EXPECT_EQ(*usage.arrayLayerIndex, 2u);
    EXPECT_EQ(usage.access, ResourceAccess::DepthStencilAttachmentReadWrite);
}

TEST(RenderGraphResourceUsageTest, ForTextureHasNoArrayLayerIndex)
{
    const ResourceUsage usage = ResourceUsage::ForTexture(TextureHandle{ 1, 1 }, ResourceAccess::ShaderRead);
    EXPECT_FALSE(usage.arrayLayerIndex.has_value());
}

// --- DispatchByKind(): routes to exactly one callable with the right handle; non-void results round-trip ---

TEST(RenderGraphDispatchByKindTest, TextureUsageDispatchesOnlyToOnTextureWithTheCorrectHandle)
{
    const ResourceUsage usage = ResourceUsage::ForTexture(TextureHandle{ 5, 2 }, ResourceAccess::ShaderRead);
    bool onTextureCalled = false;
    bool onBufferCalled = false;
    TextureHandle received;

    DispatchByKind(usage,
        [&](TextureHandle h) { onTextureCalled = true; received = h; },
        [&](BufferHandle) { onBufferCalled = true; });

    EXPECT_TRUE(onTextureCalled);
    EXPECT_FALSE(onBufferCalled);
    EXPECT_EQ(received, (TextureHandle{ 5, 2 }));
}

TEST(RenderGraphDispatchByKindTest, BufferUsageDispatchesOnlyToOnBufferWithTheCorrectHandle)
{
    const ResourceUsage usage = ResourceUsage::ForBuffer(BufferHandle{ 3, 1 }, ResourceAccess::TransferDst);
    bool onTextureCalled = false;
    bool onBufferCalled = false;
    BufferHandle received;

    DispatchByKind(usage,
        [&](TextureHandle) { onTextureCalled = true; },
        [&](BufferHandle h) { onBufferCalled = true; received = h; });

    EXPECT_FALSE(onTextureCalled);
    EXPECT_TRUE(onBufferCalled);
    EXPECT_EQ(received, (BufferHandle{ 3, 1 }));
}

TEST(RenderGraphDispatchByKindTest, DispatchByKindWithNonVoidReturnTypeProducesTheExpectedValue)
{
    const ResourceUsage textureUsage = ResourceUsage::ForTexture(TextureHandle{ 1, 1 }, ResourceAccess::ShaderRead);
    const ResourceUsage bufferUsage = ResourceUsage::ForBuffer(BufferHandle{ 2, 1 }, ResourceAccess::ShaderRead);

    auto dispatch = [](const ResourceUsage& usage) {
        return DispatchByKind(usage,
            [](TextureHandle) { return std::int32_t{ 10 }; },
            [](BufferHandle) { return std::int32_t{ 20 }; });
    };

    EXPECT_EQ(dispatch(textureUsage), 10);
    EXPECT_EQ(dispatch(bufferUsage), 20);
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

// --- PassKind / RenderPassCategory ---

TEST(RenderGraphPassKindTest, ToStringCoversEveryEnumeratorNonNullNonEmpty)
{
    const PassKind values[] = {
        PassKind::Graphics,
        PassKind::Compute,
        PassKind::Blit,
    };

    for (const PassKind value : values) {
        const char* name = ToString(value);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::string_view(name).size(), 0u);
    }
}

TEST(RenderGraphPassKindTest, ToStringProducesDistinctNamesForDistinctEnumerators)
{
    EXPECT_STREQ(ToString(PassKind::Graphics), "Graphics");
    EXPECT_STREQ(ToString(PassKind::Compute), "Compute");
    EXPECT_STREQ(ToString(PassKind::Blit), "Blit");
}

TEST(RenderGraphRenderPassCategoryTest, ToStringCoversEveryEnumeratorNonNullNonEmpty)
{
    const RenderPassCategory values[] = {
        RenderPassCategory::General,
        RenderPassCategory::Debug,
        RenderPassCategory::FrameDebuggerInternal,
    };

    for (const RenderPassCategory value : values) {
        const char* name = ToString(value);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::string_view(name).size(), 0u);
    }
}

TEST(RenderGraphRenderPassCategoryTest, ToStringProducesDistinctNamesForDistinctEnumerators)
{
    EXPECT_STREQ(ToString(RenderPassCategory::General), "General");
    EXPECT_STREQ(ToString(RenderPassCategory::Debug), "Debug");
    EXPECT_STREQ(ToString(RenderPassCategory::FrameDebuggerInternal), "FrameDebuggerInternal");
}

// --- RenderPassDrawKind ---

TEST(RenderGraphRenderPassDrawKindTest, ToStringCoversEveryEnumeratorNonNullNonEmpty)
{
    const RenderPassDrawKind values[] = {
        RenderPassDrawKind::DrawMesh,
        RenderPassDrawKind::DrawQuad,
        RenderPassDrawKind::Blit,
    };

    for (const RenderPassDrawKind value : values) {
        const char* name = ToString(value);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::string_view(name).size(), 0u);
    }
}

TEST(RenderGraphRenderPassDrawKindTest, ToStringProducesDistinctNamesForDistinctEnumerators)
{
    EXPECT_STREQ(ToString(RenderPassDrawKind::DrawMesh), "DrawMesh");
    EXPECT_STREQ(ToString(RenderPassDrawKind::DrawQuad), "DrawQuad");
    EXPECT_STREQ(ToString(RenderPassDrawKind::Blit), "Blit");
}

// --- RenderPassEvent ---

TEST(RenderGraphRenderPassEventTest, ToStringCoversEveryEnumeratorNonNullNonEmpty)
{
    const RenderPassEvent values[] = {
        RenderPassEvent::BeforeEverything,
        RenderPassEvent::PreOpaques,
        RenderPassEvent::Opaques,
        RenderPassEvent::AfterOpaques,
        RenderPassEvent::Transparents,
        RenderPassEvent::AfterTransparents,
        RenderPassEvent::AfterEverything,
    };

    for (const RenderPassEvent value : values) {
        const char* name = ToString(value);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::string_view(name).size(), 0u);
    }
}

TEST(RenderGraphRenderPassEventTest, ToStringProducesDistinctNamesForDistinctEnumerators)
{
    EXPECT_STREQ(ToString(RenderPassEvent::BeforeEverything), "BeforeEverything");
    EXPECT_STREQ(ToString(RenderPassEvent::PreOpaques), "PreOpaques");
    EXPECT_STREQ(ToString(RenderPassEvent::Opaques), "Opaques");
    EXPECT_STREQ(ToString(RenderPassEvent::AfterOpaques), "AfterOpaques");
    EXPECT_STREQ(ToString(RenderPassEvent::Transparents), "Transparents");
    EXPECT_STREQ(ToString(RenderPassEvent::AfterTransparents), "AfterTransparents");
    EXPECT_STREQ(ToString(RenderPassEvent::AfterEverything), "AfterEverything");
}

// PassRecord::renderPassEvent defaults to Opaques.
TEST(RenderGraphPassRecordTest, DefaultConstructedPassRecordHasOpaquesRenderPassEvent)
{
    const PassRecord record;
    EXPECT_EQ(record.renderPassEvent, RenderPassEvent::Opaques);
}

// --- FindMismatchedColorAttachmentExtent (pure, no device) ---

TEST(RenderGraphFindMismatchedColorAttachmentExtentTest, ZeroExtentsHasNoMismatch)
{
    const std::vector<VkExtent2D> extents;
    EXPECT_EQ(FindMismatchedColorAttachmentExtent(extents), std::nullopt);
}

TEST(RenderGraphFindMismatchedColorAttachmentExtentTest, SingleExtentHasNoMismatch)
{
    const std::vector<VkExtent2D> extents{ VkExtent2D{ 1920, 1080 } };
    EXPECT_EQ(FindMismatchedColorAttachmentExtent(extents), std::nullopt);
}

TEST(RenderGraphFindMismatchedColorAttachmentExtentTest, IdenticalExtentsHaveNoMismatch)
{
    const std::vector<VkExtent2D> extents{
        VkExtent2D{ 1920, 1080 },
        VkExtent2D{ 1920, 1080 },
        VkExtent2D{ 1920, 1080 },
    };
    EXPECT_EQ(FindMismatchedColorAttachmentExtent(extents), std::nullopt);
}

TEST(RenderGraphFindMismatchedColorAttachmentExtentTest, MismatchAtNonZeroIndexIsReported)
{
    const std::vector<VkExtent2D> extents{
        VkExtent2D{ 1920, 1080 },
        VkExtent2D{ 1920, 1080 },
        VkExtent2D{ 960, 540 }, // Mismatch at index 2.
    };
    const std::optional<std::size_t> mismatch = FindMismatchedColorAttachmentExtent(extents);
    ASSERT_TRUE(mismatch.has_value());
    EXPECT_EQ(*mismatch, 2u);
}

TEST(RenderGraphFindMismatchedColorAttachmentExtentTest, MismatchIsFoundEvenWhenOnlyHeightDiffers)
{
    const std::vector<VkExtent2D> extents{
        VkExtent2D{ 1920, 1080 },
        VkExtent2D{ 1920, 720 }, // Mismatch at index 1 - width matches, height doesn't.
    };
    const std::optional<std::size_t> mismatch = FindMismatchedColorAttachmentExtent(extents);
    ASSERT_TRUE(mismatch.has_value());
    EXPECT_EQ(*mismatch, 1u);
}

// --- BlitSpec / ResolveEffectiveBlitFilter / ResolveBlitRegion / IsValidBlitRegion (pure, no device) ---

TEST(RenderGraphResolveEffectiveBlitFilterTest, ReturnsSpecFilterWhenNeitherSideIsDepth)
{
    BlitSpec spec;
    spec.srcIsDepth = false;
    spec.dstIsDepth = false;
    spec.filter = VK_FILTER_LINEAR;
    EXPECT_EQ(ResolveEffectiveBlitFilter(spec), VK_FILTER_LINEAR);
}

TEST(RenderGraphResolveEffectiveBlitFilterTest, ForcesNearestWhenSrcIsDepth)
{
    BlitSpec spec;
    spec.srcIsDepth = true;
    spec.dstIsDepth = false;
    spec.filter = VK_FILTER_LINEAR;
    EXPECT_EQ(ResolveEffectiveBlitFilter(spec), VK_FILTER_NEAREST);
}

TEST(RenderGraphResolveEffectiveBlitFilterTest, ForcesNearestWhenDstIsDepth)
{
    BlitSpec spec;
    spec.srcIsDepth = false;
    spec.dstIsDepth = true;
    spec.filter = VK_FILTER_LINEAR;
    EXPECT_EQ(ResolveEffectiveBlitFilter(spec), VK_FILTER_NEAREST);
}

TEST(RenderGraphResolveBlitRegionTest, AllZeroSentinelResolvesToFullExtent)
{
    const VkOffset3D regionMin{};
    const VkOffset3D regionMax{};
    const VkExtent2D extent{ 512, 256 };
    const ResolvedBlitRegion resolved = ResolveBlitRegion(regionMin, regionMax, extent);
    EXPECT_EQ(resolved.min.x, 0);
    EXPECT_EQ(resolved.min.y, 0);
    EXPECT_EQ(resolved.min.z, 0);
    EXPECT_EQ(resolved.max.x, 512);
    EXPECT_EQ(resolved.max.y, 256);
    EXPECT_EQ(resolved.max.z, 1);
}

TEST(RenderGraphResolveBlitRegionTest, NonSentinelPassesThroughUnchanged)
{
    const VkOffset3D regionMin{ 10, 20, 0 };
    const VkOffset3D regionMax{ 100, 200, 1 };
    const VkExtent2D extent{ 512, 256 };
    const ResolvedBlitRegion resolved = ResolveBlitRegion(regionMin, regionMax, extent);
    EXPECT_EQ(resolved.min.x, 10);
    EXPECT_EQ(resolved.min.y, 20);
    EXPECT_EQ(resolved.min.z, 0);
    EXPECT_EQ(resolved.max.x, 100);
    EXPECT_EQ(resolved.max.y, 200);
    EXPECT_EQ(resolved.max.z, 1);
}

TEST(RenderGraphIsValidBlitRegionTest, RejectsInvertedAxis)
{
    const ResolvedBlitRegion region{ VkOffset3D{ 100, 0, 0 }, VkOffset3D{ 50, 200, 1 } }; // max.x <= min.x.
    const VkExtent2D extent{ 512, 256 };
    EXPECT_FALSE(IsValidBlitRegion(region, extent));
}

TEST(RenderGraphIsValidBlitRegionTest, RejectsOutOfBoundsMax)
{
    const ResolvedBlitRegion region{ VkOffset3D{ 0, 0, 0 }, VkOffset3D{ 1000, 200, 1 } }; // max.x exceeds extent.width.
    const VkExtent2D extent{ 512, 256 };
    EXPECT_FALSE(IsValidBlitRegion(region, extent));
}

TEST(RenderGraphIsValidBlitRegionTest, AcceptsFullResolvedExtent)
{
    const VkExtent2D extent{ 512, 256 };
    const ResolvedBlitRegion resolved = ResolveBlitRegion(VkOffset3D{}, VkOffset3D{}, extent);
    EXPECT_TRUE(IsValidBlitRegion(resolved, extent));
}

// --- IsStaleCacheEntry() / PersistentTextureCacheToken: pure, device-free ---

TEST(RenderGraphIsStaleCacheEntryTest, ZeroFramesIdleIsNeverStaleRegardlessOfThreshold)
{
    EXPECT_FALSE(IsStaleCacheEntry(/*lastUsedFrame=*/100, /*currentFrame=*/100, /*staleThresholdFrames=*/300));
}

TEST(RenderGraphIsStaleCacheEntryTest, ZeroFramesIdleWithZeroThresholdIsStillNotStale)
{
    // "more than" is strictly-greater - 0 idle frames is never > 0.
    EXPECT_FALSE(IsStaleCacheEntry(/*lastUsedFrame=*/100, /*currentFrame=*/100, /*staleThresholdFrames=*/0));
}

TEST(RenderGraphIsStaleCacheEntryTest, ExactlyAtThresholdIsNotYetStale)
{
    EXPECT_FALSE(IsStaleCacheEntry(/*lastUsedFrame=*/0, /*currentFrame=*/300, /*staleThresholdFrames=*/300));
}

TEST(RenderGraphIsStaleCacheEntryTest, OneFrameBeyondThresholdIsStale)
{
    EXPECT_TRUE(IsStaleCacheEntry(/*lastUsedFrame=*/0, /*currentFrame=*/301, /*staleThresholdFrames=*/300));
}

TEST(RenderGraphIsStaleCacheEntryTest, LargeRealisticGapIsStale)
{
    EXPECT_TRUE(IsStaleCacheEntry(/*lastUsedFrame=*/0, /*currentFrame=*/10000, /*staleThresholdFrames=*/300));
}

TEST(RenderGraphPersistentTextureCacheTokenTest, DefaultConstructedTokenIsNeverResolvedYet)
{
    const PersistentTextureCacheToken token;
    EXPECT_EQ(token.slot.epoch, 0u);
}

} // namespace
} // namespace gte::rg
