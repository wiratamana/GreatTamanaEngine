#pragma once

#include "../GraphicsHandleRegistry.h"
#include "ShaderReflection.h"

#include <volk.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gte {

// Owns one VkDescriptorSetLayout per reflected set, plus their engine registry handles.
// Handles are unregistered before their layouts are destroyed.
class ReflectedSetLayouts {
public:
    ReflectedSetLayouts() = default;

    // Builds a layout per set, ascending. Throws on an unsupported descriptor type; nothing leaks.
    ReflectedSetLayouts(VkDevice device, const std::vector<ReflectedDescriptorBinding>& bindings, const std::string& label);
    ~ReflectedSetLayouts();

    ReflectedSetLayouts(const ReflectedSetLayouts&) = delete;
    ReflectedSetLayouts& operator=(const ReflectedSetLayouts&) = delete;
    ReflectedSetLayouts(ReflectedSetLayouts&& other) noexcept;
    ReflectedSetLayouts& operator=(ReflectedSetLayouts&& other) noexcept;

    // Null / Invalid when `set` was not reflected.
    VkDescriptorSetLayout Native(std::uint32_t set) const noexcept;
    DescriptorSetLayoutHandle Handle(std::uint32_t set) const noexcept;

    // Every layout in ascending set order, ready for a pipeline layout.
    std::span<const VkDescriptorSetLayout> All() const noexcept { return m_layouts; }

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    std::vector<std::uint32_t> m_sets;
    std::vector<VkDescriptorSetLayout> m_layouts;
    mutable std::vector<DescriptorSetLayoutHandle> m_handles; // Registered on first Handle() call.
};

} // namespace gte
