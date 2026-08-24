// Unit tests for src/Assets/PmxLoader.h - LoadPmxModel()'s extraction of
// vertex positions/normals/UVs/triangle indices out of a MikuMikuDance .pmx
// model file. Touches a real temp directory (created/torn down by the
// fixture below, same convention as AssetImporterTests.cpp) but no GPU/SDL/
// ImGui at all - "Tier 1" per tests/CMakeLists.txt's own taxonomy. Always
// built - src/Assets/ has no GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL
// dependency.

#include "Assets/PmxLoader.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Hand-built, byte-precise minimal .pmx file: one triangle, no bones/
// morphs/materials/textures/rigid bodies/joints - just enough of the binary
// format (see third_party/saba/src/Saba/Model/MMD/PMXFile.cpp's ReadPMXFile
// for the exact field-by-field read order this must match) to exercise
// LoadPmxModel()'s real parsing path without depending on a large external
// model file. Same "construct the exact binary format by hand" approach as
// GtaFileTests.cpp/Ktx2EncoderTests.cpp's BuildMinimal2x2Bmp().
class PmxByteWriter {
public:
    void U8(std::uint8_t v) { m_bytes.push_back(v); }

    void U32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            m_bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    void I32(std::int32_t v) { U32(static_cast<std::uint32_t>(v)); }

    void F32(float v)
    {
        static_assert(sizeof(float) == 4, "expected 32-bit float");
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }

