// Unit tests for src/Assets/StlLoader.h - LoadStlModel()'s parsing of both
// the binary and ASCII STL variants into a MeshData, including this file's
// own hardening requirements (corrupt/truncated/oversized-claim input,
// non-finite normal/position handling). Touches a real temp directory
// (created/torn down by the fixture below, same convention as
// PmxLoaderTests.cpp) but no GPU/SDL/ImGui at all - "Tier 1" per
// tests/CMakeLists.txt's own taxonomy. Always built - src/Assets/ has no
// GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL dependency.

#include "Assets/StlLoader.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Hand-built, byte-precise binary STL fixtures - mirrors PmxByteWriter's own
// shape (see tests/Assets/PmxLoaderTests.cpp) for the same "construct the
// exact binary format by hand" approach.
class StlByteWriter {
public:
    void U8(std::uint8_t v) { m_bytes.push_back(v); }

    void U16(std::uint16_t v)
    {
        m_bytes.push_back(static_cast<std::uint8_t>(v & 0xFF));
        m_bytes.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    }

    void U32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            m_bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    void F32(float v)
    {
        static_assert(sizeof(float) == 4, "expected 32-bit float");
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }

    void Vec3F(float x, float y, float z) { F32(x); F32(y); F32(z); }

    // Raw bytes, e.g. for a bit-pattern that isn't representable via a
    // plain float literal (NaN/Infinity).
    void RawF32Bits(std::uint32_t bits) { U32(bits); }

    // Pads with zero bytes until the buffer is exactly `count` bytes long -
    // used to build the 80-byte free-form header.
    void PadTo(std::size_t count)
    {
        while (m_bytes.size() < count) {
            m_bytes.push_back(0);
        }
    }

    const std::vector<std::uint8_t>& Bytes() const { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
};

// Appends one full 50-byte binary STL triangle record: normal + v0/v1/v2 +
// a 2-byte attribute count (always 0, never interpreted).
void WriteBinaryTriangle(StlByteWriter& w, const Vec3& normal, const Vec3& v0, const Vec3& v1, const Vec3& v2)
{
    w.Vec3F(normal.x, normal.y, normal.z);
    w.Vec3F(v0.x, v0.y, v0.z);
    w.Vec3F(v1.x, v1.y, v1.z);
    w.Vec3F(v2.x, v2.y, v2.z);
    w.U16(0); // attribute byte count - always ignored
}

std::vector<std::uint8_t> BuildBinaryStl(std::uint32_t declaredTriangleCount,
    const std::vector<std::array<Vec3, 4>>& triangles)
{
    StlByteWriter w;
    w.PadTo(80); // 80-byte free-form header, never interpreted.
    w.U32(declaredTriangleCount);
    for (const auto& tri : triangles) {
        WriteBinaryTriangle(w, tri[0], tri[1], tri[2], tri[3]);
    }
    return w.Bytes();
}

constexpr std::uint32_t kNanBits = 0x7FC00000u;
constexpr std::uint32_t kPositiveInfinityBits = 0x7F800000u;

class StlLoaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteStlLoaderTest_") + info->test_suite_name() + "_" + info->name());

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

    static void WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    std::filesystem::path m_root;
};

TEST_F(StlLoaderTest, ParsesAMinimalOneTriangleBinaryStl)
{
    const Vec3 normal(0.0f, 0.0f, 1.0f);
    const Vec3 v0(0.0f, 0.0f, 0.0f);
    const Vec3 v1(1.0f, 0.0f, 0.0f);
    const Vec3 v2(0.0f, 1.0f, 0.0f);

    std::vector<std::array<Vec3, 4>> triangles = { { normal, v0, v1, v2 } };
    const auto bytes = BuildBinaryStl(1, triangles);

    const std::filesystem::path path = m_root / "one_triangle.stl";
    WriteBinaryFile(path, bytes);

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.wasBinaryFormat);
    ASSERT_EQ(result.mesh.positions.size(), 3u);
    ASSERT_EQ(result.mesh.normals.size(), 3u);
    ASSERT_EQ(result.mesh.uvs.size(), 3u);
    ASSERT_EQ(result.mesh.indices.size(), 3u);

    EXPECT_EQ(result.mesh.positions[0], v0);
    EXPECT_EQ(result.mesh.positions[1], v1);
    EXPECT_EQ(result.mesh.positions[2], v2);

    for (const auto& n : result.mesh.normals) {
        EXPECT_EQ(n, normal);
    }
    for (const auto& uv : result.mesh.uvs) {
        EXPECT_EQ(uv, Vec2::Zero());
    }

    EXPECT_EQ(result.mesh.indices[0], 0u);
    EXPECT_EQ(result.mesh.indices[1], 1u);
    EXPECT_EQ(result.mesh.indices[2], 2u);
    EXPECT_FALSE(result.message.empty());
}

