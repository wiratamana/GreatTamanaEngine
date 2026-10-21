// Text checks of the shaders that share the orthographic map set lookup and the clear-depth constant.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace gte {
namespace {

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// Every macro the shared lookup needs must be defined before the includer pulls it in.
void ExpectOrthoMacrosBeforeTheInclude(const std::filesystem::path& path)
{
    const std::string text = ReadText(path);
    ASSERT_FALSE(text.empty()) << path;
    const std::size_t include = text.find("#include \"OrthoMapSetVisibility.glsl\"");
    ASSERT_NE(include, std::string::npos) << path;

    for (const std::string_view macro : { "ORTHO_VIEW_PROJECTION(k)", "ORTHO_SPLIT_FAR(k)", "ORTHO_BLEND_WIDTH(k)",
             "ORTHO_TEXEL_WORLD(k)", "ORTHO_DEPTH_RANGE(k)", "ORTHO_EYE ", "ORTHO_FORWARD ", "ORTHO_COUNT ",
             "ORTHO_TAPS ", "ORTHO_BIAS_TEXELS ", "ORTHO_FILTER_TEXELS ", "ORTHO_INV_MAP_SIZE " }) {
        const std::size_t define = text.find("#define " + std::string(macro));
        ASSERT_NE(define, std::string::npos) << path << " " << macro;
        EXPECT_LT(define, include) << path << " " << macro;
    }
    const std::size_t depthFunction = text.find("float OrthoMapDepthAt(int k, vec2 uv)");
    ASSERT_NE(depthFunction, std::string::npos) << path;
    EXPECT_LT(depthFunction, include) << path;
}

TEST(ShaderContractTest, AerialVolumeDefinesTheOrthoMacrosBeforeTheInclude)
{
    ExpectOrthoMacrosBeforeTheInclude(
        std::filesystem::path(GTE_TEST_ATMOSPHERE_SHADER_DIR) / "AtmosphereAerialPerspectiveVolume.comp");
}

TEST(ShaderContractTest, VolumetricInjectDefinesTheOrthoMacrosBeforeTheInclude)
{
    ExpectOrthoMacrosBeforeTheInclude(
        std::filesystem::path(GTE_TEST_VOLUMETRIC_SHADER_DIR) / "VolumetricLightInject.comp");
}

TEST(ShaderContractTest, TheClearDepthLiteralLivesInOneFile)
{
    std::size_t scanned = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(GTE_TEST_SHADERS_DIR)) {
        const std::string extension = entry.path().extension().string();
        if (extension != ".glsl" || entry.path().filename() == "SceneDepthConstants.glsl") {
            continue;
        }
        ++scanned;
        EXPECT_EQ(ReadText(entry.path()).find("0.999999"), std::string::npos) << entry.path();
    }
    EXPECT_GT(scanned, 0u);

    for (const std::filesystem::path dir : { std::filesystem::path(GTE_TEST_ATMOSPHERE_SHADER_DIR),
             std::filesystem::path(GTE_TEST_VOLUMETRIC_SHADER_DIR), std::filesystem::path(GTE_TEST_SHADOW_SHADER_DIR) }) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string extension = entry.path().extension().string();
            if (extension == ".comp" || extension == ".glsl") {
                EXPECT_EQ(ReadText(entry.path()).find("0.999999"), std::string::npos) << entry.path();
            }
        }
    }
}

TEST(ShaderContractTest, InterleavedGradientNoiseHasOneDefinitionFile)
{
    for (const std::filesystem::path dir : { std::filesystem::path(GTE_TEST_ATMOSPHERE_SHADER_DIR),
             std::filesystem::path(GTE_TEST_VOLUMETRIC_SHADER_DIR), std::filesystem::path(GTE_TEST_SHADERS_DIR) }) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string extension = entry.path().extension().string();
            if ((extension != ".comp" && extension != ".glsl") || entry.path().filename() == "InterleavedGradientNoise.glsl") {
                continue;
            }
            EXPECT_EQ(ReadText(entry.path()).find("float InterleavedGradientNoise("), std::string::npos) << entry.path();
        }
    }
}

} // namespace
} // namespace gte
