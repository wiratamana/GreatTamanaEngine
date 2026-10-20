#pragma once

#include "Vulkan/DescriptorPoolGrowth.h"

#include "../Core/GraphicsHandles.h"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace gte {

// Fixed-capacity pool of material descriptor sets (one layout) with per-set free. Thread-safe.
class MaterialSetPool {
public:
    // Samplers and uniform buffers one material set consumes; must match the material set layout.
    static constexpr std::uint32_t kSamplersPerSet = 4;
    static constexpr std::uint32_t kUniformBuffersPerSet = 1;

    // Throws std::runtime_error when the pool cannot be created.
    MaterialSetPool(VkDevice device, VkDescriptorSetLayout layout, std::uint32_t capacity);
    ~MaterialSetPool(); // Destroys the pool and every set still in it.

    MaterialSetPool(const MaterialSetPool&) = delete;
    MaterialSetPool& operator=(const MaterialSetPool&) = delete;

    // Throws std::runtime_error naming the capacity when the pool is full.
    VkDescriptorSet Allocate();

    // Returns a set to the pool. A null or unknown set is ignored. The GPU must be done with it.
    void Free(VkDescriptorSet set) noexcept;

    std::uint32_t InUse() const;
    std::uint32_t Capacity() const noexcept { return m_capacity; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::uint32_t m_capacity = 0;

    mutable std::mutex m_mutex;
    DescriptorBudget m_budget;
    std::unordered_set<VkDescriptorSet> m_live;
};

// Move-only owner of one material set: registers the engine handle, frees the set on destruction.
class MaterialSetLease {
public:
    MaterialSetLease() = default;
    MaterialSetLease(std::shared_ptr<MaterialSetPool> pool, VkDescriptorSet set) noexcept;
    ~MaterialSetLease();

    MaterialSetLease(const MaterialSetLease&) = delete;
    MaterialSetLease& operator=(const MaterialSetLease&) = delete;
    MaterialSetLease(MaterialSetLease&& other) noexcept;
    MaterialSetLease& operator=(MaterialSetLease&& other) noexcept;

    DescriptorSetHandle Handle() const noexcept { return m_handle; }

private:
    void Reset() noexcept;

    std::shared_ptr<MaterialSetPool> m_pool; // Keeps the pool alive until the set is freed.
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    DescriptorSetHandle m_handle = DescriptorSetHandle::Invalid;
};

} // namespace gte
