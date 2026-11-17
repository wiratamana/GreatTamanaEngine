// Tier 1: baked world asset (codec pieces, bake, .gta file, seek reader, corrupt files, streamer provider).

#include "Features/Terrain/TerrainAsset.h"

#include "Assets/AssetDatabase.h"
#include "Assets/Crc32.h"
#include "Assets/GtaFile.h"
#include "Features/Terrain/TerrainHeightField.h"
#include "Features/Terrain/TerrainMaterial.h"
#include "Features/Terrain/TerrainNode.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>

namespace gte::terrain {

namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

constexpr double kRadToDeg = 57.29577951308232;

TerrainSettings TestSettings()
{
    TerrainSettings s;
    s.seed = 4242;
    return Sanitize(s);
}

// Region (-4..3, -4..3): covers negative indices and aligned level 1 and level 2 parents.
constexpr int kFirst = -4;
constexpr std::uint32_t kCount = 8;

const BakedTerrain& SharedBake()
{
    static const BakedTerrain baked = [] {
        InlineTerrainExecutor exec;
        auto b = BakeRegion(TestSettings(), kFirst, kFirst, kCount, kCount, {}, nullptr, &exec);
        return b ? std::move(*b) : BakedTerrain{};
    }();
    return baked;
}

struct TempDir {
    std::filesystem::path path;
    TempDir()
    {
        path = std::filesystem::temp_directory_path() / ("gte_terrain_asset_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::vector<std::uint8_t> ReadAll(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void WriteAll(const std::filesystem::path& p, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

template <class T>
T GetAt(const std::vector<std::uint8_t>& b, std::size_t off)
{
    T v{};
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

template <class T>
void PutAt(std::vector<std::uint8_t>& b, std::size_t off, T v)
{
    std::memcpy(b.data() + off, &v, sizeof(T));
}

constexpr std::size_t kMd = 64; // Metadata start in the file.

// Recomputes the metadata CRC after a patch so the parser reaches the field checks.
void FixMetadataCrc(std::vector<std::uint8_t>& file)
{
    const std::size_t mdSize = GetAt<std::uint32_t>(file, kMd + 8);
    const std::uint32_t crc = FastCrc32(std::as_bytes(std::span<const std::uint8_t>(file.data() + kMd, mdSize - 4)));
    PutAt<std::uint32_t>(file, kMd + mdSize - 4, crc);
}

// Recomputes the table CRC (stored in the metadata), then the metadata CRC.
void FixTableCrc(std::vector<std::uint8_t>& file, std::size_t tableBytes)
{
    const std::size_t payload = static_cast<std::size_t>(GetAt<std::uint64_t>(file, kMd + 76));
    PutAt<std::uint32_t>(file, kMd + 84,
        FastCrc32(std::as_bytes(std::span<const std::uint8_t>(file.data() + payload, tableBytes))));
    FixMetadataCrc(file);
}

float AngleDeg(const float* a, const float* b)
{
    const float d = std::clamp(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], -1.0f, 1.0f);
    return static_cast<float>(std::acos(d) * kRadToDeg);
}

} // namespace

// ---- codec ----

TEST(TerrainAssetTests, OctNormalRoundTripIsPrecise)
{
    std::mt19937 rng(7);
    std::normal_distribution<float> gauss;
    float worst = 0.0f;
    for (int i = 0; i < 20000; ++i) {
        float v[3] = { gauss(rng), gauss(rng), gauss(rng) };
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (float& c : v) {
            c /= len;
        }
        float out[3];
        DecodeOctNormal(EncodeOctNormal(v[0], v[1], v[2]), out[0], out[1], out[2]);
        worst = std::max(worst, AngleDeg(v, out));
    }
    // Includes the folded lower hemisphere; terrain normals point up where the map is finest.
    EXPECT_LT(worst, 0.05f); // snorm16 octahedral: about 0.04 degrees worst case.
    float up[3];
    DecodeOctNormal(EncodeOctNormal(0.0f, 1.0f, 0.0f), up[0], up[1], up[2]);
    EXPECT_NEAR(up[1], 1.0f, 1e-6f);
}

TEST(TerrainAssetTests, HeightQuantisationErrorIsHalfAStep)
{
    std::mt19937 rng(11);
    for (const float range : { 0.5f, 40.0f, 600.0f, 2250.0f }) {
        const float lo = -100.0f;
        const float hi = lo + range;
        std::uniform_real_distribution<float> dist(lo, hi);
        float worst = 0.0f;
        for (int i = 0; i < 20000; ++i) {
            const float h = dist(rng);
            worst = std::max(worst, std::fabs(DequantizeHeight(QuantizeHeight(h, lo, hi), lo, hi) - h));
        }
        const float halfStep = range / 65535.0f * 0.5f;
        EXPECT_LE(worst, halfStep * 1.01f + 1e-5f) << "range " << range;
    }
    EXPECT_EQ(DequantizeHeight(QuantizeHeight(-100.0f, -100.0f, 50.0f), -100.0f, 50.0f), -100.0f);
    EXPECT_EQ(DequantizeHeight(QuantizeHeight(50.0f, -100.0f, 50.0f), -100.0f, 50.0f), 50.0f);
    EXPECT_EQ(QuantizeHeight(5.0f, 3.0f, 3.0f), 0);
    EXPECT_EQ(DequantizeHeight(0, 3.0f, 3.0f), 3.0f);
}

TEST(TerrainAssetTests, SplatWeightsSumTo255AndFollowTheMaterialRules)
{
    const TerrainSettings s = TestSettings();
    const auto w = [](std::uint32_t p, int c) { return static_cast<int>((p >> (8 * c)) & 0xFFu); };
    for (float h = -400.0f; h <= 1800.0f; h += 37.0f) {
        for (float slopeDeg = 0.0f; slopeDeg <= 89.0f; slopeDeg += 3.0f) {
            const std::uint32_t p = SurfaceSplat(h, slopeDeg * 0.017453292f, s);
            EXPECT_EQ(w(p, 0) + w(p, 1) + w(p, 2) + w(p, 3), 255) << h << " " << slopeDeg;
        }
    }
    EXPECT_GT(w(SurfaceSplat(150.0f, 0.1f, s), 1), 200);   // Flat and low: grass.
    EXPECT_GT(w(SurfaceSplat(150.0f, 1.2f, s), 2), 200);   // Steep: rock.
    EXPECT_GT(w(SurfaceSplat(1500.0f, 0.1f, s), 3), 200);  // High and flat: snow.
    EXPECT_GT(w(SurfaceSplat(1.0f, 0.05f, s), 0), 150);    // Beach: sand.
}

TEST(TerrainAssetTests, FastCrcMatchesTheEngineCrc)
{
    std::mt19937 rng(3);
    std::vector<std::byte> data(4099);
    for (std::byte& b : data) {
        b = static_cast<std::byte>(rng() & 0xFF);
    }
    for (const std::size_t n : { std::size_t{ 0 }, std::size_t{ 1 }, std::size_t{ 7 }, std::size_t{ 8 }, std::size_t{ 9 }, std::size_t{ 1000 }, data.size() }) {
        const std::span<const std::byte> view(data.data(), n);
        EXPECT_EQ(FastCrc32(view), Crc32(view)) << n;
        EXPECT_EQ(FastCrc32(view, 0xDEADBEEFu), Crc32(view, 0xDEADBEEFu)) << n;
    }
    // Chained calls equal one call.
    const std::span<const std::byte> all(data);
    EXPECT_EQ(FastCrc32(all.subspan(100), FastCrc32(all.first(100))), FastCrc32(all));
}

TEST(TerrainAssetTests, SettingsRoundTripAndForwardCompatibility)
{
    // If this fails a field was added to TerrainSettings: add it to VisitSettings in TerrainAssetCodec.cpp.
    EXPECT_EQ(sizeof(TerrainSettings), 164u);

    TerrainSettings s;
    s.seed = 99;
    s.heightScale = 2100.5f;
    s.baseFrequency = 0.0007f;
    s.domainWarpStrength = 111.0f;
    s.domainWarpFrequency = 0.0003f;
    s.octaves = 5;
    s.gain = 0.45f;
    s.lacunarity = 2.2f;
    s.continentalFrequencyScale = 0.2f;
    s.continentalSeaLevel = 0.1f;
    s.continentalShaping = 0.3f;
    s.peaksFrequencyScale = 1.5f;
    s.peaksWeight = 0.8f;
    s.detailFrequencyScale = 40.0f;
    s.detailAmplitude = 4.0f;
    s.detailOctaves = 2;
    s.erosionEnabled = false;
    s.erosionDropletCount = 777;
    s.erosionRate = 0.21f;
    s.depositionRate = 0.22f;
    s.sedimentCapacity = 3.5f;
    s.erosionInertia = 0.07f;
    s.erosionEvaporation = 0.02f;
    s.erosionGravity = 5.0f;
    s.erosionMaxLifetime = 25;
    s.erosionBrushRadius = 3;
    s.erosionMaxLevel = 2;
    s.erosionEdgeFalloff = 9;
    s.erosionStrength = 4.0f;
    s.grassMaxSlopeDeg = 30.0f;
    s.rockMinSlopeDeg = 45.0f;
    s.snowMinHeightFraction = 0.6f;
    s.snowSlopeLimitDeg = 55.0f;
    s.sandMaxHeight = 9.0f;
    s.chunkQuads = 32;
    s.lod0Spacing = 3.0f;
    s.lodLevelCount = 6;
    s.lodDistanceFactor = 2.0f;
    s.worldHalfExtentChunks = 64;
    s.frustumCulling = false;
    s.hiZCulling = false;
    s.maxViewDistance = 9000.0f;
    s = Sanitize(s);

    const auto block = EncodeSettingsBlock(s);
    const auto back = DecodeSettingsBlock(block);
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(*back == s);
    EXPECT_EQ(Hash(*back), Hash(s));

    // Newer file: one unknown id, one unknown type, and two known ids missing -> defaults for the missing ones.
    std::vector<std::uint8_t> newer;
    const auto putField = [&](std::uint16_t id, std::uint16_t type, std::uint32_t bits) {
        const std::size_t at = newer.size();
        newer.resize(at + 8);
        std::memcpy(newer.data() + at, &id, 2);
        std::memcpy(newer.data() + at + 2, &type, 2);
        std::memcpy(newer.data() + at + 4, &bits, 4);
    };
    newer.resize(4);
    putField(1, 0, 555);          // seed
    putField(999, 2, 0x3F800000u); // Unknown id.
    putField(2, 77, 0u);          // Unknown type for heightScale: ignored.
    const std::uint32_t count = 3;
    std::memcpy(newer.data(), &count, 4);
    const auto partial = DecodeSettingsBlock(newer);
    ASSERT_TRUE(partial.has_value());
    EXPECT_EQ(partial->seed, 555u);
    EXPECT_EQ(partial->heightScale, TerrainSettings{}.heightScale);
    EXPECT_EQ(partial->octaves, TerrainSettings{}.octaves);

    EXPECT_FALSE(DecodeSettingsBlock({}).has_value());
    std::vector<std::uint8_t> lie = block;
    const std::uint32_t huge = 0x7FFFFFFFu;
    std::memcpy(lie.data(), &huge, 4);
    EXPECT_FALSE(DecodeSettingsBlock(lie).has_value());
    lie.assign(block.begin(), block.end() - 3);
    EXPECT_FALSE(DecodeSettingsBlock(lie).has_value());

    // A NaN field is sanitized instead of passed through.
    std::vector<std::uint8_t> nanBlock;
    nanBlock.resize(4 + 8);
    const std::uint32_t one = 1;
    std::memcpy(nanBlock.data(), &one, 4);
    const std::uint16_t id = 2;
    const std::uint16_t type = 2;
    const std::uint32_t nanBits = 0x7FC00000u;
    std::memcpy(nanBlock.data() + 4, &id, 2);
    std::memcpy(nanBlock.data() + 6, &type, 2);
    std::memcpy(nanBlock.data() + 8, &nanBits, 4);
    const auto sane = DecodeSettingsBlock(nanBlock);
    ASSERT_TRUE(sane.has_value());
    EXPECT_TRUE(std::isfinite(sane->heightScale));
}

// ---- bake ----

TEST(TerrainAssetTests, BakeMatchesTheGeneratorWithinTheQuantisationError)
{
    const BakedTerrain& baked = SharedBake();
    ASSERT_EQ(baked.chunks.size(), kCount * kCount);
    const TerrainHeightField field(baked.settings);

    float worstHeight = 0.0f;
    float worstStep = 0.0f;
    float worstNormal = 0.0f;
    float worstBoundsY = 0.0f;
    int colourDiffs = 0;
    int vertexCount = 0;
    for (int dz = 0; dz < static_cast<int>(kCount); ++dz) {
        for (int dx = 0; dx < static_cast<int>(kCount); ++dx) {
            const NodeKey key{ kFirst + dx, kFirst + dz, 0 };
            const NodeMesh ref = BuildNode(field, key);
            const auto got = BuildNodeFromBaked(baked, key);
            ASSERT_TRUE(got.has_value());
            ASSERT_EQ(got->vertices.size(), ref.vertices.size());
            const BakedChunk& chunk = *baked.FindChunk(key.x, key.z);
            const float step = (chunk.heightMax - chunk.heightMin) / 65535.0f;
            worstStep = std::max(worstStep, step);
            EXPECT_EQ(got->originX, ref.originX);
            EXPECT_EQ(got->originZ, ref.originZ);
            for (std::size_t i = 0; i < ref.vertices.size(); ++i) {
                const TerrainVertex& a = ref.vertices[i];
                const TerrainVertex& b = got->vertices[i];
                worstHeight = std::max(worstHeight, std::fabs(a.position[1] - b.position[1]));
                EXPECT_LE(std::fabs(a.position[1] - b.position[1]), step * 0.5f * 1.01f + 1e-4f);
                EXPECT_EQ(a.position[0], b.position[0]);
                EXPECT_EQ(a.position[2], b.position[2]);
                worstNormal = std::max(worstNormal, AngleDeg(a.normal, b.normal));
                colourDiffs += ref.colors[i] != got->colors[i] ? 1 : 0;
                ++vertexCount;
            }
            worstBoundsY = std::max({ worstBoundsY, std::fabs(ref.boundsMin.y - got->boundsMin.y), std::fabs(ref.boundsMax.y - got->boundsMax.y) });
            EXPECT_LE(std::fabs(ref.boundsMin.y - got->boundsMin.y), step * 0.5f * 1.01f + 1e-4f);
            EXPECT_LE(std::fabs(ref.boundsMax.y - got->boundsMax.y), step * 0.5f * 1.01f + 1e-4f);
        }
    }
    EXPECT_LT(worstNormal, 0.05f);
    // Quantised heights move colours only at ramp edges; the vast majority are identical.
    EXPECT_LT(colourDiffs, vertexCount / 10);
    std::printf("[terrain asset] max height error %.5f m (largest step %.5f m), max normal error %.4f deg, "
                "max bounds Y error %.5f m, colour differs on %d of %d vertices\n",
        worstHeight, worstStep, worstNormal, worstBoundsY, colourDiffs, vertexCount);
}

TEST(TerrainAssetTests, StoredBoundsAreConservativeAndTight)
{
    const BakedTerrain& baked = SharedBake();
    for (int dz = 0; dz < static_cast<int>(kCount); ++dz) {
        for (int dx = 0; dx < static_cast<int>(kCount); ++dx) {
            const NodeKey key{ kFirst + dx, kFirst + dz, 0 };
            const BakedChunk& chunk = *baked.FindChunk(key.x, key.z);
            const NodeMesh mesh = *BuildNodeFromBaked(baked, key);
            float lo = 1e30f;
            float hi = -1e30f;
            for (const TerrainVertex& v : mesh.vertices) {
                lo = std::min(lo, v.position[1]);
                hi = std::max(hi, v.position[1]);
                EXPECT_GE(static_cast<double>(chunk.bounds.min[0]), mesh.originX - 1e-3);
                EXPECT_LE(static_cast<double>(chunk.bounds.min[0]), mesh.originX + v.position[0]);
                EXPECT_GE(static_cast<double>(chunk.bounds.max[0]), mesh.originX + v.position[0]);
                EXPECT_GE(static_cast<double>(chunk.bounds.max[2]), mesh.originZ + v.position[2]);
            }
            // The Y range is exactly what the loader builds (skirt included): contains every vertex and touches both ends.
            EXPECT_EQ(chunk.bounds.min[1], lo);
            EXPECT_EQ(chunk.bounds.max[1], hi);
            EXPECT_EQ(chunk.bounds.min[1], mesh.boundsMin.y);
            EXPECT_EQ(chunk.bounds.max[1], mesh.boundsMax.y);
            EXPECT_LE(static_cast<double>(chunk.bounds.min[0]), mesh.originX);
            EXPECT_GE(static_cast<double>(chunk.bounds.max[0]), mesh.originX + mesh.size);
        }
    }
}

TEST(TerrainAssetTests, NeighbourChunksDisagreeOnlyByTheirQuantisationSteps)
{
    const BakedTerrain& baked = SharedBake();
    const int q = baked.settings.chunkQuads;
    const int verts = q + 1;
    float worstCrack = 0.0f;
    for (std::uint32_t dz = 0; dz < kCount; ++dz) {
        for (std::uint32_t dx = 0; dx + 1 < kCount; ++dx) {
            const BakedChunk& a = baked.chunks[dz * kCount + dx];
            const BakedChunk& b = baked.chunks[dz * kCount + dx + 1];
            const float limit = ((a.heightMax - a.heightMin) + (b.heightMax - b.heightMin)) / 65535.0f * 0.5f * 1.02f + 1e-4f;
            for (int iz = 0; iz < verts; ++iz) {
                const float ha = DequantizeHeight(a.heights[static_cast<std::size_t>(iz * verts + q)], a.heightMin, a.heightMax);
                const float hb = DequantizeHeight(b.heights[static_cast<std::size_t>(iz * verts)], b.heightMin, b.heightMax);
                EXPECT_LE(std::fabs(ha - hb), limit);
                worstCrack = std::max(worstCrack, std::fabs(ha - hb));
                // Stored normals come from the exact data, so shared border normals are equal.
                EXPECT_EQ(a.normals[static_cast<std::size_t>(iz * verts + q)], b.normals[static_cast<std::size_t>(iz * verts)]);
            }
        }
    }
    std::printf("[terrain asset] worst border mismatch between baked neighbours: %.5f m\n", worstCrack);
}

TEST(TerrainAssetTests, CoarserLevelsDecimateTheBakedGrid)
{
    const BakedTerrain& baked = SharedBake();
    const int q = baked.settings.chunkQuads;
    const int verts = q + 1;

    // Level 2 node (-1,-1) covers region chunks 0..3 in both axes.
    const NodeKey l2{ -1, -1, 2 };
    ASSERT_TRUE(BakedCoversNode(baked, l2));
    const auto mesh = BuildNodeFromBaked(baked, l2);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->vertices.size(), NodeVertexCount(q));
    for (const auto& ij : std::vector<std::pair<int, int>>{ { 0, 0 }, { 1, 0 }, { 16, 16 }, { 17, 3 }, { 64, 64 }, { 33, 64 }, { 0, 40 } }) {
        const int gx = ij.first * 4; // Level-0 vertex index inside the region.
        const int gz = ij.second * 4;
        const int cx = gx / q;
        const int cz = gz / q;
        const BakedChunk& c = baked.chunks[static_cast<std::size_t>(cz) * kCount + static_cast<std::size_t>(cx)];
        const std::size_t lx = static_cast<std::size_t>(gx - cx * q);
        const std::size_t lz = static_cast<std::size_t>(gz - cz * q);
        const float expected = DequantizeHeight(c.heights[lz * static_cast<std::size_t>(verts) + lx], c.heightMin, c.heightMax);
        const TerrainVertex& v = mesh->vertices[static_cast<std::size_t>(ij.second * verts + ij.first)];
        EXPECT_EQ(v.position[1], expected);
        const float len = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
        EXPECT_NEAR(len, 1.0f, 1e-4f);
        EXPECT_GT(v.normal[1], 0.0f);
    }
    EXPECT_FLOAT_EQ(mesh->size, static_cast<float>(MakeLayout(baked.settings).nodeSizeLevel0 * 4.0));

    // Level 1 inside the region works, any node that leaves the region does not.
    EXPECT_TRUE(BuildNodeFromBaked(baked, NodeKey{ -1, 0, 1 }).has_value());
    EXPECT_FALSE(BuildNodeFromBaked(baked, NodeKey{ -1, -1, 3 }).has_value()); // Level 3 grid is not aligned to this region.
    EXPECT_TRUE(BuildNodeFromBaked(baked, NodeKey{ 0, 0, 2 }).has_value());
    EXPECT_FALSE(BuildNodeFromBaked(baked, NodeKey{ 1, 0, 2 }).has_value());   // Covers chunks 4..7: outside.
    EXPECT_FALSE(BuildNodeFromBaked(baked, NodeKey{ 4, 0, 0 }).has_value());
    EXPECT_FALSE(BuildNodeFromBaked(baked, NodeKey{ -5, 0, 0 }).has_value());
}

TEST(TerrainAssetTests, MaterialSettingsStillRecolourBakedNodes)
{
    const BakedTerrain& baked = SharedBake();
    TerrainSettings other = baked.settings;
    other.snowMinHeightFraction = 0.05f;
    other = Sanitize(other);
    const NodeKey key{ 0, 0, 0 };
    const auto a = BuildNodeFromBaked(baked, key);
    const auto b = BuildNodeFromBaked(baked, key, &other);
    ASSERT_TRUE(a && b);
    EXPECT_NE(a->colors, b->colors);
    EXPECT_EQ(a->vertices[100].position[1], b->vertices[100].position[1]);
}

TEST(TerrainAssetTests, BakeReportsProgressSupportsCancelAndRejectsBadArguments)
{
    InlineTerrainExecutor exec;
    TerrainSettings s = TestSettings();
    s.erosionEnabled = false;

    std::vector<std::uint32_t> seen;
    const auto ok = BakeRegion(s, 0, 0, 3, 2, [&](std::uint32_t done, std::uint32_t total) {
        EXPECT_EQ(total, 6u);
        seen.push_back(done);
    }, nullptr, &exec);
    ASSERT_TRUE(ok.has_value());
    ASSERT_FALSE(seen.empty());
    EXPECT_EQ(seen.back(), 6u);
    EXPECT_TRUE(std::is_sorted(seen.begin(), seen.end()));

    std::atomic<bool> cancel{ false };
    const auto cancelled = BakeRegion(s, 0, 0, 4, 4, [&](std::uint32_t done, std::uint32_t) {
        if (done >= 2) {
            cancel.store(true);
        }
    }, &cancel, &exec);
    EXPECT_FALSE(cancelled.has_value());

    EXPECT_FALSE(BakeRegion(s, 0, 0, 0, 4, {}, nullptr, &exec).has_value());
    EXPECT_FALSE(BakeRegion(s, 0, 0, kTerrainAssetMaxAxisChunks + 1, 1, {}, nullptr, &exec).has_value());
    EXPECT_FALSE(BakeRegion(s, INT32_MAX, 0, 2, 1, {}, nullptr, &exec).has_value());
}

TEST(TerrainAssetTests, JobSystemBakeEqualsTheSerialBake)
{
    TerrainSettings s = TestSettings();
    s.erosionEnabled = false;
    InlineTerrainExecutor exec;
    const auto serial = BakeRegion(s, 5, 7, 3, 3, {}, nullptr, &exec);
    const auto parallel = BakeRegion(s, 5, 7, 3, 3);
    ASSERT_TRUE(serial && parallel);
    for (std::size_t i = 0; i < serial->chunks.size(); ++i) {
        EXPECT_EQ(serial->chunks[i].heights, parallel->chunks[i].heights);
        EXPECT_EQ(serial->chunks[i].normals, parallel->chunks[i].normals);
        EXPECT_EQ(serial->chunks[i].splat, parallel->chunks[i].splat);
    }
}

// ---- file ----

TEST(TerrainAssetTests, SaveLoadRoundTripAndContainerLayout)
{
    const BakedTerrain& baked = SharedBake();
    TempDir dir;
    const auto path = dir.path / "world.gta";
    std::string error;
    ASSERT_TRUE(SaveTerrainAsset(path, baked, &error)) << error;

    const auto bytes = ReadAll(path);
    ASSERT_GT(bytes.size(), 200u);
    EXPECT_EQ(std::memcmp(bytes.data(), kGtaMagic, 16), 0);
    EXPECT_EQ(GetAt<std::uint64_t>(bytes, 16), static_cast<std::uint64_t>(AssetType::Terrain));
    EXPECT_EQ(std::memcmp(bytes.data() + kMd, "TERR", 4), 0);
    EXPECT_EQ(GetAt<std::uint32_t>(bytes, kMd + 4), kTerrainAssetFormatVersion);
    const std::uint64_t payloadOffset = GetAt<std::uint64_t>(bytes, 56);
    EXPECT_EQ(GetAt<std::uint64_t>(bytes, kMd + 76), payloadOffset);
    EXPECT_EQ(GetAt<std::uint64_t>(bytes, kMd + 68), bytes.size() - payloadOffset);

    // The generic container readers understand it too.
    const auto header = ReadGtaHeader(path);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->Type(), AssetType::Terrain);
    const auto md = ReadGtaMetadataOnly(path);
    ASSERT_TRUE(md.has_value());
    EXPECT_EQ(std::memcmp(md->metadata.data(), "TERR", 4), 0);

