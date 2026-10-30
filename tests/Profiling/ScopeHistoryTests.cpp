// Per-name history ring: order, wrap, gaps, lanes, track recycling and limits.
#include "Profiling/ProfilingBudget.h"
#include "Profiling/ScopeAggregate.h"
#include "Profiling/ScopeHistory.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace gte::Profiling {
namespace {

ScopeAggregate MakeRow(ScopeLane lane, NameId name, std::uint64_t millis, std::uint32_t calls = 1)
{
    ScopeAggregate row;
    row.lane = lane;
    row.name = name;
    row.totalTicks = millis * 1'000'000u;
    row.selfTicks = millis * 500'000u;
    row.calls = calls;
    return row;
}

void RecordOne(ScopeHistory& history, std::uint64_t frame, const ScopeAggregate& row)
{
    const std::array<ScopeAggregate, 1> rows{ row };
    history.Record(frame, rows);
}

TEST(ScopeHistoryTest, FitsTheMemoryBudget)
{
    EXPECT_LE(sizeof(ScopeHistory), 1024u * 1024u);
}

TEST(ScopeHistoryTest, ReturnsFramesOldestFirst)
{
    auto history = std::make_unique<ScopeHistory>();
    for (std::uint64_t i = 0; i < 10; ++i) {
        RecordOne(*history, 100 + i, MakeRow(ScopeLane::Main, 5, i + 1, static_cast<std::uint32_t>(i + 1)));
    }

    std::array<float, 10> total{};
    std::array<float, 10> self{};
    std::array<float, 10> calls{};
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 5, ScopeSeries::TotalMs, total));
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 5, ScopeSeries::SelfMs, self));
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 5, ScopeSeries::Calls, calls));
    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_FLOAT_EQ(total[i], static_cast<float>(i + 1));
        EXPECT_FLOAT_EQ(self[i], static_cast<float>(i + 1) * 0.5f);
        EXPECT_FLOAT_EQ(calls[i], static_cast<float>(i + 1));
    }
    EXPECT_EQ(history->FrameCount(), 10u);
    EXPECT_EQ(history->NewestFrameIndex(), 109u);
}

TEST(ScopeHistoryTest, WrapsAfterTheRingIsFull)
{
    auto history = std::make_unique<ScopeHistory>();
    for (std::uint64_t i = 0; i < 650; ++i) {
        RecordOne(*history, i, MakeRow(ScopeLane::Main, 5, i));
    }

    std::vector<float> out(kMaxFrameHistory);
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 5, ScopeSeries::TotalMs, out));
    for (std::size_t k = 0; k < out.size(); ++k) {
        EXPECT_FLOAT_EQ(out[k], static_cast<float>(350 + k));
    }
    EXPECT_EQ(history->FrameCount(), kMaxFrameHistory);
    EXPECT_EQ(history->NewestFrameIndex(), 649u);

    std::vector<float> longer(kMaxFrameHistory + 20, -1.0f); // frames older than the ring read 0
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 5, ScopeSeries::TotalMs, longer));
    EXPECT_FLOAT_EQ(longer[19], 0.0f);
    EXPECT_FLOAT_EQ(longer[20], 350.0f);
}

TEST(ScopeHistoryTest, AbsentScopeReadsZero)
{
    auto history = std::make_unique<ScopeHistory>();
    RecordOne(*history, 0, MakeRow(ScopeLane::Main, 5, 5));
    history->Record(1, {});
    RecordOne(*history, 2, MakeRow(ScopeLane::Main, 5, 7, 3));

    std::array<float, 3> total{};
    std::array<float, 3> calls{};
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 5, ScopeSeries::TotalMs, total));
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 5, ScopeSeries::Calls, calls));
    EXPECT_FLOAT_EQ(total[0], 5.0f);
    EXPECT_FLOAT_EQ(total[1], 0.0f);
    EXPECT_FLOAT_EQ(total[2], 7.0f);
    EXPECT_FLOAT_EQ(calls[1], 0.0f);
    EXPECT_FLOAT_EQ(calls[2], 3.0f);
}

TEST(ScopeHistoryTest, LanesWithTheSameNameKeepSeparateTracks)
{
    auto history = std::make_unique<ScopeHistory>();
    const std::array<ScopeAggregate, 2> rows{ MakeRow(ScopeLane::Main, 5, 1), MakeRow(ScopeLane::Gpu, 5, 2) };
    history->Record(0, rows);

    std::array<float, 1> main{};
    std::array<float, 1> gpu{};
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 5, ScopeSeries::TotalMs, main));
    ASSERT_TRUE(history->CopySeries(ScopeLane::Gpu, 5, ScopeSeries::TotalMs, gpu));
    EXPECT_FLOAT_EQ(main[0], 1.0f);
    EXPECT_FLOAT_EQ(gpu[0], 2.0f);
    EXPECT_FALSE(history->CopySeries(ScopeLane::Workers, 5, ScopeSeries::TotalMs, main));
}

