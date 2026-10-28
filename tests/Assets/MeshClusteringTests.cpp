// Unit tests for BuildMeshClusters and the *.gta cluster persistence.

#include "Assets/AtomicFile.h"
#include "Assets/GtaFile.h"
#include "Assets/MeshClusterPersistence.h"
#include "Assets/MeshClusterValidation.h"
#include "Assets/MeshClustering.h"
#include "Assets/MeshFile.h"
#include "Assets/RigFile.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Flat shared-vertex grid in the z = 0 plane, CCW seen from +z, normals +z, uvs zero.
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

// The same grid with three private vertices per triangle. With `uniqueUvs` no two vertices are equal.
MeshData MakeSoup(int cells, bool uniqueUvs)
{
    const MeshData grid = MakeGrid(cells);
    MeshData soup;
    for (const std::uint32_t index : grid.indices) {
        const float u = uniqueUvs ? static_cast<float>(soup.positions.size() + 1) : 0.0f;
        soup.indices.push_back(static_cast<std::uint32_t>(soup.positions.size()));
        soup.positions.push_back(grid.positions[index]);
        soup.normals.push_back(grid.normals[index]);
        soup.uvs.emplace_back(u, 0.0f);
    }
    return soup;
}

using TriangleKey = std::array<float, 9>;

// Sorted multiset of position triples, one per triangle (vertices sorted inside the triangle).
std::vector<TriangleKey> TriangleSet(const MeshData& mesh)
{
    std::vector<TriangleKey> keys;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        std::array<Vec3, 3> v = { mesh.positions[mesh.indices[i]], mesh.positions[mesh.indices[i + 1]],
            mesh.positions[mesh.indices[i + 2]] };
        std::sort(v.begin(), v.end(), [](const Vec3& a, const Vec3& b) {
            return a.x != b.x ? a.x < b.x : (a.y != b.y ? a.y < b.y : a.z < b.z);
        });
        keys.push_back({ v[0].x, v[0].y, v[0].z, v[1].x, v[1].y, v[1].z, v[2].x, v[2].y, v[2].z });
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

// Bitwise, so a NaN position still equals itself.
bool SamePositions(const std::vector<Vec3>& a, const std::vector<Vec3>& b)
{
    const auto bits = [](float v) { return std::bit_cast<std::uint32_t>(v); };
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [&](const Vec3& l, const Vec3& r) {
               return bits(l.x) == bits(r.x) && bits(l.y) == bits(r.y) && bits(l.z) == bits(r.z);
           });
}

void ExpectMeshUnchanged(const MeshData& a, const MeshData& b)
{
    EXPECT_TRUE(SamePositions(a.positions, b.positions));
    EXPECT_EQ(a.normals, b.normals);
    EXPECT_EQ(a.uvs, b.uvs);
    EXPECT_EQ(a.indices, b.indices);
    EXPECT_TRUE(b.clusters.empty());
}

