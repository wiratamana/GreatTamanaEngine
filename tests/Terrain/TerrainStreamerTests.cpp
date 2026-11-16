// Tier 1: node cache, background generation, eviction and draw list fallback.

#include "Terrain/TerrainStreamer.h"

#include "Jobs/JobSystem.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace gte::terrain {

namespace {

TerrainSettings FastSettings()
{
    TerrainSettings s;
    s.erosionEnabled = false;
    return Sanitize(s);
}

WantedNode Want(int x, int z, int level, double distance, bool inView = true)
{
    return WantedNode{ NodeKey{ x, z, static_cast<std::uint8_t>(level) }, distance, inView };
}

// Runs frames until `done` is true or the frame cap is hit; returns the frame count used.
template <class Done>
int Pump(TerrainStreamer& streamer, const std::vector<WantedNode>& wanted, std::uint64_t& frame, StreamerUpdate& out, Done done)
{
    for (int i = 0; i < 40; ++i) {
        streamer.Update(wanted, ++frame, static_cast<double>(frame) / 60.0, out);
        if (done()) {
            return i + 1;
        }
    }
    return 40;
}

} // namespace

TEST(TerrainStreamerTests, LoadsNearestNodesFirstAndRespectsInFlightCap)
{
    InlineTerrainExecutor exec;
    StreamerConfig cfg;
    cfg.maxInFlight = 2;
    TerrainStreamer streamer(FastSettings(), cfg, &exec);
    const std::vector<WantedNode> wanted = { Want(3, 0, 0, 900.0), Want(0, 0, 0, 100.0), Want(1, 0, 0, 300.0), Want(2, 0, 0, 600.0), Want(4, 0, 0, 1200.0) };

    std::uint64_t frame = 0;
    StreamerUpdate out;
    streamer.Update(wanted, ++frame, 0.0, out);
    EXPECT_TRUE(out.ready.empty());
    EXPECT_EQ(streamer.Stats().running, 2);

    streamer.Update(wanted, ++frame, 0.1, out);
    ASSERT_EQ(out.ready.size(), 2u);
    const int a = out.ready[0]->key.x;
    const int b = out.ready[1]->key.x;
    EXPECT_EQ(std::min(a, b), 0);
    EXPECT_EQ(std::max(a, b), 1);

    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().resident == 5; });
    EXPECT_EQ(streamer.Stats().resident, 5);
    EXPECT_EQ(out.ready.size(), 5u);
    EXPECT_EQ(streamer.Stats().pending, 0);
    EXPECT_GT(streamer.Stats().bytesResident, 5u * 100u * 1024u);
}

TEST(TerrainStreamerTests, VisibleNodesLoadBeforePrefetchNodes)
{
    InlineTerrainExecutor exec;
    StreamerConfig cfg;
    cfg.maxInFlight = 1;
    TerrainStreamer streamer(FastSettings(), cfg, &exec);
    const std::vector<WantedNode> wanted = { Want(0, 0, 0, 10.0, false), Want(5, 0, 0, 5000.0, true) };
    std::uint64_t frame = 0;
    StreamerUpdate out;
    streamer.Update(wanted, ++frame, 0.0, out);
    streamer.Update(wanted, ++frame, 0.1, out);
    ASSERT_EQ(out.ready.size(), 1u);
    EXPECT_EQ(out.ready[0]->key.x, 5);
}

TEST(TerrainStreamerTests, UnwantedNodesAreEvictedAfterTheDelay)
{
    InlineTerrainExecutor exec;
    StreamerConfig cfg;
    cfg.unloadDelayFrames = 5;
    TerrainStreamer streamer(FastSettings(), cfg, &exec);
    std::vector<WantedNode> wanted = { Want(0, 0, 0, 10.0), Want(1, 0, 0, 20.0) };
    std::uint64_t frame = 0;
    StreamerUpdate out;
    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().resident == 2; });
    ASSERT_EQ(streamer.Stats().resident, 2);

    wanted = { Want(0, 0, 0, 10.0) };
    out = {};
    for (int i = 0; i < 4; ++i) {
        streamer.Update(wanted, ++frame, 1.0 + i, out);
    }
    EXPECT_TRUE(out.evicted.empty()) << "still inside the delay";
    for (int i = 0; i < 4; ++i) {
        streamer.Update(wanted, ++frame, 5.0 + i, out);
    }
    ASSERT_EQ(out.evicted.size(), 1u);
    EXPECT_EQ(out.evicted[0].x, 1);
    EXPECT_EQ(streamer.Stats().resident, 1);
    EXPECT_EQ(streamer.Stats().evictedTotal, 1u);
}

