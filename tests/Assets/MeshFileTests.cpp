// Unit tests for src/Assets/MeshFile.h - EncodeMeshDataToBytes()/
// DecodeMeshDataFromBytes() round-tripping the *.gta AssetType::Mesh
// payload's binary layout. No GPU/SDL/ImGui involved - "Tier 1" per
// tests/CMakeLists.txt's own taxonomy.

#include "Assets/MeshFile.h"

#include <cstring>

#include <gtest/gtest.h>

namespace gte {
namespace {

MeshData BuildTriangleMeshData()
{
    MeshData mesh;
    mesh.positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f) };
    mesh.normals = { Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, 1.0f) };
    mesh.uvs = { Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f) };
    mesh.indices = { 0, 1, 2 };
    return mesh;
}

TEST(MeshFileTest, EncodeThenDecodeRoundTripsAllArraysExactly)
{
    const MeshData original = BuildTriangleMeshData();
    const std::vector<std::uint8_t> encoded = EncodeMeshDataToBytes(original);

    const std::optional<MeshData> decoded = DecodeMeshDataFromBytes(encoded);
    ASSERT_TRUE(decoded.has_value());

    ASSERT_EQ(decoded->positions.size(), original.positions.size());
    ASSERT_EQ(decoded->normals.size(), original.normals.size());
    ASSERT_EQ(decoded->uvs.size(), original.uvs.size());
    ASSERT_EQ(decoded->indices.size(), original.indices.size());

    for (std::size_t i = 0; i < original.positions.size(); ++i) {
        EXPECT_EQ(decoded->positions[i], original.positions[i]);
        EXPECT_EQ(decoded->normals[i], original.normals[i]);
        EXPECT_EQ(decoded->uvs[i], original.uvs[i]);
    }
    for (std::size_t i = 0; i < original.indices.size(); ++i) {
        EXPECT_EQ(decoded->indices[i], original.indices[i]);
    }
}

TEST(MeshFileTest, EncodesAnEmptyMeshAsAHeaderOnlyBlobThatDecodesBackToEmpty)
{
    const MeshData empty;
    const std::vector<std::uint8_t> encoded = EncodeMeshDataToBytes(empty);

    const std::optional<MeshData> decoded = DecodeMeshDataFromBytes(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->positions.empty());
    EXPECT_TRUE(decoded->normals.empty());
    EXPECT_TRUE(decoded->uvs.empty());
    EXPECT_TRUE(decoded->indices.empty());
}

TEST(MeshFileTest, DecodeFailsOnEmptyBytes)
{
    EXPECT_FALSE(DecodeMeshDataFromBytes(std::vector<std::uint8_t>{}).has_value());
}

TEST(MeshFileTest, DecodeFailsOnGarbageBytes)
{
    const std::vector<std::uint8_t> garbage(64, 0xAB);
    EXPECT_FALSE(DecodeMeshDataFromBytes(garbage).has_value());
}

TEST(MeshFileTest, DecodeFailsOnATruncatedButOtherwiseValidBlob)
{
    const MeshData original = BuildTriangleMeshData();
    std::vector<std::uint8_t> encoded = EncodeMeshDataToBytes(original);
    encoded.resize(encoded.size() - 4); // Chop off the last few bytes of the index array.

    EXPECT_FALSE(DecodeMeshDataFromBytes(encoded).has_value());
}

TEST(MeshFileTest, DecodeFailsWhenMagicIsWrongEvenIfSizeWouldOtherwiseFit)
{
    MeshData original = BuildTriangleMeshData();
    std::vector<std::uint8_t> encoded = EncodeMeshDataToBytes(original);
    encoded[0] = static_cast<std::uint8_t>(~encoded[0]); // Corrupt the magic's first byte.

    EXPECT_FALSE(DecodeMeshDataFromBytes(encoded).has_value());
}

} // namespace
} // namespace gte
