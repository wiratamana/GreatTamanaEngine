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

} // namespace
