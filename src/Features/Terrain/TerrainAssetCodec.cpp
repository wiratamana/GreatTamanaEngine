#include "TerrainAsset.h"

#include "TerrainMaterial.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace gte::terrain {

static_assert(std::endian::native == std::endian::little, "The terrain asset format is little-endian only");

namespace {

constexpr float kDegToRad = 0.017453292519943295f;

inline float Saturate(float v) noexcept { return std::clamp(v, 0.0f, 1.0f); }

inline float SmoothStep(float e0, float e1, float v) noexcept
{
    if (e1 <= e0) {
        return v >= e1 ? 1.0f : 0.0f;
    }
    const float t = Saturate((v - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

inline float SignNotZero(float v) noexcept { return v >= 0.0f ? 1.0f : -1.0f; }

// ---- CRC tables (slicing by 8) ----

using CrcTables = std::array<std::array<std::uint32_t, 256>, 8>;

constexpr CrcTables MakeCrcTables() noexcept
{
    CrcTables t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int bit = 0; bit < 8; ++bit) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        t[0][i] = c;
    }
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = t[0][i];
        for (std::size_t k = 1; k < 8; ++k) {
            c = t[0][c & 0xFFu] ^ (c >> 8);
            t[k][i] = c;
        }
    }
    return t;
}

constexpr CrcTables kCrcTables = MakeCrcTables();

// ---- Settings field table: one place lists every field id, used for both directions ----

enum class FieldType : std::uint16_t { U32 = 0, I32 = 1, F32 = 2, Bool = 3 };

struct FieldWriter {
    std::vector<std::uint8_t>* out;

    void Put(std::uint16_t id, FieldType type, std::uint32_t bits) const
    {
        const std::uint16_t t = static_cast<std::uint16_t>(type);
        const std::size_t at = out->size();
        out->resize(at + 8);
        std::memcpy(out->data() + at, &id, 2);
        std::memcpy(out->data() + at + 2, &t, 2);
        std::memcpy(out->data() + at + 4, &bits, 4);
    }
    void operator()(std::uint16_t id, std::uint32_t& v) const { Put(id, FieldType::U32, v); }
    void operator()(std::uint16_t id, std::int32_t& v) const { Put(id, FieldType::I32, static_cast<std::uint32_t>(v)); }
    void operator()(std::uint16_t id, float& v) const { Put(id, FieldType::F32, std::bit_cast<std::uint32_t>(v)); }
    void operator()(std::uint16_t id, bool& v) const { Put(id, FieldType::Bool, v ? 1u : 0u); }
};

struct FieldReader {
    struct Raw {
        FieldType type;
        std::uint32_t bits;
    };
    std::unordered_map<std::uint16_t, Raw> fields;

    const Raw* Find(std::uint16_t id, FieldType type) const
    {
        const auto it = fields.find(id);
        return (it != fields.end() && it->second.type == type) ? &it->second : nullptr;
    }
    void operator()(std::uint16_t id, std::uint32_t& v) const
    {
        if (const Raw* r = Find(id, FieldType::U32)) {
            v = r->bits;
        }
    }
    void operator()(std::uint16_t id, std::int32_t& v) const
    {
        if (const Raw* r = Find(id, FieldType::I32)) {
            v = static_cast<std::int32_t>(r->bits);
        }
    }
    void operator()(std::uint16_t id, float& v) const
    {
        if (const Raw* r = Find(id, FieldType::F32)) {
            v = std::bit_cast<float>(r->bits);
        }
    }
    void operator()(std::uint16_t id, bool& v) const
    {
        if (const Raw* r = Find(id, FieldType::Bool)) {
            v = r->bits != 0;
        }
    }
};

// Ids are part of the file format: never renumber, only append.
template <class Visitor>
void VisitSettings(TerrainSettings& s, const Visitor& v)
{
    v(1, s.seed);
    v(2, s.heightScale);
    v(3, s.baseFrequency);
    v(4, s.domainWarpStrength);
    v(5, s.domainWarpFrequency);
    v(6, s.octaves);
    v(7, s.gain);
    v(8, s.lacunarity);
    v(9, s.continentalFrequencyScale);
    v(10, s.continentalSeaLevel);
    v(11, s.continentalShaping);
    v(12, s.peaksFrequencyScale);
    v(13, s.peaksWeight);
    v(14, s.detailFrequencyScale);
    v(15, s.detailAmplitude);
    v(16, s.detailOctaves);
    v(17, s.erosionEnabled);
    v(18, s.erosionDropletCount);
    v(19, s.erosionRate);
    v(20, s.depositionRate);
    v(21, s.sedimentCapacity);
    v(22, s.erosionInertia);
    v(23, s.erosionEvaporation);
    v(24, s.erosionGravity);
    v(25, s.erosionMaxLifetime);
    v(26, s.erosionBrushRadius);
    v(27, s.erosionMaxLevel);
    v(28, s.erosionEdgeFalloff);
    v(29, s.erosionStrength);
    v(30, s.grassMaxSlopeDeg);
    v(31, s.rockMinSlopeDeg);
    v(32, s.snowMinHeightFraction);
    v(33, s.snowSlopeLimitDeg);
    v(34, s.sandMaxHeight);
    v(35, s.chunkQuads);
    v(36, s.lod0Spacing);
    v(37, s.lodLevelCount);
    v(38, s.lodDistanceFactor);
    v(39, s.worldHalfExtentChunks);
    v(40, s.frustumCulling);
    v(41, s.hiZCulling);
    v(42, s.maxViewDistance);
}

float RoundDown(double v) noexcept
{
    float f = static_cast<float>(v);
    if (static_cast<double>(f) > v) {
        f = std::nextafter(f, -std::numeric_limits<float>::infinity());
    }
    return f;
}

float RoundUp(double v) noexcept
{
    float f = static_cast<float>(v);
    if (static_cast<double>(f) < v) {
        f = std::nextafter(f, std::numeric_limits<float>::infinity());
    }
    return f;
}

} // namespace