    const auto loaded = LoadTerrainAsset(path, &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    EXPECT_TRUE(loaded->settings == baked.settings);
    EXPECT_EQ(loaded->firstX, baked.firstX);
    EXPECT_EQ(loaded->firstZ, baked.firstZ);
    ASSERT_EQ(loaded->chunks.size(), baked.chunks.size());
    for (std::size_t i = 0; i < baked.chunks.size(); ++i) {
        EXPECT_EQ(loaded->chunks[i].heights, baked.chunks[i].heights);
        EXPECT_EQ(loaded->chunks[i].normals, baked.chunks[i].normals);
        EXPECT_EQ(loaded->chunks[i].splat, baked.chunks[i].splat);
        EXPECT_EQ(loaded->chunks[i].heightMin, baked.chunks[i].heightMin);
        EXPECT_EQ(loaded->chunks[i].heightMax, baked.chunks[i].heightMax);
        EXPECT_EQ(std::memcmp(&loaded->chunks[i].bounds, &baked.chunks[i].bounds, sizeof(ChunkBounds)), 0);
    }

    // Overwrite works (atomic replace) and leaves no temp file behind.
    ASSERT_TRUE(SaveTerrainAsset(path, baked, &error)) << error;
    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir.path)) {
        (void)e;
        ++files;
    }
    EXPECT_EQ(files, 1);

