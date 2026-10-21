// Tier-1 tests for name-based binding: planning, validation and declaration.

#include "Renderer/RenderGraph/ShaderBindings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace gte::rg {
namespace {

const TextureHandle kShadowDepth{ 1, 1 };
const TextureHandle kSceneDepth{ 2, 1 };
const TextureHandle kMask{ 3, 1 };
const BufferObjectHandle kParams = static_cast<BufferObjectHandle>(42);
const DescriptorSetLayoutHandle kLayout = static_cast<DescriptorSetLayoutHandle>(7);

ReflectedDescriptorBinding Slot(std::uint32_t set, std::uint32_t binding, VkDescriptorType type, const char* name,
    ReflectedAccess access = ReflectedAccess::ReadWrite, std::uint32_t count = 1)
{
    ReflectedDescriptorBinding slot;
    slot.set = set;
    slot.binding = binding;
    slot.type = type;
    slot.name = name;
    slot.access = access;
    slot.count = count;
    return slot;
}

std::vector<ReflectedDescriptorBinding> FixtureBindings()
{
    return {
        Slot(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "params", ReflectedAccess::ReadOnly),
        Slot(0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "inputDepthA", ReflectedAccess::ReadOnly),
        Slot(0, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "inputDepthB", ReflectedAccess::ReadOnly),
        Slot(0, 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "maskOutput", ReflectedAccess::WriteOnly),
    };
}

BindingArgument Tex(std::string_view name, TextureHandle handle)
{
    BindingArgument arg;
    arg.name = name;
    arg.kind = BindingResourceKind::Texture;
    arg.texture = handle;
    return arg;
}

BindingArgument Depth(std::string_view name, TextureHandle handle)
{
    BindingArgument arg = Tex(name, handle);
    arg.kind = BindingResourceKind::DepthTexture;
    return arg;
}

BindingArgument Buf(std::string_view name, BufferObjectHandle handle)
{
    BindingArgument arg;
    arg.name = name;
    arg.kind = BindingResourceKind::Buffer;
    arg.buffer = handle;
    return arg;
}

// The two depth fixtures are depth-only textures; every other handle is a color texture.
std::optional<TextureDesc> DepthForDepthHandles(TextureHandle handle)
{
    TextureDesc desc;
    const bool depthOnly = (handle == kShadowDepth || handle == kSceneDepth);
    desc.hasDepth = depthOnly;
    desc.hasColor = !depthOnly;
    return desc;
}

std::optional<TextureDesc> NoneHaveDepth(TextureHandle)
{
    return TextureDesc{};
}

std::vector<BindingArgument> HappyArgs()
{
    return { Buf("params", kParams), Depth("inputDepthA", kShadowDepth), Depth("inputDepthB", kSceneDepth),
        Tex("maskOutput", kMask) };
}

BindingPlanResult Plan(const std::vector<ReflectedDescriptorBinding>& reflected, const std::vector<BindingArgument>& args,
    DescribeTextureFn describe = DepthForDepthHandles)
{
    return PlanBindings(reflected, args, describe);
}

bool Contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

TEST(ShaderBindingsTest, HappyPathPlansEveryBindingInReflectedOrder)
{
    const BindingPlanResult result = Plan(FixtureBindings(), HappyArgs());
    ASSERT_TRUE(result.Ok()) << result.error;
    ASSERT_EQ(result.plan.bindings.size(), 4u);

    EXPECT_EQ(result.plan.bindings[0].binding, 0u);
    EXPECT_EQ(result.plan.bindings[0].type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(result.plan.bindings[0].kind, BindingResourceKind::Buffer);
    EXPECT_EQ(result.plan.bindings[0].buffer, kParams);

    EXPECT_EQ(result.plan.bindings[1].binding, 1u);
    EXPECT_EQ(result.plan.bindings[1].kind, BindingResourceKind::DepthTexture);
    EXPECT_EQ(result.plan.bindings[1].texture, kShadowDepth);
    EXPECT_EQ(result.plan.bindings[2].binding, 2u);
    EXPECT_EQ(result.plan.bindings[2].texture, kSceneDepth);

    EXPECT_EQ(result.plan.bindings[3].binding, 3u);
    EXPECT_EQ(result.plan.bindings[3].access, ReflectedAccess::WriteOnly);
    EXPECT_EQ(result.plan.bindings[3].texture, kMask);
}

TEST(ShaderBindingsTest, ArgumentOrderDoesNotMatter)
{
    std::vector<BindingArgument> args = HappyArgs();
    std::reverse(args.begin(), args.end());
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_TRUE(result.Ok()) << result.error;
    EXPECT_EQ(result.plan.bindings[0].binding, 0u);
    EXPECT_EQ(result.plan.bindings[3].binding, 3u);
}

TEST(ShaderBindingsTest, UnknownNameIsAnErrorAndListsValidNames)
{
    std::vector<BindingArgument> args = HappyArgs();
    args.push_back(Tex("typoName", kMask));
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "unknown name 'typoName'"));
    EXPECT_TRUE(Contains(result.error, "valid names: params, inputDepthA, inputDepthB, maskOutput"));
    EXPECT_TRUE(result.plan.bindings.empty());
}

