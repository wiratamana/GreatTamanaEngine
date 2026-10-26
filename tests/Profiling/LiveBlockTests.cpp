// Tier 1: per-thread live wait records.
#include "Profiling/LiveBlockTable.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>
#include <vector>

namespace gte::Profiling {
namespace {

TEST(LiveBlockTest, AWaitInProgressIsListedWithItsElapsedTime)
{
    LiveBlockTable table;
    std::array<LiveBlockEntry, kMaxLiveBlockThreads> out{};
    EXPECT_EQ(table.Read(1000, out), 0u);

    table.Begin("Offscreen", "fence", 1000);
    ASSERT_EQ(table.Read(1500, out), 1u);
    EXPECT_EQ(std::string_view(out[0].site), "Offscreen");
    EXPECT_EQ(std::string_view(out[0].detail), "fence");
    EXPECT_EQ(out[0].elapsedMicroseconds, 500u);

    table.End();
    EXPECT_EQ(table.Read(2000, out), 0u);
}

TEST(LiveBlockTest, NestedWaitsRecordTheOutermostOne)
{
    LiveBlockTable table;
    std::array<LiveBlockEntry, kMaxLiveBlockThreads> out{};

    table.Begin("Outer", "a", 100);
    table.Begin("Inner", "b", 200);
    ASSERT_EQ(table.Read(300, out), 1u);
    EXPECT_EQ(std::string_view(out[0].site), "Outer");

    table.End();
    EXPECT_EQ(table.Read(300, out), 1u); // the outer wait is still running
    table.End();
    EXPECT_EQ(table.Read(300, out), 0u);
}

TEST(LiveBlockTest, TwoThreadsKeepTwoRecords)
{
    LiveBlockTable table;
    std::atomic<bool> release{ false };
    std::atomic<int> started{ 0 };

    std::thread other([&] {
        table.Begin("Other", "", 10);
        ++started;
        while (!release.load()) {
            std::this_thread::yield();
        }
        table.End();
    });

    table.Begin("Main", "", 20);
    while (started.load() == 0) {
        std::this_thread::yield();
    }

    std::array<LiveBlockEntry, kMaxLiveBlockThreads> out{};
    const std::size_t count = table.Read(100, out);
    EXPECT_EQ(count, 2u);
    EXPECT_NE(out[0].threadSlot, out[1].threadSlot);

    release = true;
    other.join();
    table.End();
    EXPECT_EQ(table.Read(100, out), 0u);
}

TEST(LiveBlockTest, ThreadsBeyondTheLimitAreCountedNotRecorded)
{
    LiveBlockTable table;
    std::atomic<bool> release{ false };
    std::atomic<std::size_t> started{ 0 };
    constexpr std::size_t kThreads = kMaxLiveBlockThreads + 2;

    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            table.Begin("Wait", "", 5);
            ++started;
            while (!release.load()) {
                std::this_thread::yield();
            }
            table.End();
        });
    }
    while (started.load() < kThreads) {
        std::this_thread::yield();
    }

    std::array<LiveBlockEntry, kMaxLiveBlockThreads> out{};
    EXPECT_EQ(table.Read(50, out), kMaxLiveBlockThreads);
    EXPECT_EQ(table.UnrecordedCount(), 2u);

    release = true;
    for (std::thread& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(table.Read(50, out), 0u);
}

TEST(LiveBlockTest, ReaderNeverSeesAHalfWrittenRecord)
{
    LiveBlockTable table;
    std::atomic<bool> stop{ false };
    std::atomic<bool> torn{ false };

    std::thread writer([&] {
        for (int i = 0; i < 20000 && !stop.load(); ++i) {
            table.Begin("Site", "Detail", 1000);
            table.End();
        }
        stop = true;
    });

    std::array<LiveBlockEntry, kMaxLiveBlockThreads> out{};
    while (!stop.load()) {
        const std::size_t count = table.Read(2000, out);
        for (std::size_t i = 0; i < count; ++i) {
            if (std::string_view(out[i].site) != "Site" || std::string_view(out[i].detail) != "Detail"
                || out[i].elapsedMicroseconds != 1000) {
                torn = true;
            }
        }
    }
    writer.join();
    EXPECT_FALSE(torn.load());
}

TEST(LiveBlockTest, NowMicrosecondsMoves)
{
    const std::uint64_t a = NowMicroseconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_GT(NowMicroseconds(), a);
}

} // namespace
} // namespace gte::Profiling
