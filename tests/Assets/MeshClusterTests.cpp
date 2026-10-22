// Unit tests for the cluster record, its validation, rebase and the mesh payload cluster tail.

#include "Assets/Crc32.h"
#include "Assets/MeshCluster.h"
#include "Assets/MeshClusterRebase.h"
#include "Assets/MeshClusterValidation.h"
#include "Assets/MeshFile.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

MeshCluster MakeCluster(std::uint32_t first, std::uint32_t count)
{
    MeshCluster c{};
    c.sphere[3] = 1.0f;
    c.cone[2] = 1.0f;
    c.cone[3] = 0.5f;
    c.aabbMin[0] = c.aabbMin[1] = c.aabbMin[2] = -1.0f;
    c.aabbMax[0] = c.aabbMax[1] = c.aabbMax[2] = 1.0f;
    c.firstIndex = first;
    c.indexCount = count;
    return c;
}

// Four triangles over six vertices.
MeshData MakeMesh()
{
    MeshData mesh;
    for (int i = 0; i < 6; ++i) {
        mesh.positions.emplace_back(static_cast<float>(i), static_cast<float>(i % 2), 0.0f);
        mesh.normals.emplace_back(0.0f, 0.0f, 1.0f);
        mesh.uvs.emplace_back(0.0f, 0.0f);
    }
    mesh.indices = { 0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5 };
    return mesh;
}

MeshData MakeClusteredMesh()
{
    MeshData mesh = MakeMesh();
    mesh.clusters = { MakeCluster(0, 6), MakeCluster(6, 6) };
    return mesh;
}

// Appends a raw cluster tail (with a correct CRC) without going through the encoder.
void AppendRawTail(std::vector<std::uint8_t>& bytes, const std::vector<MeshCluster>& clusters)
{
    bytes.insert(bytes.end(), kMeshClusterTailMagic, kMeshClusterTailMagic + 4);
    const std::size_t crcStart = bytes.size();
    const std::uint32_t count = static_cast<std::uint32_t>(clusters.size());
    const auto* countBytes = reinterpret_cast<const std::uint8_t*>(&count);
    bytes.insert(bytes.end(), countBytes, countBytes + sizeof(count));
    const auto* tableBytes = reinterpret_cast<const std::uint8_t*>(clusters.data());
    bytes.insert(bytes.end(), tableBytes, tableBytes + clusters.size() * sizeof(MeshCluster));
    const std::uint32_t crc = Crc32(std::as_bytes(std::span<const std::uint8_t>(bytes.data() + crcStart, bytes.size() - crcStart)));
    const auto* crcBytes = reinterpret_cast<const std::uint8_t*>(&crc);
    bytes.insert(bytes.end(), crcBytes, crcBytes + sizeof(crc));
}

std::size_t TailSize(std::size_t clusterCount)
{
    return 4 + 4 + clusterCount * sizeof(MeshCluster) + 4;
}

void ExpectMeshIntactClustersDropped(const std::vector<std::uint8_t>& bytes)
{
    const MeshData expected = MakeMesh();
    std::string why;
    const std::optional<MeshData> decoded = DecodeMeshDataFromBytes(bytes, &why);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->clusters.empty());
    EXPECT_FALSE(why.empty());
    EXPECT_EQ(decoded->positions, expected.positions);
    EXPECT_EQ(decoded->normals, expected.normals);
    EXPECT_EQ(decoded->uvs, expected.uvs);
    EXPECT_EQ(decoded->indices, expected.indices);
}

TEST(Crc32Test, MatchesTheStandardCheckValue)
{
    const char text[] = "123456789";
    EXPECT_EQ(Crc32(std::as_bytes(std::span<const char>(text, 9))), 0xCBF43926u);
}

TEST(Crc32Test, SeedContinuesAStream)
{
    const char text[] = "123456789";
    const auto all = std::as_bytes(std::span<const char>(text, 9));
    EXPECT_EQ(Crc32(all.subspan(4), Crc32(all.first(4))), Crc32(all));
}

TEST(MeshClusterValidationTest, AcceptsATilingTable)
{
    const std::vector<MeshCluster> table = { MakeCluster(0, 3), MakeCluster(3, 6) };
    std::string why;
    EXPECT_TRUE(ValidateClusterTable(table, 9, &why)) << why;
}