TEST(ShaderBindingsTest, SameNameTwiceIsAnError)
{
    std::vector<BindingArgument> args = HappyArgs();
    args.push_back(Tex("maskOutput", kMask));
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'maskOutput' was bound twice"));
}

TEST(ShaderBindingsTest, BufferOnTextureSlotIsAnError)
{
    std::vector<BindingArgument> args = HappyArgs();
    args[3] = Buf("maskOutput", kParams);
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'maskOutput' is a storage image"));
}

TEST(ShaderBindingsTest, TextureOnBufferSlotIsAnError)
{
    std::vector<BindingArgument> args = HappyArgs();
    args[0] = Tex("params", kMask);
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'params' is a storage buffer"));
}

TEST(ShaderBindingsTest, BufferOnSamplerSlotIsAnError)
{
    std::vector<BindingArgument> args = HappyArgs();
    args[1] = Buf("inputDepthA", kParams);
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'inputDepthA' is a sampler"));
}

TEST(ShaderBindingsTest, DepthViewOnStorageImageIsAnError)
{
    std::vector<BindingArgument> args = HappyArgs();
    args[3] = Depth("maskOutput", kMask);
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "depth view is only valid on a sampler"));
}

TEST(ShaderBindingsTest, InvalidTextureHandleIsAnError)
{
    std::vector<BindingArgument> args = HappyArgs();
    args[3] = Tex("maskOutput", TextureHandle{});
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'maskOutput' was given an invalid resource handle"));
}

TEST(ShaderBindingsTest, InvalidBufferHandleIsAnError)
{
    std::vector<BindingArgument> args = HappyArgs();
    args[0] = Buf("params", BufferObjectHandle::Invalid);
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'params' was given an invalid resource handle"));
}

TEST(ShaderBindingsTest, DepthViewOfTextureWithoutDepthIsAnError)
{
    const BindingPlanResult result = Plan(FixtureBindings(), HappyArgs(), NoneHaveDepth);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'inputDepthA' was given a depth view of a texture without a depth aspect"));
    EXPECT_TRUE(Contains(result.error, "'inputDepthB' was given a depth view of a texture without a depth aspect"));
}

TEST(ShaderBindingsTest, DepthViewWithUnknownTextureIsAnError)
{
    const BindingPlanResult result = Plan(FixtureBindings(), HappyArgs(), DescribeTextureFn{});
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "without a depth aspect"));
}

TEST(ShaderBindingsTest, PlainBindOfADepthOnlyTextureOnASamplerAsksForDepth)
{
    std::vector<BindingArgument> args = HappyArgs();
    args[1] = Tex("inputDepthA", kShadowDepth);
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'inputDepthA' is a depth-only texture: use rg::Depth()"));
}

