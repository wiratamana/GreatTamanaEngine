#include "Assets/GltfLoader.h"

#include "Encoding/Utf8Path.h"
#include "GltfTestFixture.h"

#include <cmath>
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

// glTF space -> engine space (Z mirror), the same rule the loader uses.
Vec3 ToEngine(const Vec3& v)
{
    return Vec3(v.x, v.y, -v.z);
}

// Applies a node's 3x3 (rotation * scale) to a glTF-space vector.
Vec3 ApplyNodeMatrix(const Vec3& scale, const Vec4& rotation, const Vec3& v)
{
    const Vec3 s = v * scale;
    const Vec3 q = rotation.XYZ();
    return s + Cross(q, Cross(q, s) + s * rotation.w) * 2.0f;
}

void ExpectNear(const Vec3& actual, const Vec3& expected, float tolerance = 1e-4f)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

TEST_F(GltfLoaderTest, ZMirrorFlipsTheTangentHandedness)
{
    test::QuadOptions options;
    options.nodeJson = R"("scale":[2,2,2])";
    options.withTangents = true;
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(test::WriteGltfQuad(m_root, options)));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.mesh.tangents.size(), 4u);
    for (std::size_t i = 0; i < r.mesh.tangents.size(); ++i) {
        ExpectNear(r.mesh.tangents[i].XYZ(), Vec3(1, 0, 0));
        EXPECT_EQ(r.mesh.tangents[i].w, -1.0f);
        ExpectNear(r.mesh.normals[i], Vec3(0, 0, -1));
    }
}

TEST_F(GltfLoaderTest, MirroredNodeFlipsTheHandednessBack)
{
    test::QuadOptions options;
    options.nodeJson = R"("scale":[-1,1,1])";
    options.withTangents = true;
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(test::WriteGltfQuad(m_root, options)));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.mesh.tangents.size(), 4u);
    for (std::size_t i = 0; i < r.mesh.tangents.size(); ++i) {
        ExpectNear(r.mesh.tangents[i].XYZ(), Vec3(-1, 0, 0));
        EXPECT_EQ(r.mesh.tangents[i].w, 1.0f);
        ExpectNear(r.mesh.normals[i], Vec3(0, 0, -1));
    }
}

TEST_F(GltfLoaderTest, BitangentFollowsTheNodeMatrixForEveryTransform)
{
    struct Case {
        const char* nodeJson;
        Vec3 scale;
        Vec4 rotation;
    };
    const float h = 0.70710678f;
    const Case cases[] = {
        { R"("scale":[2,2,2])", Vec3(2, 2, 2), Vec4(0, 0, 0, 1) },
        { R"("scale":[-1,1,1])", Vec3(-1, 1, 1), Vec4(0, 0, 0, 1) },
        { R"("scale":[1,-1,1])", Vec3(1, -1, 1), Vec4(0, 0, 0, 1) },
        { R"("scale":[3,1,0.5])", Vec3(3, 1, 0.5f), Vec4(0, 0, 0, 1) },
        { R"("rotation":[0,0,0.70710678,0.70710678])", Vec3(1, 1, 1), Vec4(0, 0, h, h) },
    };
    int caseIndex = 0;
    for (const Case& c : cases) {
        for (const float wGltf : { 1.0f, -1.0f }) {
            const std::filesystem::path dir = m_root / ("case" + std::to_string(caseIndex++));
            std::filesystem::create_directories(dir);
            test::QuadOptions options;
            options.nodeJson = c.nodeJson;
            options.withTangents = true;
            options.tangent[3] = wGltf;
            const GltfLoadResult r = LoadGltfModel(PathToUtf8(test::WriteGltfQuad(dir, options)));
            ASSERT_TRUE(r.success) << c.nodeJson << ": " << r.message;
            ASSERT_EQ(r.mesh.tangents.size(), r.mesh.positions.size());

            const Vec3 bitangentGltf = Cross(Vec3(0, 0, 1), Vec3(1, 0, 0)) * wGltf;
            const Vec3 expected = Normalize(ToEngine(ApplyNodeMatrix(c.scale, c.rotation, bitangentGltf)));
            for (std::size_t i = 0; i < r.mesh.tangents.size(); ++i) {
                const Vec4& t = r.mesh.tangents[i];
                EXPECT_NEAR(Length(t.XYZ()), 1.0f, 1e-4f) << c.nodeJson;
                EXPECT_NEAR(Dot(t.XYZ(), r.mesh.normals[i]), 0.0f, 1e-4f) << c.nodeJson;
                ExpectNear(Cross(r.mesh.normals[i], t.XYZ()) * t.w, expected);
            }
        }
    }
}

