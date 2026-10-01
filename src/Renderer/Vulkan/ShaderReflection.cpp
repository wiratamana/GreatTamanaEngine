#include "ShaderReflection.h"

#include <spirv_reflect.h>

#include <fstream>
#include <map>
#include <stdexcept>

namespace gte {

namespace {

// Mirrors (deliberately duplicates, never calls) ShaderModule.cpp's own
// anonymous-namespace ReadFile() helper - that function has internal
// linkage and ShaderModule.h's only exported function always takes a live
// VkDevice, incompatible with this module's own explicit "no VkDevice
// anywhere in this file" design requirement. See PHASE1's own task doc,
// Step 2.2.
std::vector<char> ReadShaderSpirvFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "ShaderReflection: failed to open shader file '" + path + "' - was it compiled? See cmake/CompileShaders.cmake.");
    }

    const std::size_t size = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

// Maps a reflected SPIR-V descriptor type onto the three VkDescriptorType
// values this engine's own DescriptorSetLayoutBuilder/ComputeDescriptorSet
// understand today (see ComputeDescriptorSet.h's own three static
// factories) - any other reflected type is a loud, explicit error, never a
// silent drop.
VkDescriptorType MapDescriptorType(SpvReflectDescriptorType type, const std::string& shaderSpirvPath)
{
    switch (type) {
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default:
            throw std::runtime_error(
                "ShaderReflection: shader '" + shaderSpirvPath + "' declares a descriptor type this engine's "
                "DescriptorSetLayoutBuilder/ComputeDescriptorSet cannot express - only STORAGE_BUFFER / "
                "STORAGE_IMAGE / COMBINED_IMAGE_SAMPLER are supported (see ComputeDescriptorSet.h).");
    }
}

} // namespace

ShaderReflectionResult ReflectComputeShader(const std::string& shaderSpirvPath)
{
    const std::vector<char> spirv = ReadShaderSpirvFile(shaderSpirvPath);

    SpvReflectShaderModule module{};
    const SpvReflectResult createResult = spvReflectCreateShaderModule(spirv.size(), spirv.data(), &module);
    if (createResult != SPV_REFLECT_RESULT_SUCCESS) {
        throw std::runtime_error(
            "ShaderReflection: spvReflectCreateShaderModule failed for '" + shaderSpirvPath +
            "' - is this a valid compiled SPIR-V binary?");
    }

    // RAII-via-try/catch, mirroring ComputePipeline's own constructor shape
    // (see PHASE1's own task doc, Step 2.2) - every path (success or a
    // thrown "unsupported descriptor type"/"more than one push-constant
    // block" error) must call spvReflectDestroyShaderModule() exactly once.
    try {
        ShaderReflectionResult result;

        // --- Descriptor bindings --------------------------------------
        std::uint32_t bindingCount = 0;
        SpvReflectResult enumResult = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr);
        if (enumResult != SPV_REFLECT_RESULT_SUCCESS) {
            throw std::runtime_error(
                "ShaderReflection: spvReflectEnumerateDescriptorBindings (count) failed for '" + shaderSpirvPath + "'.");
        }

        std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
        if (bindingCount > 0) {
            enumResult = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, bindings.data());
            if (enumResult != SPV_REFLECT_RESULT_SUCCESS) {
                throw std::runtime_error(
                    "ShaderReflection: spvReflectEnumerateDescriptorBindings failed for '" + shaderSpirvPath + "'.");
            }
        }

        result.descriptorBindings.reserve(bindingCount);
        for (const SpvReflectDescriptorBinding* binding : bindings) {
            ReflectedDescriptorBinding reflected;
            reflected.set = binding->set;
            reflected.binding = binding->binding;
            reflected.type = MapDescriptorType(binding->descriptor_type, shaderSpirvPath);
            reflected.count = binding->count > 0 ? binding->count : 1;
            reflected.name = binding->name != nullptr ? std::string(binding->name) : std::string();
            result.descriptorBindings.push_back(reflected);
        }

        // --- Push-constant block ---------------------------------------
        std::uint32_t pushConstantCount = 0;
        enumResult = spvReflectEnumeratePushConstantBlocks(&module, &pushConstantCount, nullptr);
        if (enumResult != SPV_REFLECT_RESULT_SUCCESS) {
            throw std::runtime_error(
                "ShaderReflection: spvReflectEnumeratePushConstantBlocks (count) failed for '" + shaderSpirvPath + "'.");
        }
        if (pushConstantCount > 1) {
            throw std::runtime_error(
                "ShaderReflection: shader '" + shaderSpirvPath + "' declares more than one push-constant block - "
                "this engine's convention is exactly one layout(push_constant) block per shader.");
        }
        if (pushConstantCount == 1) {
            SpvReflectBlockVariable* block = nullptr;
            enumResult = spvReflectEnumeratePushConstantBlocks(&module, &pushConstantCount, &block);
            if (enumResult != SPV_REFLECT_RESULT_SUCCESS) {
                throw std::runtime_error(
                    "ShaderReflection: spvReflectEnumeratePushConstantBlocks failed for '" + shaderSpirvPath + "'.");
            }
            result.hasPushConstantRange = true;
            result.pushConstantRange.offset = block->offset;
            result.pushConstantRange.size = block->size;
        }

        // --- Declared local work-group size (from the shader's first/only
        //     entry point - this engine only ever compiles single-entry-
        //     point compute shaders) ---------------------------------
        if (module.entry_point_count > 0) {
            const SpvReflectEntryPoint& entryPoint = module.entry_points[0];
            result.localSize.x = entryPoint.local_size.x;
            result.localSize.y = entryPoint.local_size.y;
            result.localSize.z = entryPoint.local_size.z;
        }

        spvReflectDestroyShaderModule(&module);
        return result;
    } catch (...) {
        spvReflectDestroyShaderModule(&module);
        throw;
    }
}

// task_manager/better-render-pass-1 campaign, PHASE2
// (PHASE2_REFLECTION_BASED_COMPUTE_PIPELINE_CREATION.md) - a plain std::map
// keyed by `set` keeps distinct set numbers in ascending order "for free"
// (map iteration order == key order), and each bucket's own std::vector
// preserves the original relative order of bindings pushed into it
// (push_back never reorders).
std::vector<DescriptorBindingSetGroup> GroupDescriptorBindingsBySet(const std::vector<ReflectedDescriptorBinding>& bindings)
{
    std::map<std::uint32_t, std::vector<ReflectedDescriptorBinding>> bindingsBySet;
    for (const ReflectedDescriptorBinding& binding : bindings) {
        bindingsBySet[binding.set].push_back(binding);
    }

    std::vector<DescriptorBindingSetGroup> result;
    result.reserve(bindingsBySet.size());
    for (auto& [set, groupBindings] : bindingsBySet) {
        DescriptorBindingSetGroup group;
        group.set = set;
        group.bindings = std::move(groupBindings);
        result.push_back(std::move(group));
    }
    return result;
}

} // namespace gte
