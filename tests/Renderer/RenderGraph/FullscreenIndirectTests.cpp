// Tier-1 tests for the indirect-draw form of the fullscreen pass: reflection rules, args validation,
// pass declaration, and the graph ordering and barriers around a compute-written args buffer.

#include "Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "Renderer/RenderGraph/RenderGraphCompiler.h"
#include "Renderer/RenderGraph/ShaderBindings.h"

#include <gtest/gtest.h>

#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace gte::rg {
namespace {

void NoOpExecute(PassContext&) { }

const DescriptorSetLayoutHandle kLayout = static_cast<DescriptorSetLayoutHandle>(7);
const BufferHandle kInstances{ 1, 1 };
const BufferHandle kArgs{ 2, 1 };
const BufferObjectHandle kParams = static_cast<BufferObjectHandle>(42);
const TextureHandle kTarget{ 1, 1 };
const TextureHandle kDepth{ 2, 1 };

ReflectedDescriptorBinding Slot(std::uint32_t binding, VkDescriptorType type, const char* name, ReflectedAccess access,
    VkShaderStageFlags stages)
{
    ReflectedDescriptorBinding slot;
    slot.set = 0;
    slot.binding = binding;
    slot.type = type;
    slot.name = name;
    slot.access = access;
    slot.stages = stages;
    return slot;
}

constexpr VkShaderStageFlags kVert = VK_SHADER_STAGE_VERTEX_BIT;
constexpr VkShaderStageFlags kFrag = VK_SHADER_STAGE_FRAGMENT_BIT;
constexpr VkShaderStageFlags kBoth = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// --- ValidateFullscreenReflection ---------------------------------------------

TEST(FullscreenIndirectReflectionTest, ReadOnlyStorageBuffersInVertexAndFragmentStagesAreAccepted)
{
    ShaderReflectionResult reflection;
    reflection.descriptorBindings = {
        Slot(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "params", ReflectedAccess::ReadOnly, kBoth),
        Slot(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "instances", ReflectedAccess::ReadOnly, kVert),
        Slot(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "tints", ReflectedAccess::ReadOnly, kFrag),
    };
    EXPECT_FALSE(ValidateFullscreenReflection(reflection).has_value());
}

TEST(FullscreenIndirectReflectionTest, WritableStorageBuffersAreRefused)
{
    for (const ReflectedAccess access : { ReflectedAccess::ReadWrite, ReflectedAccess::WriteOnly }) {
        ShaderReflectionResult reflection;
        reflection.descriptorBindings = { Slot(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "counter", access, kVert) };
        const std::optional<std::string> error = ValidateFullscreenReflection(reflection);
        ASSERT_TRUE(error.has_value());
        EXPECT_NE(error->find("readonly"), std::string::npos);
        EXPECT_NE(error->find("counter"), std::string::npos);
    }
}

TEST(FullscreenIndirectReflectionTest, StorageImagesAreStillRefused)
{
    ShaderReflectionResult reflection;
    reflection.descriptorBindings = { Slot(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "img", ReflectedAccess::ReadOnly, kFrag) };
    EXPECT_TRUE(ValidateFullscreenReflection(reflection).has_value());
}

TEST(FullscreenIndirectReflectionTest, SamplerReadByTheVertexStageIsRefused)
{
    ShaderReflectionResult reflection;
    reflection.descriptorBindings = {
        Slot(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "wind", ReflectedAccess::ReadOnly, kBoth) };
    const std::optional<std::string> error = ValidateFullscreenReflection(reflection);
    ASSERT_TRUE(error.has_value());
    EXPECT_NE(error->find("vertex"), std::string::npos);
}

TEST(FullscreenIndirectReflectionTest, FragmentOnlySamplerIsAccepted)
{
    ShaderReflectionResult reflection;
    reflection.descriptorBindings = {
        Slot(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "src", ReflectedAccess::ReadOnly, kFrag) };
    EXPECT_FALSE(ValidateFullscreenReflection(reflection).has_value());
}

// --- ValidateIndirectArgs ---------------------------------------------------------

BufferDesc ArgsDesc(std::uint64_t size, BufferUsage usage = BufferUsage::StorageBuffer | BufferUsage::IndirectBuffer)
{
    return BufferDesc{ size, usage };
}

TEST(ValidateIndirectArgsTest, ThreeLodCommandsFitAFortyEightByteBufferAtOffsets0_16_32)
{
    const std::optional<BufferDesc> desc = ArgsDesc(48);
    for (const std::uint64_t offset : { 0u, 16u, 32u }) {
        EXPECT_FALSE(ValidateIndirectArgs(desc, offset).has_value()) << offset;
    }
}

TEST(ValidateIndirectArgsTest, AnOffsetThatLeavesNoRoomForACommandIsRefused)
{
    const std::optional<BufferDesc> desc = ArgsDesc(48);
    EXPECT_TRUE(ValidateIndirectArgs(desc, 36).has_value());
    EXPECT_TRUE(ValidateIndirectArgs(desc, 48).has_value());
    EXPECT_TRUE(ValidateIndirectArgs(desc, 4096).has_value());
}

TEST(ValidateIndirectArgsTest, AnOffsetThatIsNotAMultipleOfFourIsRefused)
{
    const std::optional<std::string> error = ValidateIndirectArgs(ArgsDesc(64), 2);
    ASSERT_TRUE(error.has_value());
    EXPECT_NE(error->find("multiple of 4"), std::string::npos);
}

TEST(ValidateIndirectArgsTest, ADeclaredUsageWithoutIndirectIsRefusedButAnImportPasses)
{
    EXPECT_TRUE(ValidateIndirectArgs(ArgsDesc(64, BufferUsage::StorageBuffer), 0).has_value());
    EXPECT_FALSE(ValidateIndirectArgs(ArgsDesc(64, BufferUsage::None), 0).has_value());
}

TEST(ValidateIndirectArgsTest, AnUnknownBufferIsRefused)
{
    EXPECT_TRUE(ValidateIndirectArgs(std::nullopt, 0).has_value());
}

TEST(ValidateIndirectArgsTest, AnUnknownSizeSkipsTheFitCheck)
{
    EXPECT_FALSE(ValidateIndirectArgs(ArgsDesc(0, BufferUsage::None), 160).has_value());
}

// --- Pass declaration -------------------------------------------------------------

BindingPlan DrawPlan()
{
    BindingPlan plan;
    PlannedBinding params;
    params.binding = 0;
    params.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    params.access = ReflectedAccess::ReadOnly;
    params.kind = BindingResourceKind::Buffer;
    params.buffer = kParams;
    PlannedBinding instances;
    instances.binding = 1;
    instances.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    instances.access = ReflectedAccess::ReadOnly;
    instances.kind = BindingResourceKind::TrackedBuffer;
    instances.trackedBuffer = kInstances;
    plan.bindings = { params, instances };
    return plan;
}

const std::array<TextureHandle, 1> kOneTarget{ kTarget };

TEST(DeclareFullscreenIndirectTest, DeclaresTheArgsReadTheGraphicsStorageReadAndTheAttachments)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    DeclareFullscreen(pass, DrawPlan(), kLayout, kOneTarget, std::nullopt, {}, kDepth, IndirectArgsRef{ kArgs, 16 });