std::uint32_t EncodeOctNormal(float x, float y, float z) noexcept
{
    // Octahedral map over the up axis, so near-vertical normals (terrain) are the most precise.
    const float l1 = std::fabs(x) + std::fabs(y) + std::fabs(z);
    float a = 0.0f;
    float b = 0.0f;
    if (l1 > 0.0f) {
        a = x / l1;
        b = z / l1;
        if (y < 0.0f) {
            const float fa = (1.0f - std::fabs(b)) * SignNotZero(a);
            const float fb = (1.0f - std::fabs(a)) * SignNotZero(b);
            a = fa;
            b = fb;
        }
    }
    const auto snorm = [](float v) {
        return static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
    };
    return static_cast<std::uint32_t>(snorm(a)) | (static_cast<std::uint32_t>(snorm(b)) << 16);
}

void DecodeOctNormal(std::uint32_t packed, float& x, float& y, float& z) noexcept
{
    float a = static_cast<float>(static_cast<std::int16_t>(packed & 0xFFFFu)) / 32767.0f;
    float b = static_cast<float>(static_cast<std::int16_t>(packed >> 16)) / 32767.0f;
    float c = 1.0f - std::fabs(a) - std::fabs(b);
    if (c < 0.0f) {
        const float fa = (1.0f - std::fabs(b)) * SignNotZero(a);
        const float fb = (1.0f - std::fabs(a)) * SignNotZero(b);
        a = fa;
        b = fb;
    }
    const float len = std::sqrt(a * a + b * b + c * c);
    const float inv = len > 0.0f ? 1.0f / len : 0.0f;
    x = a * inv;
    y = c * inv;
    z = b * inv;
}

std::uint16_t QuantizeHeight(float h, float lo, float hi) noexcept
{
    if (!(hi > lo)) {
        return 0;
    }
    const float t = Saturate((h - lo) / (hi - lo));
    return static_cast<std::uint16_t>(std::lround(t * 65535.0f));
}

float DequantizeHeight(std::uint16_t q, float lo, float hi) noexcept
{
    if (!(hi > lo)) {
        return lo;
    }
    const float h = lo + (static_cast<float>(q) / 65535.0f) * (hi - lo);
    return std::clamp(h, lo, hi);
}

std::uint32_t SurfaceSplat(float h, float slope, const TerrainSettings& s) noexcept
{
    // Same breakpoints as SurfaceColor, expressed as weights.
    const float slopeDeg = slope / kDegToRad;
    const float sandW = 1.0f - SmoothStep(s.sandMaxHeight * 0.5f, std::max(s.sandMaxHeight, 0.01f), h);
    const float seabedW = SmoothStep(0.0f, 6.0f, -h);
    const float rockW = SmoothStep(s.grassMaxSlopeDeg, s.rockMinSlopeDeg, slopeDeg);
    const float line = s.snowMinHeightFraction * s.heightScale;
    const float band = 0.06f * s.heightScale;
    const float snowH = SmoothStep(line - band, line + band, h);
    const float snowSlope = 1.0f - SmoothStep(s.snowSlopeLimitDeg - 8.0f, s.snowSlopeLimitDeg, slopeDeg);
    const float snowW = snowH * snowSlope;

    const float flat = (1.0f - rockW) * (1.0f - snowW);
    const float w[4] = {
        (sandW * (1.0f - seabedW) + seabedW) * flat,
        (1.0f - sandW) * (1.0f - seabedW) * flat,
        rockW * (1.0f - snowW),
        snowW,
    };
    int q[4];
    int sum = 0;
    int largest = 0;
    for (int i = 0; i < 4; ++i) {
        q[i] = static_cast<int>(std::lround(Saturate(w[i]) * 255.0f));
        sum += q[i];
        if (w[i] > w[largest]) {
            largest = i;
        }
    }
    q[largest] = std::max(0, q[largest] + (255 - sum));
    return static_cast<std::uint32_t>(q[0]) | (static_cast<std::uint32_t>(q[1]) << 8) |
        (static_cast<std::uint32_t>(q[2]) << 16) | (static_cast<std::uint32_t>(q[3]) << 24);
}