TEST(ScopeHistoryTest, UnknownKeyLeavesOutputUntouched)
{
    auto history = std::make_unique<ScopeHistory>();
    RecordOne(*history, 0, MakeRow(ScopeLane::Main, 5, 1));

    std::array<float, 2> out{ -1.0f, -2.0f };
    EXPECT_FALSE(history->CopySeries(ScopeLane::Main, 6, ScopeSeries::TotalMs, out));
    EXPECT_FLOAT_EQ(out[0], -1.0f);
    EXPECT_FLOAT_EQ(out[1], -2.0f);
}

TEST(ScopeHistoryTest, CallsClampAtSixtyFiveThousand)
{
    auto history = std::make_unique<ScopeHistory>();
    RecordOne(*history, 0, MakeRow(ScopeLane::Main, 5, 1, 100000));

    std::array<float, 1> calls{};
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 5, ScopeSeries::Calls, calls));
    EXPECT_FLOAT_EQ(calls[0], 65535.0f);
}

TEST(ScopeHistoryTest, MarkersGetNoTrack)
{
    auto history = std::make_unique<ScopeHistory>();
    ScopeAggregate row = MakeRow(ScopeLane::Main, 5, 1);
    row.kind = ScopeKind::Marker;
    RecordOne(*history, 0, row);

    EXPECT_EQ(history->TrackCount(), 0u);
}

TEST(ScopeHistoryTest, ExhaustedTracksAreCountedAndExistingOnesStayExact)
{
    auto history = std::make_unique<ScopeHistory>();
    std::vector<ScopeAggregate> rows;
    for (std::size_t i = 0; i < kHistoryTracks + 1; ++i) {
        rows.push_back(MakeRow(ScopeLane::Main, static_cast<NameId>(i + 1), 2));
    }
    history->Record(0, rows);

    EXPECT_EQ(history->DroppedTracks(), 1u);
    history->Record(1, rows); // a gauge of the newest Record, not a running total
    EXPECT_EQ(history->DroppedTracks(), 1u);
    rows.push_back(MakeRow(ScopeLane::Main, static_cast<NameId>(kHistoryTracks + 2), 2));
    history->Record(2, rows); // every unmapped row counts, however many
    EXPECT_EQ(history->DroppedTracks(), 2u);
    history->Record(3, std::span<const ScopeAggregate>(rows.data(), 4));
    EXPECT_EQ(history->DroppedTracks(), 0u);
    EXPECT_EQ(history->TrackCount(), kHistoryTracks);
    std::array<float, 1> out{};
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 1, ScopeSeries::TotalMs, out));
    EXPECT_FLOAT_EQ(out[0], 2.0f);
    EXPECT_FALSE(history->CopySeries(ScopeLane::Main, static_cast<NameId>(kHistoryTracks + 1), ScopeSeries::TotalMs, out));
}

TEST(ScopeHistoryTest, IdleTrackIsRecycledWithZeroedValues)
{
    auto history = std::make_unique<ScopeHistory>();
    std::vector<ScopeAggregate> rows;
    for (std::size_t i = 0; i < kHistoryTracks; ++i) {
        rows.push_back(MakeRow(ScopeLane::Main, static_cast<NameId>(i + 1), 9));
    }
    history->Record(0, rows);

    rows.pop_back(); // the last name is never seen again
    for (std::uint64_t frame = 1; frame < kMaxFrameHistory; ++frame) {
        history->Record(frame, rows);
    }
    EXPECT_EQ(history->TrackCount(), kHistoryTracks);

    rows.push_back(MakeRow(ScopeLane::Main, 1000, 4)); // frame 300: the idle track is free now
    history->Record(kMaxFrameHistory, rows);

    EXPECT_EQ(history->DroppedTracks(), 0u);
    std::vector<float> out(kMaxFrameHistory);
    ASSERT_TRUE(history->CopySeries(ScopeLane::Main, 1000, ScopeSeries::TotalMs, out));
    for (std::size_t k = 0; k + 1 < out.size(); ++k) {
        ASSERT_FLOAT_EQ(out[k], 0.0f) << "frame " << k;
    }
    EXPECT_FLOAT_EQ(out.back(), 4.0f);
    EXPECT_FALSE(history->CopySeries(ScopeLane::Main, static_cast<NameId>(kHistoryTracks), ScopeSeries::TotalMs, out));
}

TEST(ScopeHistoryTest, ClearForgetsEverything)
{
    auto history = std::make_unique<ScopeHistory>();
    RecordOne(*history, 3, MakeRow(ScopeLane::Main, 5, 1));
    history->Clear();

    std::array<float, 1> out{};
    EXPECT_FALSE(history->CopySeries(ScopeLane::Main, 5, ScopeSeries::TotalMs, out));
    EXPECT_EQ(history->FrameCount(), 0u);
    EXPECT_EQ(history->NewestFrameIndex(), 0u);
}

} // namespace
} // namespace gte::Profiling
