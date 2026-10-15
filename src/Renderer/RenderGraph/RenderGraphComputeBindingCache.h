#pragma once

#include "../Compute/ComputeDescriptorSet.h"
#include "../GraphicsHandleRegistry.h"
#include "BoundedRetryGates.h"
#include "RenderGraphComputeBindingKey.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {

// Owns the descriptor sets of passes that declare Bind*() resources.
// One set per (pass name, ViewScope, layout): per-view passes never share a set, which would be a data race.
class RenderGraphComputeBindingCache {
public:
    explicit RenderGraphComputeBindingCache(Renderer& renderer) noexcept;

    // Sets are freed individually only by BeginFrame()/Release(); the factory destroys its pools in bulk.
    ~RenderGraphComputeBindingCache() = default;

    RenderGraphComputeBindingCache(const RenderGraphComputeBindingCache&) = delete;
    RenderGraphComputeBindingCache& operator=(const RenderGraphComputeBindingCache&) = delete;

    // Returns the set for this identity, rewritten with `writes`. Never throws:
    // allocation failure is logged once and yields DescriptorSetHandle::Invalid.
    DescriptorSetHandle Resolve(
        const char* passName, ViewScope viewScope, VkDescriptorSetLayout layout, const std::vector<ComputeDescriptorWrite>& writes);

    // Frees sets unused for more than `staleFrames`. The age guard keeps in-flight command buffers safe.
    void BeginFrame(std::uint64_t currentFrame, std::uint64_t staleFrames = kDefaultStaleFrames);

    // Frees every set allocated against `layout`. A destroyed shader queues its release; the queue drains once
    // per frame in BeginPersistentResourceFrame, never inside Execute (frames in flight may still use the sets).
    void Release(VkDescriptorSetLayout layout);

    static constexpr std::uint64_t kDefaultStaleFrames = 600;

private:
    struct Entry {
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        DescriptorSetHandle handle = DescriptorSetHandle::Invalid;
        std::uint64_t lastUsedFrame = 0;
    };

    // Identities whose allocation failed back off per key; a failing key is never retried every frame.
    static constexpr std::size_t kMaxFailingKeys = 64;
    using FailingKeys = BoundedRetryGates<ComputeBindingKey, ComputeBindingKeyHash, ComputeBindingKeyEqual, kMaxFailingKeys>;

    void FreeEntry(const Entry& entry) noexcept;

    Renderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    std::uint64_t m_currentFrame = 0;
    std::unordered_map<ComputeBindingKey, Entry, ComputeBindingKeyHash, ComputeBindingKeyEqual> m_entries;
    FailingKeys m_failingKeys;
};

} // namespace gte::rg
