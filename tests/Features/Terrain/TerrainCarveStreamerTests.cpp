// Tier 1: streamer rebuilds only the nodes a carve change touches and keeps old meshes drawing meanwhile.

#include "Features/Terrain/TerrainStreamer.h"
#include "Features/Water/World/WaterCarve.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
#include <thread>
#include <unordered_map>

namespace gte::terrain {

namespace {

TerrainSettings FastSettings()
{
    TerrainSettings s;
    s.erosionEnabled = false;
    return Sanitize(s);
}

WantedNode Want(int x, int z, int level = 0)
{
    return WantedNode{ NodeKey{ x, z, static_cast<std::uint8_t>(level) }, 100.0 + x * 10.0 + z, true };
}

struct LakeSet {
    std::shared_ptr<const water::WaterBodySet> set;
    std::shared_ptr<const water::WaterCarve> carve;
};

LakeSet MakeLake(double cx, double cz, double size, float level, float depth = 6.0f)
{
    water::WaterBody b;
    b.name = "Lake";
    b.id = 1;
    b.kind = water::WaterBodyKind::Lake;
    b.level = level;
    b.depth = depth;
    b.polygon = water::MakeBoxPolygon(cx, cz, size, size);
    LakeSet r;
    r.set = water::WaterBodySet::Build({ b }, {});
    r.carve = water::WaterCarve::Build(*r.set);
    return r;
}

std::vector<Rect2> Dirty(const LakeSet* before, const LakeSet* after)
{
    std::vector<Rect2> rects;
    if (before != nullptr && before->set) {
        water::WaterBodySet::DirtyRects(nullptr, *before->set, rects);
    }
    if (after != nullptr && after->set) {
        water::WaterBodySet::DirtyRects(nullptr, *after->set, rects);
    }
    return rects;
}

template <class Done>
void Pump(TerrainStreamer& streamer, const std::vector<WantedNode>& wanted, std::uint64_t& frame, StreamerUpdate& out, Done done, int cap = 60)
{
    for (int i = 0; i < cap; ++i) {
        streamer.Update(wanted, ++frame, static_cast<double>(frame) / 60.0, out);
        if (done()) {
            return;
        }
    }
}

bool SameMesh(const NodeMesh& a, const NodeMesh& b)
{
    return a.vertices.size() == b.vertices.size()
        && std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(TerrainVertex)) == 0;
}

std::size_t SumBytes(const TerrainStreamer& streamer, const std::vector<NodeKey>& keys)
{
    std::size_t total = 0;
    for (const NodeKey& k : keys) {
        if (const auto m = streamer.Find(k)) {
            total += m->ByteSize();
        }
    }
    return total;
}

} // namespace

TEST(TerrainCarveStreamerTests, OnlyTouchedNodesRebuildAndTheOldMeshKeepsDrawing)
{
    InlineTerrainExecutor exec;
    TerrainStreamer streamer(FastSettings(), {}, &exec);
    std::vector<WantedNode> wanted;
    for (int x = 0; x < 10; ++x) {
        wanted.push_back(Want(x, 0));
    }
    std::uint64_t frame = 0;
    StreamerUpdate out;
    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().resident == 10; });
    ASSERT_EQ(streamer.Stats().resident, 10);
    std::vector<std::shared_ptr<const NodeMesh>> before;
    for (const WantedNode& w : wanted) {
        before.push_back(streamer.Find(w.key));
    }
    out = {};

    // A lake on node (2, 0): x 256..384, z 0..128, level near the ground there.
    const double cx = 320.0;
    const double cz = 64.0;
    const float ground = streamer.Field().BaseHeight(cx, cz, 2.0);
    const LakeSet lake = MakeLake(cx, cz, 60.0, ground - 3.0f);
    const auto dirty = Dirty(nullptr, &lake);
    const int invalidated = streamer.SetCarve(lake.carve, dirty);
    EXPECT_GE(invalidated, 1);
    EXPECT_LE(invalidated, 3) << "a 60 m lake with its 60 m bank touches at most three 128 m nodes";
    EXPECT_EQ(streamer.Stats().resident, 10) << "old meshes stay resident";
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        EXPECT_EQ(streamer.Find(wanted[i].key), before[i]) << "old mesh still drawn at once";
    }
    std::vector<DrawNode> draw;
    streamer.BuildDrawList(wanted, draw);
    EXPECT_EQ(draw.size(), 10u);

    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().rebuilding == 0 && streamer.Stats().pending == 0; });
    EXPECT_EQ(streamer.Stats().rebuilding, 0);
    EXPECT_EQ(static_cast<int>(out.rebuilt.size()), invalidated);
    EXPECT_TRUE(out.ready.empty());
    EXPECT_TRUE(out.evicted.empty());
    int changed = 0;
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        const auto now = streamer.Find(wanted[i].key);
        ASSERT_NE(now, nullptr);
        changed += now != before[i] ? 1 : 0;
        EXPECT_TRUE(SameMesh(*now, BuildNode(streamer.Field(), wanted[i].key)));
    }
    EXPECT_EQ(changed, invalidated);
    std::vector<NodeKey> keys;
    for (const WantedNode& w : wanted) {
        keys.push_back(w.key);
    }
    EXPECT_EQ(streamer.Stats().bytesResident, SumBytes(streamer, keys));
    EXPECT_EQ(streamer.CarveHash(), lake.carve->Hash());
    EXPECT_EQ(streamer.Stats().carveLastDirtyNodes, invalidated);
}

