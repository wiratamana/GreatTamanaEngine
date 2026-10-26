// Tier 1: the overhead estimate and its warning rule.
#include "Profiling/Overhead.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte::Profiling {
namespace {

TEST(OverheadMeasureTest, TheCostOfOneEventIsPositiveAndFinite)
{
    const double cost = MeasureScopeEventCostNs(5000);
    EXPECT_GT(cost, 0.0);
    EXPECT_TRUE(std::isfinite(cost));
    EXPECT_LT(cost, 1.0e6); // a millisecond per event would mean the loop did not run as intended
}

TEST(OverheadMeasureTest, ZeroIterationsMeasureNothing)
{
    EXPECT_DOUBLE_EQ(MeasureScopeEventCostNs(0), 0.0);
}

TEST(OverheadEstimateTest, NoMeasuredCostMeansUnknown)
{
    EXPECT_FALSE(EstimateOverhead(0.0, 1000, 16.0).known);
    EXPECT_FALSE(EstimateOverhead(-5.0, 1000, 16.0).known);
    EXPECT_FALSE(EstimateOverhead(std::nan(""), 1000, 16.0).known);
    EXPECT_FALSE(EstimateOverhead(0.0, 1000, 16.0).warn);
}

TEST(OverheadEstimateTest, EventsTimesCostGivesTheEstimate)
{
    const OverheadEstimate e = EstimateOverhead(100.0, 2000, 20.0);
    EXPECT_TRUE(e.known);
    EXPECT_DOUBLE_EQ(e.estimatedMs, 0.2);
    EXPECT_DOUBLE_EQ(e.frameShare, 0.01);
    EXPECT_FALSE(e.warn);
}

TEST(OverheadEstimateTest, MoreThanThreePercentOfTheFrameWarns)
{
    EXPECT_TRUE(EstimateOverhead(500.0, 2000, 20.0).warn);  // 1 ms of 20 ms = 5%
    EXPECT_FALSE(EstimateOverhead(250.0, 2000, 20.0).warn); // 2.5%
}

TEST(OverheadEstimateTest, AnUnknownFrameTimeNeverWarns)
{
    const OverheadEstimate e = EstimateOverhead(1000.0, 2000, 0.0);
    EXPECT_TRUE(e.known);
    EXPECT_DOUBLE_EQ(e.frameShare, 0.0);
    EXPECT_FALSE(e.warn);
}

} // namespace
} // namespace gte::Profiling