TEST(TerrainStreamerTests, MemoryBudgetEvictsOldestUnwantedButNeverWantedNodes)
{
    InlineTerrainExecutor exec;
    StreamerConfig cfg;
    cfg.unloadDelayFrames = 1000;
    TerrainStreamer streamer(FastSettings(), cfg, &exec);
    std::vector<WantedNode> wanted = { Want(0, 0, 0, 1.0), Want(1, 0, 0, 2.0), Want(2, 0, 0, 3.0), Want(3, 0, 0, 4.0) };
    std::uint64_t frame = 0;
    StreamerUpdate out;
    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().resident == 4; });
    ASSERT_EQ(streamer.Stats().resident, 4);
    const std::size_t oneNode = streamer.Stats().bytesResident / 4;

    // Keep node 3 wanted, drop interest in 0..2 one after another, then squeeze the budget.
    streamer.Update({ Want(0, 0, 0, 1.0), Want(1, 0, 0, 2.0), Want(2, 0, 0, 3.0), Want(3, 0, 0, 4.0) }, ++frame, 10.0, out);
    streamer.Update({ Want(1, 0, 0, 2.0), Want(2, 0, 0, 3.0), Want(3, 0, 0, 4.0) }, ++frame, 11.0, out);
    streamer.Update({ Want(2, 0, 0, 3.0), Want(3, 0, 0, 4.0) }, ++frame, 12.0, out);
    StreamerConfig tight = cfg;
    tight.memoryBudgetBytes = oneNode * 2 + oneNode / 2;
    streamer.SetConfig(tight);
    out = {};
    streamer.Update({ Want(3, 0, 0, 4.0) }, ++frame, 13.0, out);
    ASSERT_EQ(out.evicted.size(), 2u);
    EXPECT_EQ(out.evicted[0].x, 0) << "oldest first";
    EXPECT_EQ(out.evicted[1].x, 1);
    EXPECT_NE(streamer.Find(NodeKey{ 3, 0, 0 }), nullptr);

    // Even over budget, wanted nodes stay.
    tight.memoryBudgetBytes = 1;
    streamer.SetConfig(tight);
    out = {};
    streamer.Update({ Want(2, 0, 0, 3.0), Want(3, 0, 0, 4.0) }, ++frame, 14.0, out);
    EXPECT_TRUE(out.evicted.empty());
    EXPECT_EQ(streamer.Stats().resident, 2);
}

TEST(TerrainStreamerTests, DrawListFillsHolesWithAParentAndNeverOverlaps)
{
    InlineTerrainExecutor exec;
    StreamerConfig cfg;
    cfg.maxInFlight = 8;
    TerrainStreamer streamer(FastSettings(), cfg, &exec);
    std::uint64_t frame = 0;
    StreamerUpdate out;
    const NodeKey parent{ 0, 0, 1 };
    std::vector<WantedNode> coarse = { WantedNode{ parent, 50.0, true } };
    Pump(streamer, coarse, frame, out, [&] { return streamer.Find(parent) != nullptr; });
    ASSERT_NE(streamer.Find(parent), nullptr);

    // Now the camera wants the four children, none ready: the parent is drawn.
    std::vector<WantedNode> children;
    for (int c = 0; c < 4; ++c) {
        children.push_back(WantedNode{ parent.Child(c), 10.0 * (c + 1), true });
    }
    std::vector<DrawNode> draw;
    streamer.BuildDrawList(children, draw);
    ASSERT_EQ(draw.size(), 1u);
    EXPECT_EQ(draw[0].key, parent);

    // Two children ready: the parent still covers them, no overlap.
    StreamerConfig one = cfg;
    one.maxInFlight = 2;
    streamer.SetConfig(one);
    streamer.Update(children, ++frame, 100.0, out);
    streamer.Update(children, ++frame, 100.1, out);
    int readyChildren = 0;
    for (int c = 0; c < 4; ++c) {
        readyChildren += streamer.Find(parent.Child(c)) ? 1 : 0;
    }
    ASSERT_GT(readyChildren, 0);
    ASSERT_LT(readyChildren, 4);
    streamer.BuildDrawList(children, draw);
    ASSERT_EQ(draw.size(), 1u);
    EXPECT_EQ(draw[0].key, parent);

    // All four ready: the children replace the parent.
    Pump(streamer, children, frame, out, [&] {
        int n = 0;
        for (int c = 0; c < 4; ++c) {
            n += streamer.Find(parent.Child(c)) ? 1 : 0;
        }
        return n == 4;
    });
    streamer.BuildDrawList(children, draw);
    EXPECT_EQ(draw.size(), 4u);
    for (const DrawNode& d : draw) {
        EXPECT_EQ(d.key.level, 0);
    }
}

TEST(TerrainStreamerTests, DrawListUsesReadyChildrenWhenTheParentIsMissing)
{
    InlineTerrainExecutor exec;
    StreamerConfig cfg;
    cfg.maxInFlight = 8;
    TerrainStreamer streamer(FastSettings(), cfg, &exec);
    const NodeKey parent{ 2, 2, 1 };
    std::vector<WantedNode> children;
    for (int c = 0; c < 4; ++c) {
        children.push_back(WantedNode{ parent.Child(c), 10.0, true });
    }
    std::uint64_t frame = 0;
    StreamerUpdate out;
    Pump(streamer, children, frame, out, [&] { return streamer.Stats().resident == 4; });

    std::vector<DrawNode> draw;
    streamer.BuildDrawList({ WantedNode{ parent, 10.0, true } }, draw);
    EXPECT_EQ(draw.size(), 4u);
    streamer.BuildDrawList({ WantedNode{ parent, 10.0, false } }, draw);
    EXPECT_TRUE(draw.empty()) << "nodes outside the view are not drawn";
}