TEST_F(StlLoaderTest, RecomputesADegenerateStoredNormalFromVertexWinding)
{
    const Vec3 v0(0.0f, 0.0f, 0.0f);
    const Vec3 v1(1.0f, 0.0f, 0.0f);
    const Vec3 v2(0.0f, 1.0f, 0.0f);

    std::vector<std::array<Vec3, 4>> triangles = { { Vec3(0.0f, 0.0f, 0.0f), v0, v1, v2 } };
    const auto bytes = BuildBinaryStl(1, triangles);

    const std::filesystem::path path = m_root / "degenerate_normal.stl";
    WriteBinaryFile(path, bytes);

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.mesh.normals.size(), 3u);

    const Vec3 expected = Normalize(Cross(v1 - v0, v2 - v0));
    for (const auto& n : result.mesh.normals) {
        EXPECT_TRUE(ApproximatelyEqual(n, expected));
    }
}

TEST_F(StlLoaderTest, ParsesMultipleTrianglesWithNonSharedVertices)
{
    const Vec3 normal(0.0f, 0.0f, 1.0f);
    // Two triangles sharing an edge (v1, v2 of triangle 0 == v0, v1 of triangle 1).
    const Vec3 a0(0.0f, 0.0f, 0.0f);
    const Vec3 a1(1.0f, 0.0f, 0.0f);
    const Vec3 a2(0.0f, 1.0f, 0.0f);
    const Vec3 b2(1.0f, 1.0f, 0.0f);

    std::vector<std::array<Vec3, 4>> triangles = {
        { normal, a0, a1, a2 },
        { normal, a1, b2, a2 },
    };
    const auto bytes = BuildBinaryStl(2, triangles);

    const std::filesystem::path path = m_root / "two_triangles.stl";
    WriteBinaryFile(path, bytes);

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.mesh.positions.size(), 6u); // NOT welded down to 4.
    ASSERT_EQ(result.mesh.indices.size(), 6u);
    const std::vector<std::uint32_t> expectedIndices = { 0, 1, 2, 3, 4, 5 };
    EXPECT_EQ(result.mesh.indices, expectedIndices);
}

TEST_F(StlLoaderTest, ParsesAWellFormedEmptyBinaryStlWithZeroTriangles)
{
    const auto bytes = BuildBinaryStl(0, {});
    ASSERT_EQ(bytes.size(), 84u);

    const std::filesystem::path path = m_root / "empty.stl";
    WriteBinaryFile(path, bytes);

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.wasBinaryFormat);
    EXPECT_TRUE(result.mesh.positions.empty());
    EXPECT_TRUE(result.mesh.normals.empty());
    EXPECT_TRUE(result.mesh.uvs.empty());
    EXPECT_TRUE(result.mesh.indices.empty());
}

TEST_F(StlLoaderTest, FailsGracefullyWhenBinaryTriangleCountDoesNotMatchFileSize)
{
    const Vec3 normal(0.0f, 0.0f, 1.0f);
    const Vec3 v0(0.0f, 0.0f, 0.0f);
    const Vec3 v1(1.0f, 0.0f, 0.0f);
    const Vec3 v2(0.0f, 1.0f, 0.0f);

    std::vector<std::array<Vec3, 4>> triangles = { { normal, v0, v1, v2 } };
    // Real file is only long enough for 1 triangle (134 bytes), but the
    // header claims 1000.
    const auto bytes = BuildBinaryStl(1000, triangles);

    const std::filesystem::path path = m_root / "mismatched_count.stl";
    WriteBinaryFile(path, bytes);

    const StlLoadResult result = LoadStlModel(path.string());

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.mesh.positions.empty());
}

