// Unit tests for the atmosphere-scattering-2 campaign's Phase 5 aggregation
// helpers (src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp) - see
// task_manager/atmosphere-scattering-2/PHASE5_AERIAL_LUT_NUMERIC_VALIDATION_TOOL.md,
// Step 3.1. Only actually compiled/linked when GTE_ENABLE_EDITOR is ON, since
// this file itself is only ever compiled into gte_core then (see the root
// CMakeLists.txt's "Editor Module Structure") - see tests/CMakeLists.txt.
//
// No Vulkan/Renderer/live GPU device involved at all -
// AccumulateAerialPerspectiveSliceStats()/FinalizeAerialPerspectiveLutInspection()
// are pure functions over plain byte buffers/structs (the GPU-touching
// orchestration function, InspectAerialPerspectiveVolume(), is Tier-2 and has
// no automated test here, mirroring AtmosphereTransmittanceLutValidation's
// own accepted precedent).

#include "Editor/AtmosphereAerialPerspectiveLutInspection.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

// A small, test-local IEEE-754 binary32 -> binary16 encoder - the inverse of
// Encoding::DecodeHalfFloat() (src/Encoding/HdrColorVisualization.h), used
// purely to build hand-crafted raw rgba16f byte buffers for these tests.
// Only needs to be exact for the small, "nice" values (0.0, 0.5, 1.0, 3.0,
// 4.0, ...) these tests actually use - not a general-purpose, fully rounded
// float->half implementation.
std::uint16_t EncodeHalfFloatForTest(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    const std::uint32_t mantissa = bits & 0x7FFFFFu;

    if (exponent <= 0) {
        return static_cast<std::uint16_t>(sign); // Flushes to zero - fine for this file's own test values.
    }
    if (exponent >= 0x1F) {
        return static_cast<std::uint16_t>(sign | 0x7C00u); // Overflow -> infinity - not exercised by these tests.
    }
    const std::uint16_t halfMantissa = static_cast<std::uint16_t>(mantissa >> 13);
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | halfMantissa);
}

// Builds one texel's worth of raw rgba16f bytes (8 bytes) and appends them to
// `outBytes`.
void AppendTexel(std::vector<std::uint8_t>& outBytes, float r, float g, float b, float a)
{
    const std::uint16_t channels[4] = { EncodeHalfFloatForTest(r), EncodeHalfFloatForTest(g),
        EncodeHalfFloatForTest(b), EncodeHalfFloatForTest(a) };
    const std::size_t offset = outBytes.size();
    outBytes.resize(offset + 8);
    std::memcpy(outBytes.data() + offset, channels, 8);
}

constexpr float kEpsilon = 1e-4f;

// ---------------------------------------------------------------------
// AccumulateAerialPerspectiveSliceStats
// ---------------------------------------------------------------------

TEST(AtmosphereAerialPerspectiveLutInspectionTest, AllZeroRgbaOneTransmittanceVolumeIsTheFarNothingToSeeCase)
{
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 4; ++i) {
        AppendTexel(bytes, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    const AerialPerspectiveSliceStats stats = AccumulateAerialPerspectiveSliceStats(bytes.data(), 4);

    EXPECT_EQ(stats.texelCount, 4u);
    EXPECT_NEAR(stats.minTransmittance, 1.0f, kEpsilon);
    EXPECT_NEAR(stats.maxTransmittance, 1.0f, kEpsilon);
    EXPECT_NEAR(stats.sumTransmittance, 4.0, kEpsilon);
    EXPECT_NEAR(stats.minInScatteringMagnitude, 0.0f, kEpsilon);
    EXPECT_NEAR(stats.maxInScatteringMagnitude, 0.0f, kEpsilon);
    EXPECT_NEAR(stats.sumInScatteringMagnitude, 0.0, kEpsilon);

    const AtmosphereAerialPerspectiveLutInspectionResult result = FinalizeAerialPerspectiveLutInspection(stats, 2, 2, 1);
    EXPECT_TRUE(result.succeeded);
    EXPECT_NEAR(result.meanTransmittance, 1.0f, kEpsilon);
    EXPECT_NEAR(result.meanInScatteringMagnitude, 0.0f, kEpsilon);
    EXPECT_FALSE(result.likelyVisibleAtDefaultExposure);
}

TEST(AtmosphereAerialPerspectiveLutInspectionTest, AllOnesRgbHalfTransmittanceVolumeReportsExactMagnitudeAndMean)
{
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 3; ++i) {
        AppendTexel(bytes, 1.0f, 1.0f, 1.0f, 0.5f);
    }

    const AerialPerspectiveSliceStats stats = AccumulateAerialPerspectiveSliceStats(bytes.data(), 3);

    const float expectedMagnitude = std::sqrt(3.0f); // length(1,1,1)
    EXPECT_EQ(stats.texelCount, 3u);
    EXPECT_NEAR(stats.minTransmittance, 0.5f, kEpsilon);
    EXPECT_NEAR(stats.maxTransmittance, 0.5f, kEpsilon);
    EXPECT_NEAR(stats.sumTransmittance, 1.5, kEpsilon);
    EXPECT_NEAR(stats.maxInScatteringMagnitude, expectedMagnitude, kEpsilon);
    EXPECT_NEAR(stats.sumInScatteringMagnitude, 3.0 * expectedMagnitude, 1e-3);

    const AtmosphereAerialPerspectiveLutInspectionResult result = FinalizeAerialPerspectiveLutInspection(stats, 3, 1, 1);
    EXPECT_TRUE(result.succeeded);
    EXPECT_NEAR(result.meanTransmittance, 0.5f, kEpsilon);
    EXPECT_NEAR(result.meanInScatteringMagnitude, expectedMagnitude, 1e-3f);
    // (1 - 0.5) + sqrt(3) is far larger than the 5/255 heuristic threshold.
    EXPECT_TRUE(result.likelyVisibleAtDefaultExposure);
}

