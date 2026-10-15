// Pure SPIR-V reflection tests (no Vulkan device). Fixtures are compiled by tests/CMakeLists.txt.

#include "Renderer/Vulkan/ShaderReflection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using gte::ReflectedAccess;
using gte::ReflectedDescriptorBinding;

const ReflectedDescriptorBinding* FindByName(const gte::ShaderReflectionResult& result, const std::string& name)
{
    const auto it = std::find_if(result.descriptorBindings.begin(), result.descriptorBindings.end(),
        [&](const ReflectedDescriptorBinding& b) { return b.name == name; });
    return it == result.descriptorBindings.end() ? nullptr : &*it;
}

ReflectedDescriptorBinding MakeBinding(std::uint32_t set, std::uint32_t binding, VkDescriptorType type,
    const char* name = "x", VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT)
{
    ReflectedDescriptorBinding result;
    result.set = set;
    result.binding = binding;
    result.type = type;
    result.name = name;
    result.stages = stages;
    return result;
}

std::string ThrownMessage(const std::function<void()>& fn)
{
    try {
        fn();
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return {};
}

gte::ShaderReflectionResult ReflectComputeFixture()
{
    const gte::ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, GTE_TEST_REFLECT_COMP_SPV_PATH };
    return gte::ReflectShader(std::span<const gte::ShaderStageFile>(&stage, 1));
}

gte::ShaderReflectionResult ReflectGraphicsFixture()
{
    const gte::ShaderStageFile stages[] = {
        { VK_SHADER_STAGE_VERTEX_BIT, GTE_TEST_REFLECT_VERT_SPV_PATH },
        { VK_SHADER_STAGE_FRAGMENT_BIT, GTE_TEST_REFLECT_FRAG_SPV_PATH },
    };
    return gte::ReflectShader(stages);
}

TEST(ShaderReflectionTests, ReflectsBoxBlurBindingsPushConstantsAndLocalSize)
{
    const gte::ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, GTE_TEST_BOXBLUR_SPV_PATH };
    const gte::ShaderReflectionResult result = gte::ReflectShader(std::span<const gte::ShaderStageFile>(&stage, 1));

    ASSERT_EQ(result.descriptorBindings.size(), 2u);
    const ReflectedDescriptorBinding* source = FindByName(result, "sourceTexture");
    const ReflectedDescriptorBinding* dest = FindByName(result, "destinationImage");
    ASSERT_NE(source, nullptr);
    ASSERT_NE(dest, nullptr);
    EXPECT_EQ(source->binding, 0u);
    EXPECT_EQ(source->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    EXPECT_EQ(dest->binding, 1u);
    EXPECT_EQ(dest->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(std::uint32_t) * 2);
    EXPECT_EQ(result.pushConstantStages, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_COMPUTE_BIT));
    EXPECT_EQ(result.localSize.x, 16u);
    EXPECT_EQ(result.localSize.y, 16u);
    EXPECT_EQ(result.localSize.z, 1u);
}