TEST(ShaderBindingsTest, DepthOnlyTextureOnAStorageImageIsAnError)
{
    std::vector<BindingArgument> args = HappyArgs();
    args[3] = Tex("maskOutput", kSceneDepth);
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'maskOutput' is a depth-only texture: it cannot be bound to a storage image"));
}

// A view target carries a depth companion next to its color image: it is a normal color texture.
std::optional<TextureDesc> ColorWithDepthCompanion(TextureHandle)
{
    TextureDesc desc;
    desc.hasDepth = true;
    desc.hasColor = true;
    return desc;
}

TEST(ShaderBindingsTest, ColorTextureWithADepthCompanionBindsAsColorAndAsDepth)
{
    std::vector<ReflectedDescriptorBinding> reflected = {
        Slot(0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "sceneColor", ReflectedAccess::ReadOnly),
        Slot(0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "sceneDepth", ReflectedAccess::ReadOnly),
        Slot(0, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "outImage", ReflectedAccess::WriteOnly) };
    const BindingPlanResult result = Plan(reflected,
        { Tex("sceneColor", kMask), Depth("sceneDepth", kMask), Tex("outImage", kMask) }, ColorWithDepthCompanion);
    EXPECT_TRUE(result.Ok()) << result.error;
}

TEST(ShaderBindingsTest, PlainBindOfAColorTextureWithUnknownDescIsAccepted)
{
    // Without a descriptor the depth aspect cannot be judged, so only an explicit Depth() view is refused.
    const BindingPlanResult result = Plan(FixtureBindings(), HappyArgs(), DescribeTextureFn{});
    EXPECT_FALSE(Contains(result.error, "use rg::Depth()"));
}

TEST(ShaderBindingsTest, UnsuppliedBindingIsListedByName)
{
    std::vector<BindingArgument> args = HappyArgs();
    args.pop_back();
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'maskOutput' was never bound"));
}

TEST(ShaderBindingsTest, DescriptorArrayIsRefused)
{
    std::vector<ReflectedDescriptorBinding> reflected = FixtureBindings();
    reflected.push_back(Slot(0, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "layers", ReflectedAccess::ReadOnly, 4));
    const BindingPlanResult result = Plan(reflected, HappyArgs());
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'layers' is a descriptor array: not supported by name binding"));
    EXPECT_FALSE(Contains(result.error, "'layers' was never bound"));
}

TEST(ShaderBindingsTest, EmptyReflectedNameNamesItsSlot)
{
    std::vector<ReflectedDescriptorBinding> reflected = FixtureBindings();
    reflected.push_back(Slot(0, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ""));
    const BindingPlanResult result = Plan(reflected, HappyArgs());
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "set 0 binding 5 has no name"));
}

TEST(ShaderBindingsTest, EngineOwnedSetsAreIgnored)
{
    std::vector<ReflectedDescriptorBinding> reflected = FixtureBindings();
    reflected.push_back(Slot(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "sceneService"));
    reflected.push_back(Slot(1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ""));
    const BindingPlanResult result = Plan(reflected, HappyArgs());
    ASSERT_TRUE(result.Ok()) << result.error;
    EXPECT_EQ(result.plan.bindings.size(), 4u);
}

TEST(ShaderBindingsTest, SetOneNameIsNotNameable)
{
    std::vector<ReflectedDescriptorBinding> reflected = FixtureBindings();
    reflected.push_back(Slot(1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "sceneService"));
    std::vector<BindingArgument> args = HappyArgs();
    args.push_back(Tex("sceneService", kMask));
    const BindingPlanResult result = Plan(reflected, args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "unknown name 'sceneService'"));
    EXPECT_FALSE(Contains(result.error, "valid names: params, inputDepthA, inputDepthB, maskOutput, sceneService"));
}