TEST(MeshClusteringTest, GridIsTiledByValidClustersAndKeepsEveryTriangle)
{
    MeshData mesh = MakeGrid(20);
    const std::vector<TriangleKey> before = TriangleSet(mesh);
    const std::size_t triangles = mesh.indices.size() / 3;
    const MeshClusterOptions options;

    const MeshClusterReport report = BuildMeshClusters(mesh, {}, options);
    ASSERT_TRUE(report.success) << report.message;
    EXPECT_STREQ(report.lastStep, "done");
    EXPECT_EQ(report.triangleCount, triangles);
    EXPECT_EQ(report.clusterCount, mesh.clusters.size());
    EXPECT_EQ(TriangleSet(mesh), before);

    std::string why;
    EXPECT_TRUE(ValidateMeshClusters(mesh, &why)) << why;

    const float eps = 1e-3f;
    std::uint32_t cursor = 0;
    for (const MeshCluster& c : mesh.clusters) {
        EXPECT_EQ(c.firstIndex, cursor);
        EXPECT_EQ(c.indexCount % 3, 0u);
        EXPECT_LE(c.indexCount / 3, options.maxTrianglesPerCluster);
        cursor += c.indexCount;

        std::vector<std::uint32_t> unique(mesh.indices.begin() + c.firstIndex, mesh.indices.begin() + c.firstIndex + c.indexCount);
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        EXPECT_LE(unique.size(), options.maxVerticesPerCluster);
        for (const std::uint32_t v : unique) {
            const Vec3& p = mesh.positions[v];
            EXPECT_GE(p.x, c.aabbMin[0] - eps);
            EXPECT_LE(p.x, c.aabbMax[0] + eps);
            EXPECT_GE(p.y, c.aabbMin[1] - eps);
            EXPECT_LE(p.y, c.aabbMax[1] + eps);
            EXPECT_GE(p.z, c.aabbMin[2] - eps);
            EXPECT_LE(p.z, c.aabbMax[2] + eps);
            const float dx = p.x - c.sphere[0];
            const float dy = p.y - c.sphere[1];
            const float dz = p.z - c.sphere[2];
            EXPECT_LE(std::sqrt(dx * dx + dy * dy + dz * dz), c.sphere[3] + eps);
        }
    }
    EXPECT_EQ(cursor, mesh.indices.size());
}

TEST(MeshClusteringTest, FlatClustersGetAConeThatContainsEveryTriangleNormal)
{
    MeshData mesh = MakeGrid(12);
    ASSERT_TRUE(BuildMeshClusters(mesh, {}, MeshClusterOptions{}).success);

    for (const MeshCluster& c : mesh.clusters) {
        ASSERT_LT(c.cone[3], 1.0f); // A flat plane always has a usable cone.
        const float minDot = std::sqrt(1.0f - c.cone[3] * c.cone[3]);
        const Vec3 axis = Vec3(c.cone[0], c.cone[1], c.cone[2]) * kClusterConeWindingSign;
        for (std::uint32_t i = c.firstIndex; i < c.firstIndex + c.indexCount; i += 3) {
            const Vec3 n = Normalize(Cross(mesh.positions[mesh.indices[i + 1]] - mesh.positions[mesh.indices[i]],
                mesh.positions[mesh.indices[i + 2]] - mesh.positions[mesh.indices[i]]));
            EXPECT_GE(Dot(n, axis), minDot - 1e-3f);
        }
    }
}

TEST(MeshClusteringTest, MaterialSliceBoundariesAreClusterBoundaries)
{
    MeshData mesh = MakeGrid(20); // 2400 indices, so the split at 900 lies inside the mesh.
    const std::size_t total = mesh.indices.size();
    std::vector<Material> materials(2);
    materials[0].indexCount = 300 * 3;
    materials[1].indexCount = static_cast<std::uint32_t>(total - 300 * 3);

    ASSERT_TRUE(BuildMeshClusters(mesh, materials, MeshClusterOptions{}).success);
    bool boundaryFound = false;
    for (const MeshCluster& c : mesh.clusters) {
        boundaryFound |= (c.firstIndex == 900);
        EXPECT_FALSE(c.firstIndex < 900 && c.firstIndex + c.indexCount > 900);
    }
    EXPECT_TRUE(boundaryFound);
}

TEST(MeshClusteringTest, PositionOnlyWeldMergesASoupAndKeepsTheTriangles)
{
    MeshData mesh = MakeSoup(40, false);
    const std::size_t verticesBefore = mesh.positions.size();
    const std::vector<TriangleKey> before = TriangleSet(mesh);

    MeshClusterOptions options;
    options.weld = ClusterWeldMode::PositionOnly;
    const MeshClusterReport report = BuildMeshClusters(mesh, {}, options);
    ASSERT_TRUE(report.success) << report.message;
    EXPECT_EQ(report.vertexCountBefore, verticesBefore);
    EXPECT_LT(mesh.positions.size(), verticesBefore);
    EXPECT_EQ(mesh.positions.size(), 41u * 41u);
    EXPECT_EQ(mesh.normals.size(), mesh.positions.size());
    EXPECT_EQ(TriangleSet(mesh), before);
    EXPECT_FALSE(report.poorFill);
}

