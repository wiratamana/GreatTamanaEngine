// Skinning pass: reflected shader names, a dispatch missing a buffer, and many dispatches of one pass name.

#include "Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/Vulkan/ShaderReflection.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace gte {
namespace {

bool HasBinding(const ShaderReflectionResult& result, const char* name, std::uint32_t binding)
{
    return std::any_of(result.descriptorBindings.begin(), result.descriptorBindings.end(),
        [&](const ReflectedDescriptorBinding& b) { return b.name == name && b.set == 0 && b.binding == binding; });
}

ShaderReflectionResult Reflect(const char* path)
{
    const ShaderStageFile stages[] = { { VK_SHADER_STAGE_COMPUTE_BIT, path } };
    return ReflectShader(stages);
}

TEST(GpuSkinningShaderNamesTest, UntexturedShaderExposesTheBoundNames)
{
    const ShaderReflectionResult result = Reflect(GTE_TEST_SKIN_SPV_PATH);
    EXPECT_TRUE(HasBinding(result, "bindPose", 0));
    EXPECT_TRUE(HasBinding(result, "skinWeights", 1));
    EXPECT_TRUE(HasBinding(result, "boneMatrices", 2));
    EXPECT_TRUE(HasBinding(result, "outputBuffer", 3));
    EXPECT_EQ(result.descriptorBindings.size(), 4u);
    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.size, 4u);
}

TEST(GpuSkinningShaderNamesTest, TexturedShaderAddsTheUvInput)
{
    const ShaderReflectionResult result = Reflect(GTE_TEST_SKIN_UV_SPV_PATH);
    EXPECT_TRUE(HasBinding(result, "bindPose", 0));
    EXPECT_TRUE(HasBinding(result, "outputBuffer", 3));
    EXPECT_TRUE(HasBinding(result, "bindPoseUv", 4));
    EXPECT_EQ(result.descriptorBindings.size(), 5u);
    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.size, 4u);
}

TEST(GpuSkinningPassTest, ADispatchMissingABufferDeclaresNothing)
{
    rg::RenderGraphBuilder builder;
    GpuSkinningPipelines pipelines;
    const GpuSkinningDispatch dispatch; // Every buffer pointer is null.

    EXPECT_FALSE(pipelines.Declare(builder, dispatch, rg::ViewScope::Shared, rg::RenderPassEvent::PreOpaques));
    EXPECT_EQ(builder.DeclaredPassCount(), 0u);
}

// Tier-2: each output group gets its own descriptor set under one shared pass name.
TEST(GpuSkinningPassTest, ManyGroupsShareOnePassNameThroughInstanceKeys)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    const rg::DeviceRef device = fixture.GetRenderer().Device();
    using rg::GpuBuffer;
    using rg::GpuBufferUse;
    GpuBuffer bindPose(device, 64, "TestBindPose", GpuBufferUse::Storage);
    GpuBuffer weights(device, 64, "TestWeights", GpuBufferUse::Storage);
    GpuBuffer bones(device, 64, "TestBones", GpuBufferUse::Storage);
    std::vector<GpuBuffer> outputs;
    for (int i = 0; i < 4; ++i) {
        outputs.emplace_back(device, 64, "TestOutput", GpuBufferUse::Storage);
    }

    GpuSkinningPipelines pipelines;
    rg::RenderGraphBuilder builder;
    builder.SetRenderer(&fixture.GetRenderer());
    builder.SetPersistentResourceCache(nullptr, rg::ExecuteTimingMode::SynchronousImmediateReadback, 1);

    const auto declare = [&](std::size_t outputIndex, std::uint64_t instance) {
        GpuSkinningDispatch dispatch;
        dispatch.bindPose = &bindPose;
        dispatch.skinWeights = &weights;
        dispatch.boneMatrices = &bones;
        dispatch.output = &outputs[outputIndex];
        dispatch.outputHandle = builder.Import(outputs[outputIndex], "TestOutput");
        dispatch.vertexCount = 2;
        dispatch.instance = instance;
        return pipelines.Declare(builder, dispatch, rg::ViewScope::Shared, rg::RenderPassEvent::PreOpaques);
    };

    EXPECT_TRUE(declare(0, 1));
    EXPECT_TRUE(declare(1, 2));
    EXPECT_TRUE(declare(2, 3));
    EXPECT_TRUE(declare(3, 3)); // Declared, but refused at compile time: the identity repeats.

    const rg::CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 4u);
    EXPECT_FALSE(input.passes[0].skipExecute);
    EXPECT_FALSE(input.passes[1].skipExecute);
    EXPECT_FALSE(input.passes[2].skipExecute);
    EXPECT_TRUE(input.passes[3].skipExecute);
}

} // namespace
} // namespace gte
