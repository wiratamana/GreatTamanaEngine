#include "ClusterDrawBindings.h"

#include "RenderGraph.h"
#include "../DescriptorSet.h"
#include "../Renderer.h"

#include <exception>
#include <utility>

namespace gte::rg {

ClusterDrawBindings::ClusterDrawBindings(Renderer& renderer) noexcept
    : m_renderer(&renderer)
{
}

ClusterDrawBindings::~ClusterDrawBindings()
{
    ReleaseAll();
}

ClusterDrawBindings::ClusterDrawBindings(ClusterDrawBindings&& other) noexcept
    : m_renderer(std::exchange(other.m_renderer, nullptr))
    , m_sets(std::move(other.m_sets))
{
    other.m_sets.clear();
}

ClusterDrawBindings& ClusterDrawBindings::operator=(ClusterDrawBindings&& other) noexcept
{
    if (this != &other) {
        ReleaseAll();
        m_renderer = std::exchange(other.m_renderer, nullptr);
        m_sets = std::move(other.m_sets);
        other.m_sets.clear();
    }
    return *this;
}

void ClusterDrawBindings::ReleaseAll() noexcept
{
    for (const DescriptorSetHandle handle : m_sets) {
        if (m_renderer != nullptr) {
            m_renderer->FreeDescriptorSet(Native(handle));
        }
        UnregisterDescriptorSet(handle);
    }
    m_sets.clear();
}

bool ClusterDrawBindings::Prepare(std::size_t setCount)
{
    if (m_renderer == nullptr) {
        return false;
    }
    const std::size_t existing = m_sets.size();
    if (setCount <= existing) {
        return true;
    }
    try {
        m_sets.reserve(setCount);
        while (m_sets.size() < setCount) {
            const DescriptorSet set = m_renderer->AllocateDescriptorSet(m_renderer->BufferSetLayout());
            m_sets.push_back(RegisterDescriptorSet(set.Native()));
        }
    } catch (const std::exception&) {
        while (m_sets.size() > existing) {
            m_renderer->FreeDescriptorSet(Native(m_sets.back()));
            UnregisterDescriptorSet(m_sets.back());
            m_sets.pop_back();
        }
        return false;
    }
    return true;
}

bool ClusterDrawBindings::Rewrite(PassContext& ctx, std::size_t index, BufferHandle visible, BufferHandle instances)
{
    if (m_renderer == nullptr || index >= m_sets.size()) {
        return false;
    }
    const VkBuffer visibleBuffer = ctx.resolveBuffer(visible);
    const VkBuffer instanceBuffer = ctx.resolveBuffer(instances);
    if (visibleBuffer == VK_NULL_HANDLE || instanceBuffer == VK_NULL_HANDLE) {
        return false;
    }
    DescriptorSet(Native(m_sets[index]), m_renderer->GetVulkanContextInfo().device)
        .Rewrite({ DescriptorWrite::StorageBuffer(0, visibleBuffer), DescriptorWrite::StorageBuffer(1, instanceBuffer) });
    return true;
}

DescriptorSetHandle ClusterDrawBindings::Set(std::size_t index) const noexcept
{
    return index < m_sets.size() ? m_sets[index] : DescriptorSetHandle::Invalid;
}

} // namespace gte::rg
