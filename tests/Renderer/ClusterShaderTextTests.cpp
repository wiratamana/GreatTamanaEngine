// Tier-1 text checks that ClusterCull.comp keeps the constants and layouts of ClusterCullMath.h.

#include "Renderer/Culling/ClusterCullMath.h"
#include "Renderer/Culling/HizBuildPlan.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

namespace gte {
namespace {

std::string ReadShader()
{
    std::ifstream file(GTE_TEST_CLUSTER_CULL_SHADER_PATH, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

std::string UintConstant(const char* name, std::uint32_t value)
{
    return std::string("const uint ") + name + " = " + std::to_string(value) + "u;";
}

TEST(ClusterShaderText, FileIsReadable)
{
    EXPECT_FALSE(ReadShader().empty());
}

TEST(ClusterShaderText, WorkGroupSizeMatchesTheDispatchConstant)
{
    EXPECT_TRUE(Contains(ReadShader(), "layout(local_size_x = " + std::to_string(kClusterCullGroupSize) + ") in;"));
}

TEST(ClusterShaderText, FlagBitsEqualTheCppConstants)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, UintConstant("kClusterFlagUniformScale", kClusterFlagUniformScale)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterFlagPositiveHand", kClusterFlagPositiveHand)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagFrustum", kClusterCullFlagFrustum)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagBackfaceCone", kClusterCullFlagBackfaceCone)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagOcclusion", kClusterCullFlagOcclusion)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagOrthographic", kClusterCullFlagOrthographic)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagPhaseFirst", kClusterCullFlagPhaseFirst)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagPhaseSecond", kClusterCullFlagPhaseSecond)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagHistoryValid", kClusterCullFlagHistoryValid)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagRawDepth", kClusterCullFlagRawDepth)));
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

TEST(ClusterShaderText, HizConstantsAndBindingMatchTheCppOracle)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, "layout(binding = 6) uniform sampler2D hizAtlas;"));
    EXPECT_TRUE(Contains(text, "const float kHizRectMarginTexels = 1.0;"));
    EXPECT_TRUE(Contains(text, "const float kHizRelMargin = 0.002;"));
    EXPECT_TRUE(Contains(text, "const float kHizAbsMargin = 0.01;"));
    EXPECT_TRUE(Contains(text, "const float kHizDepthGuard = 4.8e-7;"));
    EXPECT_TRUE(Contains(text, "const float kHizRawBias = 0.001;"));
    EXPECT_FLOAT_EQ(kHizRectMarginTexels, 1.0f);
    EXPECT_FLOAT_EQ(kHizRelMargin, 0.002f);
    EXPECT_FLOAT_EQ(kHizAbsMargin, 0.01f);
    EXPECT_FLOAT_EQ(kHizDepthGuard, 4.8e-7f);
    EXPECT_FLOAT_EQ(kHizRawBias, 0.001f);
    // The level count comes from the parameter block, not from a constant.
    EXPECT_TRUE(Contains(text, "uint levelCount = uint(params.value.depthMap.z);"));
}

TEST(ClusterShaderText, PhasesFollowTheOracleTruthTable)
{
    const std::string text = ReadShader();
    // History block and the shared word bits.
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 7) buffer HistoryBlock {"));
    EXPECT_TRUE(Contains(text, "} history;"));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterHistoryVisible", kClusterHistoryVisible)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterHistoryDrawnFirst", kClusterHistoryDrawnFirst)));
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
    std::ifstream file(std::string(GTE_TEST_SHADERS_DIR) + "/ClusterCullStats.comp", std::ios::binary);
    const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    ASSERT_FALSE(text.empty());
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 0) readonly buffer CountBlock {"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 1) writeonly buffer OutCountBlock {"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 2) readonly buffer SecondCountBlock {"));
    EXPECT_TRUE(Contains(text, "uint sumSecond;"));
    EXPECT_TRUE(Contains(text, "count.value + (pc.sumSecond != 0u ? secondCount.value : 0u)"));
}

std::string ReadTextFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
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
}

TEST(ClusterShaderText, HizBuildTopShaderPushBlockAndBindings)
{
    const std::string text = ReadTextFile(GTE_TEST_HIZ_BUILD_TOP_SHADER_PATH);
    ASSERT_FALSE(text.empty());
    EXPECT_TRUE(Contains(text, "layout(local_size_x = " + std::to_string(kHizTopThreads) + ") in;"));
    EXPECT_TRUE(Contains(text, "layout(binding = 0, r32f) uniform coherent image2D pyramid;"));
    EXPECT_TRUE(Contains(text, UintConstant("kTopFirstLevel", kHizTopFirstLevel)));
    EXPECT_TRUE(Contains(text, "uvec4 sizes;"));
    EXPECT_TRUE(Contains(text, "uvec4 levels;"));
}

} // namespace
} // namespace gte
