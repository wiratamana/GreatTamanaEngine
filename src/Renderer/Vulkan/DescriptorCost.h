#pragma once

#include "DescriptorPoolGrowth.h"
#include "ShaderReflection.h"

#include <volk.h>

#include <cstdint>
#include <span>

namespace gte {

// Adds `count` descriptors of `type` to `cost`. Types the pool set does not serve are ignored.
inline void AddDescriptorCost(PoolCapacity& cost, VkDescriptorType type, std::uint32_t count) noexcept
{
    switch (type) {
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        cost.storageBuffers += count;
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        cost.storageImages += count;
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        cost.combinedSamplers += count;
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        cost.uniformBuffers += count;
        break;
    default:
        break;
    }
}

// Pool cost of one descriptor set holding exactly these bindings (one set plus their descriptors).
inline PoolCapacity DescriptorCostOf(std::span<const ReflectedDescriptorBinding> bindings) noexcept
{
    PoolCapacity cost;
    cost.sets = 1;
    for (const ReflectedDescriptorBinding& binding : bindings) {
        AddDescriptorCost(cost, binding.type, binding.count);
    }
    return cost;
}

} // namespace gte