    std::printf("[terrain asset] %u chunks -> %zu bytes (%.1f KB per chunk)\n", kCount * kCount, bytes.size(),
        static_cast<double>(bytes.size()) / (kCount * kCount) / 1024.0);
}

TEST(TerrainAssetTests, AssetDatabaseIndexesTheTerrainFile)
{
    const BakedTerrain& baked = SharedBake();
    TempDir dir;
    const auto path = dir.path / "sub" / "world.gta";
    ASSERT_TRUE(SaveTerrainAsset(path, baked));
    AssetDatabase db;
    db.RefreshFromDirectory(dir.path);
    const AssetRecord* record = db.FindByPath(path);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->type, AssetType::Terrain);
}

TEST(TerrainAssetTests, ReaderLoadsBoundsFirstAndSingleChunksBySeeking)
{
    const BakedTerrain& baked = SharedBake();
    TempDir dir;
    const auto path = dir.path / "world.gta";
    ASSERT_TRUE(SaveTerrainAsset(path, baked));

    std::string error;
    auto reader = TerrainAssetReader::Open(path, &error);
    ASSERT_TRUE(reader.has_value()) << error;
    const TerrainAssetInfo& info = reader->Info();
    EXPECT_EQ(info.formatVersion, 1u);
    EXPECT_EQ(info.chunkQuads, 64u);
    EXPECT_EQ(info.gridVerticesPerSide, 65u);
    EXPECT_EQ(info.countX, kCount);
    EXPECT_EQ(info.firstX, kFirst);
    EXPECT_TRUE(info.settings == baked.settings);
    ASSERT_EQ(reader->Bounds().size(), baked.chunks.size());
    for (std::size_t i = 0; i < baked.chunks.size(); ++i) {
        EXPECT_EQ(std::memcmp(&reader->Bounds()[i], &baked.chunks[i].bounds, sizeof(ChunkBounds)), 0);
        EXPECT_GE(reader->Table()[i].offset, 48u * baked.chunks.size());
    }
    // World bounds in the header cover every chunk.
    for (const ChunkBounds& b : reader->Bounds()) {
        for (int k = 0; k < 3; ++k) {
            EXPECT_LE(info.worldMin[k], b.min[k]);
            EXPECT_GE(info.worldMax[k], b.max[k]);
        }
    }

    // Out of order reads.
    for (const std::uint32_t index : { 13u, 0u, 41u, 63u, 41u }) {
        BakedChunk c;
        ASSERT_TRUE(reader->ReadChunk(index, c, &error)) << error;
        EXPECT_EQ(c.heights, baked.chunks[index].heights);
        EXPECT_EQ(c.splat, baked.chunks[index].splat);
    }
    BakedChunk c;
    EXPECT_FALSE(reader->ReadChunk(static_cast<std::uint32_t>(baked.chunks.size()), c, &error));
}