TEST(TerrainStreamerTests, HeightChangeEvictsEverythingAndRebuilds)
{
    InlineTerrainExecutor exec;
    TerrainStreamer streamer(FastSettings(), {}, &exec);
    const std::vector<WantedNode> wanted = { Want(0, 0, 0, 10.0), Want(1, 0, 0, 20.0) };
    std::uint64_t frame = 0;
    StreamerUpdate out;
    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().resident == 2; });
    const auto before = streamer.Find(NodeKey{ 0, 0, 0 });
    ASSERT_NE(before, nullptr);

    TerrainSettings changed = FastSettings();
    changed.seed = 99;
    EXPECT_TRUE(HasChange(streamer.SetSettings(changed), SettingsChange::Height));
    out = {};
    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().resident == 2 && out.ready.size() == 2; });
    EXPECT_EQ(out.evicted.size(), 2u);
    const auto after = streamer.Find(NodeKey{ 0, 0, 0 });
    ASSERT_NE(after, nullptr);
    EXPECT_NE(std::memcmp(before->vertices.data(), after->vertices.data(), before->vertices.size() * sizeof(TerrainVertex)), 0);
}

TEST(TerrainStreamerTests, MaterialChangeRecolorsWithoutEvicting)
{
    InlineTerrainExecutor exec;
    TerrainStreamer streamer(FastSettings(), {}, &exec);
    const std::vector<WantedNode> wanted = { Want(0, 0, 0, 10.0) };
    std::uint64_t frame = 0;
    StreamerUpdate out;
    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().resident == 1; });
    const auto before = streamer.Find(NodeKey{ 0, 0, 0 });

    TerrainSettings changed = FastSettings();
    changed.grassMaxSlopeDeg = 5.0f;
    changed.rockMinSlopeDeg = 6.0f;
    changed.snowMinHeightFraction = 0.0f;
    EXPECT_EQ(streamer.SetSettings(changed), SettingsChange::Material);
    out = {};
    streamer.Update(wanted, ++frame, 50.0, out);
    EXPECT_TRUE(out.evicted.empty());
    ASSERT_EQ(out.ready.size(), 1u);
    EXPECT_EQ(out.ready[0]->key, before->key);
    EXPECT_NE(out.ready[0]->colors, before->colors);
    EXPECT_EQ(std::memcmp(out.ready[0]->vertices.data(), before->vertices.data(), before->vertices.size() * sizeof(TerrainVertex)), 0);

    TerrainSettings opt = changed;
    opt.maxViewDistance = 1234.0f;
    EXPECT_EQ(streamer.SetSettings(opt), SettingsChange::Optimization);
}

TEST(TerrainStreamerTests, JobSystemBuildsTheSameNodesAsASerialBuild)
{
    TerrainStreamer streamer(FastSettings());
    std::vector<WantedNode> wanted;
    for (int i = 0; i < 8; ++i) {
        wanted.push_back(Want(i - 4, i % 3, i % 3, 10.0 * (i + 1)));
    }
    std::uint64_t frame = 0;
    StreamerUpdate out;
    const auto start = std::chrono::steady_clock::now();
    while (streamer.Stats().resident < 8 && std::chrono::steady_clock::now() - start < std::chrono::seconds(30)) {
        streamer.Update(wanted, ++frame, static_cast<double>(frame) / 60.0, out);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_EQ(streamer.Stats().resident, 8);
    EXPECT_GT(streamer.Stats().buildMsAverage, 0.0);
    for (const WantedNode& w : wanted) {
        const auto built = streamer.Find(w.key);
        ASSERT_NE(built, nullptr);
        const NodeMesh serial = BuildNode(streamer.Field(), w.key);
        ASSERT_EQ(serial.vertices.size(), built->vertices.size());
        EXPECT_EQ(std::memcmp(serial.vertices.data(), built->vertices.data(), serial.vertices.size() * sizeof(TerrainVertex)), 0);
    }
}

TEST(TerrainStreamerTests, DestructorWaitsForRunningJobs)
{
    auto streamer = std::make_unique<TerrainStreamer>(FastSettings());
    std::vector<WantedNode> wanted;
    for (int i = 0; i < 6; ++i) {
        wanted.push_back(Want(i, 0, 0, 10.0 * (i + 1)));
    }
    StreamerUpdate out;
    streamer->Update(wanted, 1, 0.0, out);
    streamer.reset(); // Must not crash or leave a job touching freed memory.
    SUCCEED();
}

} // namespace gte::terrain