TEST(ShaderBindingsTest, MultipleProblemsAreAllReported)
{
    std::vector<BindingArgument> args = { Tex("bogus", kMask), Tex("params", kMask) };
    const BindingPlanResult result = Plan(FixtureBindings(), args);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "unknown name 'bogus'"));
    EXPECT_TRUE(Contains(result.error, "'params' is a storage buffer"));
    EXPECT_TRUE(Contains(result.error, "'inputDepthA' was never bound"));
    EXPECT_TRUE(Contains(result.error, "'inputDepthB' was never bound"));
    EXPECT_TRUE(Contains(result.error, "'maskOutput' was never bound"));
    EXPECT_GE(std::count(result.error.begin(), result.error.end(), '\n'), 4);
}

TEST(ShaderBindingsTest, ValidNamesTextListsSetZeroOnly)
{
    std::vector<ReflectedDescriptorBinding> reflected = FixtureBindings();
    reflected.push_back(Slot(1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "engineOwned"));
    EXPECT_EQ(ValidNamesText(reflected), "valid names: params, inputDepthA, inputDepthB, maskOutput");
    EXPECT_EQ(ValidNamesText({}), "valid names: (none)");
}

// --- DeclareBindings -------------------------------------------------------

TEST(ShaderBindingsTest, DeclareHappyPathProducesReadsWritesAndBindings)
{
    const BindingPlanResult result = Plan(FixtureBindings(), HappyArgs());
    ASSERT_TRUE(result.Ok()) << result.error;

    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    DeclareBindings(pass, result.plan, kLayout);

    EXPECT_EQ(record.computeBindingLayout, kLayout);
    ASSERT_EQ(record.reads.size(), 2u);
    EXPECT_TRUE(record.reads[0].isDepthResource);
    EXPECT_TRUE(record.reads[1].isDepthResource);
    EXPECT_EQ(record.reads[0].access, ResourceAccess::ShaderRead);
    ASSERT_EQ(record.writes.size(), 1u);
    EXPECT_EQ(record.writes[0].access, ResourceAccess::ComputeShaderWrite);
    EXPECT_EQ(record.writes[0].texture, kMask);

    ASSERT_EQ(record.bindings.size(), 4u);
    EXPECT_EQ(record.bindings[0].kind, BindingKind::StorageBuffer);
    EXPECT_EQ(record.bindings[0].bufferObjectHandle, kParams);
    EXPECT_EQ(record.bindings[1].kind, BindingKind::CombinedImageSampler);
    EXPECT_EQ(record.bindings[1].textureAspect, TextureAspect::Depth);
    EXPECT_EQ(record.bindings[3].kind, BindingKind::StorageImage);
    EXPECT_EQ(record.bindings[3].binding, 3u);
}

TEST(ShaderBindingsTest, DeclareColorSamplerIsNotADepthRead)
{
    std::vector<ReflectedDescriptorBinding> reflected = {
        Slot(0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "source", ReflectedAccess::ReadOnly) };
    const BindingPlanResult result = Plan(reflected, { Tex("source", kMask) });
    ASSERT_TRUE(result.Ok()) << result.error;

    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    DeclareBindings(pass, result.plan, kLayout);
    ASSERT_EQ(record.reads.size(), 1u);
    EXPECT_FALSE(record.reads[0].isDepthResource);
    EXPECT_EQ(record.bindings[0].textureAspect, TextureAspect::Color);
}

TEST(ShaderBindingsTest, DeclareWriteOnlyImageHasNoRead)
{
    std::vector<ReflectedDescriptorBinding> reflected = {
        Slot(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "outImage", ReflectedAccess::WriteOnly) };
    const BindingPlanResult result = Plan(reflected, { Tex("outImage", kMask) });
    ASSERT_TRUE(result.Ok()) << result.error;

    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    DeclareBindings(pass, result.plan, kLayout);
    EXPECT_TRUE(record.reads.empty());
    ASSERT_EQ(record.writes.size(), 1u);
    EXPECT_EQ(record.bindings.size(), 1u);
}

