#include "GtaFile.h"

#include <cstring>
#include <fstream>

namespace gte {

bool WriteGtaFile(const std::filesystem::path& path, AssetType type, const Guid& guid, AssetFlags flags,
    const std::vector<std::uint8_t>& metadata, const std::vector<std::uint8_t>& payload, std::uint64_t version)
{
    std::error_code dirEc;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, dirEc); // Best-effort; the ofstream open below is the real check.
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    GtaHeader header;
    std::memcpy(header.magic, kGtaMagic, sizeof(header.magic));
    header.assetType = static_cast<std::uint64_t>(type);
    header.version = version;
    header.guidLow = guid.low;
    header.guidHigh = guid.high;
    header.flags = static_cast<std::uint64_t>(flags);
    header.payloadOffset = sizeof(GtaHeader) + metadata.size();

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!metadata.empty()) {
        out.write(reinterpret_cast<const char*>(metadata.data()), static_cast<std::streamsize>(metadata.size()));
    }
    if (!payload.empty()) {
        out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    }

    return out.good();
}

std::optional<GtaHeader> ReadGtaHeader(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }

    GtaHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || in.gcount() != static_cast<std::streamsize>(sizeof(header))) {
        return std::nullopt; // Shorter than the fixed 64-byte common header.
    }
    if (!header.IsMagicValid()) {
        return std::nullopt;
    }

    return header;
}

std::optional<GtaFileData> ReadGtaFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }

    GtaFileData data;
    in.read(reinterpret_cast<char*>(&data.header), sizeof(data.header));
    if (!in || in.gcount() != static_cast<std::streamsize>(sizeof(data.header))) {
        return std::nullopt;
    }
    if (!data.header.IsMagicValid()) {
        return std::nullopt;
    }

    in.seekg(0, std::ios::end);
    const std::streamoff fileSize = in.tellg();
    if (fileSize < 0) {
        return std::nullopt;
    }

    const std::uint64_t payloadOffset = data.header.payloadOffset;
    if (payloadOffset < sizeof(GtaHeader) || payloadOffset > static_cast<std::uint64_t>(fileSize)) {
        return std::nullopt; // Corrupt/truncated - offset points outside the actual file.
    }

    const std::uint64_t metadataSize = payloadOffset - sizeof(GtaHeader);
    const std::uint64_t payloadSize = static_cast<std::uint64_t>(fileSize) - payloadOffset;

    data.metadata.resize(static_cast<std::size_t>(metadataSize));
    if (metadataSize > 0) {
        in.seekg(static_cast<std::streamoff>(sizeof(GtaHeader)), std::ios::beg);
        in.read(reinterpret_cast<char*>(data.metadata.data()), static_cast<std::streamsize>(metadataSize));
        if (!in || in.gcount() != static_cast<std::streamsize>(metadataSize)) {
            return std::nullopt;
        }
    }

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