TEST(MeshClusterValidationTest, RejectsBrokenTables)
{
    std::vector<MeshCluster> table = { MakeCluster(0, 3), MakeCluster(3, 6) };
    EXPECT_FALSE(ValidateClusterTable(table, 12, nullptr)); // Does not cover the buffer.
    EXPECT_FALSE(ValidateClusterTable(table, 6, nullptr));  // Overshoots the buffer.
    EXPECT_FALSE(ValidateClusterTable({}, 9, nullptr));

    auto broken = [&](auto mutate) {
        std::vector<MeshCluster> t = table;
        mutate(t);
        return ValidateClusterTable(t, 9, nullptr);
    };
    EXPECT_FALSE(broken([](auto& t) { t[1].firstIndex = 4; }));  // Gap.
    EXPECT_FALSE(broken([](auto& t) { t[1].firstIndex = 2; }));  // Overlap.
    EXPECT_FALSE(broken([](auto& t) { std::swap(t[0], t[1]); })); // Unsorted.
    EXPECT_FALSE(broken([](auto& t) { t[0].sphere[0] = std::numeric_limits<float>::quiet_NaN(); }));
    EXPECT_FALSE(broken([](auto& t) { t[0].cone[3] = 1.5f; }));
    EXPECT_FALSE(broken([](auto& t) { t[0].cone[3] = -0.1f; }));
    EXPECT_FALSE(broken([](auto& t) { t[0].cone[2] = 2.0f; })); // Axis not unit with cutoff < 1.
    EXPECT_FALSE(broken([](auto& t) { t[0].aabbMin[1] = 5.0f; }));
    EXPECT_FALSE(broken([](auto& t) { t[0].sphere[3] = -1.0f; }));
    EXPECT_FALSE(broken([](auto& t) { t[1].indexCount = 5; }));
    EXPECT_FALSE(broken([](auto& t) { t[0].indexCount = 0; }));
}

TEST(MeshClusterValidationTest, NoConeAxisIsNotCheckedWhenCutoffIsOne)
{
    MeshCluster c = MakeCluster(0, 3);
    c.cone[0] = c.cone[1] = c.cone[2] = 0.0f;
    c.cone[3] = 1.0f;
    EXPECT_TRUE(ValidateClusterTable(std::span<const MeshCluster>(&c, 1), 3, nullptr));
}

TEST(MeshClusterValidationTest, OneTriangleClustersTileExactly)
{
    const std::vector<MeshCluster> table = { MakeCluster(0, 3), MakeCluster(3, 3) };
    EXPECT_TRUE(ValidateClusterTable(table, 6, nullptr));
}

TEST(MeshClusterValidationTest, MeshOverloadTreatsEmptyAsNotClustered)
{
    MeshData mesh = MakeMesh();
    EXPECT_TRUE(ValidateMeshClusters(mesh, nullptr));
    mesh.clusters = { MakeCluster(0, 6) };
    EXPECT_FALSE(ValidateMeshClusters(mesh, nullptr));
}

TEST(MeshClusterRebaseTest, KeepsAndRebasesKeptSlicesAndDropsTheRest)
{
    const std::vector<MeshCluster> clusters = { MakeCluster(0, 6), MakeCluster(6, 3), MakeCluster(9, 3), MakeCluster(12, 6) };
    const std::vector<ClusterSliceRange> slices = { { 0, 6, true }, { 6, 6, false }, { 12, 6, true } };
    std::vector<MeshCluster> out;
    std::string why;
    ASSERT_TRUE(RebaseClustersForRanges(clusters, slices, out, &why)) << why;
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].firstIndex, 0u);
    EXPECT_EQ(out[1].firstIndex, 6u);
    EXPECT_TRUE(ValidateClusterTable(out, 12, &why)) << why;
}