TEST(ShaderBindingsTest, DeclareReadOnlyImageHasNoWrite)
{
    std::vector<ReflectedDescriptorBinding> reflected = {
        Slot(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "inImage", ReflectedAccess::ReadOnly) };
    const BindingPlanResult result = Plan(reflected, { Tex("inImage", kMask) });
    ASSERT_TRUE(result.Ok()) << result.error;

    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    DeclareBindings(pass, result.plan, kLayout);
    ASSERT_EQ(record.reads.size(), 1u);
    EXPECT_EQ(record.reads[0].access, ResourceAccess::ComputeShaderRead);
    EXPECT_TRUE(record.writes.empty());
    EXPECT_EQ(record.bindings.size(), 1u);
}

TEST(ShaderBindingsTest, DeclareReadWriteImageHasReadWriteAndOneBinding)
{
    std::vector<ReflectedDescriptorBinding> reflected = {
        Slot(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "history", ReflectedAccess::ReadWrite) };
    const BindingPlanResult result = Plan(reflected, { Tex("history", kMask) });
    ASSERT_TRUE(result.Ok()) << result.error;

    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    DeclareBindings(pass, result.plan, kLayout);
    ASSERT_EQ(record.reads.size(), 1u);
    EXPECT_EQ(record.reads[0].access, ResourceAccess::ComputeShaderRead);
    ASSERT_EQ(record.writes.size(), 1u);
    EXPECT_EQ(record.writes[0].access, ResourceAccess::ComputeShaderWrite);
    EXPECT_EQ(record.bindings.size(), 1u);
}

// --- Push constants --------------------------------------------------------

TEST(ShaderBindingsTest, PushConstantSizeMatchIsOk)
{
    EXPECT_FALSE(ValidatePushConstantSize(16, 16).has_value());
    EXPECT_FALSE(ValidatePushConstantSize(0, 0).has_value());
}

TEST(ShaderBindingsTest, PushConstantSizeMismatchIsAnError)
{
    const std::optional<std::string> error = ValidatePushConstantSize(16, 8);
    ASSERT_TRUE(error.has_value());
    EXPECT_TRUE(Contains(*error, "16"));
    EXPECT_TRUE(Contains(*error, "8"));
}

TEST(ShaderBindingsTest, PushConstantSuppliedWithoutShaderBlockIsAnError)
{
    EXPECT_TRUE(ValidatePushConstantSize(0, 4).has_value());
}

TEST(ShaderBindingsTest, PushConstantMissingForShaderBlockIsAnError)
{
    EXPECT_TRUE(ValidatePushConstantSize(4, 0).has_value());
}

// --- Uniform buffers ---------------------------------------------------------

std::vector<ReflectedDescriptorBinding> UniformBindings()
{
    return { Slot(0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, "tint", ReflectedAccess::ReadOnly),
        Slot(0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "outputImage", ReflectedAccess::WriteOnly) };
}

TEST(ShaderBindingsTest, UniformBufferTakesABufferAndDeclaresAUniformBinding)
{
    const BindingPlanResult result = Plan(UniformBindings(), { Buf("tint", kParams), Tex("outputImage", kMask) });
    ASSERT_TRUE(result.Ok()) << result.error;

    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    DeclareBindings(pass, result.plan, kLayout);

    ASSERT_EQ(record.bindings.size(), 2u);
    EXPECT_EQ(record.bindings[0].kind, BindingKind::UniformBuffer);
    EXPECT_EQ(record.bindings[0].bufferObjectHandle, kParams);
    EXPECT_EQ(record.bindings[0].binding, 0u);
    EXPECT_TRUE(record.reads.empty()); // An external buffer is not graph-tracked.
}

TEST(ShaderBindingsTest, TextureOnAUniformBufferSlotIsAnError)
{
    const BindingPlanResult result = Plan(UniformBindings(), { Tex("tint", kMask), Tex("outputImage", kMask) });
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'tint' is a uniform buffer"));
}

TEST(ShaderBindingsTest, InvalidBufferOnAUniformBufferSlotIsAnError)
{
    const BindingPlanResult result =
        Plan(UniformBindings(), { Buf("tint", BufferObjectHandle::Invalid), Tex("outputImage", kMask) });
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'tint' was given an invalid resource handle"));
}

