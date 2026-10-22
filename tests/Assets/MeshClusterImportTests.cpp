#include "Assets/AssetImporter.h"
#include "Assets/GtaFile.h"
#include "Assets/MeshClusterPersistence.h"
#include "Assets/MeshFile.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

void PutU32(std::vector<std::uint8_t>& bytes, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

void PutF32(std::vector<std::uint8_t>& bytes, float v)
{
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    PutU32(bytes, bits);
}

using Triangle = std::array<float, 9>;

// Binary STL: 80-byte header, triangle count, then normal + 3 vertices + 2 attribute bytes per triangle.
std::vector<std::uint8_t> BuildBinaryStl(const std::vector<Triangle>& triangles)
{
    std::vector<std::uint8_t> bytes(80, 0);
    PutU32(bytes, static_cast<std::uint32_t>(triangles.size()));
    for (const Triangle& t : triangles) {
        PutF32(bytes, 0.0f);
        PutF32(bytes, 0.0f);
        PutF32(bytes, 1.0f);
        for (const float v : t) {
            PutF32(bytes, v);
        }
        bytes.push_back(0);
        bytes.push_back(0);
    }
    return bytes;
}

// Flat n x n grid of quads (2n^2 triangles) with shared corners.
std::vector<Triangle> BuildGrid(int n)
{
    std::vector<Triangle> triangles;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const float x = static_cast<float>(i);
            const float y = static_cast<float>(j);
            triangles.push_back({ x, y, 0, x + 1, y, 0, x, y + 1, 0 });
            triangles.push_back({ x + 1, y, 0, x + 1, y + 1, 0, x, y + 1, 0 });
        }
    }
    return triangles;
}

// Disjoint triangles: every vertex is unique, so a weld merges nothing.
std::vector<Triangle> BuildSoup(int count)
{
    std::vector<Triangle> triangles;
    for (int i = 0; i < count; ++i) {
        const float x = static_cast<float>(i) * 3.0f;
        triangles.push_back({ x, 0, 0, x + 1, 0, 0, x, 1, 0 });
    }
    return triangles;
}

class MeshClusterImportTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteMeshClusterImport_") + info->test_suite_name() + "_" + info->name());
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    AssetImportResult Import(const std::string& name, const std::vector<Triangle>& triangles,
        const MeshImportOptions& options)
    {
        const std::filesystem::path source = m_root / (name + ".stl");
        const std::vector<std::uint8_t> bytes = BuildBinaryStl(triangles);
        std::ofstream out(source, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        return ImportAssetFile(m_db, source, m_root / (name + ".stl"), options);
    }

    static std::optional<MeshData> DecodeImported(const AssetImportResult& result)
    {
        const std::optional<GtaFileData> gta = ReadGtaFile(result.finalPath);
        if (!gta.has_value()) {
            return std::nullopt;
        }
        return DecodeMeshDataFromBytes(gta->payload);
    }

    std::filesystem::path m_root;
    AssetDatabase m_db;
};

MeshImportOptions Options(ClusterBuildMode mode, std::uint32_t threshold = 100000)
{
    MeshImportOptions options;
    options.clusters = mode;
    options.autoTriangleThreshold = threshold;
    return options;
}

TEST_F(MeshClusterImportTest, OffWritesNoClusterTail)
{
    const AssetImportResult result = Import("grid", BuildGrid(40), Options(ClusterBuildMode::Off));
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.clusterCount, 0u);
    EXPECT_TRUE(result.clusterMessage.empty());

    const std::optional<MeshData> mesh = DecodeImported(result);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_TRUE(mesh->clusters.empty());
}

TEST_F(MeshClusterImportTest, AlwaysWritesAClusterTable)
{
    const AssetImportResult result = Import("grid", BuildGrid(40), Options(ClusterBuildMode::Always));
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_GT(result.clusterCount, 0u);
    EXPECT_EQ(result.clusterMessage.rfind("Clusters:", 0), 0u) << result.clusterMessage;

    const std::optional<MeshData> mesh = DecodeImported(result);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->clusters.size(), result.clusterCount);
    EXPECT_EQ(result.meshTriangleCount, 3200u);
}

TEST_F(MeshClusterImportTest, AutoBelowThresholdWritesNone)
{
    const AssetImportResult result = Import("grid", BuildGrid(40), Options(ClusterBuildMode::Auto));
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.clusterCount, 0u);
    EXPECT_TRUE(result.clusterMessage.empty());
}

TEST_F(MeshClusterImportTest, AutoAboveThresholdClustersAWellFilledMesh)
{
    const AssetImportResult result = Import("grid", BuildGrid(40), Options(ClusterBuildMode::Auto, 100));
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_GT(result.clusterCount, 0u) << result.clusterMessage;
}

TEST_F(MeshClusterImportTest, AutoSkipsASoupAfterTheCheapPreCheck)
{
    const AssetImportResult result = Import("soup", BuildSoup(200), Options(ClusterBuildMode::Auto, 10));
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.clusterCount, 0u);
    EXPECT_NE(result.clusterMessage.find("poor"), std::string::npos) << result.clusterMessage;

    // The early out runs before any reorder, so the mesh keeps its unshared vertices.
    EXPECT_EQ(result.meshVertexCount, 600u);
    const std::optional<MeshData> mesh = DecodeImported(result);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_TRUE(mesh->clusters.empty());
}

TEST_F(MeshClusterImportTest, ImportWithAlwaysAndOffThenPersistenceWriteTheSamePayload)
{
    const MeshImportOptions always = Options(ClusterBuildMode::Always);
    const AssetImportResult viaImport = Import("direct", BuildGrid(40), always);
    ASSERT_TRUE(viaImport.success) << viaImport.message;
    ASSERT_GT(viaImport.clusterCount, 0u);

    const AssetImportResult plain = Import("later", BuildGrid(40), Options(ClusterBuildMode::Off));
    ASSERT_TRUE(plain.success) << plain.message;
    const MeshClusterBuildOutcome built = BuildClustersInGtaFile(plain.finalPath, always.cluster, nullptr);
    ASSERT_TRUE(built.success) << built.message;

    const std::optional<GtaFileData> a = ReadGtaFile(viaImport.finalPath);
    const std::optional<GtaFileData> b = ReadGtaFile(plain.finalPath);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a->payload, b->payload);
}

} // namespace
} // namespace gte