TEST(TerrainAssetTests, RejectsMissingWrongAndHostileFiles)
{
    const BakedTerrain& baked = SharedBake();
    TempDir dir;
    const auto good = dir.path / "good.gta";
    ASSERT_TRUE(SaveTerrainAsset(good, baked));
    const std::vector<std::uint8_t> bytes = ReadAll(good);
    const std::size_t n = baked.chunks.size();
    const std::size_t payloadOffset = static_cast<std::size_t>(GetAt<std::uint64_t>(bytes, 56));
    const std::size_t tableBytes = n * 48;
    std::string error;

    const auto expectFail = [&](const std::vector<std::uint8_t>& file, const char* what) {
        const auto p = dir.path / "bad.gta";
        WriteAll(p, file);
        std::string err;
        EXPECT_FALSE(LoadTerrainAsset(p, &err).has_value()) << what;
        EXPECT_FALSE(err.empty()) << what;
    };

    EXPECT_FALSE(LoadTerrainAsset(dir.path / "missing.gta", &error).has_value());
    EXPECT_FALSE(error.empty());

    // Truncations: empty, inside the header, inside the metadata, inside the tables, inside the last block, one byte short.
    for (const std::size_t cut : { std::size_t{ 0 }, std::size_t{ 10 }, std::size_t{ 63 }, std::size_t{ 100 }, payloadOffset - 1,
             payloadOffset + 100, payloadOffset + tableBytes + 5, bytes.size() - 1 }) {
        expectFail(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)), "truncated");
    }

    std::vector<std::uint8_t> f = bytes;
    f[0] = 'X';
    expectFail(f, "container magic");

    f = bytes;
    PutAt<std::uint64_t>(f, 16, static_cast<std::uint64_t>(AssetType::Mesh));
    expectFail(f, "asset type");

    f = bytes;
    PutAt<std::uint64_t>(f, 48, 1); // Compressed flag.
    expectFail(f, "container flags");

    f = bytes;
    f[kMd] = 'Q';
    FixMetadataCrc(f);
    expectFail(f, "TERR magic");

    f = bytes;
    PutAt<std::uint32_t>(f, kMd + 4, 2);
    FixMetadataCrc(f);
    expectFail(f, "format version");

    f = bytes;
    PutAt<std::uint32_t>(f, kMd + 4, 0);
    FixMetadataCrc(f);
    expectFail(f, "format version 0");

    f = bytes;
    PutAt<std::uint32_t>(f, kMd + 32, 0xFFFFFFFFu); // countX
    FixMetadataCrc(f);
    expectFail(f, "huge chunk count");

    f = bytes;
    PutAt<std::uint32_t>(f, kMd + 32, 4096);
    PutAt<std::uint32_t>(f, kMd + 36, 4096);
    FixMetadataCrc(f);
    expectFail(f, "too many chunks");

    f = bytes;
    PutAt<std::uint32_t>(f, kMd + 16, 63); // chunkQuads
    FixMetadataCrc(f);
    expectFail(f, "bad chunk size");

    f = bytes;
    PutAt<std::uint32_t>(f, kMd + 24, 0x7FFFFFFFu); // firstX
    FixMetadataCrc(f);
    expectFail(f, "range overflow");

    f = bytes;
    PutAt<std::uint64_t>(f, kMd + 68, ~0ull); // payloadSize
    FixMetadataCrc(f);
    expectFail(f, "payload size");

    f = bytes;
    PutAt<std::uint64_t>(f, kMd + 76, 64); // payloadOffset
    FixMetadataCrc(f);
    expectFail(f, "payload offset");

    f = bytes;
    PutAt<std::uint32_t>(f, kMd + 88, 0xFFFFFFF0u); // settings block size
    FixMetadataCrc(f);
    expectFail(f, "settings size");

    f = bytes;
    PutAt<float>(f, kMd + 40, std::numeric_limits<float>::quiet_NaN()); // lod0Spacing
    FixMetadataCrc(f);
    expectFail(f, "nan spacing");

    f = bytes;
    f[kMd + 50] ^= 0x40; // Metadata bit flip without a CRC fix.
    expectFail(f, "metadata crc");

    f = bytes;
    f[payloadOffset + 3] ^= 0x01; // Table bit flip.
    expectFail(f, "table crc");

    // A chunk table row that points outside the payload, with all CRCs repaired.
    f = bytes;
    PutAt<std::uint64_t>(f, payloadOffset + n * 24, 0xFFFFFFFFFFFFFFF0ull);
    FixTableCrc(f, tableBytes);
    expectFail(f, "chunk offset");

    f = bytes;
    PutAt<std::uint32_t>(f, payloadOffset + n * 24 + 8, 0xFFFFFFFFu); // chunk size
    FixTableCrc(f, tableBytes);
    expectFail(f, "chunk size");

    f = bytes;
    PutAt<float>(f, payloadOffset + n * 24 + 16, 10.0f); // heightMin
    PutAt<float>(f, payloadOffset + n * 24 + 20, -10.0f);
    FixTableCrc(f, tableBytes);
    expectFail(f, "inverted height range");

    f = bytes;
    PutAt<float>(f, payloadOffset + 0, std::numeric_limits<float>::infinity()); // bounds
    FixTableCrc(f, tableBytes);
    expectFail(f, "infinite bounds");

    // A damaged block is found by its own CRC: the tables still open, the full load fails.
    f = bytes;
    f[payloadOffset + tableBytes + 1000] ^= 0x80;
    WriteAll(dir.path / "block.gta", f);
    auto reader = TerrainAssetReader::Open(dir.path / "block.gta", &error);
    ASSERT_TRUE(reader.has_value()) << error;
    BakedChunk c;
    EXPECT_FALSE(reader->ReadChunk(0, c, &error));
    EXPECT_NE(error.find("CRC"), std::string::npos);
    EXPECT_TRUE(reader->ReadChunk(1, c, &error)) << error;
    EXPECT_FALSE(LoadTerrainAsset(dir.path / "block.gta", &error).has_value());
}

