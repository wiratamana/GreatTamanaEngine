// Pins the toon shader contract: files, includes, bindings, class ids and the uniform block tail.

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

namespace gte {
namespace {

std::string ReadSrc(const std::string& relativePath)
{
    std::ifstream file(std::string(GTE_REPO_SRC_DIR) + "/" + relativePath, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string ReadToonShader(const char* fileName)
{
    return ReadSrc(std::string("Features/ToonCharacter/Shaders/") + fileName);
}

bool Has(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

TEST(ToonShaderPinTest, EveryShaderFileExists)
{
    for (const char* name : { "ToonSurface.vert", "ToonSurface.frag", "ToonOutline.vert", "ToonOutline.frag",
                              "ToonOverlay.vert", "ToonOverlay.frag", "ToonCommon.glsl" }) {
        EXPECT_FALSE(ReadToonShader(name).empty()) << name;
    }
}

TEST(ToonShaderPinTest, SurfaceUsesTheSharedLightingAndTheSphereBinding)
{
    const std::string frag = ReadToonShader("ToonSurface.frag");
    EXPECT_TRUE(Has(frag, "#include \"DirectionalLighting.glsl\""));
    EXPECT_TRUE(Has(frag, "layout(set = 0, binding = 5) uniform sampler2D uSphereTexture"));
    EXPECT_TRUE(Has(frag, "SunShadowAmount("));
    EXPECT_TRUE(Has(frag, "ShadeDirectionalLinear("));
    EXPECT_TRUE(Has(frag, "gl_FrontFacing"));

    const std::string overlay = ReadToonShader("ToonOverlay.frag");
    EXPECT_TRUE(Has(overlay, "layout(set = 0, binding = 5) uniform sampler2D uSphereTexture"));
}

TEST(ToonShaderPinTest, ClassIdsAndShadeFloorAreFixed)
{
    const std::string common = ReadToonShader("ToonCommon.glsl");
    EXPECT_TRUE(Has(common, "const int kToonCloth = 0;"));
    EXPECT_TRUE(Has(common, "const int kToonSkin = 1;"));
    EXPECT_TRUE(Has(common, "const int kToonFaceSkin = 2;"));
    EXPECT_TRUE(Has(common, "const int kToonFaceLine = 3;"));
    EXPECT_TRUE(Has(common, "const int kToonEye = 4;"));
    EXPECT_TRUE(Has(common, "const int kToonHair = 5;"));
    EXPECT_TRUE(Has(common, "const int kToonHairOverlay = 6;"));
    EXPECT_TRUE(Has(common, "const int kToonGem = 7;"));
    EXPECT_TRUE(Has(common, "const int kToonClothBack = 8;"));
    EXPECT_TRUE(Has(common, "const float kToonMinShadeLuma = 0.35;"));
}

TEST(ToonShaderPinTest, OutlineReadsEdgeDataFromTheMaterialParams)
{
    const std::string vert = ReadToonShader("ToonOutline.vert");
    EXPECT_TRUE(Has(vert, "uMaterial.pbr.w"));
    EXPECT_TRUE(Has(vert, "uMaterial.pbr.z"));
    EXPECT_TRUE(Has(vert, "gl_Position = vec4(2.0, 2.0, 2.0, 1.0);"));
    EXPECT_TRUE(Has(vert, "DIRECTIONAL_LIGHTING_NO_SHADOWS"));
}

TEST(ToonShaderPinTest, UniformBlockEndsWithTheToonLookRows)
{
    const std::string glsl = ReadSrc("Shaders/DirectionalLighting.glsl");
    const std::size_t cascade3 = glsl.find("mat4 cascadeViewProjection3;");
    const std::size_t look0 = glsl.find("vec4 toonLook0;");
    const std::size_t look1 = glsl.find("vec4 toonLook1;");
    const std::size_t look2 = glsl.find("vec4 toonLook2;");
    const std::size_t look3 = glsl.find("vec4 toonLook3;");
    const std::size_t blockEnd = glsl.find("} uSceneLighting;");
    ASSERT_NE(cascade3, std::string::npos);
    ASSERT_NE(blockEnd, std::string::npos);
    EXPECT_LT(cascade3, look0);
    EXPECT_LT(look0, look1);
    EXPECT_LT(look1, look2);
    EXPECT_LT(look2, look3);
    EXPECT_LT(look3, blockEnd);
}

} // namespace
} // namespace gte
