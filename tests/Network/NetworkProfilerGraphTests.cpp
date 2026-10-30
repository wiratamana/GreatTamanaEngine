// Tier 1: graph picture parameters, geometry and PNG.
#include "Network/NetworkProfilerGraph.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace gte::Network {
namespace {

using Profiling::MissingMetric;
using Profiling::PresentMetric;
using Profiling::SampleStatus;
using Profiling::SnapshotFrameRow;
using Profiling::Unit;

constexpr std::array<std::uint8_t, 4> kGrey{ 179, 179, 179, 255 };
constexpr std::array<std::uint8_t, 4> kBlue{ 77, 166, 255, 255 };
constexpr std::array<std::uint8_t, 4> kOrange{ 255, 158, 51, 255 };
constexpr std::array<std::uint8_t, 4> kGuide{ 62, 62, 70, 255 };

SnapshotFrameRow Row(double cpu, double busy, double gpu, bool gpuPresent = true)
{
    SnapshotFrameRow row;
    row.cpu = PresentMetric(cpu, Unit::Milliseconds);
    row.cpuBusy = PresentMetric(busy, Unit::Milliseconds);
    row.gpuSpan = gpuPresent ? PresentMetric(gpu, Unit::Milliseconds) : MissingMetric(Unit::Milliseconds, SampleStatus::Absent);
    return row;
}

std::size_t CountColor(const GraphPixelPlan& plan, const std::array<std::uint8_t, 4>& rgba)
{
    return static_cast<std::size_t>(std::count_if(
        plan.quads.begin(), plan.quads.end(), [&](const OverlayQuad2D& quad) { return quad.rgba == rgba; }));
}

TEST(GraphParamTest, EmptyMeansDefaultAndTheRestIsBounded)
{
    EXPECT_EQ(ParseGraphParam("", 300, 100, 800), 300);
    EXPECT_EQ(ParseGraphParam("500", 300, 100, 800), 500);
    EXPECT_FALSE(ParseGraphParam("99", 300, 100, 800).has_value());
    EXPECT_FALSE(ParseGraphParam("801", 300, 100, 800).has_value());
    EXPECT_FALSE(ParseGraphParam("abc", 300, 100, 800).has_value());
    EXPECT_FALSE(ParseGraphParam("400px", 300, 100, 800).has_value());
}

TEST(GraphPlanTest, EachSeriesDrawsTwoQuadsPerNeighbourPair)
{
    const std::vector<SnapshotFrameRow> rows{ Row(10.0, 6.0, 8.0), Row(12.0, 7.0, 9.0), Row(11.0, 6.5, 8.5) };
    const GraphPixelPlan plan = PlanGraphPixels(rows, 600, 200);
    EXPECT_EQ(plan.width, 600);
    EXPECT_EQ(plan.height, 200);
    EXPECT_EQ(CountColor(plan, kGrey), 4u);
    EXPECT_EQ(CountColor(plan, kBlue), 4u);
    EXPECT_EQ(CountColor(plan, kOrange), 4u);
}

TEST(GraphPlanTest, EveryQuadStaysInsideTheImage)
{
    const std::vector<SnapshotFrameRow> rows{ Row(80.0, 70.0, 90.0), Row(5.0, 4.0, 3.0) };
    const GraphPixelPlan plan = PlanGraphPixels(rows, 400, 150);
    for (const OverlayQuad2D& quad : plan.quads) {
        for (const Vec2& p : quad.p) {
            EXPECT_GE(p.x, 0.0f);
            EXPECT_LE(p.x, 400.0f);
            EXPECT_GE(p.y, 0.0f);
            EXPECT_LE(p.y, 150.0f);
        }
    }
}

TEST(GraphPlanTest, AGpuGapDrawsNoOrange)
{
    const std::vector<SnapshotFrameRow> rows{ Row(10.0, 6.0, 0.0, false), Row(12.0, 7.0, 9.0), Row(11.0, 6.5, 0.0, false) };
    const GraphPixelPlan plan = PlanGraphPixels(rows, 600, 200);
    EXPECT_EQ(CountColor(plan, kOrange), 0u);
    EXPECT_EQ(CountColor(plan, kGrey), 4u);
}

TEST(GraphPlanTest, GuideLinesShowOnlyBelowTheAxisTop)
{
    const GraphPixelPlan low = PlanGraphPixels(std::vector<SnapshotFrameRow>{ Row(5.0, 4.0, 3.0) }, 600, 200);
    EXPECT_EQ(CountColor(low, kGuide), 0u);

    const GraphPixelPlan mid = PlanGraphPixels(std::vector<SnapshotFrameRow>{ Row(25.0, 4.0, 3.0) }, 600, 200);
    EXPECT_EQ(CountColor(mid, kGuide), 1u);

    const GraphPixelPlan high = PlanGraphPixels(std::vector<SnapshotFrameRow>{ Row(60.0, 4.0, 3.0) }, 600, 200);
    EXPECT_EQ(CountColor(high, kGuide), 2u);
}

TEST(GraphPlanTest, EmptyInputKeepsTheSizeAndDrawsNoLines)
{
    const GraphPixelPlan plan = PlanGraphPixels({}, 5000, 10);
    EXPECT_EQ(plan.width, kGraphWidthMax);
    EXPECT_EQ(plan.height, kGraphHeightMin);
    EXPECT_EQ(CountColor(plan, kGrey) + CountColor(plan, kBlue) + CountColor(plan, kOrange), 0u);
}

TEST(GraphPlanTest, ASingleFrameDrawsOneDotPerSeries)
{
    const GraphPixelPlan plan = PlanGraphPixels(std::vector<SnapshotFrameRow>{ Row(10.0, 6.0, 8.0) }, 600, 200);
    EXPECT_EQ(CountColor(plan, kGrey), 1u);
    EXPECT_EQ(CountColor(plan, kBlue), 1u);
    EXPECT_EQ(CountColor(plan, kOrange), 1u);
}

TEST(GraphPngTest, TheBytesAreAPngOfThePlannedSize)
{
    const GraphPixelPlan plan = PlanGraphPixels(std::vector<SnapshotFrameRow>{ Row(10.0, 6.0, 8.0), Row(12.0, 7.0, 9.0) }, 300, 120);
    const std::vector<std::uint8_t> png = RenderGraphPng(plan);
    ASSERT_GT(png.size(), 24u);
    const std::uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(png[i], signature[i]);
    }
    const auto read32 = [&](std::size_t at) {
        return (static_cast<int>(png[at]) << 24) | (static_cast<int>(png[at + 1]) << 16) | (static_cast<int>(png[at + 2]) << 8)
            | static_cast<int>(png[at + 3]);
    };
    EXPECT_EQ(read32(16), plan.width);
    EXPECT_EQ(read32(20), plan.height);
}

} // namespace
} // namespace gte::Network