TEST_F(StlLoaderTest, FailsGracefullyWhenBinaryTriangleCountWouldOverflow)
{
    const Vec3 normal(0.0f, 0.0f, 1.0f);
    const Vec3 v0(0.0f, 0.0f, 0.0f);
    const Vec3 v1(1.0f, 0.0f, 0.0f);
    const Vec3 v2(0.0f, 1.0f, 0.0f);

    std::vector<std::array<Vec3, 4>> triangles = { { normal, v0, v1, v2 } };
    const auto bytes = BuildBinaryStl(0xFFFFFFFFu, triangles);

    const std::filesystem::path path = m_root / "overflow_count.stl";
    WriteBinaryFile(path, bytes);

    const StlLoadResult result = LoadStlModel(path.string());

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.mesh.positions.empty());
}

TEST_F(StlLoaderTest, RecomputesANonFiniteStoredBinaryNormalFromVertexWinding)
{
    const Vec3 v0(0.0f, 0.0f, 0.0f);
    const Vec3 v1(1.0f, 0.0f, 0.0f);
    const Vec3 v2(0.0f, 1.0f, 0.0f);

    StlByteWriter w;
    w.PadTo(80);
    w.U32(1);
    w.RawF32Bits(kNanBits); // normal.x == NaN
    w.F32(0.0f); // normal.y
    w.F32(0.0f); // normal.z
    w.Vec3F(v0.x, v0.y, v0.z);
    w.Vec3F(v1.x, v1.y, v1.z);
    w.Vec3F(v2.x, v2.y, v2.z);
    w.U16(0);

    const std::filesystem::path path = m_root / "nan_normal.stl";
    WriteBinaryFile(path, w.Bytes());

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.mesh.normals.size(), 3u);

    const Vec3 expected = Normalize(Cross(v1 - v0, v2 - v0));
    for (const auto& n : result.mesh.normals) {
        EXPECT_TRUE(ApproximatelyEqual(n, expected));
    }
}

TEST_F(StlLoaderTest, FailsGracefullyOnABinaryFileWithANonFiniteVertexPosition)
{
    StlByteWriter w;
    w.PadTo(80);
    w.U32(1);
    w.Vec3F(0.0f, 0.0f, 1.0f); // normal
    w.RawF32Bits(kPositiveInfinityBits); // v0.x == +Infinity
    w.F32(0.0f);
    w.F32(0.0f);
    w.Vec3F(1.0f, 0.0f, 0.0f); // v1
    w.Vec3F(0.0f, 1.0f, 0.0f); // v2
    w.U16(0);

    const std::filesystem::path path = m_root / "nonfinite_position.stl";
    WriteBinaryFile(path, w.Bytes());

    const StlLoadResult result = LoadStlModel(path.string());

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.mesh.positions.empty());
}

