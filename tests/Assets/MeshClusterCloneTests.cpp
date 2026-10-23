// Tier-1 tests for the clustered clone of a mesh asset and the mesh index check.

#include "Assets/GtaFile.h"
#include "Assets/MeshClusterClone.h"
#include "Assets/MeshClusterValidation.h"
#include "Assets/MeshFile.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Flat shared-vertex grid in the z = 0 plane.
MeshData MakeGrid(int cells)
{
    MeshData mesh;
    const int stride = cells + 1;
    for (int y = 0; y <= cells; ++y) {
        for (int x = 0; x <= cells; ++x) {
            mesh.positions.emplace_back(static_cast<float>(x), static_cast<float>(y), 0.0f);
            mesh.normals.emplace_back(0.0f, 0.0f, 1.0f);
            mesh.uvs.emplace_back(0.0f, 0.0f);
        }
    }
    for (int y = 0; y < cells; ++y) {
        for (int x = 0; x < cells; ++x) {
            const std::uint32_t i00 = static_cast<std::uint32_t>(y * stride + x);
            const std::uint32_t i10 = i00 + 1;
            const std::uint32_t i01 = i00 + static_cast<std::uint32_t>(stride);
            const std::uint32_t i11 = i01 + 1;
            mesh.indices.insert(mesh.indices.end(), { i00, i10, i01, i10, i11, i01 });
        }
    }
    return mesh;
}

// The same grid with three private vertices per triangle and a unique normal per triangle,
// so an Exact weld cannot merge anything but a position-only weld can.
MeshData MakeSoup(int cells)
{
    const MeshData grid = MakeGrid(cells);
    MeshData soup;
    for (std::size_t i = 0; i < grid.indices.size(); ++i) {
        const float tilt = static_cast<float>(i / 3) * 1e-4f;
        const float invLength = 1.0f / std::sqrt(1.0f + tilt * tilt);
        soup.indices.push_back(static_cast<std::uint32_t>(soup.positions.size()));
        soup.positions.push_back(grid.positions[grid.indices[i]]);
        soup.normals.emplace_back(tilt * invLength, 0.0f, invLength);
        soup.uvs.emplace_back(0.0f, 0.0f);
    }
    return soup;
}

class MeshClusterCloneTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path() / (std::string("MeshClusterCloneTests_") + info->name());
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    std::filesystem::path WriteSource(const char* name, const MeshData& mesh) const
    {
        const std::filesystem::path path = m_root / name;
        EXPECT_TRUE(WriteGtaFile(path, AssetType::Mesh, Guid::Generate(), AssetFlags::None, {}, EncodeMeshDataToBytes(mesh)));
        return path;
    }

    static std::vector<char> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    std::size_t FileCount() const
    {
        return static_cast<std::size_t>(std::distance(
            std::filesystem::directory_iterator(m_root), std::filesystem::directory_iterator()));
    }

    std::filesystem::path m_root;
};

TEST_F(MeshClusterCloneTest, CloneWritesANewClusteredFileAndLeavesTheSourceUntouched)
{
    const std::filesystem::path source = WriteSource("grid.gta", MakeGrid(20));
    const std::vector<char> sourceBytes = ReadBytes(source);
    const std::optional<GtaFileData> sourceGta = ReadGtaFile(source);
    ASSERT_TRUE(sourceGta.has_value());

    const std::filesystem::path destination = ChooseClusteredCloneDestination(source);
    ASSERT_EQ(destination.filename().string(), "grid_clustered.gta");

    const MeshClusterCloneOutcome outcome = CloneMeshAssetWithClusters(source, destination, {}, nullptr);
    ASSERT_TRUE(outcome.success) << outcome.message;
    EXPECT_EQ(outcome.createdPath, destination);
    EXPECT_FALSE(outcome.weldFellBack);
    EXPECT_EQ(ReadBytes(source), sourceBytes);
    EXPECT_EQ(FileCount(), 2u);

    const std::optional<GtaFileData> clone = ReadGtaFile(destination);
    ASSERT_TRUE(clone.has_value());
    EXPECT_NE(clone->header.Id(), sourceGta->header.Id());
    EXPECT_EQ(clone->header.Id(), outcome.guid);
    EXPECT_EQ(clone->header.version, sourceGta->header.version);

    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(clone->payload);
    ASSERT_TRUE(mesh.has_value());
    ASSERT_FALSE(mesh->clusters.empty());
    std::string why;
    EXPECT_TRUE(ValidateClusterTable(mesh->clusters, mesh->indices.size(), &why)) << why;
}

