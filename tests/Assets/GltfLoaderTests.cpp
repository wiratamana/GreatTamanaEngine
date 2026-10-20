#include "Assets/GltfLoader.h"

#include "Encoding/Utf8Path.h"
#include "GltfTestFixture.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

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

    std::filesystem::path WriteTriangle(bool withMaterial)
    {
        return withMaterial ? test::WriteGltfTriangle(m_root, test::kMaskedTexturedMaterialJson, true)
                            : test::WriteGltfTriangle(m_root);
    }

    std::filesystem::path m_root;
};

TEST_F(GltfLoaderTest, BakesNodeScaleAndMirrorsZ)
{
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(WriteTriangle(false)));
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
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(WriteTriangle(false)));
    ASSERT_TRUE(r.success) << r.message;
    const std::vector<std::uint32_t> expected = { 0, 2, 1 };
    EXPECT_EQ(r.mesh.indices, expected);
    ASSERT_EQ(r.mesh.uvs.size(), r.mesh.positions.size());
}

TEST_F(GltfLoaderTest, PrimitiveWithoutMaterialGetsADefaultMaterialRun)
{
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(WriteTriangle(false)));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.materials.materials.size(), 1u);
    EXPECT_EQ(r.materials.materials[0].indexCount, 3u);
    EXPECT_EQ(r.materials.materials[0].textureIndex, -1);
}

TEST_F(GltfLoaderTest, ReportsMaterialTextureSlotsAndAlphaMode)
{
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(WriteTriangle(true)));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.materials.materials.size(), 1u);
    ASSERT_EQ(r.materials.textures.size(), 1u); // Same image reused by two slots.
    const Material& m = r.materials.materials[0];
    EXPECT_EQ(m.textureIndex, 0);
    EXPECT_EQ(m.normalTextureIndex, 0);
    EXPECT_EQ(m.indexCount, 3u);
    EXPECT_EQ(m.alphaMode, AlphaMode::Mask);
    EXPECT_FLOAT_EQ(m.alphaCutoff, 0.25f);
    EXPECT_TRUE(m.useBaseColorFactor);
    EXPECT_NE(r.materials.textures[0].sourcePath.find("white.png"), std::string::npos);
}

TEST_F(GltfLoaderTest, UntexturedMaterialKeepsItsBaseColorFactor)
{
    const std::filesystem::path path = test::WriteGltfTriangle(m_root,
        R"(,"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1]}}])", /*primitiveHasMaterial=*/true);
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(path));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.materials.materials.size(), 1u);
    const Material& m = r.materials.materials[0];
    EXPECT_EQ(m.textureIndex, -1);
    EXPECT_FLOAT_EQ(m.diffuse.x, 1.0f);
    EXPECT_FLOAT_EQ(m.diffuse.y, 0.0f);
    EXPECT_FLOAT_EQ(m.diffuse.z, 0.0f);
    EXPECT_FLOAT_EQ(m.diffuse.w, 1.0f);
}

TEST_F(GltfLoaderTest, LoadsMetallicRoughnessNormalScaleAndOcclusionStrength)
{
    const std::filesystem::path path = test::WriteGltfTriangle(m_root,
        R"(,"materials":[{"pbrMetallicRoughness":{"metallicFactor":0.25,"roughnessFactor":0.75},
              "normalTexture":{"index":0,"scale":2},"occlusionTexture":{"index":0,"strength":0.5}}],
             "textures":[{"source":0}],"images":[{"uri":"white.png"}])", /*primitiveHasMaterial=*/true);
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(path));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.materials.materials.size(), 1u);
    const Material& m = r.materials.materials[0];
    EXPECT_FLOAT_EQ(m.metallicFactor, 0.25f);
    EXPECT_FLOAT_EQ(m.roughnessFactor, 0.75f);
    EXPECT_FLOAT_EQ(m.normalScale, 2.0f);
    EXPECT_FLOAT_EQ(m.occlusionStrength, 0.5f);
    EXPECT_EQ(m.normalTextureIndex, 0);
    EXPECT_EQ(m.occlusionTextureIndex, 0);
}

TEST_F(GltfLoaderTest, MaterialWithoutPbrObjectOrTexturesUsesDefaultFactors)
{
    const std::filesystem::path path = test::WriteGltfTriangle(m_root, R"(,"materials":[{"name":"plain"}])",
        /*primitiveHasMaterial=*/true);
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(path));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.materials.materials.size(), 1u);
    const Material& m = r.materials.materials[0];
    EXPECT_FLOAT_EQ(m.metallicFactor, 1.0f);
    EXPECT_FLOAT_EQ(m.roughnessFactor, 1.0f);
    EXPECT_FLOAT_EQ(m.normalScale, 1.0f);
    EXPECT_FLOAT_EQ(m.occlusionStrength, 1.0f);
    EXPECT_EQ(r.message.find("unsupported"), std::string::npos) << r.message;
}

TEST_F(GltfLoaderTest, ReportsUnsupportedTexcoordSetsAndShadingModels)
{
    const std::filesystem::path path = test::WriteGltfTriangle(m_root,
        R"(,"materials":[{"normalTexture":{"index":0,"texCoord":1},
              "extensions":{"KHR_materials_unlit":{}}}],
             "textures":[{"source":0}],"images":[{"uri":"white.png"}],
             "extensionsUsed":["KHR_materials_unlit"])", /*primitiveHasMaterial=*/true);
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(path));
    ASSERT_TRUE(r.success) << r.message;
    EXPECT_NE(r.message.find("1 texture slots use TEXCOORD_1+ (unsupported)."), std::string::npos) << r.message;
    EXPECT_NE(r.message.find("1 materials use an unsupported shading model."), std::string::npos) << r.message;
}

TEST_F(GltfLoaderTest, FailsGracefullyOnAMissingFile)
{
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(m_root / "nope.gltf"));
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
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(path));
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
