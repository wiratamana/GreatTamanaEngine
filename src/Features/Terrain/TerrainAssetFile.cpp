#include "TerrainAsset.h"

#include "../../Assets/AtomicFile.h"
#include "../../Assets/GtaFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace gte::terrain {

namespace {

constexpr std::size_t kRowBytes = 24; // Chunk bounds row and chunk table row.

void SetError(std::string* error, const std::string& message)
{
    if (error != nullptr) {
        *error = message;
    }
}

template <class T>
void Put(std::vector<std::uint8_t>& out, const T& value)
{
    const std::size_t at = out.size();
    out.resize(at + sizeof(T));
    std::memcpy(out.data() + at, &value, sizeof(T));
}

template <class T>
T Get(const std::uint8_t* base, std::size_t offset)
{
    T value{};
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}

bool IsFinite3(const float* v) noexcept
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

bool ValidQuads(std::uint32_t q) noexcept { return q == 16 || q == 32 || q == 64 || q == 128; }

std::uint64_t VertexCount(std::uint32_t quads) noexcept
{
    return static_cast<std::uint64_t>(quads + 1) * static_cast<std::uint64_t>(quads + 1);
}

std::span<const std::byte> Bytes(const void* p, std::size_t n) noexcept
{
    return std::span<const std::byte>(static_cast<const std::byte*>(p), n);
}

std::uint32_t BlockCrc(const BakedChunk& c) noexcept
{
    std::uint32_t crc = FastCrc32(Bytes(c.heights.data(), c.heights.size() * 2));
    crc = FastCrc32(Bytes(c.normals.data(), c.normals.size() * 4), crc);
    return FastCrc32(Bytes(c.splat.data(), c.splat.size() * 4), crc);
}

// Parses and validates the metadata block (all fields and the trailing CRC).
bool ParseMetadata(std::span<const std::uint8_t> md, TerrainAssetInfo& info, std::string* error)
{
    if (md.size() < kTerrainAssetFixedMetadataBytes + 4 + 4) {
        SetError(error, "metadata block too small");
        return false;
    }
    const std::uint8_t* p = md.data();
    if (std::memcmp(p, kTerrainAssetMagic, 4) != 0) {
        SetError(error, "bad TERR magic");
        return false;
    }
    info.formatVersion = Get<std::uint32_t>(p, 4);
    if (info.formatVersion != kTerrainAssetFormatVersion) {
        SetError(error, "unsupported terrain format version " + std::to_string(info.formatVersion));
        return false;
    }
    if (Get<std::uint32_t>(p, 8) != md.size()) {
        SetError(error, "metadata size field does not match the container");
        return false;
    }
    if (Get<std::uint32_t>(p, 12) != 0) {
        SetError(error, "unknown format flags");
        return false;
    }
    info.chunkQuads = Get<std::uint32_t>(p, 16);
    info.gridVerticesPerSide = Get<std::uint32_t>(p, 20);
    info.firstX = Get<std::int32_t>(p, 24);
    info.firstZ = Get<std::int32_t>(p, 28);
    info.countX = Get<std::uint32_t>(p, 32);
    info.countZ = Get<std::uint32_t>(p, 36);
    info.lod0Spacing = Get<float>(p, 40);
    for (int i = 0; i < 3; ++i) {
        info.worldMin[i] = Get<float>(p, 44 + static_cast<std::size_t>(i) * 4);
        info.worldMax[i] = Get<float>(p, 56 + static_cast<std::size_t>(i) * 4);
    }
    info.payloadSize = Get<std::uint64_t>(p, 68);
    info.payloadOffset = Get<std::uint64_t>(p, 76);
    info.payloadCrc32 = Get<std::uint32_t>(p, 84);
    const std::uint32_t settingsBytes = Get<std::uint32_t>(p, 88);

    if (!ValidQuads(info.chunkQuads) || info.gridVerticesPerSide != info.chunkQuads + 1) {
        SetError(error, "invalid chunk size");
        return false;
    }
    if (info.countX == 0 || info.countZ == 0 || info.countX > kTerrainAssetMaxAxisChunks ||
        info.countZ > kTerrainAssetMaxAxisChunks ||
        static_cast<std::uint64_t>(info.countX) * info.countZ > kTerrainAssetMaxChunks) {
        SetError(error, "invalid chunk count");
        return false;
    }
    if (static_cast<std::int64_t>(info.firstX) + info.countX > std::numeric_limits<std::int32_t>::max() ||
        static_cast<std::int64_t>(info.firstZ) + info.countZ > std::numeric_limits<std::int32_t>::max()) {
        SetError(error, "chunk range overflows");
        return false;
    }
    if (!std::isfinite(info.lod0Spacing) || info.lod0Spacing <= 0.0f || !IsFinite3(info.worldMin) ||
        !IsFinite3(info.worldMax)) {
        SetError(error, "invalid world dimensions");
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (info.worldMin[i] > info.worldMax[i]) {
            SetError(error, "world bounds are inverted");
            return false;
        }
    }
    if (settingsBytes > md.size() || kTerrainAssetFixedMetadataBytes + static_cast<std::size_t>(settingsBytes) + 4 != md.size()) {
        SetError(error, "settings block size does not match the metadata size");
        return false;
    }
    const std::uint32_t storedCrc = Get<std::uint32_t>(p, md.size() - 4);
    if (FastCrc32(Bytes(p, md.size() - 4)) != storedCrc) {
        SetError(error, "metadata CRC mismatch");
        return false;
    }
    const auto settings = DecodeSettingsBlock(md.subspan(kTerrainAssetFixedMetadataBytes, settingsBytes));
    if (!settings) {
        SetError(error, "settings block is malformed");
        return false;
    }
    info.settings = *settings;
    if (static_cast<std::uint32_t>(info.settings.chunkQuads) != info.chunkQuads || info.settings.lod0Spacing != info.lod0Spacing) {
        SetError(error, "settings disagree with the stored world dimensions");
        return false;
    }
    return true;
}

} // namespace