TEST(ShaderReflectionTests, ComputeFixtureReportsNamesTypesAccessAndLocalSize)
{
    const gte::ShaderReflectionResult result = ReflectComputeFixture();

    ASSERT_EQ(result.descriptorBindings.size(), 4u);
    const ReflectedDescriptorBinding* params = FindByName(result, "params");
    const ReflectedDescriptorBinding* depthA = FindByName(result, "inputDepthA");
    const ReflectedDescriptorBinding* depthB = FindByName(result, "inputDepthB");
    const ReflectedDescriptorBinding* mask = FindByName(result, "maskOutput");
    ASSERT_NE(params, nullptr);
    ASSERT_NE(depthA, nullptr);
    ASSERT_NE(depthB, nullptr);
    ASSERT_NE(mask, nullptr);

    EXPECT_EQ(params->type, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_EQ(params->access, ReflectedAccess::ReadOnly);
    EXPECT_EQ(depthA->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    EXPECT_EQ(depthA->access, ReflectedAccess::ReadOnly);
    EXPECT_EQ(depthB->access, ReflectedAccess::ReadOnly);
    EXPECT_EQ(mask->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(mask->access, ReflectedAccess::WriteOnly);

    for (const ReflectedDescriptorBinding& binding : result.descriptorBindings) {
        EXPECT_EQ(binding.stages, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_COMPUTE_BIT));
    }
    EXPECT_FALSE(result.hasPushConstantRange);
    EXPECT_EQ(result.localSize.x, 16u);
    EXPECT_EQ(result.localSize.y, 16u);
}

// Fails if the shader compiler ever strips debug names: name-based binding needs them.
TEST(ShaderReflectionTests, FixtureBindingNamesSurviveCompilation)
{
    const gte::ShaderReflectionResult result = ReflectComputeFixture();
    ASSERT_FALSE(result.descriptorBindings.empty());
    for (const ReflectedDescriptorBinding& binding : result.descriptorBindings) {
        EXPECT_FALSE(binding.name.empty());
    }
    EXPECT_NO_THROW(gte::ValidateBindingNames(result.descriptorBindings, "ReflectFixture.comp"));
}

TEST(ShaderReflectionTests, GraphicsFixtureMergesStagesAndPushBlock)
{
    const gte::ShaderReflectionResult result = ReflectGraphicsFixture();

    ASSERT_EQ(result.descriptorBindings.size(), 2u);
    const ReflectedDescriptorBinding* color = FindByName(result, "sourceColor");
    const ReflectedDescriptorBinding* mask = FindByName(result, "sourceMask");
    ASSERT_NE(color, nullptr);
    ASSERT_NE(mask, nullptr);

    // sourceColor is declared by both stages, sourceMask only by the fragment stage.
    EXPECT_EQ(color->stages, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));
    EXPECT_EQ(mask->stages, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_FRAGMENT_BIT));
    EXPECT_EQ(color->access, ReflectedAccess::ReadOnly);
    EXPECT_EQ(mask->access, ReflectedAccess::ReadOnly);

    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.size, 4u);
    EXPECT_EQ(result.pushConstantStages, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_FRAGMENT_BIT));
}

TEST(ShaderReflectionTests, StageMismatchThrows)
{
    const gte::ShaderStageFile stage{ VK_SHADER_STAGE_FRAGMENT_BIT, GTE_TEST_REFLECT_COMP_SPV_PATH };
    EXPECT_THROW(gte::ReflectShader(std::span<const gte::ShaderStageFile>(&stage, 1)), std::runtime_error);
}

TEST(ShaderReflectionTests, NonExistentFileThrowsRuntimeError)
{
    const gte::ShaderStageFile stage{ VK_SHADER_STAGE_COMPUTE_BIT, "shaders/DoesNotExist.comp.spv" };
    EXPECT_THROW(gte::ReflectShader(std::span<const gte::ShaderStageFile>(&stage, 1)), std::runtime_error);
}

TEST(ShaderReflectionTests, NoStagesThrows)
{
    EXPECT_THROW(gte::ReflectShader({}), std::runtime_error);
}

TEST(ShaderReflectionTests, MergeOrsStageFlagsForTheSameSlot)
{
    std::vector<ReflectedDescriptorBinding> merged{ MakeBinding(0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "a",
        VK_SHADER_STAGE_VERTEX_BIT) };
    const ReflectedDescriptorBinding incoming[] = { MakeBinding(
        0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "a", VK_SHADER_STAGE_FRAGMENT_BIT) };

    gte::MergeStageBindings(merged, incoming);

    ASSERT_EQ(merged.size(), 1u);
    EXPECT_EQ(merged[0].stages, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));
}

TEST(ShaderReflectionTests, MergeAppendsDistinctSlots)
{
    std::vector<ReflectedDescriptorBinding> merged{ MakeBinding(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "a") };
    const ReflectedDescriptorBinding incoming[] = { MakeBinding(0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "b") };

    gte::MergeStageBindings(merged, incoming);

    EXPECT_EQ(merged.size(), 2u);
}

TEST(ShaderReflectionTests, MergeCombinesDifferingAccessToReadWrite)
{
    ReflectedDescriptorBinding readOnly = MakeBinding(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "a");
    readOnly.access = ReflectedAccess::ReadOnly;
    ReflectedDescriptorBinding writeOnly = readOnly;
    writeOnly.access = ReflectedAccess::WriteOnly;
    std::vector<ReflectedDescriptorBinding> merged{ readOnly };

    gte::MergeStageBindings(merged, std::span<const ReflectedDescriptorBinding>(&writeOnly, 1));

    EXPECT_EQ(merged[0].access, ReflectedAccess::ReadWrite);
}