TEST(TerrainAssetTests, RandomCorruptionNeverCrashes)
{
    const BakedTerrain& baked = SharedBake();
    TempDir dir;
    const auto good = dir.path / "good.gta";
    ASSERT_TRUE(SaveTerrainAsset(good, baked));
    const std::vector<std::uint8_t> bytes = ReadAll(good);
    const std::size_t head = static_cast<std::size_t>(GetAt<std::uint64_t>(bytes, 56)) + baked.chunks.size() * 48 + 64;
    std::mt19937 rng(5);
    int accepted = 0;
    for (int i = 0; i < 300; ++i) {
        std::vector<std::uint8_t> f = bytes;
        const int flips = 1 + static_cast<int>(rng() % 4);
        for (int k = 0; k < flips; ++k) {
            f[rng() % head] = static_cast<std::uint8_t>(rng());
        }
        WriteAll(dir.path / "fuzz.gta", f);
        accepted += TerrainAssetReader::Open(dir.path / "fuzz.gta").has_value() ? 1 : 0;
    }
    // Only the GUID, which no check covers, can change without being noticed.
    EXPECT_LT(accepted, 20);
}

// ---- streamer provider ----

TEST(TerrainAssetTests, StreamerServesBakedNodesAndFallsBackToTheGenerator)
{
    const auto baked = std::make_shared<const BakedTerrain>(SharedBake());
    InlineTerrainExecutor exec;
    TerrainStreamer streamer(baked->settings, {}, &exec);
    streamer.SetNodeProvider(MakeBakedNodeProvider(baked));

    const NodeKey inside{ 0, 0, 0 };
    const NodeKey outside{ 20, 20, 0 };
    const std::vector<WantedNode> wanted = { WantedNode{ inside, 10.0, true }, WantedNode{ outside, 20.0, true } };
    std::uint64_t frame = 0;
    StreamerUpdate update;
    for (int i = 0; i < 6; ++i) {
        streamer.Update(wanted, ++frame, frame / 60.0, update);
    }
    const auto a = streamer.Find(inside);
    const auto b = streamer.Find(outside);
    ASSERT_TRUE(a && b);
    const NodeMesh bakedRef = *BuildNodeFromBaked(*baked, inside);
    const NodeMesh generated = BuildNode(streamer.Field(), outside);
    EXPECT_EQ(a->vertices[500].position[1], bakedRef.vertices[500].position[1]);
    EXPECT_EQ(b->vertices[500].position[1], generated.vertices[500].position[1]);

    // A height change makes the provider step aside: the streamer builds the node itself again.
    TerrainSettings changed = baked->settings;
    changed.seed += 1;
    streamer.SetSettings(changed);
    for (int i = 0; i < 6; ++i) {
        streamer.Update(wanted, ++frame, frame / 60.0, update);
    }
    const auto c = streamer.Find(inside);
    ASSERT_TRUE(c);
    const NodeMesh regenerated = BuildNode(streamer.Field(), inside);
    EXPECT_EQ(c->vertices[500].position[1], regenerated.vertices[500].position[1]);
    EXPECT_NE(c->vertices[500].position[1], bakedRef.vertices[500].position[1]);

    // Removing the provider also works and drops resident nodes.
    streamer.SetNodeProvider({});
    EXPECT_EQ(streamer.Find(inside), nullptr);
}

