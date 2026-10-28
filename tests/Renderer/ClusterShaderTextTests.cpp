// Tier-1 text checks that the cull and pyramid shaders keep the layouts of ClusterCullMath.h and take their
// constants from the generated include.

#include "Renderer/Culling/ClusterCullConstants.h"
#include "Renderer/Culling/ClusterCullMath.h"
#include "Renderer/Culling/HizBuildPlan.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace gte {
namespace {

std::string ReadTextFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string ReadShader()
{
    return ReadTextFile(GTE_TEST_CLUSTER_CULL_SHADER_PATH);
}

bool Contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

struct UintRow {
    const char* name;
    std::uint32_t value;
};

struct FloatRow {
    const char* name;
    float value;
};

// Every constant of ClusterCullConstants.h.
constexpr UintRow kUintRows[] = {
    { "kClusterFlagUniformScale", kClusterFlagUniformScale },
    { "kClusterFlagPositiveHand", kClusterFlagPositiveHand },
    { "kClusterCullFlagFrustum", kClusterCullFlagFrustum },
    { "kClusterCullFlagBackfaceCone", kClusterCullFlagBackfaceCone },
    { "kClusterCullFlagOcclusion", kClusterCullFlagOcclusion },
    { "kClusterCullFlagOrthographic", kClusterCullFlagOrthographic },
    { "kClusterCullFlagPhaseFirst", kClusterCullFlagPhaseFirst },
    { "kClusterCullFlagPhaseSecond", kClusterCullFlagPhaseSecond },
    { "kClusterCullFlagHistoryValid", kClusterCullFlagHistoryValid },
    { "kClusterCullFlagRawDepth", kClusterCullFlagRawDepth },
    { "kClusterHistoryVisible", kClusterHistoryVisible },
    { "kClusterHistoryDrawnFirst", kClusterHistoryDrawnFirst },
    { "kHizMaxLevels", kHizMaxLevels },
    { "kHizTopFirstLevel", kHizTopFirstLevel },
};

constexpr FloatRow kFloatRows[] = {
    { "kHizRectMarginTexels", kHizRectMarginTexels },
    { "kHizRelMargin", kHizRelMargin },
    { "kHizAbsMargin", kHizAbsMargin },
    { "kHizDepthGuard", kHizDepthGuard },
    { "kHizRawBias", kHizRawBias },
};

TEST(ClusterShaderText, FileIsReadable)
{
    EXPECT_FALSE(ReadShader().empty());
}

TEST(ClusterShaderText, WorkGroupSizeMatchesTheDispatchConstant)
{
    EXPECT_TRUE(Contains(ReadShader(), "layout(local_size_x = " + std::to_string(kClusterCullGroupSize) + ") in;"));
}

TEST(ClusterShaderText, GeneratedConstantsEqualTheCppConstants)
{
    const std::string text = ReadTextFile(GTE_TEST_CLUSTER_CULL_CONSTANTS_GLSL_PATH);
    ASSERT_FALSE(text.empty());

    for (const UintRow& row : kUintRows) {
        EXPECT_TRUE(Contains(text, std::string("const uint ") + row.name + " = " + std::to_string(row.value) + "u;\n"))
            << row.name;
    }
    for (const FloatRow& row : kFloatRows) {
        const std::string prefix = std::string("const float ") + row.name + " = ";
        const std::size_t at = text.find(prefix);
        ASSERT_NE(at, std::string::npos) << row.name;
        EXPECT_EQ(std::strtof(text.c_str() + at + prefix.size(), nullptr), row.value) << row.name;
    }

    // Nothing in the generated file may be missing from the tables above.
    std::size_t declarations = 0;
    for (std::size_t at = text.find("\nconst "); at != std::string::npos; at = text.find("\nconst ", at + 1)) {
        ++declarations;
    }
    EXPECT_EQ(declarations, std::size(kUintRows) + std::size(kFloatRows));
}

TEST(ClusterShaderText, ShadersIncludeTheGeneratedConstantsAndNeverRedeclareThem)
{
    const std::string shaders[] = {
        ReadShader(),
        ReadTextFile(GTE_TEST_HIZ_BUILD_BASE_SHADER_PATH),
        ReadTextFile(GTE_TEST_HIZ_BUILD_TOP_SHADER_PATH),
    };
    for (const std::string& text : shaders) {
        ASSERT_FALSE(text.empty());
        EXPECT_TRUE(Contains(text, "#include \"ClusterCullConstants.glsl\""));
        for (const UintRow& row : kUintRows) {
            EXPECT_FALSE(Contains(text, std::string("const uint ") + row.name)) << row.name;
        }
        for (const FloatRow& row : kFloatRows) {
            EXPECT_FALSE(Contains(text, std::string("const float ") + row.name)) << row.name;
        }
    }
}

TEST(ClusterShaderText, EveryBlockHasItsInstanceNameAndQualifier)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 0) readonly buffer ParamsBlock {"));
    EXPECT_TRUE(Contains(text, "} params;"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 1) readonly buffer InstancesBlock {"));
    EXPECT_TRUE(Contains(text, "} instances;"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 2) readonly buffer ClustersBlock {"));
    EXPECT_TRUE(Contains(text, "} clusters;"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 3) writeonly buffer VisibleBlock {"));
    EXPECT_TRUE(Contains(text, "} visible;"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 4) writeonly buffer CommandsBlock {"));
    EXPECT_TRUE(Contains(text, "} commands;"));
    // The counter takes atomics, so it has no readonly or writeonly qualifier.
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 5) buffer CountBlock {"));
    EXPECT_TRUE(Contains(text, "} count;"));
}

TEST(ClusterShaderText, StructFieldsFollowTheCppRecords)
{
    const std::string text = ReadShader();
    // ClusterCullParams, in declaration order.
    const std::size_t planes = text.find("vec4 planes[6];");
    const std::size_t eye = text.find("vec4 eyeOrDirection;");
    const std::size_t viewProjection = text.find("mat4 viewProjection;");
    const std::size_t counts = text.find("uvec4 counts;");
    const std::size_t hiz = text.find("vec4 hiz;");
    const std::size_t depthMap = text.find("vec4 depthMap;");
    ASSERT_NE(planes, std::string::npos);
    ASSERT_NE(eye, std::string::npos);
    ASSERT_NE(viewProjection, std::string::npos);
    ASSERT_NE(counts, std::string::npos);
    ASSERT_NE(hiz, std::string::npos);
    ASSERT_NE(depthMap, std::string::npos);
    EXPECT_LT(planes, eye);
    EXPECT_LT(eye, viewProjection);
    EXPECT_LT(viewProjection, counts);
    EXPECT_LT(counts, hiz);
    EXPECT_LT(hiz, depthMap);
    // ClusterInstanceGpu: mat4 world + uvec4 info.
    EXPECT_TRUE(Contains(text, "mat4 world;"));
    EXPECT_TRUE(Contains(text, "uvec4 info;"));

    // MeshCluster: sphere, cone, aabbMin + firstIndex, aabbMax + indexCount.
    EXPECT_TRUE(Contains(text, "vec4 sphere;"));
    EXPECT_TRUE(Contains(text, "vec4 cone;"));
    EXPECT_TRUE(Contains(text, "vec3 aabbMin;"));
    EXPECT_TRUE(Contains(text, "uint firstIndex;"));
    EXPECT_TRUE(Contains(text, "vec3 aabbMax;"));
    EXPECT_TRUE(Contains(text, "uint indexCount;"));
}

TEST(ClusterShaderText, CommandsAreAFlatUintArrayOfFivePerSlot)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, "uint values[];"));
    EXPECT_TRUE(Contains(text, "slot * 5u"));
    EXPECT_EQ(kClusterCommandBytes, 5u * sizeof(std::uint32_t));
}

TEST(ClusterShaderText, HizBindingMatchesTheCppOracle)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, "layout(binding = 6) uniform sampler2D hizAtlas;"));
    // The level count comes from the parameter block, not from a constant.
    EXPECT_TRUE(Contains(text, "uint levelCount = uint(params.value.depthMap.z);"));
}

TEST(ClusterShaderText, PhasesFollowTheOracleTruthTable)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 7) buffer HistoryBlock {"));
    EXPECT_TRUE(Contains(text, "} history;"));
    // The view test runs once, before the phase branch.
    const std::size_t inView = text.find("bool inView =");
    const std::size_t first = text.find("if ((cullFlags & kClusterCullFlagPhaseFirst) != 0u) {");
    const std::size_t second = text.find("} else if ((cullFlags & kClusterCullFlagPhaseSecond) != 0u) {");
    ASSERT_NE(inView, std::string::npos);
    ASSERT_NE(first, std::string::npos);
    ASSERT_NE(second, std::string::npos);
    EXPECT_LT(inView, first);
    EXPECT_LT(first, second);
    EXPECT_EQ(text.find("bool inView =", inView + 1), std::string::npos);
    EXPECT_TRUE(Contains(text, "history.items[id] = (word & kClusterHistoryVisible) | (drawn ? kClusterHistoryDrawnFirst : 0u);"));
    EXPECT_TRUE(Contains(text, "history.items[id] = isVisible ? kClusterHistoryVisible : 0u;"));
}

TEST(ClusterShaderText, OcclusionClampsTheScreenRectAndKnowsNoMovedBit)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, "ndcMin = max(ndcMin, vec2(-1.0));"));
    EXPECT_TRUE(Contains(text, "ndcMax = min(ndcMax, vec2(1.0));"));
    EXPECT_FALSE(Contains(text, "Moved"));
    EXPECT_FALSE(Contains(text, "prevViewProjection"));
}

