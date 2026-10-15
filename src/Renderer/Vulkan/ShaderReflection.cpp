#include "ShaderReflection.h"

#include "../ShaderFile.h"

#include <spirv_reflect.h>

#include <map>
#include <stdexcept>

namespace gte {

namespace {

// Owns one SpvReflectShaderModule.
class ReflectModule {
public:
    ReflectModule(const std::vector<std::byte>& spirv, const std::string& path)
    {
        if (spvReflectCreateShaderModule(spirv.size(), spirv.data(), &m_module) != SPV_REFLECT_RESULT_SUCCESS) {
            throw std::runtime_error("ShaderReflection: '" + path + "' is not a valid compiled SPIR-V binary.");
        }
    }
    ~ReflectModule() { spvReflectDestroyShaderModule(&m_module); }
    ReflectModule(const ReflectModule&) = delete;
    ReflectModule& operator=(const ReflectModule&) = delete;

    SpvReflectShaderModule& Get() noexcept { return m_module; }

private:
    SpvReflectShaderModule m_module{};
};

VkDescriptorType MapDescriptorType(SpvReflectDescriptorType type, const std::string& path)
{
    switch (type) {
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default:
            throw std::runtime_error("ShaderReflection: '" + path
                + "' declares an unsupported descriptor type (only storage buffer, storage image and "
                  "combined image sampler are supported).");
    }
}

// Samplers are always read. Storage resources follow the readonly/writeonly decorations.
ReflectedAccess InferAccess(const SpvReflectDescriptorBinding& binding, VkDescriptorType type)
{
    if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        return ReflectedAccess::ReadOnly;
    }
    const bool nonWritable = (binding.decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0;
    const bool nonReadable = (binding.decoration_flags & SPV_REFLECT_DECORATION_NON_READABLE) != 0;
    if (nonWritable && !nonReadable) {
        return ReflectedAccess::ReadOnly;
    }
    if (nonReadable && !nonWritable) {
        return ReflectedAccess::WriteOnly;
    }
    return ReflectedAccess::ReadWrite;
}

ReflectedAccess MergeAccess(ReflectedAccess a, ReflectedAccess b)
{
    return a == b ? a : ReflectedAccess::ReadWrite;
}

std::string SlotText(std::uint32_t set, std::uint32_t binding)
{
    return "(set " + std::to_string(set) + ", binding " + std::to_string(binding) + ")";
}

struct StageReflection {
    std::vector<ReflectedDescriptorBinding> bindings;
    bool hasPush = false;
    ReflectedPushConstantRange push;
    ReflectedLocalSize localSize;
};

StageReflection ReflectStage(const ShaderStageFile& file)
{
    const std::vector<std::byte> spirv = ReadShaderFileBytes(file.spirvPath);
    ReflectModule module(spirv, file.spirvPath);
    SpvReflectShaderModule& mod = module.Get();

    if (static_cast<VkShaderStageFlagBits>(mod.shader_stage) != file.stage) {
        throw std::runtime_error("ShaderReflection: '" + file.spirvPath + "' is not the declared shader stage.");
    }

    StageReflection out;

    std::uint32_t bindingCount = 0;
    if (spvReflectEnumerateDescriptorBindings(&mod, &bindingCount, nullptr) != SPV_REFLECT_RESULT_SUCCESS) {
        throw std::runtime_error("ShaderReflection: cannot enumerate descriptor bindings of '" + file.spirvPath + "'.");
    }
    std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
    if (bindingCount > 0
        && spvReflectEnumerateDescriptorBindings(&mod, &bindingCount, bindings.data()) != SPV_REFLECT_RESULT_SUCCESS) {
        throw std::runtime_error("ShaderReflection: cannot enumerate descriptor bindings of '" + file.spirvPath + "'.");
    }

    out.bindings.reserve(bindingCount);
    for (const SpvReflectDescriptorBinding* binding : bindings) {
        ReflectedDescriptorBinding reflected;
        reflected.set = binding->set;
        reflected.binding = binding->binding;
        reflected.type = MapDescriptorType(binding->descriptor_type, file.spirvPath);
        reflected.count = binding->count > 0 ? binding->count : 1;
        reflected.name = binding->name != nullptr ? binding->name : "";
        reflected.stages = static_cast<VkShaderStageFlags>(file.stage);
        reflected.access = InferAccess(*binding, reflected.type);
        out.bindings.push_back(std::move(reflected));
    }

    std::uint32_t pushCount = 0;
    if (spvReflectEnumeratePushConstantBlocks(&mod, &pushCount, nullptr) != SPV_REFLECT_RESULT_SUCCESS) {
        throw std::runtime_error("ShaderReflection: cannot enumerate push constants of '" + file.spirvPath + "'.");
    }
    if (pushCount > 1) {
        throw std::runtime_error(
            "ShaderReflection: '" + file.spirvPath + "' declares more than one push-constant block.");
    }
    if (pushCount == 1) {
        SpvReflectBlockVariable* block = nullptr;
        if (spvReflectEnumeratePushConstantBlocks(&mod, &pushCount, &block) != SPV_REFLECT_RESULT_SUCCESS) {
            throw std::runtime_error("ShaderReflection: cannot enumerate push constants of '" + file.spirvPath + "'.");
        }
        out.hasPush = true;
        out.push = { block->offset, block->size };
    }

    if (file.stage == VK_SHADER_STAGE_COMPUTE_BIT && mod.entry_point_count > 0) {
        const SpvReflectEntryPoint& entry = mod.entry_points[0];
        out.localSize = { entry.local_size.x, entry.local_size.y, entry.local_size.z };
    }
    return out;
}

} // namespace

void MergeStageBindings(
    std::vector<ReflectedDescriptorBinding>& merged, std::span<const ReflectedDescriptorBinding> stageBindings)
{
    for (const ReflectedDescriptorBinding& incoming : stageBindings) {
        ReflectedDescriptorBinding* existing = nullptr;
        for (ReflectedDescriptorBinding& candidate : merged) {
            if (candidate.set == incoming.set && candidate.binding == incoming.binding) {
                existing = &candidate;
                break;
            }
        }
        if (existing == nullptr) {
            merged.push_back(incoming);
            continue;
        }
        if (existing->type != incoming.type || existing->name != incoming.name || existing->count != incoming.count) {
            throw std::runtime_error("ShaderReflection: stages disagree on type, name or count of slot "
                + SlotText(incoming.set, incoming.binding) + " ('" + existing->name + "' vs '" + incoming.name + "').");
        }
        existing->stages |= incoming.stages;
        existing->access = MergeAccess(existing->access, incoming.access);
    }
}

void MergePushConstantRange(
    ShaderReflectionResult& result, const ReflectedPushConstantRange& range, VkShaderStageFlags stages)
{
    if (!result.hasPushConstantRange) {
        result.hasPushConstantRange = true;
        result.pushConstantRange = range;
        result.pushConstantStages = stages;
        return;
    }
    if (result.pushConstantRange.size != range.size || result.pushConstantRange.offset != range.offset) {
        throw std::runtime_error("ShaderReflection: stages declare push-constant blocks of different size ("
            + std::to_string(result.pushConstantRange.size) + " vs " + std::to_string(range.size) + " bytes).");
    }
    result.pushConstantStages |= stages;
}

void ValidateBindingNames(std::span<const ReflectedDescriptorBinding> bindings, const std::string& shaderLabel)
{
    std::string unnamed;
    for (const ReflectedDescriptorBinding& binding : bindings) {
        if (binding.name.empty()) {
            unnamed += (unnamed.empty() ? "" : ", ") + SlotText(binding.set, binding.binding);
        }
    }
    if (!unnamed.empty()) {
        throw std::runtime_error("ShaderReflection: '" + shaderLabel + "' has unnamed bindings " + unnamed
            + " - give the variable an instance name and compile without stripping debug names.");
    }
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        for (std::size_t j = i + 1; j < bindings.size(); ++j) {
            if (bindings[i].name == bindings[j].name) {
                throw std::runtime_error("ShaderReflection: '" + shaderLabel + "' uses the name '" + bindings[i].name
                    + "' for " + SlotText(bindings[i].set, bindings[i].binding) + " and "
                    + SlotText(bindings[j].set, bindings[j].binding) + ".");
            }
        }
    }
}