TEST(ShaderBindingsTest, FullscreenReflectionAcceptsUniformBuffersButNotStorageBuffers)
{
    ShaderReflectionResult reflection;
    reflection.descriptorBindings = { Slot(0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "source"),
        Slot(0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, "tint") };
    EXPECT_FALSE(ValidateFullscreenReflection(reflection).has_value());

    reflection.descriptorBindings.push_back(Slot(0, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "bad"));
    EXPECT_TRUE(ValidateFullscreenReflection(reflection).has_value());
}

// --- Graph (tracked) buffers -------------------------------------------------

const BufferHandle kItems{ 1, 1 };
const BufferHandle kResults{ 2, 1 };
const BufferHandle kCounter{ 3, 1 };

BindingArgument Tracked(std::string_view name, BufferHandle handle)
{
    BindingArgument arg;
    arg.name = name;
    arg.kind = BindingResourceKind::TrackedBuffer;
    arg.trackedBuffer = handle;
    return arg;
}

std::vector<ReflectedDescriptorBinding> BufferBindings()
{
    return { Slot(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "items", ReflectedAccess::ReadOnly),
        Slot(0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "results", ReflectedAccess::WriteOnly),
        Slot(0, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "counter", ReflectedAccess::ReadWrite) };
}

std::optional<BufferDesc> KnownBuffers(BufferHandle handle)
{
    if (handle.index >= 1 && handle.index <= 3) {
        return BufferDesc{ 64, BufferUsage::None };
    }
    return std::nullopt;
}

BindingPlanResult PlanBuffers(const std::vector<BindingArgument>& args)
{
    return PlanBindings(BufferBindings(), args, DescribeTextureFn{}, KnownBuffers);
}

std::vector<BindingArgument> TrackedArgs()
{
    return { Tracked("items", kItems), Tracked("results", kResults), Tracked("counter", kCounter) };
}

TEST(ShaderBindingsTest, TrackedBuffersPlanWithTheirHandles)
{
    const BindingPlanResult result = PlanBuffers(TrackedArgs());
    ASSERT_TRUE(result.Ok()) << result.error;
    ASSERT_EQ(result.plan.bindings.size(), 3u);

    const BufferHandle expected[] = { kItems, kResults, kCounter };
    const ReflectedAccess access[] = { ReflectedAccess::ReadOnly, ReflectedAccess::WriteOnly, ReflectedAccess::ReadWrite };
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(result.plan.bindings[i].kind, BindingResourceKind::TrackedBuffer);
        EXPECT_EQ(result.plan.bindings[i].trackedBuffer, expected[i]);
        EXPECT_EQ(result.plan.bindings[i].buffer, BufferObjectHandle::Invalid);
        EXPECT_EQ(result.plan.bindings[i].access, access[i]);
    }
}

TEST(ShaderBindingsTest, UnknownTrackedBufferIsAnError)
{
    const BindingPlanResult result =
        PlanBuffers({ Tracked("items", BufferHandle{ 9, 1 }), Tracked("results", kResults), Tracked("counter", kCounter) });
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'items' was given a buffer the graph does not know"));
}

TEST(ShaderBindingsTest, InvalidTrackedBufferIsAnError)
{
    const BindingPlanResult result =
        PlanBuffers({ Tracked("items", BufferHandle{}), Tracked("results", kResults), Tracked("counter", kCounter) });
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'items' was given an invalid resource handle"));
}

TEST(ShaderBindingsTest, TrackedBufferOnASamplerSlotIsAnError)
{
    const std::vector<ReflectedDescriptorBinding> reflected{ Slot(0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "tex") };
    const BindingPlanResult result = PlanBindings(reflected, std::vector{ Tracked("tex", kItems) }, DescribeTextureFn{}, KnownBuffers);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'tex' is a sampler"));
}