TEST(ShaderReflectionTests, MergeThrowsOnTypeNameOrCountConflict)
{
    const ReflectedDescriptorBinding base = MakeBinding(1, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "a");

    ReflectedDescriptorBinding otherType = base;
    otherType.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ReflectedDescriptorBinding otherName = base;
    otherName.name = "b";
    ReflectedDescriptorBinding otherCount = base;
    otherCount.count = 4;

    for (const ReflectedDescriptorBinding& conflicting : { otherType, otherName, otherCount }) {
        std::vector<ReflectedDescriptorBinding> merged{ base };
        const std::string message = ThrownMessage(
            [&] { gte::MergeStageBindings(merged, std::span<const ReflectedDescriptorBinding>(&conflicting, 1)); });
        EXPECT_NE(message.find("set 1, binding 2"), std::string::npos) << message;
    }
}

TEST(ShaderReflectionTests, PushBlocksOfEqualSizeMergeAndUnionStages)
{
    gte::ShaderReflectionResult result;
    gte::MergePushConstantRange(result, { 0, 16 }, VK_SHADER_STAGE_VERTEX_BIT);
    gte::MergePushConstantRange(result, { 0, 16 }, VK_SHADER_STAGE_FRAGMENT_BIT);

    EXPECT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.size, 16u);
    EXPECT_EQ(result.pushConstantStages, static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));
}

TEST(ShaderReflectionTests, PushBlocksOfDifferentSizeThrow)
{
    gte::ShaderReflectionResult result;
    gte::MergePushConstantRange(result, { 0, 16 }, VK_SHADER_STAGE_VERTEX_BIT);
    EXPECT_THROW(gte::MergePushConstantRange(result, { 0, 8 }, VK_SHADER_STAGE_FRAGMENT_BIT), std::runtime_error);
}

TEST(ShaderReflectionTests, ValidateNamesListsEveryUnnamedSlot)
{
    const std::vector<ReflectedDescriptorBinding> bindings{
        MakeBinding(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "params"),
        MakeBinding(0, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ""),
        MakeBinding(1, 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ""),
    };

    const std::string message = ThrownMessage([&] { gte::ValidateBindingNames(bindings, "Shader"); });

    EXPECT_NE(message.find("set 0, binding 2"), std::string::npos) << message;
    EXPECT_NE(message.find("set 1, binding 5"), std::string::npos) << message;
}

TEST(ShaderReflectionTests, ValidateNamesRejectsTheSameNameOnTwoSlots)
{
    const std::vector<ReflectedDescriptorBinding> bindings{
        MakeBinding(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "dup"),
        MakeBinding(0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "dup"),
    };
    EXPECT_THROW(gte::ValidateBindingNames(bindings, "Shader"), std::runtime_error);
}

TEST(ShaderReflectionTests, GroupBySetEmptyInputReturnsEmptyResult)
{
    EXPECT_TRUE(gte::GroupDescriptorBindingsBySet({}).empty());
}

TEST(ShaderReflectionTests, GroupBySetSplitsAcrossSetsInAscendingOrderKeepingBindingOrder)
{
    const std::vector<ReflectedDescriptorBinding> bindings{
        MakeBinding(1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        MakeBinding(0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        MakeBinding(0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        MakeBinding(1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
    };

    const std::vector<gte::DescriptorBindingSetGroup> groups = gte::GroupDescriptorBindingsBySet(bindings);

    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].set, 0u);
    ASSERT_EQ(groups[0].bindings.size(), 2u);
    EXPECT_EQ(groups[0].bindings[0].binding, 0u);
    EXPECT_EQ(groups[0].bindings[1].binding, 1u);
    EXPECT_EQ(groups[1].set, 1u);
    ASSERT_EQ(groups[1].bindings.size(), 2u);
    EXPECT_EQ(groups[1].bindings[0].binding, 0u);
    EXPECT_EQ(groups[1].bindings[1].binding, 1u);
}

} // namespace
