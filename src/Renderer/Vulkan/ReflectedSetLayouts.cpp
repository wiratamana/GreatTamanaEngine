#include "ReflectedSetLayouts.h"

#include "DescriptorSetLayoutBuilder.h"

#include <stdexcept>
#include <utility>

namespace gte {

ReflectedSetLayouts::ReflectedSetLayouts(
    VkDevice device, const std::vector<ReflectedDescriptorBinding>& bindings, const std::string& label)
    : m_device(device)
{
    const std::vector<DescriptorBindingSetGroup> groups = GroupDescriptorBindingsBySet(bindings);
    m_sets.reserve(groups.size());
    m_layouts.reserve(groups.size());
    m_handles.assign(groups.size(), DescriptorSetLayoutHandle::Invalid);

    try {
        for (const DescriptorBindingSetGroup& group : groups) {
            DescriptorSetLayoutBuilder builder(device);
            for (const ReflectedDescriptorBinding& binding : group.bindings) {
                switch (binding.type) {
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    builder.AddStorageBuffer(binding.binding, binding.stages, binding.count);
                    break;
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    builder.AddUniformBuffer(binding.binding, binding.stages, binding.count);
                    break;
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    builder.AddStorageImage(binding.binding, binding.stages, binding.count);
                    break;
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    builder.AddCombinedImageSampler(binding.binding, binding.stages, binding.count);
                    break;
                default:
                    throw std::runtime_error("reflected an unsupported descriptor type in '" + label + "'");
                }
            }
            m_layouts.push_back(builder.Build());
            m_sets.push_back(group.set);
        }
    } catch (...) {
        Destroy();
        throw;
    }
}

ReflectedSetLayouts::~ReflectedSetLayouts()
{
    Destroy();
}

ReflectedSetLayouts::ReflectedSetLayouts(ReflectedSetLayouts&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_sets(std::move(other.m_sets))
    , m_layouts(std::move(other.m_layouts))
    , m_handles(std::move(other.m_handles))
{
    other.m_sets.clear();
    other.m_layouts.clear();
    other.m_handles.clear();
}

ReflectedSetLayouts& ReflectedSetLayouts::operator=(ReflectedSetLayouts&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_sets = std::move(other.m_sets);
        m_layouts = std::move(other.m_layouts);
        m_handles = std::move(other.m_handles);
        other.m_sets.clear();
        other.m_layouts.clear();
        other.m_handles.clear();
    }
    return *this;
}

VkDescriptorSetLayout ReflectedSetLayouts::Native(std::uint32_t set) const noexcept
{
    for (std::size_t i = 0; i < m_sets.size(); ++i) {
        if (m_sets[i] == set && i < m_layouts.size()) {
            return m_layouts[i];
        }
    }
    return VK_NULL_HANDLE;
}

DescriptorSetLayoutHandle ReflectedSetLayouts::Handle(std::uint32_t set) const noexcept
{
    for (std::size_t i = 0; i < m_sets.size(); ++i) {
        if (m_sets[i] != set || i >= m_layouts.size()) {
            continue;
        }
        if (m_handles[i] == DescriptorSetLayoutHandle::Invalid) {
            m_handles[i] = RegisterDescriptorSetLayout(m_layouts[i]);
        }
        return m_handles[i];
    }
    return DescriptorSetLayoutHandle::Invalid;
}

void ReflectedSetLayouts::Destroy() noexcept
{
    // Registry entries must die before the layouts they point at do.
    for (const DescriptorSetLayoutHandle handle : m_handles) {
        if (handle != DescriptorSetLayoutHandle::Invalid) {
            UnregisterDescriptorSetLayout(handle);
        }
    }
    m_handles.clear();
    for (const VkDescriptorSetLayout layout : m_layouts) {
        if (layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, layout, nullptr);
        }
    }
    m_layouts.clear();
    m_sets.clear();
}

} // namespace gte
