#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace gte {

// "GTV1": a baked 2D or 3D texel block (little endian). A 32-byte header, then tightly packed texels,
// x fastest, then y, then z. depth == 1 is a 2D texture.
inline constexpr std::size_t kGtvHeaderBytes = 32;
inline constexpr std::uint32_t kGtvMaxAxis = 2048;
inline constexpr std::uint64_t kGtvMaxTexelBytes = 256ull * 1024 * 1024;

enum class GtvFormat : std::uint32_t {
    Rgba8Unorm = 0,
    R8Unorm = 1,
};

// Bit 0 of the header flags: the texels wrap on every axis.
inline constexpr std::uint32_t kGtvFlagTileable = 1u;

enum class GtvError : std::uint8_t {
    None,
    FileUnreadable,
    TooSmall,
    BadMagic,
    ZeroDimension,
    DimensionTooLarge,
    UnknownFormat,
    SizeTooLarge,
    TruncatedData,
    TrailingData,
};

struct GtvImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    GtvFormat format = GtvFormat::Rgba8Unorm;
    std::uint32_t flags = 0;
    std::vector<std::uint8_t> texels;

    bool IsVolume() const noexcept { return depth > 1; }
    bool Tileable() const noexcept { return (flags & kGtvFlagTileable) != 0; }
};

struct GtvParseResult {
    GtvError error = GtvError::None;
    GtvImage image;

    bool Ok() const noexcept { return error == GtvError::None; }
};

// Bytes per texel of `format`.
constexpr std::uint32_t GtvTexelBytes(GtvFormat format) noexcept
{
    return format == GtvFormat::R8Unorm ? 1u : 4u;
}

// Checks the header and the exact data size, then copies the texels.
GtvParseResult ParseGtv(std::span<const std::byte> bytes);

// Reads `path` and parses it. The header is checked before the texels are read.
GtvParseResult ReadGtvFile(const std::filesystem::path& path);

// Short text for logs.
const char* GtvErrorName(GtvError error) noexcept;

// Where engine-owned baked assets live, relative to the working directory: "assets/<file>".
std::string EngineAssetPath(const std::string& relativeFile);

} // namespace gte
