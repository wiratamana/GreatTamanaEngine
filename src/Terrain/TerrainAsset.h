#pragma once

// Baked world asset: a rectangle of finest-level nodes stored in a .gta container (AssetType::Terrain).
// Metadata block: 'TERR' header + settings. Payload: chunk AABBs, chunk table, then per-chunk height, normal and splat blocks.

#include "TerrainSettings.h"
#include "TerrainStreamer.h"
#include "TerrainTypes.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte::terrain {

inline constexpr char kTerrainAssetMagic[4] = { 'T', 'E', 'R', 'R' };
inline constexpr std::uint32_t kTerrainAssetFormatVersion = 1;
inline constexpr std::uint32_t kTerrainAssetMaxAxisChunks = 4096;
inline constexpr std::uint32_t kTerrainAssetMaxChunks = 1u << 20;
inline constexpr std::uint32_t kTerrainAssetMaxMetadataBytes = 1u << 20;
inline constexpr std::uint32_t kTerrainAssetMaxSettingsFields = 1024;

// Bytes of the fixed part of the metadata block (before the settings block and the trailing CRC).
inline constexpr std::uint32_t kTerrainAssetFixedMetadataBytes = 96;
// Bytes per chunk: height u16 + oct normal (2 x i16) + splat RGBA8.
inline constexpr std::uint32_t kTerrainAssetBytesPerVertex = 10;

// World-space box of one chunk, rounded outward.
struct ChunkBounds {
    float min[3] = { 0.0f, 0.0f, 0.0f };
    float max[3] = { 0.0f, 0.0f, 0.0f };
};

// One baked finest-level node: (quads + 1)^2 grid vertices, row-major, no skirt.
struct BakedChunk {
    float heightMin = 0.0f; // Grid height range; heights[] spans it in 65535 steps.
    float heightMax = 0.0f;
    ChunkBounds bounds;     // Skirt included, derived from the quantised heights.
    std::vector<std::uint16_t> heights;
    std::vector<std::uint32_t> normals; // Octahedral, x in the low 16 bits, y in the high 16 (snorm16).
    std::vector<std::uint32_t> splat;   // Weights 0..255: R sand/seabed, G grass, B rock, A snow; they sum to 255.
};

struct BakedTerrain {
    TerrainSettings settings; // The settings the world was baked with (sanitized).
    std::int32_t firstX = 0;  // Finest-level node index of the first chunk.
    std::int32_t firstZ = 0;
    std::uint32_t countX = 0;
    std::uint32_t countZ = 0;
    std::vector<BakedChunk> chunks; // Index = (z - firstZ) * countX + (x - firstX).

    const BakedChunk* FindChunk(std::int64_t x, std::int64_t z) const noexcept;
};

// Everything the metadata block says about a file.
struct TerrainAssetInfo {
    std::uint32_t formatVersion = 0;
    std::uint32_t chunkQuads = 0;
    std::uint32_t gridVerticesPerSide = 0;
    std::int32_t firstX = 0;
    std::int32_t firstZ = 0;
    std::uint32_t countX = 0;
    std::uint32_t countZ = 0;
    float lod0Spacing = 0.0f;
    float worldMin[3] = { 0.0f, 0.0f, 0.0f };
    float worldMax[3] = { 0.0f, 0.0f, 0.0f };
    std::uint64_t payloadSize = 0;
    std::uint64_t payloadOffset = 0; // Absolute file offset.
    std::uint32_t payloadCrc32 = 0;  // CRC-32 of the table section (chunk bounds + chunk table, which holds every block CRC).
    TerrainSettings settings;
    std::uint64_t fileSize = 0;
};

// Chunk table row (on disk: 24 bytes).
struct ChunkTableEntry {
    std::uint64_t offset = 0; // From the payload start.
    std::uint32_t size = 0;
    std::uint32_t crc32 = 0;
    float heightMin = 0.0f;
    float heightMax = 0.0f;
};

// ---- Pure codec pieces (exposed for tests and for a GPU-side reader) ----

// Octahedral unit vector <-> two snorm16 packed in a uint32.
std::uint32_t EncodeOctNormal(float x, float y, float z) noexcept;
void DecodeOctNormal(std::uint32_t packed, float& x, float& y, float& z) noexcept;