TEST(MeshClusteringTest, ExactWeldOfAnUnsharedSoupIsPoorFillAndAutoEarlyOutSkipsTheClusterSteps)
{
    MeshData mesh = MakeSoup(20, true);
    const MeshData original = mesh;

    const MeshClusterReport full = BuildMeshClusters(mesh, {}, MeshClusterOptions{});
    ASSERT_TRUE(full.success) << full.message;
    EXPECT_TRUE(full.poorFill);

    MeshData other = original;
    const MeshClusterReport early = BuildMeshClusters(other, {}, MeshClusterOptions{}, nullptr, true);
    EXPECT_FALSE(early.success);
    EXPECT_TRUE(early.poorFill);
    EXPECT_STREQ(early.lastStep, "weld");
    ExpectMeshUnchanged(original, other);
}

TEST(MeshClusteringTest, PositionOnlyWeldRefusesTexturedMaterialsAndUvs)
{
    MeshClusterOptions options;
    options.weld = ClusterWeldMode::PositionOnly;

    MeshData textured = MakeGrid(4);
    std::vector<Material> materials(1);
    materials[0].indexCount = static_cast<std::uint32_t>(textured.indices.size());
    materials[0].textureIndex = 0;
    const MeshData texturedOriginal = textured;
    EXPECT_FALSE(BuildMeshClusters(textured, materials, options).success);
    ExpectMeshUnchanged(texturedOriginal, textured);

    MeshData mapped = MakeGrid(4);
    mapped.uvs[3] = Vec2(0.5f, 0.0f);
    const MeshData mappedOriginal = mapped;
    EXPECT_FALSE(BuildMeshClusters(mapped, {}, options).success);
    ExpectMeshUnchanged(mappedOriginal, mapped);
}

TEST(MeshClusteringTest, RefusesSkinnedAndBadInputWithoutTouchingTheMesh)
{
    const auto expectRefused = [](MeshData mesh, const MeshClusterOptions& options) {
        const MeshData original = mesh;
        const MeshClusterReport report = BuildMeshClusters(mesh, {}, options);
        EXPECT_FALSE(report.success);
        EXPECT_FALSE(report.message.empty());
        ExpectMeshUnchanged(original, mesh);
    };

    MeshData skinned = MakeGrid(2);
    skinned.skinWeights.resize(skinned.positions.size());
    expectRefused(skinned, {});

    MeshData outOfRange = MakeGrid(2);
    outOfRange.indices[1] = 9999;
    expectRefused(outOfRange, {});

    MeshData nan = MakeGrid(2);
    nan.positions[0].x = std::numeric_limits<float>::quiet_NaN();
    expectRefused(nan, {});

    MeshData badCount = MakeGrid(2);
    badCount.indices.pop_back();
    expectRefused(badCount, {});

    expectRefused(MeshData{}, {});

    MeshData mismatch = MakeGrid(2);
    mismatch.uvs.pop_back();
    expectRefused(mismatch, {});

    MeshClusterOptions badOptions;
    badOptions.maxTrianglesPerCluster = 126;
    expectRefused(MakeGrid(2), badOptions);
}

