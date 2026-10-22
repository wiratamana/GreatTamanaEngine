#include "GtaFile.h"

#include "AtomicFile.h"

#include <array>
#include <cstring>
#include <fstream>
#include <span>

namespace gte {

namespace {

// Reads and validates the header, then the metadata section. Returns the file size, or nullopt on any problem.
std::optional<std::uint64_t> ReadHeaderAndMetadata(
    std::ifstream& in, GtaHeader& header, std::vector<std::uint8_t>& metadata)
{
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || in.gcount() != static_cast<std::streamsize>(sizeof(header)) || !header.IsMagicValid()) {
        return std::nullopt;
    }

    in.seekg(0, std::ios::end);
    const std::streamoff fileSize = in.tellg();
    if (fileSize < 0) {
        return std::nullopt;
    }

    const std::uint64_t payloadOffset = header.payloadOffset;
    if (payloadOffset < sizeof(GtaHeader) || payloadOffset > static_cast<std::uint64_t>(fileSize)) {
        return std::nullopt; // Offset points outside the file.
    }

    const std::uint64_t metadataSize = payloadOffset - sizeof(GtaHeader);
    metadata.resize(static_cast<std::size_t>(metadataSize));
    if (metadataSize > 0) {
        in.seekg(static_cast<std::streamoff>(sizeof(GtaHeader)), std::ios::beg);
        in.read(reinterpret_cast<char*>(metadata.data()), static_cast<std::streamsize>(metadataSize));
        if (!in || in.gcount() != static_cast<std::streamsize>(metadataSize)) {
            return std::nullopt;
        }
    }
    return static_cast<std::uint64_t>(fileSize);
}

} // namespace

bool WriteGtaFile(const std::filesystem::path& path, AssetType type, const Guid& guid, AssetFlags flags,
    const std::vector<std::uint8_t>& metadata, const std::vector<std::uint8_t>& payload, std::uint64_t version,
    std::string* error)
{
    std::error_code dirEc;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, dirEc); // Best-effort; the atomic write is the real check.
    }

    GtaHeader header;
    std::memcpy(header.magic, kGtaMagic, sizeof(header.magic));
    header.assetType = static_cast<std::uint64_t>(type);
    header.version = version;
    header.guidLow = guid.low;
    header.guidHigh = guid.high;
    header.flags = static_cast<std::uint64_t>(flags);
    header.payloadOffset = sizeof(GtaHeader) + metadata.size();

    const std::array<ConstByteSpan, 3> parts = {
        std::as_bytes(std::span<const GtaHeader, 1>(&header, 1)),
        std::as_bytes(std::span<const std::uint8_t>(metadata)),
        std::as_bytes(std::span<const std::uint8_t>(payload)),
    };
    return WriteFileAtomically(path, parts, nullptr, error);
}

std::optional<GtaHeader> ReadGtaHeader(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }

    GtaHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || in.gcount() != static_cast<std::streamsize>(sizeof(header)) || !header.IsMagicValid()) {
        return std::nullopt;
    }
    return header;
}

std::optional<GtaMetadataData> ReadGtaMetadataOnly(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }

    GtaMetadataData data;
    if (!ReadHeaderAndMetadata(in, data.header, data.metadata).has_value()) {
        return std::nullopt;
    }
    return data;
}

std::optional<GtaFileData> ReadGtaFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }

    GtaFileData data;
    const std::optional<std::uint64_t> fileSize = ReadHeaderAndMetadata(in, data.header, data.metadata);
    if (!fileSize.has_value()) {
        return std::nullopt;
    }

    const std::uint64_t payloadOffset = data.header.payloadOffset;
    const std::uint64_t payloadSize = *fileSize - payloadOffset;
    data.payload.resize(static_cast<std::size_t>(payloadSize));
    if (payloadSize > 0) {
        in.seekg(static_cast<std::streamoff>(payloadOffset), std::ios::beg);
        in.read(reinterpret_cast<char*>(data.payload.data()), static_cast<std::streamsize>(payloadSize));
        if (!in || in.gcount() != static_cast<std::streamsize>(payloadSize)) {
            return std::nullopt;
        }
    }
    return data;
}

} // namespace gte
