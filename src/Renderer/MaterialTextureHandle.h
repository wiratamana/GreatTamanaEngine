#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace gte {

// Generational id of a MaterialTexture in a ResourcePool. Standalone (no Vulkan) so ECS components can name it.
struct MaterialTextureHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0; // 0 == never assigned / invalid.

    bool IsValid() const noexcept { return generation != 0; }

    friend bool operator==(const MaterialTextureHandle& a, const MaterialTextureHandle& b) noexcept
    {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const MaterialTextureHandle& a, const MaterialTextureHandle& b) noexcept
    {
        return !(a == b);
    }
};

inline constexpr MaterialTextureHandle kInvalidMaterialTextureHandle{};

} // namespace gte

template <> struct std::hash<gte::MaterialTextureHandle> {
    std::size_t operator()(const gte::MaterialTextureHandle& handle) const noexcept
    {
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(handle.index) << 32) | handle.generation);
    }
};