TEST(MeshClusteringTest, OptionAndSizeChecks)
{
    EXPECT_TRUE(ValidateMeshClusterOptions(MeshClusterOptions{}, nullptr));

    MeshClusterOptions o;
    o.maxVerticesPerCluster = 2;
    EXPECT_FALSE(ValidateMeshClusterOptions(o, nullptr));
    o = {};
    o.maxVerticesPerCluster = 256;
    EXPECT_FALSE(ValidateMeshClusterOptions(o, nullptr));
    o = {};
    o.maxTrianglesPerCluster = 516;
    EXPECT_FALSE(ValidateMeshClusterOptions(o, nullptr));
    o = {};
    o.coneWeight = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(ValidateMeshClusterOptions(o, nullptr));
    o = {};
    o.coneWeight = 1.5f;
    EXPECT_FALSE(ValidateMeshClusterOptions(o, nullptr));

    EXPECT_TRUE(ClusterBuildCountsFit(1000, 3000));
    EXPECT_FALSE(ClusterBuildCountsFit(std::size_t{ 1 } << 32, 3));
    EXPECT_FALSE(ClusterBuildCountsFit(3, std::size_t{ 1 } << 32));
}

TEST(MeshClusteringTest, CancelFlagLeavesTheMeshUnchanged)
{
    MeshData mesh = MakeGrid(6);
    const MeshData original = mesh;
    const std::atomic<bool> cancel{ true };
    const MeshClusterReport report = BuildMeshClusters(mesh, {}, MeshClusterOptions{}, &cancel);
    EXPECT_FALSE(report.success);
    EXPECT_EQ(report.message, "cancelled");
    ExpectMeshUnchanged(original, mesh);
}

TEST(MeshClusteringTest, SameInputGivesSameBytes)
{
    MeshData a = MakeGrid(15);
    MeshData b = MakeGrid(15);
    ASSERT_TRUE(BuildMeshClusters(a, {}, MeshClusterOptions{}).success);
    ASSERT_TRUE(BuildMeshClusters(b, {}, MeshClusterOptions{}).success);
    const std::vector<std::uint8_t> bytesA = EncodeMeshDataToBytes(a);
    EXPECT_FALSE(bytesA.empty());
    EXPECT_EQ(bytesA, EncodeMeshDataToBytes(b));
}

TEST(MeshClusteringTest, WeldMergesNegativeZeroInEveryFloatStream)
{
    for (int variant = 0; variant < 3; ++variant) {
        MeshData mesh;
        // Triangle A and triangle B both use the point (0,0,0); B carries a negative zero in one stream.
        mesh.positions = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 0), Vec3(0, 1, 0), Vec3(-1, 0, 0) };
        mesh.normals.assign(6, Vec3(0, 0, 1));
        mesh.uvs.assign(6, Vec2(0, 0));
        mesh.indices = { 0, 1, 2, 3, 4, 5 };
        // Point (-1,0,0) and (1,0,0) get distinct uvs so only the shared points can merge.
        mesh.uvs[1] = Vec2(1, 0);
        mesh.uvs[5] = Vec2(-1, 0);
        if (variant == 0) {
            mesh.positions[3].x = -0.0f;
        } else if (variant == 1) {
            mesh.normals[3].x = -0.0f;
        } else {
            mesh.uvs[3].x = -0.0f;
        }

        const MeshClusterReport report = BuildMeshClusters(mesh, {}, MeshClusterOptions{});
        ASSERT_TRUE(report.success) << report.message;
        EXPECT_EQ(mesh.positions.size(), 4u) << "variant " << variant;
    }
}

TEST(MeshClusteringTest, DegenerateTrianglesAreKept)
{
    MeshData mesh = MakeGrid(4);
    mesh.indices.insert(mesh.indices.end(), { 0, 1, 2 }); // Three collinear points: zero area.
    mesh.indices.insert(mesh.indices.end(), { 0, 0, 1 }); // Repeated index.
    const std::size_t triangles = mesh.indices.size() / 3;

    const MeshClusterReport report = BuildMeshClusters(mesh, {}, MeshClusterOptions{});
    ASSERT_TRUE(report.success) << report.message;
    EXPECT_EQ(mesh.indices.size() / 3, triangles);
    std::string why;
    EXPECT_TRUE(ValidateMeshClusters(mesh, &why)) << why;
}