const BakedChunk* BakedTerrain::FindChunk(std::int64_t x, std::int64_t z) const noexcept
{
    const std::int64_t rx = x - firstX;
    const std::int64_t rz = z - firstZ;
    if (rx < 0 || rz < 0 || rx >= static_cast<std::int64_t>(countX) || rz >= static_cast<std::int64_t>(countZ)) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(rz) * countX + static_cast<std::size_t>(rx);
    return index < chunks.size() ? &chunks[index] : nullptr;
}

bool SaveTerrainAsset(const std::filesystem::path& path, const BakedTerrain& baked, std::string* error)
{
    const std::uint32_t quads = static_cast<std::uint32_t>(baked.settings.chunkQuads);
    if (!ValidQuads(quads) || baked.countX == 0 || baked.countZ == 0 || baked.countX > kTerrainAssetMaxAxisChunks ||
        baked.countZ > kTerrainAssetMaxAxisChunks) {
        SetError(error, "invalid terrain dimensions");
        return false;
    }
    const std::size_t n = static_cast<std::size_t>(baked.countX) * baked.countZ;
    const std::size_t verts = static_cast<std::size_t>(VertexCount(quads));
    if (n > kTerrainAssetMaxChunks || baked.chunks.size() != n) {
        SetError(error, "chunk list does not match the chunk count");
        return false;
    }
    for (const BakedChunk& c : baked.chunks) {
        if (c.heights.size() != verts || c.normals.size() != verts || c.splat.size() != verts) {
            SetError(error, "a chunk has the wrong vertex count");
            return false;
        }
    }

    const std::uint64_t blockSize = static_cast<std::uint64_t>(verts) * kTerrainAssetBytesPerVertex;
    const std::uint64_t tableBytes = static_cast<std::uint64_t>(n) * kRowBytes * 2;
    const std::uint64_t payloadSize = tableBytes + blockSize * n;

    // Section 1 of the payload: bounds rows, then chunk table rows.
    std::vector<std::uint8_t> tables;
    tables.reserve(static_cast<std::size_t>(tableBytes));
    float worldMin[3] = { 1.0e30f, 1.0e30f, 1.0e30f };
    float worldMax[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
    for (const BakedChunk& c : baked.chunks) {
        for (int i = 0; i < 3; ++i) {
            Put(tables, c.bounds.min[i]);
        }
        for (int i = 0; i < 3; ++i) {
            Put(tables, c.bounds.max[i]);
            worldMin[i] = std::min(worldMin[i], c.bounds.min[i]);
            worldMax[i] = std::max(worldMax[i], c.bounds.max[i]);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        const BakedChunk& c = baked.chunks[i];
        Put(tables, static_cast<std::uint64_t>(tableBytes + blockSize * i));
        Put(tables, static_cast<std::uint32_t>(blockSize));
        Put(tables, BlockCrc(c));
        Put(tables, c.heightMin);
        Put(tables, c.heightMax);
    }

    // Metadata block.
    const std::vector<std::uint8_t> settingsBlock = EncodeSettingsBlock(baked.settings);
    std::vector<std::uint8_t> md;
    md.reserve(kTerrainAssetFixedMetadataBytes + settingsBlock.size() + 4);
    md.insert(md.end(), std::begin(kTerrainAssetMagic), std::end(kTerrainAssetMagic));
    Put(md, kTerrainAssetFormatVersion);
    Put(md, static_cast<std::uint32_t>(kTerrainAssetFixedMetadataBytes + settingsBlock.size() + 4));
    Put(md, std::uint32_t{ 0 });
    Put(md, quads);
    Put(md, quads + 1);
    Put(md, baked.firstX);
    Put(md, baked.firstZ);
    Put(md, baked.countX);
    Put(md, baked.countZ);
    Put(md, baked.settings.lod0Spacing);
    for (const float v : worldMin) {
        Put(md, v);
    }
    for (const float v : worldMax) {
        Put(md, v);
    }
    Put(md, payloadSize);
    Put(md, static_cast<std::uint64_t>(sizeof(GtaHeader) + kTerrainAssetFixedMetadataBytes + settingsBlock.size() + 4));
    Put(md, FastCrc32(Bytes(tables.data(), tables.size())));
    Put(md, static_cast<std::uint32_t>(settingsBlock.size()));
    Put(md, std::uint32_t{ 0 });
    md.insert(md.end(), settingsBlock.begin(), settingsBlock.end());
    Put(md, FastCrc32(Bytes(md.data(), md.size())));

    GtaHeader header;
    std::memcpy(header.magic, kGtaMagic, sizeof(header.magic));
    header.assetType = static_cast<std::uint64_t>(AssetType::Terrain);
    header.version = kGtaCurrentVersion;
    const Guid guid = Guid::Generate();
    header.guidLow = guid.low;
    header.guidHigh = guid.high;
    header.flags = static_cast<std::uint64_t>(AssetFlags::None);
    header.payloadOffset = sizeof(GtaHeader) + md.size();

    // Parts are written straight from the chunk arrays: the payload is never concatenated in memory.
    std::vector<ConstByteSpan> parts;
    parts.reserve(3 + n * 3);
    parts.push_back(std::as_bytes(std::span<const GtaHeader, 1>(&header, 1)));
    parts.push_back(std::as_bytes(std::span<const std::uint8_t>(md)));
    parts.push_back(std::as_bytes(std::span<const std::uint8_t>(tables)));
    for (const BakedChunk& c : baked.chunks) {
        parts.push_back(std::as_bytes(std::span<const std::uint16_t>(c.heights)));
        parts.push_back(std::as_bytes(std::span<const std::uint32_t>(c.normals)));
        parts.push_back(std::as_bytes(std::span<const std::uint32_t>(c.splat)));
    }

    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    return WriteFileAtomically(path, parts, nullptr, error);
}

std::optional<TerrainAssetReader> TerrainAssetReader::Open(const std::filesystem::path& path, std::string* error)
{
    TerrainAssetReader r;
    r.m_file.open(path, std::ios::binary);
    if (!r.m_file.is_open()) {
        SetError(error, "cannot open file");
        return std::nullopt;
    }
    r.m_file.seekg(0, std::ios::end);
    const std::streamoff endPos = r.m_file.tellg();
    if (endPos < 0) {
        SetError(error, "cannot get file size");
        return std::nullopt;
    }
    const std::uint64_t fileSize = static_cast<std::uint64_t>(endPos);
    r.m_file.seekg(0, std::ios::beg);

    GtaHeader header;
    r.m_file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!r.m_file || !header.IsMagicValid()) {
        SetError(error, "not a .gta container");
        return std::nullopt;
    }
    if (header.Type() != AssetType::Terrain) {
        SetError(error, "container does not hold a terrain asset");
        return std::nullopt;
    }
    if (header.version != kGtaCurrentVersion || header.flags != 0) {
        SetError(error, "unsupported container version or flags");
        return std::nullopt;
    }
    if (header.payloadOffset < sizeof(GtaHeader) || header.payloadOffset > fileSize) {
        SetError(error, "payload offset outside the file");
        return std::nullopt;
    }
    const std::uint64_t mdSize = header.payloadOffset - sizeof(GtaHeader);
    if (mdSize > kTerrainAssetMaxMetadataBytes) {
        SetError(error, "metadata block too large");
        return std::nullopt;
    }
    std::vector<std::uint8_t> md(static_cast<std::size_t>(mdSize));
    r.m_file.read(reinterpret_cast<char*>(md.data()), static_cast<std::streamsize>(mdSize));
    if (!r.m_file) {
        SetError(error, "file is truncated inside the metadata");
        return std::nullopt;
    }
    if (!ParseMetadata(md, r.m_info, error)) {
        return std::nullopt;
    }
    TerrainAssetInfo& info = r.m_info;
    info.fileSize = fileSize;
    if (info.payloadOffset != header.payloadOffset || info.payloadSize != fileSize - header.payloadOffset) {
        SetError(error, "payload offset or size disagree with the file");
        return std::nullopt;
    }

    const std::uint64_t n = static_cast<std::uint64_t>(info.countX) * info.countZ;
    const std::uint64_t blockSize = VertexCount(info.chunkQuads) * kTerrainAssetBytesPerVertex;
    const std::uint64_t tableBytes = n * kRowBytes * 2;
    if (tableBytes > info.payloadSize || (info.payloadSize - tableBytes) / blockSize != n ||
        (info.payloadSize - tableBytes) % blockSize != 0) {
        SetError(error, "payload size does not match the chunk count");
        return std::nullopt;
    }
    std::vector<std::uint8_t> tables(static_cast<std::size_t>(tableBytes));
    r.m_file.seekg(static_cast<std::streamoff>(info.payloadOffset), std::ios::beg);
    r.m_file.read(reinterpret_cast<char*>(tables.data()), static_cast<std::streamsize>(tableBytes));
    if (!r.m_file) {
        SetError(error, "file is truncated inside the tables");
        return std::nullopt;
    }
    if (FastCrc32(Bytes(tables.data(), tables.size())) != info.payloadCrc32) {
        SetError(error, "table CRC mismatch");
        return std::nullopt;
    }

    r.m_bounds.resize(static_cast<std::size_t>(n));
    r.m_table.resize(static_cast<std::size_t>(n));
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* b = tables.data() + i * kRowBytes;
        ChunkBounds& cb = r.m_bounds[i];
        for (int k = 0; k < 3; ++k) {
            cb.min[k] = Get<float>(b, static_cast<std::size_t>(k) * 4);
            cb.max[k] = Get<float>(b, 12 + static_cast<std::size_t>(k) * 4);
        }
        if (!IsFinite3(cb.min) || !IsFinite3(cb.max) || cb.min[0] > cb.max[0] || cb.min[1] > cb.max[1] || cb.min[2] > cb.max[2]) {
            SetError(error, "chunk " + std::to_string(i) + " has invalid bounds");
            return std::nullopt;
        }
        const std::uint8_t* t = tables.data() + static_cast<std::size_t>(n) * kRowBytes + i * kRowBytes;
        ChunkTableEntry& e = r.m_table[i];
        e.offset = Get<std::uint64_t>(t, 0);
        e.size = Get<std::uint32_t>(t, 8);
        e.crc32 = Get<std::uint32_t>(t, 12);
        e.heightMin = Get<float>(t, 16);
        e.heightMax = Get<float>(t, 20);
        if (e.size != blockSize || e.offset < tableBytes || e.offset > info.payloadSize ||
            e.size > info.payloadSize - e.offset) {
            SetError(error, "chunk " + std::to_string(i) + " lies outside the payload");
            return std::nullopt;
        }
        if (!std::isfinite(e.heightMin) || !std::isfinite(e.heightMax) || e.heightMin > e.heightMax) {
            SetError(error, "chunk " + std::to_string(i) + " has an invalid height range");
            return std::nullopt;
        }
    }
    return r;
}