TEST_F(StlLoaderTest, ParsesAMinimalOneTriangleAsciiStl)
{
    const std::string text =
        "solid test\n"
        " facet normal 0 0 1\n"
        " outer loop\n"
        " vertex 0 0 0\n"
        " vertex 1 0 0\n"
        " vertex 0 1 0\n"
        " endloop\n"
        " endfacet\n"
        " endsolid test\n";

    const std::filesystem::path path = m_root / "ascii_triangle.stl";
    WriteTextFile(path, text);

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_FALSE(result.wasBinaryFormat);
    ASSERT_EQ(result.mesh.positions.size(), 3u);
    ASSERT_EQ(result.mesh.normals.size(), 3u);
    ASSERT_EQ(result.mesh.uvs.size(), 3u);
    ASSERT_EQ(result.mesh.indices.size(), 3u);

    EXPECT_EQ(result.mesh.positions[0], Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(result.mesh.positions[1], Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(result.mesh.positions[2], Vec3(0.0f, 1.0f, 0.0f));

    for (const auto& n : result.mesh.normals) {
        EXPECT_EQ(n, Vec3(0.0f, 0.0f, 1.0f));
    }
    for (const auto& uv : result.mesh.uvs) {
        EXPECT_EQ(uv, Vec2::Zero());
    }

    EXPECT_EQ(result.mesh.indices[0], 0u);
    EXPECT_EQ(result.mesh.indices[1], 1u);
    EXPECT_EQ(result.mesh.indices[2], 2u);
}

TEST_F(StlLoaderTest, AsciiParsingToleratesExtraWhitespaceAndMixedLineEndings)
{
    const std::string text =
        "solid test\r\n"
        "\r\n"
        "   facet normal 0 0 1\r\n"
        "     outer loop\n"
        "\n"
        "       vertex 0 0 0\r\n"
        "       vertex 1 0 0\n"
        "       vertex 0 1 0\r\n"
        "     endloop\n"
        "   endfacet\r\n"
        "endsolid test\n";

    const std::filesystem::path path = m_root / "ascii_whitespace.stl";
    WriteTextFile(path, text);

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.mesh.positions.size(), 3u);
    EXPECT_EQ(result.mesh.positions[0], Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(result.mesh.positions[1], Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(result.mesh.positions[2], Vec3(0.0f, 1.0f, 0.0f));
}

TEST_F(StlLoaderTest, FailsGracefullyOnATruncatedAsciiFacet)
{
    const std::string text =
        "solid test\n"
        " facet normal 0 0 1\n"
        " outer loop\n"
        " vertex 0 0 0\n"
        " vertex 1 0 0\n";
        // Missing the 3rd vertex + endloop + endfacet + endsolid.

    const std::filesystem::path path = m_root / "ascii_truncated.stl";
    WriteTextFile(path, text);

    const StlLoadResult result = LoadStlModel(path.string());

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.mesh.positions.empty());
}

TEST_F(StlLoaderTest, FailsGracefullyOnAnAsciiFacetWithTooManyVertexLines)
{
    const std::string text =
        "solid test\n"
        " facet normal 0 0 1\n"
        " outer loop\n"
        " vertex 0 0 0\n"
        " vertex 1 0 0\n"
        " vertex 0 1 0\n"
        " vertex 1 1 0\n"
        " endloop\n"
        " endfacet\n"
        " endsolid test\n";

    const std::filesystem::path path = m_root / "ascii_too_many_vertices.stl";
    WriteTextFile(path, text);

    const StlLoadResult result = LoadStlModel(path.string());

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.mesh.positions.empty());
}

TEST_F(StlLoaderTest, FailsGracefullyOnAsciiFileWithANonNumericFloatToken)
{
    const std::string text =
        "solid test\n"
        " facet normal 0 0 1\n"
        " outer loop\n"
        " vertex abc def ghi\n"
        " vertex 1 0 0\n"
        " vertex 0 1 0\n"
        " endloop\n"
        " endfacet\n"
        " endsolid test\n";

    const std::filesystem::path path = m_root / "ascii_non_numeric.stl";
    WriteTextFile(path, text);

    const StlLoadResult result = LoadStlModel(path.string());

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.mesh.positions.empty());
}

TEST_F(StlLoaderTest, FailsGracefullyOnAsciiFileWithANonFiniteFloatToken)
{
    const std::string text =
        "solid test\n"
        " facet normal 0 0 1\n"
        " outer loop\n"
        " vertex nan nan nan\n"
        " vertex 1 0 0\n"
        " vertex 0 1 0\n"
        " endloop\n"
        " endfacet\n"
        " endsolid test\n";

    const std::filesystem::path path = m_root / "ascii_non_finite.stl";
    WriteTextFile(path, text);

    const StlLoadResult result = LoadStlModel(path.string());

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.mesh.positions.empty());
}

TEST_F(StlLoaderTest, ParsesAsciiFloatsWrittenInScientificNotation)
{
    const std::string text =
        "solid test\n"
        " facet normal 1.5e-01 -2.3E+00 1.23e-05\n"
        " outer loop\n"
        " vertex 0e0 0e0 0e0\n"
        " vertex 1e0 0e0 0e0\n"
        " vertex 0e0 1e0 0e0\n"
        " endloop\n"
        " endfacet\n"
        " endsolid test\n";

    const std::filesystem::path path = m_root / "ascii_scientific.stl";
    WriteTextFile(path, text);

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.mesh.normals.size(), 3u);
    for (const auto& n : result.mesh.normals) {
        EXPECT_TRUE(ApproximatelyEqual(n, Vec3(1.5e-01f, -2.3f, 1.23e-05f)));
    }
    ASSERT_EQ(result.mesh.positions.size(), 3u);
    EXPECT_EQ(result.mesh.positions[0], Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(result.mesh.positions[1], Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(result.mesh.positions[2], Vec3(0.0f, 1.0f, 0.0f));
}

TEST_F(StlLoaderTest, IgnoresContentAfterTheFirstEndsolidInAMultiSolidAsciiFile)
{
    const std::string text =
        "solid first\n"
        " facet normal 0 0 1\n"
        " outer loop\n"
        " vertex 0 0 0\n"
        " vertex 1 0 0\n"
        " vertex 0 1 0\n"
        " endloop\n"
        " endfacet\n"
        "endsolid first\n"
        "solid second\n"
        " facet normal 0 0 1\n"
        " outer loop\n"
        " vertex 9 9 9\n"
        " vertex 8 8 8\n"
        " vertex 7 7 7\n"
        " endloop\n"
        " endfacet\n"
        "endsolid second\n";

    const std::filesystem::path path = m_root / "ascii_multi_solid.stl";
    WriteTextFile(path, text);

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.mesh.positions.size(), 3u); // Only the first solid's triangle.

    const Vec3 secondSolidVertex(9.0f, 9.0f, 9.0f);
    for (const auto& p : result.mesh.positions) {
        EXPECT_NE(p, secondSolidVertex);
    }
}

