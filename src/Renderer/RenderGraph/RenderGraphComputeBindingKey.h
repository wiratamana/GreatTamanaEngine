#pragma once

#include "RenderGraphHandles.h"

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gte::rg {

// Identity of one descriptor set: pass name, view scope, layout and an instance number (0 unless a pass repeats).
struct ComputeBindingKey {
    std::string name;
    ViewScope scope = ViewScope::Shared;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::uint64_t instance = 0;
};

// Allocation-free lookup form of ComputeBindingKey.
struct ComputeBindingKeyView {
    std::string_view name;
    ViewScope scope = ViewScope::Shared;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::uint64_t instance = 0;
};

namespace detail {

constexpr std::size_t HashBindingKey(
    std::string_view name, ViewScope scope, std::uintptr_t layout, std::uint64_t instance) noexcept
{
    std::uint64_t h = 1469598103934665603ull; // FNV-1a over name, scope, layout bytes, then instance bytes.
    for (const char c : name) {
        h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    }
    h = (h ^ static_cast<std::uint64_t>(scope)) * 1099511628211ull;
    for (int i = 0; i < 8; ++i) {
        h = (h ^ ((static_cast<std::uint64_t>(layout) >> (i * 8)) & 0xFFu)) * 1099511628211ull;
    }
    for (int i = 0; i < 8; ++i) {
        h = (h ^ ((instance >> (i * 8)) & 0xFFu)) * 1099511628211ull;
    }
    return static_cast<std::size_t>(h);
}

} // namespace detail

// Transparent hash/equality so lookups with a string_view never build a std::string.
struct ComputeBindingKeyHash {
    using is_transparent = void;

    std::size_t operator()(const ComputeBindingKey& k) const noexcept
    {
        return detail::HashBindingKey(k.name, k.scope, reinterpret_cast<std::uintptr_t>(k.layout), k.instance);
    }
    std::size_t operator()(const ComputeBindingKeyView& k) const noexcept
    {
        return detail::HashBindingKey(k.name, k.scope, reinterpret_cast<std::uintptr_t>(k.layout), k.instance);
    }
};

struct ComputeBindingKeyEqual {
    using is_transparent = void;

    template <typename A, typename B>
    bool operator()(const A& a, const B& b) const noexcept
    {
        return a.scope == b.scope && a.layout == b.layout && a.instance == b.instance
            && std::string_view(a.name) == std::string_view(b.name);
    }
};

} // namespace gte::rg