TEST(ShaderBindingsTest, TrackedBufferOnAStorageImageSlotIsAnError)
{
    const std::vector<ReflectedDescriptorBinding> reflected{
        Slot(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "image", ReflectedAccess::WriteOnly)
    };
    const BindingPlanResult result = PlanBindings(reflected, std::vector{ Tracked("image", kItems) }, DescribeTextureFn{}, KnownBuffers);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "'image' is a storage image"));
}

TEST(ShaderBindingsTest, TrackedBufferOnAUniformSlotIsAnError)
{
    const BindingPlanResult result = PlanBindings(
        UniformBindings(), std::vector{ Tracked("tint", kItems), Tex("outputImage", kMask) }, DescribeTextureFn{}, KnownBuffers);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(Contains(result.error, "a graph buffer binds storage buffers only"));
}

TEST(ShaderBindingsTest, TrackedAndExternalBuffersMixInOnePlan)
{
    const std::vector<ReflectedDescriptorBinding> reflected{
        Slot(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "params", ReflectedAccess::ReadOnly),
        Slot(0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "items", ReflectedAccess::ReadOnly)
    };
    const BindingPlanResult result = PlanBindings(
        reflected, std::vector{ Buf("params", kParams), Tracked("items", kItems) }, DescribeTextureFn{}, KnownBuffers);
    ASSERT_TRUE(result.Ok()) << result.error;
    ASSERT_EQ(result.plan.bindings.size(), 2u);
    EXPECT_EQ(result.plan.bindings[0].kind, BindingResourceKind::Buffer);
    EXPECT_EQ(result.plan.bindings[0].buffer, kParams);
    EXPECT_EQ(result.plan.bindings[1].kind, BindingResourceKind::TrackedBuffer);
    EXPECT_EQ(result.plan.bindings[1].trackedBuffer, kItems);
}

TEST(ShaderBindingsTest, DeclareTrackedBuffersFollowTheReflectedAccess)
{
    const BindingPlanResult result = PlanBuffers(TrackedArgs());
    ASSERT_TRUE(result.Ok()) << result.error;

    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    DeclareBindings(pass, result.plan, kLayout);

    ASSERT_EQ(record.reads.size(), 2u);
    EXPECT_EQ(record.reads[0].buffer, kItems);
    EXPECT_EQ(record.reads[0].access, ResourceAccess::ComputeShaderRead);
    EXPECT_EQ(record.reads[1].buffer, kCounter);
    ASSERT_EQ(record.writes.size(), 2u);
    EXPECT_EQ(record.writes[0].buffer, kResults);
    EXPECT_EQ(record.writes[0].access, ResourceAccess::ComputeShaderWrite);
    EXPECT_EQ(record.writes[1].buffer, kCounter);

    ASSERT_EQ(record.bindings.size(), 3u);
    for (const PendingBinding& binding : record.bindings) {
        EXPECT_TRUE(binding.bufferHandle.IsValid());
        EXPECT_EQ(binding.bufferObjectHandle, BufferObjectHandle::Invalid);
    }
}

TEST(ShaderBindingsTest, TrackedBufferWithoutStorageUsageIsAnError)
{
    const auto withUsage = [](BufferUsage usage) {
        return [usage](BufferHandle handle) -> std::optional<BufferDesc> {
            if (handle.index >= 1 && handle.index <= 3) {
                return BufferDesc{ 64, usage };
            }
            return std::nullopt;
        };
    };

    const BindingPlanResult uniformOnly =
        PlanBindings(BufferBindings(), TrackedArgs(), DescribeTextureFn{}, withUsage(BufferUsage::UniformBuffer));
    ASSERT_FALSE(uniformOnly.Ok());
    EXPECT_TRUE(Contains(uniformOnly.error, "'items' was given a buffer without storage usage"));

    EXPECT_TRUE(PlanBindings(BufferBindings(), TrackedArgs(), DescribeTextureFn{}, withUsage(BufferUsage::StorageBuffer)).Ok());
    EXPECT_TRUE(PlanBindings(BufferBindings(), TrackedArgs(), DescribeTextureFn{}, withUsage(BufferUsage::None)).Ok());
}

} // namespace
} // namespace gte::rg