TEST(TerrainCarveStreamerTests, SameCarveAgainIsFreeAndRemovalRestoresTheOriginal)
{
    InlineTerrainExecutor exec;
    TerrainStreamer streamer(FastSettings(), {}, &exec);
    const std::vector<WantedNode> wanted = { Want(1, 0), Want(2, 0) };
    std::uint64_t frame = 0;
    StreamerUpdate out;
    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().resident == 2; });
    const NodeMesh original = BuildNode(streamer.Field(), wanted[1].key);

    const LakeSet lake = MakeLake(320.0, 64.0, 60.0, streamer.Field().BaseHeight(320.0, 64.0, 2.0) - 3.0f);
    const auto dirty = Dirty(nullptr, &lake);
    EXPECT_GE(streamer.SetCarve(lake.carve, dirty), 1);
    EXPECT_EQ(streamer.SetCarve(lake.carve, dirty), 0);
    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().rebuilding == 0; });
    EXPECT_FALSE(SameMesh(*streamer.Find(wanted[1].key), original));

    EXPECT_GE(streamer.SetCarve(nullptr, dirty), 1);
    Pump(streamer, wanted, frame, out, [&] { return streamer.Stats().rebuilding == 0; });
    EXPECT_TRUE(SameMesh(*streamer.Find(wanted[1].key), original));
    EXPECT_EQ(streamer.CarveHash(), 0u);
}

TEST(TerrainCarveStreamerTests, SettingsChangeKeepsTheCarve)
{
    InlineTerrainExecutor exec;
    TerrainStreamer streamer(FastSettings(), {}, &exec);
    const LakeSet lake = MakeLake(320.0, 64.0, 60.0, 5.0f);
    streamer.SetCarve(lake.carve, {});
    TerrainSettings s = streamer.Settings();
    s.heightScale += 100.0f;
    EXPECT_NE(streamer.SetSettings(s), SettingsChange::None);
    EXPECT_EQ(streamer.CarveHash(), lake.carve->Hash());
}