// 16-bit quantisation over [lo, hi]; a flat range maps everything to 0.
std::uint16_t QuantizeHeight(float h, float lo, float hi) noexcept;
float DequantizeHeight(std::uint16_t q, float lo, float hi) noexcept;

// Sand/seabed, grass, rock, snow weights for a surface point; sum is exactly 255.
std::uint32_t SurfaceSplat(float heightMetres, float slopeRadians, const TerrainSettings& s) noexcept;

// Table-driven CRC-32 with the same result as gte::Crc32 (seed = previous result).
std::uint32_t FastCrc32(std::span<const std::byte> data, std::uint32_t seed = 0) noexcept;

// Settings as tagged 8-byte fields (id u16, type u16, value u32), preceded by a u32 count.
// Unknown ids are skipped on decode and missing ids keep the defaults, so versions can grow.
std::vector<std::uint8_t> EncodeSettingsBlock(const TerrainSettings& s);
std::optional<TerrainSettings> DecodeSettingsBlock(std::span<const std::uint8_t> block);

// Bounds of a chunk built from its quantised data (what BuildNodeFromBaked produces), world space, rounded outward.
ChunkBounds ComputeChunkBounds(const BakedChunk& chunk, const TerrainSettings& s, std::int32_t x, std::int32_t z) noexcept;

// ---- Bake ----

using BakeProgress = std::function<void(std::uint32_t done, std::uint32_t total)>;

// Bakes countX * countZ finest-level nodes on the job system. Progress runs on the calling thread.
// Returns nullopt for bad arguments or when `cancel` was raised.
std::optional<BakedTerrain> BakeRegion(const TerrainSettings& settings, std::int32_t firstX, std::int32_t firstZ,
    std::uint32_t countX, std::uint32_t countZ, const BakeProgress& progress = {},
    const std::atomic<bool>* cancel = nullptr, ITerrainExecutor* executor = nullptr);

// ---- File ----

// Writes the .gta container atomically. `error` gets the reason on failure.
bool SaveTerrainAsset(const std::filesystem::path& path, const BakedTerrain& baked, std::string* error = nullptr);

// Reads and checks everything (all CRCs).
std::optional<BakedTerrain> LoadTerrainAsset(const std::filesystem::path& path, std::string* error = nullptr);

// Seek-based reader: opens the file, validates the header and tables, then reads single chunks on demand.
class TerrainAssetReader {
public:
    static std::optional<TerrainAssetReader> Open(const std::filesystem::path& path, std::string* error = nullptr);

    TerrainAssetReader(TerrainAssetReader&&) noexcept = default;
    TerrainAssetReader& operator=(TerrainAssetReader&&) noexcept = default;
    TerrainAssetReader(const TerrainAssetReader&) = delete;
    TerrainAssetReader& operator=(const TerrainAssetReader&) = delete;

    const TerrainAssetInfo& Info() const noexcept { return m_info; }
    std::span<const ChunkBounds> Bounds() const noexcept { return m_bounds; }
    std::span<const ChunkTableEntry> Table() const noexcept { return m_table; }

    // Reads and verifies one chunk block (index = row * countX + column).
    bool ReadChunk(std::uint32_t index, BakedChunk& out, std::string* error = nullptr);

private:
    TerrainAssetReader() = default;

    std::ifstream m_file;
    TerrainAssetInfo m_info;
    std::vector<ChunkBounds> m_bounds;
    std::vector<ChunkTableEntry> m_table;
};

// ---- Runtime ----

// True when `key` is a finest-level node inside the region, or a coarser node whose whole area is inside it.
bool BakedCoversNode(const BakedTerrain& baked, const NodeKey& key) noexcept;

// Builds a node from baked data: level 0 uses the stored normals, coarser levels decimate the baked grid.
// Colours use `colorSettings` when given (so material sliders still work), else the baked settings.
std::optional<NodeMesh> BuildNodeFromBaked(
    const BakedTerrain& baked, const NodeKey& key, const TerrainSettings* colorSettings = nullptr);

// Streamer provider: serves baked nodes while the height settings still match the bake.
NodeProvider MakeBakedNodeProvider(std::shared_ptr<const BakedTerrain> baked);

} // namespace gte::terrain