TEST_F(MeshClusterCloneTest, AutoWeldFallsBackToPositionOnlyForAnUnsharedMesh)
{
    const std::filesystem::path source = WriteSource("soup.gta", MakeSoup(40));
    const std::filesystem::path destination = m_root / "soup_clustered.gta";

    const MeshClusterCloneOutcome outcome = CloneMeshAssetWithClusters(source, destination, {}, nullptr);
    ASSERT_TRUE(outcome.success) << outcome.message;
    EXPECT_TRUE(outcome.weldFellBack);
    EXPECT_EQ(outcome.weldUsed, ClusterWeldMode::PositionOnly);
    EXPECT_LT(outcome.report.vertexCountAfter, outcome.report.vertexCountBefore);
}

TEST_F(MeshClusterCloneTest, ExistingDestinationIsNeverReplaced)
{
    const std::filesystem::path source = WriteSource("grid.gta", MakeGrid(6));
    const std::filesystem::path destination = WriteSource("taken.gta", MakeGrid(2));
    const std::vector<char> takenBytes = ReadBytes(destination);

    const MeshClusterCloneOutcome outcome = CloneMeshAssetWithClusters(source, destination, {}, nullptr);
    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.message, "destination exists");
    EXPECT_EQ(ReadBytes(destination), takenBytes);
    EXPECT_EQ(FileCount(), 2u);
}

TEST_F(MeshClusterCloneTest, CancelBeforeStartCreatesNoFile)
{
    const std::filesystem::path source = WriteSource("grid.gta", MakeGrid(6));
    const std::atomic<bool> cancel{ true };

    const MeshClusterCloneOutcome outcome =
        CloneMeshAssetWithClusters(source, m_root / "grid_clustered.gta", {}, &cancel);
    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.message, "cancelled");
    EXPECT_EQ(FileCount(), 1u);
}

TEST_F(MeshClusterCloneTest, MissingSourceFailsWithoutAFile)
{
    const MeshClusterCloneOutcome outcome =
        CloneMeshAssetWithClusters(m_root / "none.gta", m_root / "none_clustered.gta", {}, nullptr);
    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(FileCount(), 0u);
}

TEST_F(MeshClusterCloneTest, DestinationNamesCountUpAndStayNextToTheSource)
{
    const std::filesystem::path source = m_root / "a.gta";
    EXPECT_EQ(ChooseClusteredCloneDestination(source), m_root / "a_clustered.gta");

    std::ofstream(m_root / "a_clustered.gta") << "x";
    EXPECT_EQ(ChooseClusteredCloneDestination(source), m_root / "a_clustered_2.gta");
}

TEST(MeshIndexValidationTest, AcceptsTrianglesAndRejectsBadLists)
{
    const std::vector<std::uint32_t> good = { 0, 1, 2, 2, 1, 3 };
    std::string why;
    EXPECT_TRUE(ValidateMeshIndices(4, good, &why)) << why;

    const std::vector<std::uint32_t> pastEnd = { 0, 1, 4 };
    EXPECT_FALSE(ValidateMeshIndices(4, pastEnd, &why));
    EXPECT_FALSE(why.empty());

    const std::vector<std::uint32_t> notTriangles = { 0, 1, 2, 3 };
    EXPECT_FALSE(ValidateMeshIndices(4, notTriangles, nullptr));
    EXPECT_FALSE(ValidateMeshIndices(4, {}, nullptr));
}

} // namespace
} // namespace gte
