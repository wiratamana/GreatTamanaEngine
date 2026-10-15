#pragma once

#include "RenderGraphHandles.h"

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gte::rg {

// Identity of one descriptor set: pass name, view scope, and the layout it was allocated against.
struct ComputeBindingKey {
    std::string name;
    ViewScope scope = ViewScope::Shared;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
};

// Allocation-free lookup form of ComputeBindingKey.
struct ComputeBindingKeyView {
    std::string_view name;
    ViewScope scope = ViewScope::Shared;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
};

namespace detail {

constexpr std::size_t HashBindingKey(std::string_view name, ViewScope scope, std::uintptr_t layout) noexcept
{
    std::uint64_t h = 1469598103934665603ull; // FNV-1a over name, scope, then layout bytes.
    for (const char c : name) {
        h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    }
    h = (h ^ static_cast<std::uint64_t>(scope)) * 1099511628211ull;
    for (int i = 0; i < 8; ++i) {
        h = (h ^ ((static_cast<std::uint64_t>(layout) >> (i * 8)) & 0xFFu)) * 1099511628211ull;
    }
    return static_cast<std::size_t>(h);
}

} // namespace detail

// Transparent hash/equality so lookups with a string_view never build a std::string.
struct ComputeBindingKeyHash {
    using is_transparent = void;

    std::size_t operator()(const ComputeBindingKey& k) const noexcept
    {
        return detail::HashBindingKey(k.name, k.scope, reinterpret_cast<std::uintptr_t>(k.layout));
    }
    std::size_t operator()(const ComputeBindingKeyView& k) const noexcept
    {
        return detail::HashBindingKey(k.name, k.scope, reinterpret_cast<std::uintptr_t>(k.layout));
    }
};

struct ComputeBindingKeyEqual {
    using is_transparent = void;

    template <typename A, typename B>
    bool operator()(const A& a, const B& b) const noexcept
    {
        return a.scope == b.scope && a.layout == b.layout && std::string_view(a.name) == std::string_view(b.name);
    }
};

} // namespace gte::rg