TEST(AtmosphereAerialPerspectiveLutInspectionTest, MixedValueCaseMatchesHandComputedMinMaxMean)
{
    // texel 0: near-camera froxel - no haze/scattering yet (transmittance 1,
    // in-scattering 0). texel 1: far froxel - transmittance dropped to 0.5,
    // in-scattering (3, 4, 0) -> magnitude exactly 5.
    std::vector<std::uint8_t> bytes;
    AppendTexel(bytes, 0.0f, 0.0f, 0.0f, 1.0f);
    AppendTexel(bytes, 3.0f, 4.0f, 0.0f, 0.5f);

    const AerialPerspectiveSliceStats stats = AccumulateAerialPerspectiveSliceStats(bytes.data(), 2);

    EXPECT_EQ(stats.texelCount, 2u);
    EXPECT_NEAR(stats.minTransmittance, 0.5f, kEpsilon);
    EXPECT_NEAR(stats.maxTransmittance, 1.0f, kEpsilon);
    EXPECT_NEAR(stats.sumTransmittance, 1.5, kEpsilon);
    EXPECT_NEAR(stats.minInScatteringMagnitude, 0.0f, kEpsilon); // Struct default - see its own header comment.
    EXPECT_NEAR(stats.maxInScatteringMagnitude, 5.0f, kEpsilon);
    EXPECT_NEAR(stats.sumInScatteringMagnitude, 5.0, kEpsilon);

    const AtmosphereAerialPerspectiveLutInspectionResult result = FinalizeAerialPerspectiveLutInspection(stats, 2, 1, 1);
    EXPECT_TRUE(result.succeeded);
    EXPECT_EQ(result.texelCount, 2u);
    EXPECT_NEAR(result.minTransmittance, 0.5f, kEpsilon);
    EXPECT_NEAR(result.maxTransmittance, 1.0f, kEpsilon);
    EXPECT_NEAR(result.meanTransmittance, 0.75f, kEpsilon);
    EXPECT_NEAR(result.minInScatteringMagnitude, 0.0f, kEpsilon);
    EXPECT_NEAR(result.maxInScatteringMagnitude, 5.0f, kEpsilon);
    EXPECT_NEAR(result.meanInScatteringMagnitude, 2.5f, kEpsilon);
    EXPECT_TRUE(result.likelyVisibleAtDefaultExposure);
}

TEST(AtmosphereAerialPerspectiveLutInspectionTest, AccumulateChainsAcrossMultipleSlicesCorrectly)
{
    // Mirrors InspectAerialPerspectiveVolume()'s own per-slice accumulation
    // loop: feed two separate "slices" through the SAME running
    // AerialPerspectiveSliceStats accumulator, one call at a time.
    std::vector<std::uint8_t> sliceA;
    AppendTexel(sliceA, 0.0f, 0.0f, 0.0f, 1.0f);
    std::vector<std::uint8_t> sliceB;
    AppendTexel(sliceB, 3.0f, 4.0f, 0.0f, 0.5f);

    AerialPerspectiveSliceStats running;
    running = AccumulateAerialPerspectiveSliceStats(sliceA.data(), 1, running);
    running = AccumulateAerialPerspectiveSliceStats(sliceB.data(), 1, running);

    EXPECT_EQ(running.texelCount, 2u);
    EXPECT_NEAR(running.minTransmittance, 0.5f, kEpsilon);
    EXPECT_NEAR(running.maxTransmittance, 1.0f, kEpsilon);
    EXPECT_NEAR(running.maxInScatteringMagnitude, 5.0f, kEpsilon);
}

TEST(AtmosphereAerialPerspectiveLutInspectionTest, NullOrEmptyInputIsASafeNoOp)
{
    const AerialPerspectiveSliceStats seed;
    const AerialPerspectiveSliceStats afterNull = AccumulateAerialPerspectiveSliceStats(nullptr, 4, seed);
    EXPECT_EQ(afterNull.texelCount, seed.texelCount);
    EXPECT_EQ(afterNull.sumTransmittance, seed.sumTransmittance);

    std::vector<std::uint8_t> bytes;
    AppendTexel(bytes, 1.0f, 1.0f, 1.0f, 1.0f);
    const AerialPerspectiveSliceStats afterZeroCount = AccumulateAerialPerspectiveSliceStats(bytes.data(), 0, seed);
    EXPECT_EQ(afterZeroCount.texelCount, seed.texelCount);
}