ShaderReflectionResult ReflectShader(std::span<const ShaderStageFile> stages)
{
    if (stages.empty()) {
        throw std::runtime_error("ShaderReflection: no shader stages given.");
    }
    ShaderReflectionResult result;
    for (const ShaderStageFile& file : stages) {
        const StageReflection stage = ReflectStage(file);
        MergeStageBindings(result.descriptorBindings, stage.bindings);
        if (stage.hasPush) {
            MergePushConstantRange(result, stage.push, static_cast<VkShaderStageFlags>(file.stage));
        }
        if (file.stage == VK_SHADER_STAGE_COMPUTE_BIT) {
            result.localSize = stage.localSize;
        }
    }
    return result;
}

std::vector<DescriptorBindingSetGroup> GroupDescriptorBindingsBySet(
    const std::vector<ReflectedDescriptorBinding>& bindings)
{
    std::map<std::uint32_t, std::vector<ReflectedDescriptorBinding>> bindingsBySet;
    for (const ReflectedDescriptorBinding& binding : bindings) {
        bindingsBySet[binding.set].push_back(binding);
    }

    std::vector<DescriptorBindingSetGroup> result;
    result.reserve(bindingsBySet.size());
    for (auto& [set, groupBindings] : bindingsBySet) {
        result.push_back({ set, std::move(groupBindings) });
    }
    return result;
}

} // namespace gte