    const std::vector<std::uint8_t>& Bytes() const { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
};

// Builds one BDEF1-weighted vertex: position, normal, uv, (zero addUV
// entries), weight type (0 == BDEF1), one 1-byte bone index, edge magnitude.
void WriteVertex(PmxByteWriter& w, float px, float py, float pz, float nx, float ny, float nz, float u, float v)
{
    w.F32(px); w.F32(py); w.F32(pz);
    w.F32(nx); w.F32(ny); w.F32(nz);
    w.F32(u); w.F32(v);
    w.U8(0); // PMXVertexWeight::BDEF1
    w.U8(0); // bone index (boneIndexSize == 1 below), unused by LoadPmxModel()
    w.F32(0.0f); // edge magnitude
}

std::vector<std::uint8_t> BuildMinimalTrianglePmx()
{
    PmxByteWriter w;

    // --- Header ---
    w.U8('P'); w.U8('M'); w.U8('X'); w.U8(' '); // magic (4 bytes, MMDFileString<4> only reads Size bytes)
    w.F32(2.0f); // version
    w.U8(8); // dataSize (read but never used by the parser)
    w.U8(1); // encode: 1 == UTF-8
    w.U8(0); // addUVNum
    w.U8(1); // vertexIndexSize
    w.U8(1); // textureIndexSize
    w.U8(1); // materialIndexSize
    w.U8(1); // boneIndexSize
    w.U8(1); // morphIndexSize
    w.U8(1); // rigidbodyIndexSize

    // --- Info --- (modelName/englishModelName/comment/englishComment, all empty)
    w.U32(0); w.U32(0); w.U32(0); w.U32(0);

    // --- Vertices ---
    w.I32(3);
    WriteVertex(w, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    WriteVertex(w, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    WriteVertex(w, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);

    // --- Faces --- (raw index count, ReadFace() divides by 3 for face count)
    w.I32(3);
    w.U8(0); w.U8(1); w.U8(2);

    // --- Textures / Materials / Bones / Morphs / DisplayFrames / Rigidbodies / Joints ---
    // (all empty - the file ends exactly here, so ReadPMXFile()'s trailing
    // "is there a softbody section?" Tell() < GetSize() check is false and
    // ReadSoftbody() is correctly skipped)
    w.I32(0); // textures
    w.I32(0); // materials
    w.I32(0); // bones
    w.I32(0); // morphs
    w.I32(0); // display frames
    w.I32(0); // rigidbodies
    w.I32(0); // joints

    return w.Bytes();
}

class PmxLoaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GtePmxLoaderTest_") + info->test_suite_name() + "_" + info->name());

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    static void WriteBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    std::filesystem::path m_root;
};

TEST_F(PmxLoaderTest, ExtractsPositionsNormalsUvsAndIndicesFromAMinimalTriangle)
{
    const std::filesystem::path path = m_root / "triangle.pmx";
    WriteBinaryFile(path, BuildMinimalTrianglePmx());

    const PmxLoadResult result = LoadPmxModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.mesh.positions.size(), 3u);
    ASSERT_EQ(result.mesh.normals.size(), 3u);
    ASSERT_EQ(result.mesh.uvs.size(), 3u);
    ASSERT_EQ(result.mesh.indices.size(), 3u);

    EXPECT_EQ(result.mesh.positions[0], Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(result.mesh.positions[1], Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(result.mesh.positions[2], Vec3(0.0f, 1.0f, 0.0f));

    for (const auto& normal : result.mesh.normals) {
        EXPECT_EQ(normal, Vec3(0.0f, 0.0f, 1.0f));
    }

    EXPECT_EQ(result.mesh.uvs[0], Vec2(0.0f, 0.0f));
    EXPECT_EQ(result.mesh.uvs[1], Vec2(1.0f, 0.0f));
    EXPECT_EQ(result.mesh.uvs[2], Vec2(0.0f, 1.0f));

    EXPECT_EQ(result.mesh.indices[0], 0u);
    EXPECT_EQ(result.mesh.indices[1], 1u);
    EXPECT_EQ(result.mesh.indices[2], 2u);

    EXPECT_FALSE(result.message.empty());
}

TEST_F(PmxLoaderTest, FailsGracefullyWhenFileDoesNotExist)
{
    const PmxLoadResult result = LoadPmxModel((m_root / "DoesNotExist.pmx").string());

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.mesh.positions.empty());
    EXPECT_TRUE(result.mesh.normals.empty());
    EXPECT_TRUE(result.mesh.uvs.empty());
    EXPECT_TRUE(result.mesh.indices.empty());
    EXPECT_FALSE(result.message.empty());
}

TEST_F(PmxLoaderTest, FailsGracefullyOnATruncatedFile)
{
    std::vector<std::uint8_t> bytes = BuildMinimalTrianglePmx();
    bytes.resize(bytes.size() / 2); // Cut off mid-way through the vertex section.

    const std::filesystem::path path = m_root / "truncated.pmx";
    WriteBinaryFile(path, bytes);

    const PmxLoadResult result = LoadPmxModel(path.string());
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
}

// Optional real-world smoke test: if the MMD test model this integration was
// verified against (see AGENTS.md-style task notes for this session) is
// present on THIS machine, actually parse it end-to-end and sanity-check the
// extracted counts - a genuine, non-synthetic .pmx file exercising every
// section BuildMinimalTrianglePmx() above deliberately leaves empty
// (materials/bones/morphs/textures/...). Never a hard requirement: the
// model is a large third-party asset that isn't (and shouldn't be) vendored
// into this repo, so this GTEST_SKIP()s cleanly on every other machine/CI
// runner instead of failing. Finds any *.pmx file inside the fixture
// directory via directory_iterator (rather than a hardcoded exact filename)
// deliberately - the real fixture's filename contains non-ASCII (Japanese)
// characters, and matching by extension sidesteps any narrow-string-literal
// source-encoding pitfalls entirely.
TEST(PmxLoaderRealModelSmokeTest, LoadsAnMmdModelIfPresentOnThisMachine)
{
    const std::filesystem::path directory = "C:\\Users\\F5954\\Documents\\TAMANA\\MMD_Model_Furina";

    std::error_code ec;
    if (!std::filesystem::exists(directory, ec) || !std::filesystem::is_directory(directory, ec)) {
        GTEST_SKIP() << "MMD test model directory not present on this machine - skipping.";
    }

    std::filesystem::path pmxPath;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".pmx") {
            pmxPath = entry.path();
            break;
        }
    }

    if (pmxPath.empty()) {
        GTEST_SKIP() << "No .pmx file found in the MMD test model directory - skipping.";
    }

    const PmxLoadResult result = LoadPmxModel(pmxPath.string());

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_GT(result.mesh.positions.size(), 0u);
    EXPECT_EQ(result.mesh.positions.size(), result.mesh.normals.size());
    EXPECT_EQ(result.mesh.positions.size(), result.mesh.uvs.size());
    EXPECT_GT(result.mesh.indices.size(), 0u);
    EXPECT_EQ(result.mesh.indices.size() % 3, 0u); // Always a whole number of triangles.

    // Informational only (not an assertion) - makes the real vertex/
    // triangle counts visible in the test log when this actually runs.
    std::cout << "[PmxLoaderRealModelSmokeTest] " << pmxPath.string() << ": "
              << result.mesh.positions.size() << " vertices, "
              << (result.mesh.indices.size() / 3) << " triangles\n";
}

} // namespace
} // namespace gte
