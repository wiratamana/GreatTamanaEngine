#pragma once

#include "../Compute/ComputeDescriptorSet.h"
#include "../GraphicsHandleRegistry.h"
#include "BoundedOnceSet.h"
#include "RenderGraphComputeBindingKey.h"

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

    // Frees every set allocated against `layout`. The caller guarantees the GPU is done with them.
    void Release(VkDescriptorSetLayout layout);

    std::size_t EntryCount() const noexcept { return m_entries.size(); }

    static constexpr std::uint64_t kDefaultStaleFrames = 600;

private:
    struct Entry {
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        DescriptorSetHandle handle = DescriptorSetHandle::Invalid;
        std::uint64_t lastUsedFrame = 0;
    };

    void FreeEntry(const Entry& entry) noexcept;

    Renderer* m_renderer = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    std::uint64_t m_currentFrame = 0;
    std::unordered_map<ComputeBindingKey, Entry, ComputeBindingKeyHash, ComputeBindingKeyEqual> m_entries;
    BoundedOnceSet<64> m_reportedFailures;
};

} // namespace gte::rg