// ---- manual benchmark ----

TEST(TerrainAssetBench, DISABLED_Bake32x32SaveAndLoad)
{
    TerrainSettings s = TestSettings();
    const std::uint32_t count = 32;
    const TempDir dir;

    // Cost depends on the region: ocean nodes skip erosion and detail, mountains pay for every layer.
    const std::int32_t origins[3][2] = { { 0, 0 }, { -96, 32 }, { 40, -80 } };
    for (const auto& o : origins) {
        const auto t0 = Clock::now();
        const auto b = BakeRegion(s, o[0], o[1], count, count);
        const double ms = MsSince(t0);
        ASSERT_TRUE(b.has_value());
        int ocean = 0;
        float hi = -1e30f;
        for (const BakedChunk& c : b->chunks) {
            ocean += c.heightMax < 0.0f ? 1 : 0;
            hi = std::max(hi, c.heightMax);
        }
        std::printf("[bench] bake 32x32 at (%d,%d), job system: %.1f ms total, %.3f ms per node, %d ocean-only nodes, highest %.0f m\n",
            o[0], o[1], ms, ms / (count * count), ocean, hi);
    }

    auto t = Clock::now();
    std::uint32_t last = 0;
    const auto baked = BakeRegion(s, -96, 32, count, count, [&](std::uint32_t done, std::uint32_t) { last = done; });
    const double bakeMs = MsSince(t);
    ASSERT_TRUE(baked.has_value());
    std::printf("[bench] bake again for the file tests: %.1f ms, progress ended at %u\n", bakeMs, last);

    const auto path = dir.path / "world.gta";
    std::string error;
    t = Clock::now();
    ASSERT_TRUE(SaveTerrainAsset(path, *baked, &error)) << error;
    const double saveMs = MsSince(t);
    const auto size = std::filesystem::file_size(path);
    std::printf("[bench] save: %.1f ms, file %.2f MB (%.1f KB per node)\n", saveMs, static_cast<double>(size) / 1048576.0,
        static_cast<double>(size) / (count * count) / 1024.0);

    t = Clock::now();
    auto reader = TerrainAssetReader::Open(path, &error);
    ASSERT_TRUE(reader.has_value()) << error;
    std::printf("[bench] open (header, bounds table, chunk table, CRCs): %.3f ms\n", MsSince(t));

    t = Clock::now();
    BakedChunk one;
    for (std::uint32_t i = 0; i < 64; ++i) {
        ASSERT_TRUE(reader->ReadChunk(i * 16 % (count * count), one, &error)) << error;
    }
    std::printf("[bench] seek-read one chunk: %.3f ms average over 64\n", MsSince(t) / 64.0);

    t = Clock::now();
    const auto loaded = LoadTerrainAsset(path, &error);
    ASSERT_TRUE(loaded.has_value()) << error;
    std::printf("[bench] full load with all CRCs: %.1f ms\n", MsSince(t));

    const std::int32_t fx = loaded->firstX;
    const std::int32_t fz = loaded->firstZ;
    t = Clock::now();
    NodeMesh keep;
    for (std::uint32_t i = 0; i < 256; ++i) {
        keep = *BuildNodeFromBaked(*loaded, NodeKey{ fx + static_cast<std::int32_t>(i % count), fz + static_cast<std::int32_t>(i / count), 0 });
    }
    std::printf("[bench] BuildNodeFromBaked level 0: %.3f ms per node\n", MsSince(t) / 256.0);
    t = Clock::now();
    for (int i = 0; i < 16; ++i) {
        keep = *BuildNodeFromBaked(*loaded, NodeKey{ fx / 4 + i % 8, fz / 4 + i / 8, 2 });
    }
    std::printf("[bench] BuildNodeFromBaked level 2: %.3f ms per node\n", MsSince(t) / 16.0);

    const TerrainHeightField field(loaded->settings);
    t = Clock::now();
    for (std::uint32_t i = 0; i < 64; ++i) {
        keep = BuildNode(field, NodeKey{ fx + static_cast<std::int32_t>(i % count), fz + static_cast<std::int32_t>(i / count), 0 });
    }
    std::printf("[bench] BuildNode level 0 (generator, one thread): %.3f ms per node\n", MsSince(t) / 64.0);

    std::vector<std::byte> blob(64u << 20);
    for (std::size_t i = 0; i < blob.size(); i += 4099) {
        blob[i] = static_cast<std::byte>(i);
    }
    t = Clock::now();
    const std::uint32_t fast = FastCrc32(blob);
    const double fastMs = MsSince(t);
    t = Clock::now();
    const std::uint32_t slow = Crc32(std::span<const std::byte>(blob.data(), 8u << 20));
    const double slowMs = MsSince(t) * 8.0;
    const std::uint32_t fastPart = FastCrc32(std::span<const std::byte>(blob.data(), 8u << 20));
    std::printf("[bench] CRC-32 of 64 MB: FastCrc32 %.1f ms (%.0f MB/s), gte::Crc32 about %.1f ms (%.0f MB/s), match=%d (%08x)\n",
        fastMs, 64000.0 / fastMs, slowMs, 64000.0 / slowMs, slow == fastPart ? 1 : 0, fast);
}

} // namespace gte::terrain