TEST_F(GltfLoaderTest, PrimitiveWithoutTangentGetsUnitPerpendicularGeneratedTangents)
{
    test::QuadOptions options;
    options.nodeJson = R"("scale":[2,2,2])";
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(test::WriteGltfQuad(m_root, options)));
    ASSERT_TRUE(r.success) << r.message;
    ASSERT_EQ(r.mesh.tangents.size(), r.mesh.positions.size());
    for (std::size_t i = 0; i < r.mesh.tangents.size(); ++i) {
        EXPECT_NEAR(Length(r.mesh.tangents[i].XYZ()), 1.0f, 1e-4f);
        EXPECT_NEAR(Dot(r.mesh.tangents[i].XYZ(), r.mesh.normals[i]), 0.0f, 1e-4f);
    }
    EXPECT_NE(r.message.find("4 tangents (1 primitives generated, 0 vertices split)"), std::string::npos) << r.message;
}

TEST_F(GltfLoaderTest, GeneratedTangentsMatchAuthoredTangentsOfTheSameConvention)
{
    int caseIndex = 0;
    for (const char* nodeJson : { R"("scale":[2,2,2])", R"("scale":[-1,1,1])" }) {
        const std::filesystem::path authoredDir = m_root / ("authored" + std::to_string(caseIndex));
        const std::filesystem::path generatedDir = m_root / ("generated" + std::to_string(caseIndex++));
        std::filesystem::create_directories(authoredDir);
        std::filesystem::create_directories(generatedDir);

        test::QuadOptions authored;
        authored.nodeJson = nodeJson;
        authored.withTangents = true;
        test::QuadOptions generated;
        generated.nodeJson = nodeJson;

        const GltfLoadResult a = LoadGltfModel(PathToUtf8(test::WriteGltfQuad(authoredDir, authored)));
        const GltfLoadResult g = LoadGltfModel(PathToUtf8(test::WriteGltfQuad(generatedDir, generated)));
        ASSERT_TRUE(a.success) << a.message;
        ASSERT_TRUE(g.success) << g.message;
        ASSERT_EQ(a.mesh.tangents.size(), g.mesh.tangents.size()) << nodeJson;
        for (std::size_t i = 0; i < a.mesh.tangents.size(); ++i) {
            ExpectNear(g.mesh.tangents[i].XYZ(), a.mesh.tangents[i].XYZ(), 1e-3f);
            EXPECT_EQ(g.mesh.tangents[i].w, a.mesh.tangents[i].w) << nodeJson;
        }
    }
}

TEST_F(GltfLoaderTest, TangentsStayIndexAlignedWhenPrimitivesSplitVertices)
{
    test::QuadOptions options;
    options.mirrorSecondTriangleUv = true;
    options.primitiveCount = 2;
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(test::WriteGltfQuad(m_root, options)));
    ASSERT_TRUE(r.success) << r.message;
    EXPECT_GT(r.mesh.positions.size(), 8u);
    EXPECT_EQ(r.mesh.tangents.size(), r.mesh.positions.size());
    EXPECT_EQ(r.mesh.normals.size(), r.mesh.positions.size());
    EXPECT_EQ(r.mesh.uvs.size(), r.mesh.positions.size());
    for (const std::uint32_t index : r.mesh.indices) {
        EXPECT_LT(index, r.mesh.positions.size());
    }
    EXPECT_NE(r.message.find("2 primitives generated"), std::string::npos) << r.message;
    EXPECT_EQ(r.message.find(" 0 vertices split"), std::string::npos) << r.message;
}

TEST_F(GltfLoaderTest, AuthoredTangentsNeverSplitVertices)
{
    test::QuadOptions options;
    options.withTangents = true;
    options.mirrorSecondTriangleUv = true;
    options.primitiveCount = 2;
    const GltfLoadResult r = LoadGltfModel(PathToUtf8(test::WriteGltfQuad(m_root, options)));
    ASSERT_TRUE(r.success) << r.message;
    EXPECT_EQ(r.mesh.positions.size(), 8u);
    EXPECT_EQ(r.mesh.tangents.size(), 8u);
    EXPECT_NE(r.message.find("0 primitives generated, 0 vertices split"), std::string::npos) << r.message;
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
    ASSERT_EQ(r.mesh.tangents.size(), r.mesh.positions.size());
    std::size_t badTangents = 0;
    for (std::size_t i = 0; i < r.mesh.tangents.size(); ++i) {
        const Vec3 t = r.mesh.tangents[i].XYZ();
        const bool ok = std::abs(Length(t) - 1.0f) < 1e-3f && std::abs(Dot(t, r.mesh.normals[i])) < 1e-3f
            && std::abs(r.mesh.tangents[i].w) == 1.0f;
        badTangents += ok ? 0 : 1;
    }
    EXPECT_EQ(badTangents, 0u);
    std::size_t covered = 0;
    for (const Material& m : r.materials.materials) {
        covered += m.indexCount;
    }
    EXPECT_EQ(covered, r.mesh.indices.size()); // Every Sponza primitive has a material.
}

} // namespace
} // namespace gte