TEST(ClusterShaderText, StatsShaderSumsTwoLanesBehindAPushFlag)
{
    const std::string text = ReadTextFile((std::string(GTE_TEST_SHADERS_DIR) + "/ClusterCullStats.comp").c_str());
    ASSERT_FALSE(text.empty());
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 0) readonly buffer CountBlock {"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 1) writeonly buffer OutCountBlock {"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 2) readonly buffer SecondCountBlock {"));
    EXPECT_TRUE(Contains(text, "uint sumSecond;"));
    EXPECT_TRUE(Contains(text, "count.value + (pc.sumSecond != 0u ? secondCount.value : 0u)"));
}

TEST(ClusterShaderText, HizBuildBaseShaderPushBlockAndBindings)
{
    const std::string text = ReadTextFile(GTE_TEST_HIZ_BUILD_BASE_SHADER_PATH);
    ASSERT_FALSE(text.empty());
    EXPECT_TRUE(Contains(text, "layout(local_size_x = 16, local_size_y = 16) in;"));
    EXPECT_TRUE(Contains(text, "layout(binding = 0) uniform sampler2D sceneDepth;"));
    EXPECT_TRUE(Contains(text, "layout(binding = 1, r32f) uniform writeonly image2D pyramid;"));
    EXPECT_TRUE(Contains(text, "uvec4 sizes;"));
    EXPECT_TRUE(Contains(text, "uvec4 levels;"));
    EXPECT_TRUE(Contains(text, "#include \"HizPyramidLayout.glsl\""));
    EXPECT_TRUE(Contains(text, "level < kHizTopFirstLevel"));
}

TEST(ClusterShaderText, HizBuildTopShaderPushBlockAndBindings)
{
    const std::string text = ReadTextFile(GTE_TEST_HIZ_BUILD_TOP_SHADER_PATH);
    ASSERT_FALSE(text.empty());
    EXPECT_TRUE(Contains(text, "layout(local_size_x = " + std::to_string(kHizTopThreads) + ") in;"));
    EXPECT_TRUE(Contains(text, "layout(binding = 0, r32f) uniform coherent image2D pyramid;"));
    EXPECT_TRUE(Contains(text, "level = kHizTopFirstLevel;"));
    EXPECT_TRUE(Contains(text, "uvec4 sizes;"));
    EXPECT_TRUE(Contains(text, "uvec4 levels;"));
}

} // namespace
} // namespace gte
