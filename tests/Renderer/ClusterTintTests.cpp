// Tier-1 checks of the shared cluster tint hash: shader text and known-answer hash bytes.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

namespace gte {
namespace {

std::string ReadShaderFile(const char* name)
{
    std::ifstream file(std::string(GTE_TEST_SHADERS_DIR) + "/" + name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

// Same formula as ClusterTint.glsl, returning the three hash bytes before the 0.7 / 0.2 scaling.
std::array<std::uint32_t, 3> HashBytes(std::uint32_t id)
{
    std::uint32_t h = id * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return { h & 255u, (h >> 8u) & 255u, (h >> 16u) & 255u };
}

TEST(ClusterTint, HashConstantsLiveInTheSharedInclude)
{
    const std::string text = ReadShaderFile("ClusterTint.glsl");
    ASSERT_FALSE(text.empty());
    EXPECT_TRUE(Contains(text, "747796405u"));
    EXPECT_TRUE(Contains(text, "2891336453u"));
    EXPECT_TRUE(Contains(text, "277803737u"));
    EXPECT_TRUE(Contains(text, "vec3 ClusterTint(uint id)"));
}

TEST(ClusterTint, EngineAndViewerShadersIncludeTheHashAndDoNotRedefineIt)
{
    const std::string engine = ReadShaderFile("MeshClusterTint.frag");
    ASSERT_FALSE(engine.empty());
    EXPECT_TRUE(Contains(engine, "#include \"ClusterTint.glsl\""));
    EXPECT_FALSE(Contains(engine, "vec3 ClusterTint("));
    EXPECT_FALSE(Contains(engine, "747796405u"));

    const std::string shading = ReadShaderFile("ClusterViewShading.glsl");
    ASSERT_FALSE(shading.empty());
    EXPECT_TRUE(Contains(shading, "#include \"ClusterTint.glsl\""));
    EXPECT_FALSE(Contains(shading, "vec3 ClusterTint("));
}

// Known answers worked out by hand with the formula in ClusterTint.glsl.
TEST(ClusterTint, HashKnownAnswers)
{
    EXPECT_EQ(HashBytes(0), (std::array<std::uint32_t, 3>{ 226u, 47u, 187u }));
    EXPECT_EQ(HashBytes(1), (std::array<std::uint32_t, 3>{ 60u, 234u, 190u }));
    EXPECT_EQ(HashBytes(2), (std::array<std::uint32_t, 3>{ 136u, 204u, 126u }));
}

} // namespace
} // namespace gte