// ---------------------------------------------------------------------
// ToDiagnosticString
// ---------------------------------------------------------------------

TEST(AtmosphereAerialPerspectiveLutInspectionTest, DiagnosticStringReportsFailureReasonWhenNotSucceeded)
{
    AtmosphereAerialPerspectiveLutInspectionResult result;
    result.succeeded = false;
    result.failureReason = "some reason";

    const std::string text = ToDiagnosticString(result);
    EXPECT_NE(text.find("FAILED"), std::string::npos);
    EXPECT_NE(text.find("some reason"), std::string::npos);
}

TEST(AtmosphereAerialPerspectiveLutInspectionTest, DiagnosticStringReportsDimensionsOnSuccess)
{
    AerialPerspectiveSliceStats stats;
    stats = AccumulateAerialPerspectiveSliceStats(nullptr, 0, stats); // no-op, just to exercise the include path.
    const AtmosphereAerialPerspectiveLutInspectionResult result = FinalizeAerialPerspectiveLutInspection(stats, 128, 128, 32);

    const std::string text = ToDiagnosticString(result);
    EXPECT_NE(text.find("128x128x32"), std::string::npos);
}

// ---------------------------------------------------------------------
// FinalizeAerialPerspectiveBandSummary (atmosphere-scattering-3 campaign,
// Phase 1 - see task_manager/atmosphere-scattering-3/
// PHASE1_ROOT_CAUSE_INSTRUMENTATION_AND_REGRESSION_TESTS.md, Step 3.3)
// ---------------------------------------------------------------------

TEST(AtmosphereAerialPerspectiveLutInspectionTest, BandSummaryReductionMatchesHandComputedMean)
{
    std::vector<std::uint8_t> bytes;
    AppendTexel(bytes, 0.0f, 0.0f, 0.0f, 1.0f);
    AppendTexel(bytes, 3.0f, 4.0f, 0.0f, 0.5f);

    const AerialPerspectiveSliceStats stats = AccumulateAerialPerspectiveSliceStats(bytes.data(), 2);
    const AerialPerspectiveBandSummary summary = FinalizeAerialPerspectiveBandSummary(stats, 5, 9);

    EXPECT_EQ(summary.sliceBeginInclusive, 5);
    EXPECT_EQ(summary.sliceEndExclusive, 9);
    // Hand-computed: sum / texelCount == (1.0 + 0.5) / 2 == 0.75, and
    // (0 + 5.0) / 2 == 2.5 (magnitude of texel 1 is length(3,4,0) == 5).
    EXPECT_NEAR(summary.meanTransmittance, 0.75f, kEpsilon);
    EXPECT_NEAR(summary.meanInScatteringMagnitude, 2.5f, kEpsilon);
}

TEST(AtmosphereAerialPerspectiveLutInspectionTest, NearBandStaysClearerThanFarBand)
{
    std::vector<std::uint8_t> nearBytes;
    AppendTexel(nearBytes, 0.0f, 0.0f, 0.0f, 1.0f);
    AppendTexel(nearBytes, 0.0f, 0.0f, 0.0f, 1.0f);

    std::vector<std::uint8_t> farBytes;
    AppendTexel(farBytes, 3.0f, 4.0f, 0.0f, 0.5f);
    AppendTexel(farBytes, 3.0f, 4.0f, 0.0f, 0.5f);

    const AerialPerspectiveSliceStats nearStats = AccumulateAerialPerspectiveSliceStats(nearBytes.data(), 2);
    const AerialPerspectiveSliceStats farStats = AccumulateAerialPerspectiveSliceStats(farBytes.data(), 2);

    const AerialPerspectiveBandSummary nearSummary = FinalizeAerialPerspectiveBandSummary(nearStats, 0, 11);
    const AerialPerspectiveBandSummary farSummary = FinalizeAerialPerspectiveBandSummary(farStats, 22, 32);

    // A real, checked proof that "near stays clearer than far" is something
    // this tool can actually detect, not just something a diagnostic string
    // prints and hopes is right.
    EXPECT_GT(nearSummary.meanTransmittance, farSummary.meanTransmittance);
    EXPECT_LT(nearSummary.meanInScatteringMagnitude, farSummary.meanInScatteringMagnitude);
}

TEST(AtmosphereAerialPerspectiveLutInspectionTest, EmptyBandReportsDefaultsRatherThanDividingByZero)
{
    const AerialPerspectiveSliceStats emptyStats; // Default-constructed - texelCount == 0.
    const AerialPerspectiveBandSummary summary = FinalizeAerialPerspectiveBandSummary(emptyStats, 10, 10);

    EXPECT_EQ(summary.sliceBeginInclusive, 10);
    EXPECT_EQ(summary.sliceEndExclusive, 10);
    EXPECT_NEAR(summary.meanTransmittance, 1.0f, kEpsilon);
    EXPECT_NEAR(summary.meanInScatteringMagnitude, 0.0f, kEpsilon);
}

} // namespace
} // namespace gte
