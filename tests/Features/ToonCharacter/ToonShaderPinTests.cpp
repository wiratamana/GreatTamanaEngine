// Pins the toon shader contract: files, includes, bindings, class ids and the uniform block tail.

#include "Game/Lighting/ToonClass.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <utility>

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
}

TEST(ToonShaderPinTest, ClassIdsMatchTheEnumAndShadeFloorLiftsTowardWhite)
{
    const std::string common = ReadToonShader("ToonCommon.glsl");
    const std::pair<const char*, ToonClass> ids[] = {
        { "kToonCloth", ToonClass::Cloth }, { "kToonSkin", ToonClass::Skin },
        { "kToonFaceSkin", ToonClass::FaceSkin }, { "kToonFaceLine", ToonClass::FaceLine },
        { "kToonEye", ToonClass::Eye }, { "kToonHair", ToonClass::Hair },
        { "kToonHairOverlay", ToonClass::HairOverlay }, { "kToonGem", ToonClass::Gem },
        { "kToonClothBack", ToonClass::ClothBack } };
    EXPECT_EQ(std::size(ids), kToonClassCount);
    for (const auto& [name, id] : ids) {
        const std::string line = std::string("const int ") + name + " = " + std::to_string(static_cast<int>(id)) + ";";
        EXPECT_TRUE(Has(common, line.c_str())) << line;
    }
    EXPECT_TRUE(Has(common, "const float kToonMinShadeLuma = 0.35;"));
    EXPECT_FALSE(Has(common, "ToonClassHasNoOutline"));

    const std::string frag = ReadToonShader("ToonSurface.frag");
    EXPECT_TRUE(Has(frag, "tone = mix(tone, vec3(1.0), (kToonMinShadeLuma - toneLuma) / max(1.0 - toneLuma, 1e-4));"));
    EXPECT_FALSE(Has(frag, "tone *="));
}

TEST(ToonShaderPinTest, TintTablesMatchTheCpuTable)
{
    const std::string common = ReadToonShader("ToonCommon.glsl");
    EXPECT_TRUE(Has(common, "vec3(0.65, 0.62, 0.83), vec3(0.92, 0.74, 0.72), vec3(0.97, 0.86, 0.84),"));
    EXPECT_TRUE(Has(common, "vec3(1.0), vec3(1.0), vec3(0.62, 0.70, 0.93),"));
    EXPECT_TRUE(Has(common, "vec3(1.0), vec3(1.0), vec3(0.65, 0.62, 0.83));"));
    EXPECT_TRUE(Has(common, "vec3(0.80, 0.82, 0.92), vec3(1.0), vec3(1.0),"));
    EXPECT_TRUE(Has(common, "vec3(1.0), vec3(1.0), vec3(0.85, 0.88, 0.95),"));
    EXPECT_TRUE(Has(common, "vec3(0.30, 0.36, 0.62), vec3(0.30, 0.36, 0.62), vec3(0.03, 0.05, 0.14),"));
    EXPECT_FALSE(Has(common, "kToonRingTint"));
}

TEST(ToonShaderPinTest, HairRingIsAMaskedBandWithTheHighlightAsOpacity)
{
    const std::string overlay = ReadToonShader("ToonOverlay.frag");
    EXPECT_TRUE(Has(overlay, "ToonCameraAxes(right, up);"));
    EXPECT_TRUE(Has(overlay, "float mask = smoothstep(0.30, 0.50, ny) * (1.0 - smoothstep(0.62, 0.80, ny));"));
    EXPECT_TRUE(Has(overlay, "clamp(look.hairHighlight, 0.0, 1.0) * mask"));
    EXPECT_TRUE(Has(overlay, "if (alpha < 0.01) { discard; }"));
    EXPECT_TRUE(Has(overlay, "ShadeDirectionalLinear(vec3(1.0), normalize(SceneSunDirection()), true, inWorldPos)"));
    EXPECT_TRUE(Has(overlay, "outColor = vec4(min(kToonRingBand * lit, vec3(kToonMaxRadiance)), alpha);"));
    EXPECT_TRUE(Has(ReadToonShader("ToonCommon.glsl"), "const vec3 kToonRingBand = vec3(0.45, 0.65, 1.00);"));
    // The layer texture is a faint alpha mask (alpha below the cutoff): sampling or clipping on it draws nothing.
    EXPECT_FALSE(Has(overlay, "uDiffuseTexture"));
    EXPECT_FALSE(Has(overlay, "kToonAlphaCutoff"));
    EXPECT_FALSE(Has(overlay, "uSphereTexture"));
    EXPECT_FALSE(Has(overlay, "kToonRingTint"));
    EXPECT_TRUE(Has(ReadSrc("Features/ToonCharacter/ToonCharacterFeature.h"), "BlendMode::AlphaBlend"));
    EXPECT_FALSE(Has(ReadSrc("Features/ToonCharacter/ToonCharacterFeature.h"), "BlendMode::Additive"));
}