TEST(TerrainCarveStreamerTests, RandomCarveEditsWhileMovingKeepEveryInvariant)
{
    InlineTerrainExecutor exec;
    StreamerConfig cfg;
    cfg.unloadDelayFrames = 6;
    cfg.maxInFlight = 3;
    TerrainStreamer streamer(FastSettings(), cfg, &exec);
    std::mt19937 rng(1234);
    std::unordered_map<NodeKey, const NodeMesh*, NodeKeyHash> published;
    std::uint64_t frame = 0;
    StreamerUpdate out;
    LakeSet current;
    std::vector<WantedNode> wanted;
    int cx = 0;
    int cz = 0;

    const auto consume = [&] {
        for (const auto& m : out.evicted) {
            EXPECT_EQ(published.erase(m), 1u) << "evicted a node that was never published";
        }
        for (const auto& m : out.ready) {
            published[m->key] = m.get();
        }
        for (const auto& m : out.rebuilt) {
            const auto it = published.find(m->key);
            ASSERT_NE(it, published.end()) << "rebuilt node was never published";
            EXPECT_NE(it->second, m.get());
            it->second = m.get();
        }
        out = {};
    };
    const auto verify = [&] {
        int resident = 0;
        for (int x = -6; x <= 10; ++x) {
            for (int z = -6; z <= 6; ++z) {
                const auto m = streamer.Find(NodeKey{ x, z, 0 });
                const auto it = published.find(NodeKey{ x, z, 0 });
                EXPECT_EQ(m != nullptr, it != published.end()) << x << "," << z;
                if (m && it != published.end()) {
                    EXPECT_EQ(m.get(), it->second);
                    ++resident;
                }
            }
        }
        EXPECT_EQ(resident, static_cast<int>(published.size()));
    };

    for (int step = 0; step < 80; ++step) {
        if (step % 3 == 0) {
            cx = static_cast<int>(rng() % 5) + 1;
            cz = static_cast<int>(rng() % 3) - 1;
        }
        wanted.clear();
        for (int x = cx - 1; x <= cx + 1; ++x) {
            for (int z = cz - 1; z <= cz + 1; ++z) {
                wanted.push_back(Want(x, z));
            }
        }
        if (step % 2 == 0) {
            LakeSet next;
            if (rng() % 4 != 0) {
                const double x = static_cast<double>(rng() % 900);
                const double z = static_cast<double>(rng() % 300) - 100.0;
                const double size = 40.0 + static_cast<double>(rng() % 120);
                next = MakeLake(x, z, size, streamer.Field().BaseHeight(x, z, 2.0) - 2.0f);
            }
            std::vector<Rect2> rects;
            if (current.set) {
                water::WaterBodySet::DirtyRects(current.set.get(), next.set ? *next.set : *water::WaterBodySet::Build({}, {}), rects);
            } else if (next.set) {
                water::WaterBodySet::DirtyRects(nullptr, *next.set, rects);
            }
            streamer.SetCarve(next.carve, rects);
            current = next;
        }
        streamer.Update(wanted, ++frame, static_cast<double>(frame) / 60.0, out);
        consume();
        verify();
    }

    // Settle on one view: every resident node must equal a fresh build with the final carve.
    wanted.clear();
    for (int x = cx - 1; x <= cx + 1; ++x) {
        for (int z = cz - 1; z <= cz + 1; ++z) {
            wanted.push_back(Want(x, z));
        }
    }
    for (int i = 0; i < 40; ++i) {
        streamer.Update(wanted, ++frame, static_cast<double>(frame) / 60.0, out);
        consume();
    }
    EXPECT_EQ(streamer.Stats().rebuilding, 0);
    EXPECT_EQ(streamer.Stats().pending, 0);
    for (const WantedNode& w : wanted) {
        const auto m = streamer.Find(w.key);
        ASSERT_NE(m, nullptr);
        EXPECT_TRUE(SameMesh(*m, BuildNode(streamer.Field(), w.key))) << w.key.x << "," << w.key.z;
    }
    for (const auto& [key, mesh] : published) {
        const auto m = streamer.Find(key);
        ASSERT_NE(m, nullptr);
        EXPECT_TRUE(SameMesh(*m, BuildNode(streamer.Field(), key))) << "stale resident node " << key.x << "," << key.z;
    }
}

TEST(TerrainCarveStreamerTests, JobSystemRebuildMatchesSerialBuild)
{
    TerrainStreamer streamer(FastSettings());
    std::vector<WantedNode> wanted;
    for (int x = 0; x < 6; ++x) {
        wanted.push_back(Want(x, 0));
    }
    std::uint64_t frame = 0;
    StreamerUpdate out;
    const auto wait = [&](auto done) {
        const auto start = std::chrono::steady_clock::now();
        do {
            streamer.Update(wanted, ++frame, static_cast<double>(frame) / 60.0, out);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (!done() && std::chrono::steady_clock::now() - start < std::chrono::seconds(30));
    };
    wait([&] { return streamer.Stats().resident == 6; });
    const LakeSet lake = MakeLake(500.0, 64.0, 200.0, streamer.Field().BaseHeight(500.0, 64.0, 2.0) - 3.0f);
    const int n = streamer.SetCarve(lake.carve, Dirty(nullptr, &lake));
    EXPECT_GE(n, 2);
    wait([&] { return streamer.Stats().rebuilding == 0 && streamer.Stats().pending == 0; });
    ASSERT_EQ(streamer.Stats().rebuilding, 0);
    for (const WantedNode& w : wanted) {
        const auto m = streamer.Find(w.key);
        ASSERT_NE(m, nullptr);
        EXPECT_TRUE(SameMesh(*m, BuildNode(streamer.Field(), w.key)));
    }
    std::printf("[terrain] carve rebuild of %d nodes took %.1f ms wall (job system)\n", n, streamer.Stats().carveRebuildMs);
}

} // namespace gte::terrain