    ASSERT_EQ(record.reads.size(), 2u);
    EXPECT_EQ(record.reads[0].kind, ResourceKind::Buffer);
    EXPECT_EQ(record.reads[0].buffer, kInstances);
    EXPECT_EQ(record.reads[0].access, ResourceAccess::GraphicsShaderStorageRead);
    EXPECT_EQ(record.reads[1].buffer, kArgs);
    EXPECT_EQ(record.reads[1].access, ResourceAccess::IndirectCommandRead);
    EXPECT_TRUE(record.writes.size() >= 1u);

    ASSERT_EQ(record.bindings.size(), 2u);
    EXPECT_EQ(record.bindings[0].bufferObjectHandle, kParams); // External buffer: bound, not tracked.
    EXPECT_EQ(record.bindings[1].bufferHandle, kInstances);
    EXPECT_EQ(record.colorAttachments.size(), 1u);
}

TEST(DeclareFullscreenIndirectTest, WithoutIndirectArgsNoArgsReadIsDeclared)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    DeclareFullscreen(pass, DrawPlan(), kLayout, kOneTarget, std::nullopt, {}, std::nullopt);

    for (const ResourceUsage& read : record.reads) {
        EXPECT_NE(read.access, ResourceAccess::IndirectCommandRead);
    }
}

