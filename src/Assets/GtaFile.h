#pragma once

#include "AssetTypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// Every *.gta file starts with this 16-byte magic.
inline constexpr char kGtaMagic[16] = { 'G', 'R', 'E', 'A', 'T', 'T', 'A', 'M', 'A', 'N', 'A', 'A', 'S', 'S', 'E', 'T' };

// On-disk format version this build writes (GtaHeader::version).
inline constexpr std::uint64_t kGtaCurrentVersion = 1;

// Fixed 64-byte header at the start of every *.gta file, read and written as one blob.
// payloadOffset is the byte offset from the file start where the payload begins; the bytes
// between 64 and payloadOffset are the metadata section.
#pragma pack(push, 1)
struct GtaHeader {
    char magic[16] = { 0 };
    std::uint64_t assetType = 0;
    std::uint64_t version = 0;
    std::uint64_t guidLow = 0;
    std::uint64_t guidHigh = 0;
    std::uint64_t flags = 0;
    std::uint64_t payloadOffset = 0;

    bool IsMagicValid() const noexcept
    {
        for (int i = 0; i < 16; ++i) {
            if (magic[i] != kGtaMagic[i]) {
                return false;
            }
        }
        return true;
    }

    AssetType Type() const noexcept { return static_cast<AssetType>(assetType); }
    AssetFlags Flags() const noexcept { return static_cast<AssetFlags>(flags); }
    Guid Id() const noexcept { return Guid{ guidLow, guidHigh }; }
};
#pragma pack(pop)

static_assert(sizeof(GtaHeader) == 64, "GtaHeader must be exactly the documented 64-byte common header");

// A decoded *.gta file. Metadata is an opaque byte blob.
struct GtaFileData {
    GtaHeader header;
    std::vector<std::uint8_t> metadata;
    std::vector<std::uint8_t> payload;
};

// Header and metadata of a *.gta file, without the payload.
struct GtaMetadataData {
    GtaHeader header;
    std::vector<std::uint8_t> metadata;
};

// Writes header + metadata + payload atomically (temp file, then rename), creating missing
// parent directories. With `createOnly`, an existing file is never replaced.
// Never throws. On failure the old file is untouched and `error` gets the reason.
bool WriteGtaFile(const std::filesystem::path& path, AssetType type, const Guid& guid, AssetFlags flags,
    const std::vector<std::uint8_t>& metadata, const std::vector<std::uint8_t>& payload,
    std::uint64_t version = kGtaCurrentVersion, std::string* error = nullptr, bool createOnly = false);

// Reads only the 64-byte header (cheap enough for directory scans). Nullopt when the file is
// missing, shorter than 64 bytes or has a bad magic. Never throws.
std::optional<GtaHeader> ReadGtaHeader(const std::filesystem::path& path);

// Reads header and metadata, never the payload. Nullopt for the same cases as ReadGtaFile().
std::optional<GtaMetadataData> ReadGtaMetadataOnly(const std::filesystem::path& path);

// Reads the whole file. Nullopt when it cannot be opened or is malformed
// (bad magic, payloadOffset outside [64, file size]). Never throws.
std::optional<GtaFileData> ReadGtaFile(const std::filesystem::path& path);

} // namespace gte