TEST(MeshClusterRebaseTest, FailsOnStraddleGapOverlapAndUnsorted)
{
    std::vector<MeshCluster> out;
    const std::vector<ClusterSliceRange> twoSlices = { { 0, 6, true }, { 6, 6, true } };
    EXPECT_FALSE(RebaseClustersForRanges(std::vector<MeshCluster>{ MakeCluster(3, 6) }, twoSlices, out, nullptr));
    EXPECT_TRUE(out.empty());

    const std::vector<ClusterSliceRange> gapSlices = { { 0, 6, true }, { 12, 6, true } };
    EXPECT_FALSE(RebaseClustersForRanges(std::vector<MeshCluster>{ MakeCluster(6, 6) }, gapSlices, out, nullptr));

    const std::vector<ClusterSliceRange> overlapping = { { 0, 6, true }, { 3, 6, true } };
    EXPECT_FALSE(RebaseClustersForRanges(std::vector<MeshCluster>{ MakeCluster(0, 3) }, overlapping, out, nullptr));

    EXPECT_FALSE(RebaseClustersForRanges(std::vector<MeshCluster>{ MakeCluster(6, 6), MakeCluster(0, 6) }, twoSlices, out, nullptr));
}

TEST(MeshClusterRebaseTest, EmptyInputGivesEmptyOutput)
{
    std::vector<MeshCluster> out = { MakeCluster(0, 3) };
    EXPECT_TRUE(RebaseClustersForRanges({}, std::vector<ClusterSliceRange>{ { 0, 6, true } }, out, nullptr));
    EXPECT_TRUE(out.empty());
}

TEST(MeshClusterFileTest, RoundTripWithClusters)
{
    const MeshData original = MakeClusteredMesh();
    const std::vector<std::uint8_t> bytes = EncodeMeshDataToBytes(original);
    ASSERT_FALSE(bytes.empty());

    std::string why;
    const std::optional<MeshData> decoded = DecodeMeshDataFromBytes(bytes, &why);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(why.empty());
    ASSERT_EQ(decoded->clusters.size(), 2u);
    EXPECT_EQ(std::memcmp(decoded->clusters.data(), original.clusters.data(), 2 * sizeof(MeshCluster)), 0);
    EXPECT_EQ(EncodeMeshDataToBytes(*decoded), bytes);
}

TEST(MeshClusterFileTest, RoundTripWithTangentsAndClusters)
{
    MeshData original = MakeClusteredMesh();
    for (int i = 0; i < 6; ++i) {
        original.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
    }
    const std::vector<std::uint8_t> bytes = EncodeMeshDataToBytes(original);
    const std::optional<MeshData> decoded = DecodeMeshDataFromBytes(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->tangents, original.tangents);
    EXPECT_EQ(decoded->clusters.size(), 2u);
}

TEST(MeshClusterFileTest, MeshWithoutClustersMatchesTheHandBuiltLayout)
{
    const MeshData mesh = MakeMesh();
    std::vector<std::uint8_t> expected(kMeshFileMagic, kMeshFileMagic + 8);
    const std::uint32_t counts[2] = { 6, 12 };
    const auto* countBytes = reinterpret_cast<const std::uint8_t*>(counts);
    expected.insert(expected.end(), countBytes, countBytes + 8);
    auto append = [&](const auto& v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(v.data());
        expected.insert(expected.end(), p, p + v.size() * sizeof(v[0]));
    };
    append(mesh.positions);
    append(mesh.normals);
    append(mesh.uvs);
    append(mesh.indices);
    EXPECT_EQ(EncodeMeshDataToBytes(mesh), expected);
}

TEST(MeshClusterFileTest, DamagedTailKeepsTheMeshAndDropsTheClusters)
{
    const std::vector<std::uint8_t> good = EncodeMeshDataToBytes(MakeClusteredMesh());
    const std::size_t tail = good.size() - TailSize(2);

    { // Truncated inside the table.
        std::vector<std::uint8_t> b = good;
        b.resize(b.size() - 3);
        ExpectMeshIntactClustersDropped(b);
    }
    { // Truncated to the magic plus a partial count.
        std::vector<std::uint8_t> b = good;
        b.resize(tail + 6);
        ExpectMeshIntactClustersDropped(b);
    }
    { // Count far larger than the data (64-bit overflow check).
        std::vector<std::uint8_t> b = good;
        const std::uint32_t huge = 0xFFFFFFFFu;
        std::memcpy(b.data() + tail + 4, &huge, 4);
        ExpectMeshIntactClustersDropped(b);
    }
    { // Count one larger than the data.
        std::vector<std::uint8_t> b = good;
        const std::uint32_t count = 3;
        std::memcpy(b.data() + tail + 4, &count, 4);
        ExpectMeshIntactClustersDropped(b);
    }
    { // Count 0.
        std::vector<std::uint8_t> b = good;
        const std::uint32_t zero = 0;
        std::memcpy(b.data() + tail + 4, &zero, 4);
        ExpectMeshIntactClustersDropped(b);
    }
    { // Trailing garbage after the table.
        std::vector<std::uint8_t> b = good;
        b.push_back(0);
        ExpectMeshIntactClustersDropped(b);
    }
    { // One flipped bit in a finite float: the checksum catches it.
        std::vector<std::uint8_t> b = good;
        b[tail + 8 + 2] ^= 0x01;
        ExpectMeshIntactClustersDropped(b);
    }
}

