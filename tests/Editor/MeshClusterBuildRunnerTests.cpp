// Unit tests for MeshClusterBuildRunner: start, poll-once, busy and the path guard.

#include "Assets/GtaFile.h"
#include "Assets/MeshFile.h"
#include "Assets/RigFile.h"
#include "Editor/MeshClusterBuildRunner.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

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

class MeshClusterBuildRunnerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_root = std::filesystem::temp_directory_path() / "MeshClusterBuildRunnerTests";
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root);
        m_path = m_root / "grid.gta";
        ASSERT_TRUE(WriteGtaFile(m_path, AssetType::Mesh, Guid::Generate(), AssetFlags::None,
            EncodeRigDataToBytes(RigFileData{}), EncodeMeshDataToBytes(MakeGrid(20))));
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    // Polls until the build finished or a generous timeout passes.
    static std::optional<MeshClusterFinishedBuild> WaitForBuild(MeshClusterBuildRunner& runner)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto finished = runner.Poll()) {
                return finished;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return std::nullopt;
    }

    std::filesystem::path m_root;
    std::filesystem::path m_path;
};

TEST_F(MeshClusterBuildRunnerTest, BuildFinishesOnceAndCarriesTheRequestId)
{
    MeshClusterBuildRunner runner;
    ASSERT_EQ(runner.Start(m_path, MeshClusterOptions{}, 7), MeshClusterBuildRunner::StartResult::Started);
    EXPECT_TRUE(runner.Busy());
    EXPECT_EQ(runner.ActiveRequestId(), 7u);
    EXPECT_TRUE(runner.IsBuildingPath(m_path));

    const std::optional<MeshClusterFinishedBuild> finished = WaitForBuild(runner);
    ASSERT_TRUE(finished.has_value());
    EXPECT_TRUE(finished->outcome.success) << finished->outcome.message;
    EXPECT_EQ(finished->requestId, 7u);
    EXPECT_GT(finished->outcome.report.clusterCount, 0u);

    EXPECT_FALSE(runner.Busy());
    EXPECT_FALSE(runner.IsBuildingPath(m_path));
    EXPECT_FALSE(runner.Poll().has_value());
    ASSERT_TRUE(runner.LastFinished().has_value());
}

TEST_F(MeshClusterBuildRunnerTest, SecondStartWhileBusyIsRefused)
{
    MeshClusterBuildRunner runner;
    ASSERT_EQ(runner.Start(m_path, MeshClusterOptions{}, 1), MeshClusterBuildRunner::StartResult::Started);
    EXPECT_EQ(runner.Start(m_path, MeshClusterOptions{}, 2), MeshClusterBuildRunner::StartResult::Busy);
    ASSERT_TRUE(WaitForBuild(runner).has_value());
    EXPECT_EQ(runner.Start(m_path, MeshClusterOptions{}, 3), MeshClusterBuildRunner::StartResult::Started);
    EXPECT_TRUE(WaitForBuild(runner).has_value());
}

TEST_F(MeshClusterBuildRunnerTest, MissingFileReportsFailureWithoutThrowing)
{
    MeshClusterBuildRunner runner;
    ASSERT_EQ(runner.Start(m_root / "missing.gta", MeshClusterOptions{}, 0), MeshClusterBuildRunner::StartResult::Started);
    const std::optional<MeshClusterFinishedBuild> finished = WaitForBuild(runner);
    ASSERT_TRUE(finished.has_value());
    EXPECT_FALSE(finished->outcome.success);
    EXPECT_FALSE(finished->outcome.message.empty());
}

} // namespace
} // namespace gte