TEST(DeclareBindingsDomainTest, ComputeDomainKeepsTheComputeStorageRead)
{
    PassRecord record;
    RenderGraphBuilder::PassBuilder pass(record);
    DeclareBindings(pass, DrawPlan(), kLayout, BindingDomain::Compute);
    ASSERT_EQ(record.reads.size(), 1u);
    EXPECT_EQ(record.reads[0].access, ResourceAccess::ComputeShaderRead);
}

// --- Barriers -------------------------------------------------------------------

TEST(GraphicsShaderStorageReadTest, StateCoversTheVertexAndFragmentStagesAndIsNotAWrite)
{
    const ResourceState state = RequiredStateFor(ResourceAccess::GraphicsShaderStorageRead, false);
    EXPECT_EQ(state.stageMask, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    EXPECT_EQ(state.accessMask, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    EXPECT_FALSE(IsBufferWriteState(state));
}

TEST(GraphicsShaderStorageReadTest, ComputeWriteThenGraphicsReadBarriersComputeToVertexAndFragment)
{
    const ResourceState write = RequiredStateFor(ResourceAccess::ComputeShaderWrite, false);
    const ResourceState read = RequiredStateFor(ResourceAccess::GraphicsShaderStorageRead, false);
    ASSERT_TRUE(RequiresBufferBarrier(write, read));

    const VkBufferMemoryBarrier2 barrier = BuildBufferMemoryBarrier2(VK_NULL_HANDLE, 0, VK_WHOLE_SIZE, write, read);
    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

// The executor's per-buffer loop: one barrier after the compute write, none for the next two draws.
TEST(GraphicsShaderStorageReadTest, ThreeDrawsReadingOneBufferNeedOnlyTheFirstBarrier)
{
    for (const ResourceAccess read : { ResourceAccess::IndirectCommandRead, ResourceAccess::GraphicsShaderStorageRead }) {
        ResourceState state;
        int barriers = 0;
        const ResourceAccess sequence[] = { ResourceAccess::ComputeShaderWrite, read, read, read };
        for (std::size_t i = 0; i < std::size(sequence); ++i) {
            const ResourceState next = RequiredStateFor(sequence[i], false);
            if (RequiresBufferBarrier(state, next) && i > 0) {
                ++barriers;
            }
            state = next;
        }
        EXPECT_EQ(barriers, 1);
    }
}

// --- Graph ordering -----------------------------------------------------------------

TEST(FullscreenIndirectGraphTest, ACullPassThatWritesTheArgsIsOrderedBeforeThreeDrawsAndNothingIsCulled)
{
    RenderGraphBuilder builder;
    const BufferHandle instances = builder.CreateBuffer("Instances", BufferDesc{ 4096, BufferUsage::StorageBuffer });
    const BufferHandle args = builder.CreateBuffer("Args", BufferDesc{ 48, BufferUsage::StorageBuffer | BufferUsage::IndirectBuffer });
    const TextureHandle color = builder.CreateTexture("Color", TextureDesc{ 64, 64, PixelFormat::RGBA16_Float, false });

    builder.AddComputePass(
        "Cull",
        [&](RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteStorageBuffer(0, instances);
            pass.WriteStorageBuffer(1, args);
        },
        NoOpExecute); // index 0
    for (const std::uint64_t offset : { 0u, 16u, 32u }) {
        builder.AddPass(
            "Draw",
            [&, offset](RenderGraphBuilder::PassBuilder& pass) {
                const BindingPlan plan = [&] {
                    BindingPlan p = DrawPlan();
                    p.bindings[1].trackedBuffer = instances;
                    return p;
                }();
                DeclareFullscreen(pass, plan, kLayout, std::array<TextureHandle, 1>{ color }, std::nullopt, {},
                    std::nullopt, IndirectArgsRef{ args, offset });
            },
            NoOpExecute);
    }

    CompiledGraphInput input = builder.Finish();
    const TextureHandle finalOutputs[] = { color };
    const CompiledGraph compiled = Compile(input, finalOutputs);

    ASSERT_EQ(compiled.executionOrder.size(), 4u);
    EXPECT_EQ(compiled.executionOrder[0].index, 0u);
    for (const PassRecord& pass : input.passes) {
        EXPECT_FALSE(pass.isCulled);
    }
}

} // namespace
} // namespace gte::rg
