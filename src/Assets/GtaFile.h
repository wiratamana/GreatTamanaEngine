#pragma once

#include "AssetTypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace gte {

// The 16-byte magic every *.gta (Great Tamana Asset) file starts with -
// "GREATTAMANAASSET" fits exactly, with no null terminator/padding needed.
inline constexpr char kGtaMagic[16] = { 'G', 'R', 'E', 'A', 'T', 'T', 'A', 'M', 'A', 'N', 'A', 'A', 'S', 'S', 'E', 'T' };

// The current on-disk *.gta format version this build writes (GtaHeader::
// version) - bump this, and add explicit handling in ReadGtaFile() for
// whatever changed, the moment the format's layout/meaning changes in a way
// that isn't backward compatible. Readers must not assume this is the ONLY
// version they'll ever see on disk.
inline constexpr std::uint64_t kGtaCurrentVersion = 1;

// The fixed 64-byte header every *.gta file begins with - see README.md's
// original spec for the exact byte layout this mirrors field-for-field:
//
//   16 bytes : Magic ("GREATTAMANAASSET")
//    8 bytes : Asset Type Enum (uint64_t)          -> assetType
//    8 bytes : Version (uint64_t)                  -> version
//   16 bytes : Asset ID / GUID (low, high)          -> guidLow, guidHigh
//    8 bytes : Flags (uint64_t)                     -> flags
//    8 bytes : Metadata / Payload Offset (uint64_t) -> payloadOffset
//
// `payloadOffset` is the byte offset (from the very start of the file, so
// always >= 64) where this asset's raw PAYLOAD bytes begin - everything
// between byte 64 and payloadOffset is this asset's METADATA section (see
// GtaFileData below), which is what actually gives this one field its
// dual "Metadata / Payload Offset" name: it simultaneously marks the end of
// metadata and the start of payload. An asset with no metadata at all is
// simply payloadOffset == 64.
//
// #pragma pack(push, 1) is used defensively even though every field here is
// already naturally 8-byte-aligned (the leading 16-byte char array is a
// multiple of 8) - this guarantees zero compiler-inserted padding on any
// toolchain, now or in the future, matching the fact that this struct is
// read/written as a raw byte blob, not through individual field
// serialization.
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

// A fully-decoded *.gta file: its header, plus the metadata/payload byte
// ranges that follow it (see GtaHeader::payloadOffset above). Metadata is
// left as an opaque byte blob on purpose - this engine's text-file/metadata
// serialization story is deliberately deferred (see README.md: "text file
// can stay still for now"); today's callers either leave it empty or stash
// whatever ad hoc bytes they want (e.g. a plain UTF-8 string), without this
// format needing to know or care what's actually in it yet.
struct GtaFileData {
    GtaHeader header;
    std::vector<std::uint8_t> metadata;
    std::vector<std::uint8_t> payload;
};

// Writes a complete *.gta file to `path`: the 64-byte common header (built
// from `type`/`guid`/`flags`/kGtaCurrentVersion), followed by `metadata`,
// followed by `payload`. Creates any missing parent directories first (same
// tolerant convention as ProjectPanelData's own filesystem helpers). Never
// throws; returns false (and leaves no partially-written file where
// avoidable) on any I/O failure.
bool WriteGtaFile(const std::filesystem::path& path, AssetType type, const Guid& guid, AssetFlags flags,
    const std::vector<std::uint8_t>& metadata, const std::vector<std::uint8_t>& payload,
    std::uint64_t version = kGtaCurrentVersion);

// Reads ONLY the fixed 64-byte header from `path` - deliberately cheap
// (never touches metadata/payload bytes) so a directory-wide scan (see
// AssetDatabase::RefreshFromDirectory()) can index thousands of *.gta files,
// including ones with huge texture/mesh payloads, without reading their
// bulk data at all. Returns std::nullopt if the file doesn't exist, is
// shorter than 64 bytes, or its magic doesn't match - never throws.
std::optional<GtaHeader> ReadGtaHeader(const std::filesystem::path& path);

// Reads a complete *.gta file (header + metadata + payload) from `path`.
// Returns std::nullopt if the file can't be opened, is malformed (bad
// magic, or a payloadOffset that isn't within [64, file size]) - never
// throws.
std::optional<GtaFileData> ReadGtaFile(const std::filesystem::path& path);

} // namespace gte