TEST(ToonShaderPinTest, OverlayDebugViewsDrawEveryHairOverlayFragment)
{
    const std::string overlay = ReadToonShader("ToonOverlay.frag");
    const std::size_t classTest = overlay.find("!= kToonHairOverlay) { discard; }");
    const std::size_t view6 = overlay.find("if (look.debugView == 6) { outColor = vec4(1.0, 0.0, 1.0, 1.0); return; }");
    const std::size_t viewDiscard = overlay.find("if (look.debugView != 0 && look.debugView < 6) { discard; }");
    const std::size_t view7 = overlay.find("look.debugView == 7");
    const std::size_t view8 = overlay.find("if (look.debugView == 8) { outColor = vec4(vec3(ny * 0.5 + 0.5), 1.0); return; }");
    const std::size_t alphaDiscard = overlay.find("if (alpha < 0.01) { discard; }");
    ASSERT_NE(classTest, std::string::npos);
    ASSERT_NE(view6, std::string::npos);
    ASSERT_NE(viewDiscard, std::string::npos);
    ASSERT_NE(view7, std::string::npos);
    ASSERT_NE(view8, std::string::npos);
    EXPECT_LT(classTest, view6);
    EXPECT_LT(view6, viewDiscard);
    EXPECT_LT(view7, alphaDiscard);
    EXPECT_LT(view8, alphaDiscard);
    EXPECT_TRUE(Has(overlay, "(mask > 0.01) ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(vec3(clamp(ny, 0.0, 1.0)), 1.0)"));
    // Views 6..8 keep the normal look in the other passes.
    EXPECT_TRUE(Has(ReadToonShader("ToonOutline.frag"), "look.debugView >= 1 && look.debugView <= 5 && look.debugView != 4"));
    EXPECT_FALSE(Has(ReadToonShader("ToonSurface.frag"), "debugView == 6"));
}

TEST(ToonShaderPinTest, OverlayLayerIsClippedByClassInTheVertexStage)
{
    const std::string vertex = ReadToonShader("ToonSurfaceVertex.glsl");
    EXPECT_TRUE(Has(vertex, "if (overlayClass) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }"));
    EXPECT_TRUE(Has(vertex, "if (!overlayClass) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }"));
    EXPECT_TRUE(Has(vertex, "gl_Position.z -= TOON_DEPTH_NUDGE * gl_Position.w;"));
    EXPECT_TRUE(Has(ReadToonShader("ToonSurface.vert"), "#define TOON_SKIP_OVERLAY_CLASS"));
    EXPECT_FALSE(Has(ReadToonShader("ToonSurface.vert"), "TOON_DEPTH_NUDGE"));
    const std::string overlayVert = ReadToonShader("ToonOverlay.vert");
    EXPECT_TRUE(Has(overlayVert, "#define TOON_KEEP_ONLY_OVERLAY_CLASS"));
    EXPECT_TRUE(Has(overlayVert, "#define TOON_DEPTH_NUDGE 2e-5"));
}

TEST(ToonShaderPinTest, ToonLookReaderLivesInTheToonHeaderOnly)
{
    const std::string common = ReadToonShader("ToonCommon.glsl");
    EXPECT_TRUE(Has(common, "struct ToonLookParams"));
    EXPECT_TRUE(Has(common, "ToonLookParams ReadToonLook()"));
    const std::string lighting = ReadSrc("Shaders/DirectionalLighting.glsl");
    EXPECT_FALSE(Has(lighting, "ToonLookParams"));
    EXPECT_FALSE(Has(lighting, "ReadToonLook"));
}

TEST(ToonShaderPinTest, SurfaceAndOverlayShareAnInvariantPosition)
{
    EXPECT_TRUE(Has(ReadToonShader("ToonSurfaceVertex.glsl"), "invariant gl_Position;"));
    EXPECT_TRUE(Has(ReadToonShader("ToonSurface.vert"), "#include \"ToonSurfaceVertex.glsl\""));
    EXPECT_TRUE(Has(ReadToonShader("ToonOverlay.vert"), "#include \"ToonSurfaceVertex.glsl\""));
}

TEST(ToonShaderPinTest, OutlineReadsEdgeDataFromTheMaterialParams)
{
    const std::string vert = ReadToonShader("ToonOutline.vert");
    EXPECT_TRUE(Has(vert, "uMaterial.pbr.w < 0.5"));
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
