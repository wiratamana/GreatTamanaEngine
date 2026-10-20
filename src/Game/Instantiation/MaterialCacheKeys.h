#pragma once

#include "../../Assets/AssetTypes.h"
#include "../../Renderer/MaterialParams.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string_view>

namespace gte {

// One uploaded image: the same Guid is uploaded once per color encoding.
struct MaterialImageKey {
    Guid guid;
    bool srgb = false;

    friend bool operator==(const MaterialImageKey&, const MaterialImageKey&) noexcept = default;
};

struct MaterialImageKeyHash {
    std::size_t operator()(const MaterialImageKey& key) const noexcept
    {
        return std::hash<Guid>{}(key.guid) ^ (key.srgb ? 0x9e3779b9u : 0u);
    }
};

// One material: the four image Guids (role order) and the exact params bytes.
struct MaterialKey {
    std::array<Guid, 4> guids;
    MaterialParams params;

    friend bool operator==(const MaterialKey& a, const MaterialKey& b) noexcept
    {
        return a.guids == b.guids && std::memcmp(&a.params, &b.params, sizeof(MaterialParams)) == 0;
    }
};

// Hashes the same bytes operator== compares, so equal keys always hash equal.
struct MaterialKeyHash {
    std::size_t operator()(const MaterialKey& key) const noexcept
    {
        std::size_t hash = std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char*>(&key.params), sizeof(MaterialParams)));
        for (const Guid& guid : key.guids) {
            hash ^= std::hash<Guid>{}(guid) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

} // namespace gte