std::uint32_t FastCrc32(std::span<const std::byte> data, std::uint32_t seed) noexcept
{
    std::uint32_t crc = ~seed;
    const std::byte* p = data.data();
    std::size_t n = data.size();
    while (n >= 8) {
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        std::memcpy(&a, p, 4);
        std::memcpy(&b, p + 4, 4);
        a ^= crc;
        crc = kCrcTables[7][a & 0xFFu] ^ kCrcTables[6][(a >> 8) & 0xFFu] ^ kCrcTables[5][(a >> 16) & 0xFFu] ^
            kCrcTables[4][a >> 24] ^ kCrcTables[3][b & 0xFFu] ^ kCrcTables[2][(b >> 8) & 0xFFu] ^
            kCrcTables[1][(b >> 16) & 0xFFu] ^ kCrcTables[0][b >> 24];
        p += 8;
        n -= 8;
    }
    while (n > 0) {
        crc = kCrcTables[0][(crc ^ static_cast<std::uint32_t>(*p)) & 0xFFu] ^ (crc >> 8);
        ++p;
        --n;
    }
    return ~crc;
}

std::vector<std::uint8_t> EncodeSettingsBlock(const TerrainSettings& settings)
{
    TerrainSettings copy = settings;
    std::vector<std::uint8_t> out;
    out.resize(4);
    VisitSettings(copy, FieldWriter{ &out });
    const std::uint32_t count = static_cast<std::uint32_t>((out.size() - 4) / 8);
    std::memcpy(out.data(), &count, 4);
    return out;
}

std::optional<TerrainSettings> DecodeSettingsBlock(std::span<const std::uint8_t> block)
{
    if (block.size() < 4) {
        return std::nullopt;
    }
    std::uint32_t count = 0;
    std::memcpy(&count, block.data(), 4);
    if (count > kTerrainAssetMaxSettingsFields || block.size() != 4u + static_cast<std::size_t>(count) * 8u) {
        return std::nullopt;
    }
    FieldReader reader;
    reader.fields.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* e = block.data() + 4 + static_cast<std::size_t>(i) * 8;
        std::uint16_t id = 0;
        std::uint16_t type = 0;
        std::uint32_t bits = 0;
        std::memcpy(&id, e, 2);
        std::memcpy(&type, e + 2, 2);
        std::memcpy(&bits, e + 4, 4);
        if (type > static_cast<std::uint16_t>(FieldType::Bool)) {
            continue; // A type from a newer writer: skip the field.
        }
        reader.fields[id] = { static_cast<FieldType>(type), bits };
    }
    TerrainSettings s;
    VisitSettings(s, reader);
    return Sanitize(s);
}

ChunkBounds ComputeChunkBounds(const BakedChunk& chunk, const TerrainSettings& s, std::int32_t x, std::int32_t z) noexcept
{
    const TerrainLayout layout = MakeLayout(s);
    const int q = layout.quads;
    const int verts = q + 1;
    const float sp = static_cast<float>(layout.spacingLevel0);
    const float drop = 1.5f * sp;
    float minY = 1.0e30f;
    float maxY = -1.0e30f;
    const std::size_t grid = static_cast<std::size_t>(verts) * static_cast<std::size_t>(verts);
    if (chunk.heights.size() == grid) {
        for (std::size_t i = 0; i < grid; ++i) {
            const float h = DequantizeHeight(chunk.heights[i], chunk.heightMin, chunk.heightMax);
            minY = std::min(minY, h);
            maxY = std::max(maxY, h);
            const int ix = static_cast<int>(i % static_cast<std::size_t>(verts));
            const int iz = static_cast<int>(i / static_cast<std::size_t>(verts));
            if (ix == 0 || iz == 0 || ix == q || iz == q) {
                minY = std::min(minY, h - drop);
            }
        }
    }
    const double size = layout.nodeSizeLevel0;
    const double ox = static_cast<double>(x) * size;
    const double oz = static_cast<double>(z) * size;
    ChunkBounds b;
    b.min[0] = RoundDown(ox);
    b.min[1] = minY;
    b.min[2] = RoundDown(oz);
    b.max[0] = RoundUp(ox + size);
    b.max[1] = maxY;
    b.max[2] = RoundUp(oz + size);
    return b;
}

} // namespace gte::terrain