TEST(MeshClusterFileTest, StructurallyBadTableWithValidChecksumIsDropped)
{
    const std::vector<std::uint8_t> base = EncodeMeshDataToBytes(MakeMesh());
    auto expectDropped = [&](std::vector<MeshCluster> table) {
        std::vector<std::uint8_t> b = base;
        AppendRawTail(b, table);
        ExpectMeshIntactClustersDropped(b);
    };
    expectDropped({ MakeCluster(6, 6), MakeCluster(0, 6) }); // Unsorted.
    expectDropped({ MakeCluster(0, 6), MakeCluster(3, 9) }); // Overlap.
    expectDropped({ MakeCluster(0, 3), MakeCluster(6, 6) }); // Gap.

    MeshCluster nan = MakeCluster(0, 12);
    nan.aabbMax[2] = std::numeric_limits<float>::infinity();
    expectDropped({ nan });

    MeshCluster cutoff = MakeCluster(0, 12);
    cutoff.cone[3] = 2.0f;
    expectDropped({ cutoff });
}

TEST(MeshClusterFileTest, DamageToTheMeshItselfStillRejectsTheFile)
{
    std::vector<std::uint8_t> badMagic = EncodeMeshDataToBytes(MakeClusteredMesh());
    badMagic[0] ^= 0xFF;
    EXPECT_FALSE(DecodeMeshDataFromBytes(badMagic).has_value());

    std::vector<std::uint8_t> truncated = EncodeMeshDataToBytes(MakeClusteredMesh());
    truncated.resize(8 + 8 + 6 * 12 + 6 * 12 + 6 * 8 + 4); // Cuts into the indices.
    EXPECT_FALSE(DecodeMeshDataFromBytes(truncated).has_value());

    MeshData withTangents = MakeMesh();
    for (int i = 0; i < 6; ++i) {
        withTangents.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
    }
    std::vector<std::uint8_t> trailing = EncodeMeshDataToBytes(withTangents);
    trailing.insert(trailing.end(), 8, 0x7F);
    EXPECT_FALSE(DecodeMeshDataFromBytes(trailing).has_value());
}

TEST(MeshClusterFileTest, EncoderRefusesInconsistentMeshes)
{
    MeshData m = MakeMesh();
    m.normals.pop_back();
    EXPECT_TRUE(EncodeMeshDataToBytes(m).empty());

    m = MakeMesh();
    m.uvs.emplace_back(0.0f, 0.0f);
    EXPECT_TRUE(EncodeMeshDataToBytes(m).empty());

    m = MakeMesh();
    m.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(EncodeMeshDataToBytes(m).empty());

    m = MakeMesh();
    m.indices[4] = 6;
    EXPECT_TRUE(EncodeMeshDataToBytes(m).empty());

    m = MakeClusteredMesh();
    m.clusters[1].firstIndex = 7;
    EXPECT_TRUE(EncodeMeshDataToBytes(m).empty());

    EXPECT_FALSE(EncodeMeshDataToBytes(MakeMesh()).empty());
}

TEST(MeshClusterFileTest, CountsAboveThirtyTwoBitsDoNotFit)
{
    EXPECT_TRUE(MeshCountsFitUint32(10, 30, 3));
    EXPECT_TRUE(MeshCountsFitUint32(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu));
    if constexpr (sizeof(std::size_t) > 4) {
        const std::size_t tooBig = static_cast<std::size_t>(1) << 32;
        EXPECT_FALSE(MeshCountsFitUint32(tooBig, 3, 0));
        EXPECT_FALSE(MeshCountsFitUint32(3, tooBig, 0));
        EXPECT_FALSE(MeshCountsFitUint32(3, 3, tooBig));
    }
}

} // namespace
} // namespace gte
