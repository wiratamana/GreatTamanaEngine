// Tier 1: the CPU/GPU clock fit with fake samples.
#include "Renderer/GpuClockCalibration.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte {
namespace {

// GPU runs at 1 tick = 1 ns here; the CPU clock is offset by `offset` and drifts by `slope`.
std::vector<ClockSample> MakeSamples(std::size_t count, double slope, std::uint64_t offset, std::int64_t noiseNs = 0)
{
    std::vector<ClockSample> out;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint64_t gpu = 1'000'000 + i * 10'000'000;
        const double cpu = static_cast<double>(offset) + slope * static_cast<double>(gpu);
        const std::int64_t jitter = (i % 2 == 0) ? noiseNs : -noiseNs;
        out.push_back({ gpu, static_cast<std::uint64_t>(static_cast<std::int64_t>(cpu) + jitter) });
    }
    return out;
}

TEST(GpuClockCalibrationTest, ExactSamplesGiveAnExactMapping)
{
    const std::vector<ClockSample> samples = MakeSamples(8, 1.0, 5'000'000'000ull);
    const ClockCalibration fit = FitClockCalibration(samples, 1.0);
    ASSERT_EQ(fit.state, ClockCalibrationState::Present);
    EXPECT_EQ(fit.reason, ClockCalibrationReason::None);
    EXPECT_NEAR(fit.slope, 1.0, 1e-9);
    EXPECT_LT(fit.uncertaintyNs, 1.0);

    const std::optional<std::int64_t> cpu = GpuTicksToCpuNs(fit, 123'000'000);
    ASSERT_TRUE(cpu.has_value());
    EXPECT_NEAR(static_cast<double>(*cpu), 5'000'000'000.0 + 123'000'000.0, 2.0);
}

TEST(GpuClockCalibrationTest, DriftIsFittedAndExtrapolates)
{
    const std::vector<ClockSample> samples = MakeSamples(6, 1.0005, 9'000'000'000ull);
    const ClockCalibration fit = FitClockCalibration(samples, 1.0);
    ASSERT_EQ(fit.state, ClockCalibrationState::Present);
    EXPECT_NEAR(fit.slope, 1.0005, 1e-6);

    const std::optional<std::int64_t> cpu = GpuTicksToCpuNs(fit, 500'000'000);
    ASSERT_TRUE(cpu.has_value());
    EXPECT_NEAR(static_cast<double>(*cpu), 9'000'000'000.0 + 1.0005 * 500'000'000.0, 100.0);
}

TEST(GpuClockCalibrationTest, TickPeriodScalesTheGpuClock)
{
    // 52 ns per tick: GPU ticks are scaled before the fit.
    std::vector<ClockSample> samples;
    for (std::uint64_t i = 0; i < 5; ++i) {
        const std::uint64_t ticks = 1000 + i * 100'000;
        samples.push_back({ ticks, 7'000'000 + static_cast<std::uint64_t>(static_cast<double>(ticks) * 52.0) });
    }
    const ClockCalibration fit = FitClockCalibration(samples, 52.0);
    ASSERT_EQ(fit.state, ClockCalibrationState::Present);
    EXPECT_NEAR(fit.slope, 1.0, 1e-9);
}

TEST(GpuClockCalibrationTest, NoiseIsReportedAsUncertainty)
{
    const std::vector<ClockSample> samples = MakeSamples(8, 1.0, 2'000'000'000ull, 500);
    const ClockCalibration fit = FitClockCalibration(samples, 1.0);
    ASSERT_EQ(fit.state, ClockCalibrationState::Present);
    EXPECT_GT(fit.uncertaintyNs, 100.0);
    EXPECT_LT(fit.uncertaintyNs, 1000.0);
}

TEST(GpuClockCalibrationTest, TooFewSamplesAndBadPeriodAreErrors)
{
    const std::vector<ClockSample> one = MakeSamples(1, 1.0, 0);
    EXPECT_EQ(FitClockCalibration(one, 1.0).reason, ClockCalibrationReason::TooFewSamples);
    EXPECT_EQ(FitClockCalibration({}, 1.0).state, ClockCalibrationState::Error);

    const std::vector<ClockSample> two = MakeSamples(2, 1.0, 0);
    EXPECT_EQ(FitClockCalibration(two, 0.0).reason, ClockCalibrationReason::BadPeriod);
}

TEST(GpuClockCalibrationTest, NonIncreasingClocksAreErrors)
{
    std::vector<ClockSample> samples = MakeSamples(4, 1.0, 1000);
    samples[2].gpuTicks = samples[1].gpuTicks;
    EXPECT_EQ(FitClockCalibration(samples, 1.0).reason, ClockCalibrationReason::ClockNotMonotonic);

    samples = MakeSamples(4, 1.0, 1000);
    samples[3].cpuNs = samples[2].cpuNs - 1;
    EXPECT_EQ(FitClockCalibration(samples, 1.0).reason, ClockCalibrationReason::ClockNotMonotonic);
}

TEST(GpuClockCalibrationTest, ImplausibleDriftIsRejected)
{
    const std::vector<ClockSample> samples = MakeSamples(5, 1.5, 0);
    const ClockCalibration fit = FitClockCalibration(samples, 1.0);
    EXPECT_EQ(fit.state, ClockCalibrationState::Error);
    EXPECT_EQ(fit.reason, ClockCalibrationReason::SlopeOutOfRange);
}

TEST(GpuClockCalibrationTest, VeryNoisySamplesAreRejected)
{
    const std::vector<ClockSample> samples = MakeSamples(6, 1.0, 5'000'000'000ull, 3'000'000);
    const ClockCalibration fit = FitClockCalibration(samples, 1.0);
    EXPECT_EQ(fit.state, ClockCalibrationState::Error);
    EXPECT_EQ(fit.reason, ClockCalibrationReason::UncertaintyTooLarge);
}

TEST(GpuClockCalibrationTest, OnlyAPresentFitConvertsTicks)
{
    EXPECT_FALSE(GpuTicksToCpuNs(MissingClockCalibration(ClockCalibrationReason::ExtensionNotEnabled), 10).has_value());
    EXPECT_EQ(MissingClockCalibration(ClockCalibrationReason::ExtensionNotEnabled).state, ClockCalibrationState::Unsupported);
    ClockCalibration failed;
    failed.state = ClockCalibrationState::Error;
    EXPECT_FALSE(GpuTicksToCpuNs(failed, 10).has_value());
}

TEST(GpuClockCalibrationTest, ReasonsHaveText)
{
    EXPECT_STREQ(ToString(ClockCalibrationReason::ExtensionNotEnabled), "calibrated timestamp extension not enabled");
    EXPECT_STREQ(ToString(ClockCalibrationReason::None), "none");
}

} // namespace
} // namespace gte
