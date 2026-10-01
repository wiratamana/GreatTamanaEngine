#pragma once

// task_manager/better-render-pass-1 campaign, PHASE1
// (PHASE1_SPIRV_REFLECT_DEPENDENCY_AND_SHADER_REFLECTION_CORE.md) - pure,
// Tier-1-testable SPIR-V reflection: reads an already-compiled .comp.spv
// file (via the vendored SPIRV-Reflect library, third_party/spirv_reflect/)
// and returns its descriptor bindings / push-constant range / declared
// compute local work-group size.
//
// Deliberately lives under Vulkan/ (mirrors DescriptorSetLayoutBuilder.h's
// own location) and deliberately does NOT depend on ComputePipeline.h/
// GpuResourceFactory.h - wiring reflection INTO those is PHASE2's own job.
// No live VkDevice is needed anywhere in this file - pure binary parsing
// only, operating entirely on an already-compiled SPIR-V byte blob read off
// disk.

#include <volk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

struct ReflectedDescriptorBinding {
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    std::uint32_t count = 1;
    std::string name;
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
    // A shader may declare zero or one push-constant block (this engine's
    // own established one-layout(push_constant)-block-per-shader
    // convention - see ReflectComputeShader()'s own doc comment below for
    // the "more than one" rejection case). Emptiness is expressed via this
    // plain bool rather than std::optional<ReflectedPushConstantRange> -
    // see PHASE1_COMPLETION_REPORT.md for why this was the chosen style.
    bool hasPushConstantRange = false;
    ReflectedPushConstantRange pushConstantRange;
    ReflectedLocalSize localSize;
};

// Reads the compiled SPIR-V binary at `shaderSpirvPath` and returns its
// reflected descriptor bindings / push-constant range / declared compute
// local work-group size. Throws std::runtime_error if the file cannot be
// read, is not valid SPIR-V, declares more than one push-constant block, or
// declares a descriptor type this engine's own DescriptorSetLayoutBuilder/
// ComputeDescriptorSet cannot express (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER /
// STORAGE_IMAGE / COMBINED_IMAGE_SAMPLER only - see ComputeDescriptorSet.h's
// own three static factories).
ShaderReflectionResult ReflectComputeShader(const std::string& shaderSpirvPath);

// task_manager/better-render-pass-1 campaign, PHASE2
// (PHASE2_REFLECTION_BASED_COMPUTE_PIPELINE_CREATION.md) - one `set`
// number's worth of reflected descriptor bindings, produced by
// GroupDescriptorBindingsBySet() below so ComputePipeline's new
// reflection-driven constructor path can build one VkDescriptorSetLayout
// per distinct `set` a shader declares. Every real compute shader in this
// engine today uses set = 0 exclusively - this is not hardcoded as an
// assumption anywhere in the grouping logic itself, though
// ComputePipeline's own consumption of this result is only proven correct
// for the contiguous-from-zero case real shaders use (see that class's own
// comment).
struct DescriptorBindingSetGroup {
    std::uint32_t set = 0;
    std::vector<ReflectedDescriptorBinding> bindings;
};

// Groups `bindings` by their own `.set` field, returning one
// DescriptorBindingSetGroup per distinct set value present, in ASCENDING
// set-number order - each group's own `bindings` preserves the original
// relative order of the bindings that share that set (never reordered by
// binding number). An empty `bindings` input returns an empty result. Pure
// logic, Tier-1-testable - no VkDevice involved anywhere.
std::vector<DescriptorBindingSetGroup> GroupDescriptorBindingsBySet(const std::vector<ReflectedDescriptorBinding>& bindings);

} // namespace gte
