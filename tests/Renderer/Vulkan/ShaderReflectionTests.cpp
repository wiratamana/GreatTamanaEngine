// task_manager/better-render-pass-1 campaign, PHASE1
// (PHASE1_SPIRV_REFLECT_DEPENDENCY_AND_SHADER_REFLECTION_CORE.md) - Tier-1
// test for gte::ReflectComputeShader() (src/Renderer/Vulkan/
// ShaderReflection.h/.cpp), asserted against a REAL, already-compiled
// .comp.spv fixture - Shaders/BoxBlur.comp.spv, staged next to this test
// binary by tests/CMakeLists.txt's own gte_add_shader(GreatTamanaEngineTests
// src/Shaders/BoxBlur.comp) call. No live VkDevice involved anywhere in this
// file - pure binary parsing only.

#include "Renderer/Vulkan/ShaderReflection.h"

#include <gtest/gtest.h>

namespace {

TEST(ShaderReflectionTests, ReflectsBoxBlurComputeShaderBindingsPushConstantsAndLocalSize)
{
#ifndef GTE_TEST_BOXBLUR_SPV_PATH
    FAIL() << "GTE_TEST_BOXBLUR_SPV_PATH was not defined by tests/CMakeLists.txt.";
#else
    const gte::ShaderReflectionResult result = gte::ReflectComputeShader(GTE_TEST_BOXBLUR_SPV_PATH);

    // Shaders/BoxBlur.comp's own documented binding convention (see that
    // file's header comment, and ComputeBlurValidation.cpp): binding 0 is a
    // combined-image-sampler input, binding 1 is a storage-image output.
    ASSERT_EQ(result.descriptorBindings.size(), 2u);

    const gte::ReflectedDescriptorBinding* binding0 = nullptr;
    const gte::ReflectedDescriptorBinding* binding1 = nullptr;
    for (const gte::ReflectedDescriptorBinding& binding : result.descriptorBindings) {
        EXPECT_EQ(binding.set, 0u);
        if (binding.binding == 0) {
            binding0 = &binding;
        } else if (binding.binding == 1) {
            binding1 = &binding;
        }
    }

    ASSERT_NE(binding0, nullptr);
    EXPECT_EQ(binding0->type, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    EXPECT_EQ(binding0->count, 1u);

    ASSERT_NE(binding1, nullptr);
    EXPECT_EQ(binding1->type, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    EXPECT_EQ(binding1->count, 1u);

    // Push-constant block: { uint width; uint height; } -> 2 * sizeof(uint32_t).
    ASSERT_TRUE(result.hasPushConstantRange);
    EXPECT_EQ(result.pushConstantRange.size, sizeof(std::uint32_t) * 2);

    // layout(local_size_x = 16, local_size_y = 16) in;
    EXPECT_EQ(result.localSize.x, 16u);
    EXPECT_EQ(result.localSize.y, 16u);
    EXPECT_EQ(result.localSize.z, 1u);
#endif
}

TEST(ShaderReflectionTests, NonExistentFileThrowsRuntimeErrorRatherThanCrashing)
{
    EXPECT_THROW(
        { gte::ReflectComputeShader("shaders/DoesNotExist.comp.spv"); },
        std::runtime_error);
}

// task_manager/better-render-pass-1 campaign, PHASE2
// (PHASE2_REFLECTION_BASED_COMPUTE_PIPELINE_CREATION.md) - Tier-1 tests for
// the new, pure gte::GroupDescriptorBindingsBySet() helper
// ComputePipeline's own reflection-driven constructor path uses to build
// one VkDescriptorSetLayout per distinct `set` a shader declares. Hand-
// built ReflectedDescriptorBinding fixtures only - no real .spv file/VkDevice
// involved.

namespace {

gte::ReflectedDescriptorBinding MakeBinding(std::uint32_t set, std::uint32_t binding, VkDescriptorType type)
{
    gte::ReflectedDescriptorBinding result;
    result.set = set;
    result.binding = binding;
    result.type = type;
    result.count = 1;
    return result;
}

} // namespace

TEST(ShaderReflectionTests, GroupDescriptorBindingsBySetEmptyInputReturnsEmptyResult)
{
    const std::vector<gte::ReflectedDescriptorBinding> empty;
    const std::vector<gte::DescriptorBindingSetGroup> groups = gte::GroupDescriptorBindingsBySet(empty);
    EXPECT_TRUE(groups.empty());
}

TEST(ShaderReflectionTests, GroupDescriptorBindingsBySetAllBindingsInSetZero)
{
    const std::vector<gte::ReflectedDescriptorBinding> bindings{
        MakeBinding(0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        MakeBinding(0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        MakeBinding(0, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    };

    const std::vector<gte::DescriptorBindingSetGroup> groups = gte::GroupDescriptorBindingsBySet(bindings);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].set, 0u);
    ASSERT_EQ(groups[0].bindings.size(), 3u);
    // Relative order within the set must be preserved - never reordered by
    // binding number.
    EXPECT_EQ(groups[0].bindings[0].binding, 0u);
    EXPECT_EQ(groups[0].bindings[1].binding, 1u);
    EXPECT_EQ(groups[0].bindings[2].binding, 2u);
}

TEST(ShaderReflectionTests, GroupDescriptorBindingsBySetSplitsAcrossSetZeroAndSetOneInAscendingOrder)
{
    // Deliberately interleaved input order (set 1 binding declared before a
    // later set 0 binding) to prove grouping is driven purely by `.set`,
    // and the OUTPUT group order is still ascending by set number
    // regardless of input order.
    const std::vector<gte::ReflectedDescriptorBinding> bindings{
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