bool TerrainAssetReader::ReadChunk(std::uint32_t index, BakedChunk& out, std::string* error)
{
    if (index >= m_table.size()) {
        SetError(error, "chunk index out of range");
        return false;
    }
    const ChunkTableEntry& e = m_table[index];
    const std::size_t verts = static_cast<std::size_t>(VertexCount(m_info.chunkQuads));
    std::vector<std::uint8_t> block(e.size);
    m_file.clear();
    m_file.seekg(static_cast<std::streamoff>(m_info.payloadOffset + e.offset), std::ios::beg);
    m_file.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size()));
    if (!m_file) {
        SetError(error, "chunk " + std::to_string(index) + " could not be read");
        return false;
    }
    if (FastCrc32(Bytes(block.data(), block.size())) != e.crc32) {
        SetError(error, "chunk " + std::to_string(index) + " CRC mismatch");
        return false;
    }
    out.heightMin = e.heightMin;
    out.heightMax = e.heightMax;
    out.bounds = m_bounds[index];
    out.heights.resize(verts);
    out.normals.resize(verts);
    out.splat.resize(verts);
    std::memcpy(out.heights.data(), block.data(), verts * 2);
    std::memcpy(out.normals.data(), block.data() + verts * 2, verts * 4);
    std::memcpy(out.splat.data(), block.data() + verts * 6, verts * 4);
    return true;
}

std::optional<BakedTerrain> LoadTerrainAsset(const std::filesystem::path& path, std::string* error)
{
    auto reader = TerrainAssetReader::Open(path, error);
    if (!reader) {
        return std::nullopt;
    }
    const TerrainAssetInfo& info = reader->Info();
    BakedTerrain baked;
    baked.settings = info.settings;
    baked.firstX = info.firstX;
    baked.firstZ = info.firstZ;
    baked.countX = info.countX;
    baked.countZ = info.countZ;
    baked.chunks.resize(reader->Table().size());
    for (std::uint32_t i = 0; i < baked.chunks.size(); ++i) {
        if (!reader->ReadChunk(i, baked.chunks[i], error)) {
            return std::nullopt;
        }
    }
    return baked;
}

} // namespace gte::terrain
