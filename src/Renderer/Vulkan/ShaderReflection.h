#pragma once

// Pure SPIR-V reflection over compiled shader stages. No VkDevice needed.

#include <volk.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gte {

// Access a shader performs on a storage resource, from GLSL readonly/writeonly qualifiers.
enum class ReflectedAccess : std::uint8_t { ReadOnly, WriteOnly, ReadWrite };

struct ReflectedDescriptorBinding {
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    std::uint32_t count = 1;
    std::string name; // May be empty for an anonymous block.
    VkShaderStageFlags stages = 0; // Every stage that declares this slot.
    ReflectedAccess access = ReflectedAccess::ReadWrite;
};

struct ReflectedPushConstantRange {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct ReflectedLocalSize {
    std::uint32_t x = 1;
    std::uint32_t y = 1;
    std::uint32_t z = 1;
};

struct ShaderReflectionResult {
    std::vector<ReflectedDescriptorBinding> descriptorBindings;
    // At most one push-constant block per shader.
    bool hasPushConstantRange = false;
    ReflectedPushConstantRange pushConstantRange;
    VkShaderStageFlags pushConstantStages = 0;
    ReflectedLocalSize localSize; // Compute stage only.
};

struct ShaderStageFile {
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_COMPUTE_BIT;
    std::string spirvPath;
};

// Reflects and merges every stage (compute, or vertex + fragment).
// Throws std::runtime_error on an unreadable or invalid file, a stage that does not match the
// file, an unsupported descriptor type, or stages that disagree on a slot or on the push block.
ShaderReflectionResult ReflectShader(std::span<const ShaderStageFile> stages);

// Merges one stage's bindings into `merged`: same (set, binding) becomes one entry with the
// stages ORed. Type, name or count disagreement throws.
void MergeStageBindings(
    std::vector<ReflectedDescriptorBinding>& merged, std::span<const ReflectedDescriptorBinding> stageBindings);

// Merges one stage's push block into `result`. A different size or offset throws.
void MergePushConstantRange(
    ShaderReflectionResult& result, const ReflectedPushConstantRange& range, VkShaderStageFlags stages);

// Throws unless every binding has a name and no name is used by two slots.
// Required by name-based binding; layout creation itself does not need names.
void ValidateBindingNames(std::span<const ReflectedDescriptorBinding> bindings, const std::string& shaderLabel);

// One `set` number's bindings, in the order they were reflected.
struct DescriptorBindingSetGroup {
    std::uint32_t set = 0;
    std::vector<ReflectedDescriptorBinding> bindings;
};

// Groups bindings by set, ascending by set number. Order inside a set is preserved.
std::vector<DescriptorBindingSetGroup> GroupDescriptorBindingsBySet(
    const std::vector<ReflectedDescriptorBinding>& bindings);

} // namespace gte