// Real temp-file round trips.
class MeshClusterPersistenceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path() / (std::string("MeshClusteringTests_") + info->name());
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    std::filesystem::path WriteMesh(const char* name, const MeshData& mesh, const std::vector<std::uint8_t>& metadata)
    {
        const std::filesystem::path path = m_root / name;
        EXPECT_TRUE(WriteGtaFile(path, AssetType::Mesh, Guid::Generate(), AssetFlags::None, metadata, EncodeMeshDataToBytes(mesh)));
        return path;
    }

    static std::vector<char> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    std::filesystem::path m_root;
};

TEST_F(MeshClusterPersistenceTest, BuildsAndWritesAValidTableKeepingHeaderAndMetadata)
{
    const std::filesystem::path path = WriteMesh("ok.gta", MakeGrid(20), EncodeRigDataToBytes(RigFileData{}));
    const std::optional<GtaFileData> before = ReadGtaFile(path);
    ASSERT_TRUE(before.has_value());

    const MeshClusterBuildOutcome outcome = BuildClustersInGtaFile(path, MeshClusterOptions{}, nullptr);
    ASSERT_TRUE(outcome.success) << outcome.message;
    EXPECT_GT(outcome.report.clusterCount, 0u);

    const std::optional<GtaFileData> after = ReadGtaFile(path);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->header.Id(), before->header.Id());
    EXPECT_EQ(after->header.version, before->header.version);
    EXPECT_EQ(after->metadata, before->metadata);

    std::string rejection;
    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(after->payload, &rejection);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_TRUE(rejection.empty()) << rejection;
    EXPECT_EQ(mesh->clusters.size(), outcome.report.clusterCount);
    std::string why;
    EXPECT_TRUE(ValidateMeshClusters(*mesh, &why)) << why;
}

TEST_F(MeshClusterPersistenceTest, RiggedOrUndecodableMetadataIsRefusedAndTheFileStaysIdentical)
{
    RigFileData rig;
    rig.skeleton.bones.emplace_back();
    const std::filesystem::path rigged = WriteMesh("rigged.gta", MakeGrid(6), EncodeRigDataToBytes(rig));
    const std::filesystem::path garbage = WriteMesh("garbage.gta", MakeGrid(6), { 1, 2, 3, 4, 5 });

    for (const std::filesystem::path& path : { rigged, garbage }) {
        const std::vector<char> before = ReadBytes(path);
        const MeshClusterBuildOutcome outcome = BuildClustersInGtaFile(path, MeshClusterOptions{}, nullptr);
        EXPECT_FALSE(outcome.success);
        EXPECT_FALSE(outcome.message.empty());
        EXPECT_EQ(ReadBytes(path), before);
    }

    const std::optional<GtaMetadataData> meta = ReadGtaMetadataOnly(rigged);
    ASSERT_TRUE(meta.has_value());
    EXPECT_TRUE(ClusterBuildRefusalReason(*meta).has_value());
    const std::optional<GtaMetadataData> clean = ReadGtaMetadataOnly(WriteMesh("clean.gta", MakeGrid(6), {}));
    ASSERT_TRUE(clean.has_value());
    EXPECT_FALSE(ClusterBuildRefusalReason(*clean).has_value());
}

TEST_F(MeshClusterPersistenceTest, MissingFileAndCancelAreReportedAsFailures)
{
    EXPECT_FALSE(BuildClustersInGtaFile(m_root / "missing.gta", MeshClusterOptions{}, nullptr).success);

    const std::filesystem::path path = WriteMesh("cancel.gta", MakeGrid(6), {});
    const std::vector<char> before = ReadBytes(path);
    const std::atomic<bool> cancel{ true };
    EXPECT_FALSE(BuildClustersInGtaFile(path, MeshClusterOptions{}, &cancel).success);
    EXPECT_EQ(ReadBytes(path), before);
}

} // namespace
} // namespace gte
