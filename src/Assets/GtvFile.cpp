#include "GtvFile.h"

#include <array>
#include <cstring>
#include <fstream>

namespace gte {

namespace {

constexpr std::array<char, 4> kMagic{ 'G', 'T', 'V', '1' };

struct GtvHeader {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    GtvFormat format = GtvFormat::Rgba8Unorm;
    std::uint32_t flags = 0;
    std::uint64_t texelBytes = 0;
};

std::uint32_t ReadU32(const std::byte* at) noexcept
{
    return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8)
        | (static_cast<std::uint32_t>(at[2]) << 16) | (static_cast<std::uint32_t>(at[3]) << 24);
}

// Validates the 32 header bytes; `header` is filled only when the result is None.
GtvError ParseHeader(std::span<const std::byte> bytes, GtvHeader& header) noexcept
{
    if (bytes.size() < kGtvHeaderBytes) {
        return GtvError::TooSmall;
    }
    if (std::memcmp(bytes.data(), kMagic.data(), kMagic.size()) != 0) {
        return GtvError::BadMagic;
    }
    const std::uint32_t width = ReadU32(bytes.data() + 4);
    const std::uint32_t height = ReadU32(bytes.data() + 8);
    const std::uint32_t depth = ReadU32(bytes.data() + 12);
    const std::uint32_t format = ReadU32(bytes.data() + 16);
    if (width == 0 || height == 0 || depth == 0) {
        return GtvError::ZeroDimension;
    }
    if (width > kGtvMaxAxis || height > kGtvMaxAxis || depth > kGtvMaxAxis) {
        return GtvError::DimensionTooLarge;
    }
    if (format > static_cast<std::uint32_t>(GtvFormat::R8Unorm)) {
        return GtvError::UnknownFormat;
    }
    const GtvFormat known = static_cast<GtvFormat>(format);
    const std::uint64_t texelBytes
        = std::uint64_t{ width } * height * depth * GtvTexelBytes(known);
    if (texelBytes > kGtvMaxTexelBytes) {
        return GtvError::SizeTooLarge;
    }
    header = GtvHeader{ width, height, depth, known, ReadU32(bytes.data() + 20), texelBytes };
    return GtvError::None;
}

} // namespace

GtvParseResult ParseGtv(std::span<const std::byte> bytes)
{
    GtvParseResult result;
    GtvHeader header;
    result.error = ParseHeader(bytes, header);
    if (!result.Ok()) {
        return result;
    }
    const std::uint64_t available = bytes.size() - kGtvHeaderBytes;
    if (available < header.texelBytes) {
        result.error = GtvError::TruncatedData;
        return result;
    }
    if (available > header.texelBytes) {
        result.error = GtvError::TrailingData;
        return result;
    }
    result.image.width = header.width;
    result.image.height = header.height;
    result.image.depth = header.depth;
    result.image.format = header.format;
    result.image.flags = header.flags;
    const std::byte* first = bytes.data() + kGtvHeaderBytes;
    result.image.texels.resize(static_cast<std::size_t>(header.texelBytes));
    std::memcpy(result.image.texels.data(), first, result.image.texels.size());
    return result;
}

GtvParseResult ReadGtvFile(const std::filesystem::path& path)
{
    GtvParseResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.error = GtvError::FileUnreadable;
        return result;
    }
    std::array<std::byte, kGtvHeaderBytes> head{};
    file.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    GtvHeader header;
    if (static_cast<std::size_t>(file.gcount()) < head.size()) {
        result.error = GtvError::TooSmall;
        return result;
    }
    result.error = ParseHeader(head, header);
    if (!result.Ok()) {
        return result;
    }

    // The whole file is read only after the header proved the size is sane.
    std::vector<std::byte> bytes(static_cast<std::size_t>(kGtvHeaderBytes + header.texelBytes + 1));
    std::memcpy(bytes.data(), head.data(), head.size());
    file.read(reinterpret_cast<char*>(bytes.data() + kGtvHeaderBytes),
        static_cast<std::streamsize>(header.texelBytes + 1));
    bytes.resize(kGtvHeaderBytes + static_cast<std::size_t>(file.gcount()));
    return ParseGtv(bytes);
}

const char* GtvErrorName(GtvError error) noexcept
{
    switch (error) {
    case GtvError::None: return "none";
    case GtvError::FileUnreadable: return "file is missing or unreadable";
    case GtvError::TooSmall: return "file is smaller than the 32-byte header";
    case GtvError::BadMagic: return "bad magic, expected GTV1";
    case GtvError::ZeroDimension: return "a dimension is zero";
    case GtvError::DimensionTooLarge: return "a dimension is larger than 2048";
    case GtvError::UnknownFormat: return "unknown texel format";
    case GtvError::SizeTooLarge: return "texel data is larger than 256 MiB";
    case GtvError::TruncatedData: return "texel data is shorter than the header says";
    case GtvError::TrailingData: return "texel data is longer than the header says";
    }
    return "unknown";
}

std::string EngineAssetPath(const std::string& relativeFile)
{
    return "assets/" + relativeFile;
}

} // namespace gte
