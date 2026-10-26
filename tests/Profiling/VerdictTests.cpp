// Tier 1: the frame verdict picks the right limit, breaks ties by share and always prints its numbers.
#include "Profiling/NameTable.h"
#include "Profiling/Verdict.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace gte::Profiling {
namespace {

VerdictFrame Frame(double frame, double cpu, double gpu, double present, double other, double untracked)
{
    VerdictFrame f;
    f.frameMs = frame;
    f.cpuWorkMs = cpu;
    f.blockedGpuMs = gpu;
    f.blockedPresentMs = present;
    f.blockedOtherMs = other;
    f.untrackedMs = untracked;
    return f;
}

struct Row {
    const char* label;
    VerdictFrame frame;
    VerdictKind expected;
};

TEST(VerdictDecideTest, TableOfInputsAndAnswers)
{
    const Row rows[] = {
        { "no frame time", Frame(0, 0, 0, 0, 0, 0), VerdictKind::Unknown },
        { "untracked above 10%", Frame(20, 10, 4, 0, 0, 3), VerdictKind::Unreliable },
        { "untracked exactly 10% is allowed", Frame(20, 18, 0, 0, 0, 2), VerdictKind::CpuLimited },
        { "other waits above 20%", Frame(20, 10, 2, 0, 5, 0.5), VerdictKind::Stalled },
        { "other waits exactly 20% is not a stall", Frame(20, 14, 2, 0, 4, 0), VerdictKind::CpuLimited },
        { "present above 30%", Frame(20, 8, 2, 7, 0, 0.5), VerdictKind::PresentLimited },
        { "gpu at 40%", Frame(20, 11, 8, 0, 0, 0.5), VerdictKind::GpuLimited },
        { "gpu just under 40%", Frame(20, 11, 7.9, 0, 0, 0.5), VerdictKind::Serial },
        { "cpu at 60%", Frame(20, 12, 4, 0, 0, 0.5), VerdictKind::CpuLimited },
        { "cpu just under 60% is serial", Frame(20, 11.9, 4, 0, 0, 0.5), VerdictKind::Serial },
        { "untracked wins over a stall", Frame(20, 5, 2, 0, 8, 4), VerdictKind::Unreliable },
    };
    for (const Row& row : rows) {
        EXPECT_EQ(DecideVerdict(row.frame), row.expected) << row.label;
    }
}

TEST(VerdictDecideTest, WhenPresentAndGpuBothMatchTheLargerShareWins)
{
    EXPECT_EQ(DecideVerdict(Frame(20, 4, 9, 7, 0, 0)), VerdictKind::GpuLimited);
    EXPECT_EQ(DecideVerdict(Frame(20, 2, 8, 9, 0, 0)), VerdictKind::PresentLimited);
}

TEST(VerdictDecideTest, AFailingSelfCheckMakesTheVerdictUnreliable)
{
    VerdictFrame f = Frame(20, 18, 0, 0, 0, 0);
    EXPECT_EQ(DecideVerdict(f), VerdictKind::CpuLimited);
    f.checksFailing = true;
    EXPECT_EQ(DecideVerdict(f), VerdictKind::Unreliable);
}

TEST(VerdictComputeTest, TooFewFramesGiveNoVerdict)
{
    const std::vector<VerdictFrame> frames(VerdictConstants::kMinFrames - 1, Frame(20, 18, 0, 0, 0, 0));
    const Verdict verdict = ComputeVerdict(frames);
    EXPECT_EQ(verdict.kind, VerdictKind::Unknown);
    EXPECT_EQ(verdict.frames, 0u);
}

TEST(VerdictComputeTest, TheMedianIgnoresOneOutlierFrame)
{
    std::vector<VerdictFrame> frames(9, Frame(20, 18, 1, 0, 0, 0.5));
    frames[4] = Frame(200, 10, 190, 0, 0, 0); // one huge GPU wait
    const Verdict verdict = ComputeVerdict(frames);
    EXPECT_EQ(verdict.kind, VerdictKind::CpuLimited);
    EXPECT_DOUBLE_EQ(verdict.median.frameMs, 20.0);
    EXPECT_EQ(verdict.frames, 9u);
}

TEST(VerdictComputeTest, OnlyTheNewestWindowCounts)
{
    std::vector<VerdictFrame> frames(100, Frame(20, 4, 14, 0, 0, 0.5)); // old frames: GPU bound
    for (std::size_t i = 0; i < VerdictConstants::kWindowFrames; ++i) {
        frames[70 + i] = Frame(20, 18, 1, 0, 0, 0.5); // newest 30: CPU bound
    }
    const Verdict verdict = ComputeVerdict(frames);
    EXPECT_EQ(verdict.frames, VerdictConstants::kWindowFrames);
    EXPECT_EQ(verdict.kind, VerdictKind::CpuLimited);
}

TEST(VerdictComputeTest, AFailingCheckOnMostFramesTaintsTheVerdict)
{
    std::vector<VerdictFrame> frames(10, Frame(20, 18, 1, 0, 0, 0.5));
    for (std::size_t i = 0; i < 4; ++i) {
        frames[i].checksFailing = true;
    }
    EXPECT_EQ(ComputeVerdict(frames).kind, VerdictKind::CpuLimited);
    for (std::size_t i = 0; i < 6; ++i) {
        frames[i].checksFailing = true;
    }
    EXPECT_EQ(ComputeVerdict(frames).kind, VerdictKind::Unreliable);
}

TEST(VerdictComputeTest, AStallNamesTheSiteOfTheWorstFrame)
{
    const NameId site = InternName("VerdictTestStallSite");
    std::vector<VerdictFrame> frames(9, Frame(20, 8, 1, 0, 6, 0.5));
    frames[3].blockedOtherMs = 9;
    frames[3].stallSite = site;

    const Verdict verdict = ComputeVerdict(frames);
    EXPECT_EQ(verdict.kind, VerdictKind::Stalled);
    EXPECT_EQ(verdict.stallSite, site);
    EXPECT_NEAR(verdict.stallShare, 0.3, 1e-9);

    std::array<char, 160> text{};
    FormatVerdict(verdict, text);
    EXPECT_NE(std::string(text.data()).find("VerdictTestStallSite"), std::string::npos);
}

TEST(VerdictFormatTest, EveryVerdictPrintsItsNumbers)
{
    const VerdictFrame inputs[] = {
        Frame(20, 18, 1, 0, 0, 0.5),   // CPU
        Frame(20, 4, 12, 0, 0, 0.5),   // GPU
        Frame(20, 4, 2, 10, 0, 0.5),   // present
        Frame(20, 8, 2, 0, 8, 0.5),    // stalled
        Frame(20, 8, 2, 0, 0, 8),      // unreliable
        Frame(20, 9, 6, 0, 0, 0.5),    // serial
    };
    for (const VerdictFrame& frame : inputs) {
        const std::vector<VerdictFrame> frames(5, frame);
        const Verdict verdict = ComputeVerdict(frames);
        std::array<char, 192> text{};
        const std::size_t length = FormatVerdict(verdict, text);
        const std::string line(text.data(), length);
        EXPECT_NE(line.find("20.0 ms"), std::string::npos) << line;
        EXPECT_NE(line.find('%'), std::string::npos) << line;
    }
}

TEST(VerdictFormatTest, TheOutputIsAlwaysCutToFitAndTerminated)
{
    const std::vector<VerdictFrame> frames(5, Frame(20, 18, 1, 0, 0, 0.5));
    const Verdict verdict = ComputeVerdict(frames);
    std::array<char, 12> small{};
    const std::size_t length = FormatVerdict(verdict, small);
    EXPECT_LE(length, small.size() - 1);
    EXPECT_EQ(small.back(), '\0');
    EXPECT_EQ(FormatVerdict(verdict, std::span<char>{}), 0u);
}

TEST(VerdictFormatTest, UnknownSaysHowManyFramesWereMeasured)
{
    std::array<char, 96> text{};
    FormatVerdict(Verdict{}, text);
    EXPECT_STREQ(text.data(), "No verdict yet: 0 frames measured");
}

TEST(VerdictMakeFrameTest, SplitsASummaryIntoTheRuleClasses)
{
    FrameSummary s;
    s.cpuMilliseconds = 20.0;
    s.topLevelTicks = 19'000'000;
    s.untrackedTicks = 1'000'000;
    s.blockedGpuTicks = 8'000'000;     // includes the present wait
    s.blockedPresentTicks = 3'000'000;
    s.blockedOtherTicks = 1'000'000;
    s.blockedWorkerTicks = 1'000'000;

    const VerdictFrame f = MakeVerdictFrame(s, false);
    EXPECT_DOUBLE_EQ(f.frameMs, 20.0);
    EXPECT_DOUBLE_EQ(f.untrackedMs, 1.0);
    EXPECT_DOUBLE_EQ(f.blockedPresentMs, 3.0);
    EXPECT_DOUBLE_EQ(f.blockedGpuMs, 5.0);
    EXPECT_DOUBLE_EQ(f.blockedOtherMs, 2.0);
    EXPECT_DOUBLE_EQ(f.cpuWorkMs, 9.0); // 19 top-level minus 10 of waits
}

TEST(VerdictMakeFrameTest, BrokenInputNeverGoesNegative)
{
    FrameSummary s;
    s.cpuMilliseconds = 10.0;
    s.topLevelTicks = 1'000'000;
    s.blockedGpuTicks = 5'000'000;
    s.blockedPresentTicks = 9'000'000; // more than the GPU total: clamped
    const VerdictFrame f = MakeVerdictFrame(s, false);
    EXPECT_GE(f.blockedGpuMs, 0.0);
    EXPECT_GE(f.cpuWorkMs, 0.0);
}

TEST(VerdictNamesTest, EveryKindHasAStableName)
{
    EXPECT_STREQ(ToString(VerdictKind::Unknown), "unknown");
    EXPECT_STREQ(ToString(VerdictKind::Unreliable), "unreliable");
    EXPECT_STREQ(ToString(VerdictKind::Stalled), "stalled");
    EXPECT_STREQ(ToString(VerdictKind::PresentLimited), "present_limited");
    EXPECT_STREQ(ToString(VerdictKind::GpuLimited), "gpu_limited");
    EXPECT_STREQ(ToString(VerdictKind::CpuLimited), "cpu_limited");
    EXPECT_STREQ(ToString(VerdictKind::Serial), "serial");
}

} // namespace
} // namespace gte::Profiling
