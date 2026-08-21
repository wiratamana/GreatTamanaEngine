#include "AssetTypes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <random>

namespace gte {

Guid Guid::Generate()
{
    // thread_local so concurrent callers (should this ever be used from
    // more than one thread in the future) never share/race a single engine
    // instance - each thread gets its own, seeded independently from
    // std::random_device.
    thread_local std::mt19937_64 engine{ std::random_device{}() };
    thread_local std::uniform_int_distribution<std::uint64_t> dist;

    Guid guid;
    do {
        guid.low = dist(engine);
        guid.high = dist(engine);
    } while (!guid.IsValid()); // Astronomically unlikely, but never hand back the reserved all-zero value.
    return guid;
}

std::string Guid::ToString() const
{
    char buffer[33];
    std::snprintf(buffer, sizeof(buffer), "%016llx%016llx", static_cast<unsigned long long>(high),
        static_cast<unsigned long long>(low));
    return std::string(buffer, 32);
}

namespace {

bool IsHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::uint64_t ParseHex16(const std::string& text, std::size_t offset)
{
    return static_cast<std::uint64_t>(std::strtoull(text.substr(offset, 16).c_str(), nullptr, 16));
}

} // namespace

Guid Guid::Parse(const std::string& text)
{
    if (text.size() != 32) {
        return Guid::Invalid();
    }
    if (!std::all_of(text.begin(), text.end(), IsHexDigit)) {
        return Guid::Invalid();
    }

    Guid guid;
    guid.high = ParseHex16(text, 0);
    guid.low = ParseHex16(text, 16);
    return guid;
}

AssetType AssetTypeFromExtension(const std::string& extensionLowercaseWithDot)
{
    static const std::array<std::pair<const char*, AssetType>, 21> kMap = { {
        // Texture - matches AssetInspectorData.h's IsSupportedImageExtension() list.
        { ".png", AssetType::Texture },
        { ".jpg", AssetType::Texture },
        { ".jpeg", AssetType::Texture },
        { ".bmp", AssetType::Texture },
        { ".tga", AssetType::Texture },
        { ".gif", AssetType::Texture },
        { ".psd", AssetType::Texture },
        { ".hdr", AssetType::Texture },
        { ".pic", AssetType::Texture },
        { ".pnm", AssetType::Texture },
        { ".ppm", AssetType::Texture },
        { ".pgm", AssetType::Texture },
        { ".ktx2", AssetType::Texture },
        // Mesh.
        { ".obj", AssetType::Mesh },
        { ".gltf", AssetType::Mesh },
        { ".glb", AssetType::Mesh },
        { ".fbx", AssetType::Mesh },
        // Audio.
        { ".wav", AssetType::Audio },
        { ".mp3", AssetType::Audio },
        { ".ogg", AssetType::Audio },
        // Shader source.
        { ".glsl", AssetType::Shader },
    } };

    for (const auto& [ext, type] : kMap) {
        if (extensionLowercaseWithDot == ext) {
            return type;
        }
    }
    return AssetType::Unknown;
}

} // namespace gte
