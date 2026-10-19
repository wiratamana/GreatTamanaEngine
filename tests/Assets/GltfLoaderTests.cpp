#include "Assets/GltfLoader.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

std::string ToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

class GltfLoaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteGltfLoaderTest_") + info->test_suite_name() + "_" + info->name());
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    // One triangle (0,0,0) (1,0,0) (0,1,0), normals +Z, uint16 indices, node scale 2.
    std::filesystem::path WriteTriangle(bool withMaterial)
    {
        std::vector<std::uint8_t> bin(80, 0);
        const float floats[18] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
        std::memcpy(bin.data(), floats, sizeof(floats));
        const std::uint16_t indices[3] = { 0, 1, 2 };
        std::memcpy(bin.data() + 72, indices, sizeof(indices));
        std::ofstream(m_root / "tri.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));

        const std::string materialJson = withMaterial
            ? R"(,"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},
                  "normalTexture":{"index":0},"alphaMode":"MASK","alphaCutoff":0.25}],
                 "textures":[{"source":0}],"images":[{"uri":"white.png"}])"
            : "";
        const std::string primitiveMaterial = withMaterial ? R"(,"material":0)" : "";

        const std::string gltf = R"({"asset":{"version":"2.0"},
          "buffers":[{"uri":"tri.bin","byteLength":80}],
          "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
                         {"buffer":0,"byteOffset":36,"byteLength":36},
                         {"buffer":0,"byteOffset":72,"byteLength":6}],
          "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
                       {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
                       {"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}],
          "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2)" + primitiveMaterial + R"(}]}],
          "nodes":[{"mesh":0,"scale":[2,2,2]}],
          "scenes":[{"nodes":[0]}],"scene":0)" + materialJson + "}";

        const std::filesystem::path path = m_root / "tri.gltf";
        std::ofstream(path, std::ios::binary).write(gltf.data(), static_cast<std::streamsize>(gltf.size()));
        return path;
    }

    std::filesystem::path m_root;
};

TEST_F(GltfLoaderTest, BakesNodeScaleAndMirrorsZ)
{
    const GltfLoadResult r = LoadGltfModel(ToUtf8(WriteTriangle(false)));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.mesh.positions.size(), 3u);
    EXPECT_FLOAT_EQ(r.mesh.positions[1].x, 2.0f);
    EXPECT_FLOAT_EQ(r.mesh.positions[2].y, 2.0f);
    for (const Vec3& n : r.mesh.normals) {
        EXPECT_FLOAT_EQ(n.z, -1.0f); // +Z became -Z
    }
}

TEST_F(GltfLoaderTest, SwapsWindingForTheMirrorAndFillsMissingUvs)
{
    const GltfLoadResult r = LoadGltfModel(ToUtf8(WriteTriangle(false)));
    ASSERT_TRUE(r.success) << r.message;
    const std::vector<std::uint32_t> expected = { 0, 2, 1 };
    EXPECT_EQ(r.mesh.indices, expected);
    ASSERT_EQ(r.mesh.uvs.size(), r.mesh.positions.size());
}

TEST_F(GltfLoaderTest, PrimitiveWithoutMaterialGetsADefaultMaterialRun)
{
    const GltfLoadResult r = LoadGltfModel(ToUtf8(WriteTriangle(false)));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.materials.materials.size(), 1u);
    EXPECT_EQ(r.materials.materials[0].indexCount, 3u);
    EXPECT_EQ(r.materials.materials[0].textureIndex, -1);
}

TEST_F(GltfLoaderTest, ReportsMaterialTextureSlotsAndAlphaMode)
{
    const GltfLoadResult r = LoadGltfModel(ToUtf8(WriteTriangle(true)));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.materials.materials.size(), 1u);
    ASSERT_EQ(r.materials.textures.size(), 1u); // Same image reused by two slots.
    const Material& m = r.materials.materials[0];
    EXPECT_EQ(m.textureIndex, 0);
    EXPECT_EQ(m.normalTextureIndex, 0);
    EXPECT_EQ(m.indexCount, 3u);
    EXPECT_EQ(m.alphaMode, AlphaMode::Mask);
    EXPECT_FLOAT_EQ(m.alphaCutoff, 0.25f);
    EXPECT_NE(r.materials.textures[0].sourcePath.find("white.png"), std::string::npos);
}

TEST_F(GltfLoaderTest, FailsGracefullyOnAMissingFile)
{
    const GltfLoadResult r = LoadGltfModel(ToUtf8(m_root / "nope.gltf"));
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.message.empty());
}

// The real Sponza, skipped when it is not on this machine.
TEST(GltfLoaderRealModelSmokeTest, LoadsSponzaIfPresentOnThisMachine)
{
    const std::filesystem::path path = std::filesystem::path(GTE_TEST_SPONZA_DIR) / "glTF" / "Sponza.gltf";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        GTEST_SKIP() << "Sponza not present - skipping.";
    }
    const GltfLoadResult r = LoadGltfModel(ToUtf8(path));
    ASSERT_TRUE(r.success) << r.message;
    EXPECT_EQ(r.materials.materials.size(), 25u);
    EXPECT_EQ(r.mesh.normals.size(), r.mesh.positions.size());
    EXPECT_EQ(r.mesh.uvs.size(), r.mesh.positions.size());
    std::size_t covered = 0;
    for (const Material& m : r.materials.materials) {
        covered += m.indexCount;
    }
    EXPECT_EQ(covered, r.mesh.indices.size()); // Every Sponza primitive has a material.
}

} // namespace
} // namespace gte