TEST_F(StlLoaderTest, ParsesAWellFormedEmptyAsciiStlWithZeroFacets)
{
    const std::string text = "solid test\nendsolid test\n";

    const std::filesystem::path path = m_root / "ascii_empty.stl";
    WriteTextFile(path, text);

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_FALSE(result.wasBinaryFormat);
    EXPECT_TRUE(result.mesh.positions.empty());
    EXPECT_TRUE(result.mesh.normals.empty());
    EXPECT_TRUE(result.mesh.uvs.empty());
    EXPECT_TRUE(result.mesh.indices.empty());
}

TEST_F(StlLoaderTest, FailsGracefullyWhenFileDoesNotExist)
{
    const StlLoadResult result = LoadStlModel((m_root / "DoesNotExist.stl").string());

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.mesh.positions.empty());
    EXPECT_TRUE(result.mesh.normals.empty());
    EXPECT_TRUE(result.mesh.uvs.empty());
    EXPECT_TRUE(result.mesh.indices.empty());
    EXPECT_FALSE(result.message.empty());
}

TEST_F(StlLoaderTest, FailsGracefullyOnEmptyFile)
{
    const std::filesystem::path path = m_root / "zero_bytes.stl";
    WriteBinaryFile(path, {});

    const StlLoadResult result = LoadStlModel(path.string());

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.mesh.positions.empty());
}

TEST_F(StlLoaderTest, FailsGracefullyOnUnrecognizedContent)
{
    const std::string text = "this is not an stl file at all";

    const std::filesystem::path path = m_root / "unrecognized.stl";
    WriteTextFile(path, text);

    const StlLoadResult result = LoadStlModel(path.string());

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.mesh.positions.empty());
}

// Optional real-world smoke test: if this campaign's real reference fixture
// (a genuinely large, non-vendored 52MB/1,045,458-triangle binary STL) is
// present on THIS machine, actually parse it end-to-end - mirrors
// PmxLoaderTests.cpp's own PmxLoaderRealModelSmokeTest pattern EXACTLY
// (GTEST_SKIP()s cleanly rather than failing on a machine without it).
TEST(StlLoaderRealModelSmokeTest, LoadsTheRealTerrainStlIfPresentOnThisMachine)
{
    const std::filesystem::path path =
        "C:\\Users\\F5954\\Documents\\TAMANA\\GreatTamanaEngine\\_reference\\pl-sky\\assets\\terrain.stl";

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        GTEST_SKIP() << "Real terrain.stl reference fixture not present on this machine - skipping.";
    }

    const StlLoadResult result = LoadStlModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.wasBinaryFormat);
    EXPECT_EQ(result.mesh.positions.size(), 1045458u * 3u);
    EXPECT_EQ(result.mesh.normals.size(), result.mesh.positions.size());
    EXPECT_EQ(result.mesh.uvs.size(), result.mesh.positions.size());
    EXPECT_EQ(result.mesh.indices.size(), result.mesh.positions.size());
    EXPECT_TRUE(result.mesh.skinWeights.empty());

    std::cout << "[StlLoaderRealModelSmokeTest] " << path.string() << ": "
              << result.mesh.positions.size() << " vertices, "
              << (result.mesh.indices.size() / 3) << " triangles\n";
}

} // namespace
} // namespace gte
