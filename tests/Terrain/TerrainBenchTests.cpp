// Manual benchmarks and image dumps. Run with:
//   GreatTamanaEngineTests.exe --gtest_also_run_disabled_tests --gtest_filter=TerrainBench.*
// Set GTE_TERRAIN_DUMP_DIR to a folder to get PNG previews from the Dump test.

#include "Encoding/PngEncoder.h"
#include "Jobs/JobSystem.h"
#include "Terrain/TerrainLod.h"
#include "Terrain/TerrainMaterial.h"
#include "Terrain/TerrainNode.h"
#include "Terrain/TerrainStreamer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace gte::terrain {

namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// Land nodes near the origin for a level (centre above 150 m), up to `count`.
std::vector<NodeKey> LandNodes(const TerrainHeightField& f, int level, int count)
{
    std::vector<NodeKey> keys;
    const double size = NodeSize(f.Layout(), level);
    const double spacing = NodeSpacing(f.Layout(), level);
    for (int r = 0; r < 40 && static_cast<int>(keys.size()) < count; ++r) {
        for (int z = -r; z <= r && static_cast<int>(keys.size()) < count; ++z) {
            for (int x = -r; x <= r && static_cast<int>(keys.size()) < count; ++x) {
                if (std::max(std::abs(x), std::abs(z)) != r) {
                    continue;
                }
                if (f.Height((x + 0.5) * size, (z + 0.5) * size, spacing) > 150.0) {
                    keys.push_back(NodeKey{ x, z, static_cast<std::uint8_t>(level) });
                }
            }
        }
    }
    return keys;
}

void WritePng(const std::string& path, const std::vector<std::uint8_t>& rgba, int w, int h)
{
    const std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(rgba.data(), w, h);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

void Put(std::vector<std::uint8_t>& img, int w, int x, int y, float r, float g, float b)
{
    std::uint8_t* p = &img[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u];
    p[0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
    p[1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
    p[2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
    p[3] = 255;
}

// Shaded relief of a rectangle of the world, sampled with the height field only.
void DumpRelief(const TerrainHeightField& f, const std::string& path, double cx, double cz, double extent, int px, double spacing)
{
    std::vector<std::uint8_t> img(static_cast<std::size_t>(px) * static_cast<std::size_t>(px) * 4u);
    const double step = extent / px;
    const Vec3 sun = Normalize(Vec3{ -0.5f, 0.7f, -0.45f });
    for (int j = 0; j < px; ++j) {
        for (int i = 0; i < px; ++i) {
            const double x = cx - extent * 0.5 + i * step;
            const double z = cz + extent * 0.5 - j * step;
            const float h = f.Height(x, z, spacing);
            const float hx = f.Height(x + step, z, spacing) - f.Height(x - step, z, spacing);
            const float hz = f.Height(x, z + step, spacing) - f.Height(x, z - step, spacing);
            const Vec3 n = Normalize(Vec3{ -hx, static_cast<float>(2.0 * step), -hz });
            const float slope = std::acos(std::min(1.0f, n.y));
            const float lit = 0.3f + 0.7f * std::max(0.0f, Dot(n, sun));
            if (h <= 0.0f) {
                const float depth = std::clamp(-h / 300.0f, 0.0f, 1.0f);
                Put(img, px, i, j, 0.05f, 0.25f - 0.12f * depth, 0.5f - 0.2f * depth);
                continue;
            }
            const Rgb c = SurfaceColor(h, slope, f.Settings());
            Put(img, px, i, j, c.r * lit, c.g * lit, c.b * lit);
        }
    }
    WritePng(path, img, px, px);
}

// Draws a grid of built nodes top-down using their own normals and colours.
void DumpNodes(const TerrainHeightField& f, const std::string& path, NodeKey first, int nodesPerSide, int pxPerNode)
{
    const int q = f.Layout().quads;
    const int px = nodesPerSide * pxPerNode;
    std::vector<std::uint8_t> img(static_cast<std::size_t>(px) * static_cast<std::size_t>(px) * 4u);
    const Vec3 sun = Normalize(Vec3{ -0.5f, 0.7f, -0.45f });
    for (int nz = 0; nz < nodesPerSide; ++nz) {
        for (int nx = 0; nx < nodesPerSide; ++nx) {
            const NodeMesh m = BuildNode(f, NodeKey{ first.x + nx, first.z + nz, first.level });
            for (int j = 0; j < pxPerNode; ++j) {
                for (int i = 0; i < pxPerNode; ++i) {
                    const int vx = std::min(q, i * q / pxPerNode);
                    const int vz = std::min(q, j * q / pxPerNode);
                    const std::size_t vi = static_cast<std::size_t>(vz) * static_cast<std::size_t>(q + 1) + static_cast<std::size_t>(vx);
                    const TerrainVertex& v = m.vertices[vi];
                    const std::uint32_t col = m.colors[vi];
                    const float lit = 0.3f + 0.7f * std::max(0.0f, Dot(Vec3{ v.normal[0], v.normal[1], v.normal[2] }, sun));
                    const int x = nx * pxPerNode + i;
                    const int y = (nodesPerSide - 1 - nz) * pxPerNode + (pxPerNode - 1 - j);
                    Put(img, px, x, y, static_cast<float>(col & 0xFFu) / 255.0f * lit, static_cast<float>((col >> 8) & 0xFFu) / 255.0f * lit,
                        static_cast<float>((col >> 16) & 0xFFu) / 255.0f * lit);
                }
            }
        }
    }
    WritePng(path, img, px, px);
}

} // namespace

TEST(TerrainBench, DISABLED_BuildNodeCostPerLevel)
{
    TerrainSettings s;
    const TerrainHeightField f(Sanitize(s));
    std::printf("\n[TerrainBench] BuildNode, single thread, ms per node (average of 8 land nodes)\n");
    std::printf("%-6s %10s %10s %10s %10s %10s\n", "level", "height", "erosion", "mesh", "total", "erodes?");
    for (int level = 0; level < f.Settings().lodLevelCount; ++level) {
        std::vector<NodeKey> keys = LandNodes(f, level, 8);
        if (keys.empty()) {
            keys.push_back(NodeKey{ 0, 0, static_cast<std::uint8_t>(level) });
        }
        NodeBuildTiming sum;
        double total = 0.0;
        for (const NodeKey& k : keys) {
            // Best of 5 runs per node removes scheduler and clock noise.
            NodeBuildTiming best;
            double bestTotal = 1.0e30;
            best.heightMs = best.erosionMs = best.meshMs = 1.0e30;
            for (int run = 0; run < 5; ++run) {
                NodeBuildTiming t;
                const auto t0 = Clock::now();
                const NodeMesh m = BuildNode(f, k, &t);
                bestTotal = std::min(bestTotal, MsSince(t0));
                best.heightMs = std::min(best.heightMs, t.heightMs);
                best.erosionMs = std::min(best.erosionMs, t.erosionMs);
                best.meshMs = std::min(best.meshMs, t.meshMs);
                EXPECT_FALSE(m.vertices.empty());
            }
            total += bestTotal;
            sum.heightMs += best.heightMs;
            sum.erosionMs += best.erosionMs;
            sum.meshMs += best.meshMs;
        }
        const double n = static_cast<double>(keys.size());
        std::printf("%-6d %10.3f %10.3f %10.3f %10.3f %10s\n", level, sum.heightMs / n, sum.erosionMs / n, sum.meshMs / n, total / n,
            NodeUsesErosion(f.Settings(), level) ? "yes" : "no");
    }
}

TEST(TerrainBench, DISABLED_HeightLayerCost)
{
    TerrainSettings base;
    const TerrainHeightField probe(Sanitize(base));
    const NodeKey key = LandNodes(probe, 0, 1).at(0);
    const NodeGeometry g = NodeGeometryOf(probe.Layout(), key);
    struct Variant {
        const char* name;
        TerrainSettings settings;
    };
    std::vector<Variant> variants;
    variants.push_back({ "all layers", base });
    TerrainSettings v = base;
    v.domainWarpStrength = 0.0f;
    variants.push_back({ "no warp", v });
    v = base;
    v.peaksWeight = 0.0f;
    variants.push_back({ "no peaks", v });
    v = base;
    v.detailAmplitude = 0.0f;
    variants.push_back({ "no detail", v });
    v = base;
    v.octaves = 3;
    variants.push_back({ "3 peak octaves", v });
    std::vector<float> grid(97u * 97u);
    std::printf("\n[TerrainBench] FillGrid 97x97 at level 0 (best of 30), ms\n");
    for (const Variant& var : variants) {
        const TerrainHeightField f(Sanitize(var.settings));
        double best = 1.0e30;
        for (int run = 0; run < 30; ++run) {
            const auto t0 = Clock::now();
            f.FillGrid(g.originX - 16 * g.spacing, g.originZ - 16 * g.spacing, g.spacing, 97, 97, grid.data());
            best = std::min(best, MsSince(t0));
        }
        std::printf("  %-16s %8.3f ms  (%.0f ns/sample)\n", var.name, best, best * 1.0e6 / (97.0 * 97.0));
    }
    // Reference: the same samples through the per-sample entry point (no lattice).
    double best = 1.0e30;
    for (int run = 0; run < 30; ++run) {
        const auto t0 = Clock::now();
        float sink = 0.0f;
        for (int j = 0; j < 97; ++j) {
            for (int i = 0; i < 97; ++i) {
                sink += probe.Height(g.originX + (i - 16) * g.spacing, g.originZ + (j - 16) * g.spacing, g.spacing);
            }
        }
        best = std::min(best, MsSince(t0));
        EXPECT_TRUE(std::isfinite(sink));
    }
    std::printf("  %-16s %8.3f ms  (%.0f ns/sample)\n", "per-sample calls", best, best * 1.0e6 / (97.0 * 97.0));

    // How far the interpolated lattice layers move heights and slopes compared with exact per-sample noise.
    probe.FillGrid(g.originX - 16 * g.spacing, g.originZ - 16 * g.spacing, g.spacing, 97, 97, grid.data());
    double maxErr = 0.0;
    double sumErr = 0.0;
    double maxSlopeErr = 0.0;
    for (int j = 1; j < 96; ++j) {
        for (int i = 1; i < 96; ++i) {
            const auto exact = [&](int ii, int jj) {
                return probe.Height(g.originX + (ii - 16) * g.spacing, g.originZ + (jj - 16) * g.spacing, g.spacing);
            };
            const double e = std::fabs(grid[static_cast<std::size_t>(j) * 97u + static_cast<std::size_t>(i)] - exact(i, j));
            maxErr = std::max(maxErr, e);
            sumErr += e;
            const double gxApprox = (grid[static_cast<std::size_t>(j) * 97u + static_cast<std::size_t>(i) + 1] - grid[static_cast<std::size_t>(j) * 97u + static_cast<std::size_t>(i) - 1]) / (2.0 * g.spacing);
            const double gxExact = (exact(i + 1, j) - exact(i - 1, j)) / (2.0 * g.spacing);
            maxSlopeErr = std::max(maxSlopeErr, std::fabs(gxApprox - gxExact));
        }
    }
    std::printf("  lattice vs exact: mean height error %.3f m, max %.3f m, max x-slope error %.4f\n", sumErr / (95.0 * 95.0), maxErr, maxSlopeErr);
}

TEST(TerrainBench, DISABLED_NodesPerSecondSingleVersusJobSystem)
{
    TerrainSettings s;
    const TerrainHeightField f(Sanitize(s));
    const std::vector<NodeKey> keys = LandNodes(f, 0, 32);
    ASSERT_FALSE(keys.empty());
    auto t0 = Clock::now();
    for (const NodeKey& k : keys) {
        const NodeMesh m = BuildNode(f, k);
        EXPECT_FALSE(m.vertices.empty());
    }
    const double serialMs = MsSince(t0);

    StreamerConfig cfg;
    cfg.maxInFlight = static_cast<int>(Jobs::JobSystem::Instance().WorkerCount());
    TerrainStreamer streamer(Sanitize(s), cfg);
    std::vector<WantedNode> wanted;
    for (const NodeKey& k : keys) {
        wanted.push_back(WantedNode{ k, 1.0, true });
    }
    t0 = Clock::now();
    std::uint64_t frame = 0;
    StreamerUpdate out;
    while (streamer.Stats().resident < static_cast<int>(keys.size()) && MsSince(t0) < 60000.0) {
        streamer.Update(wanted, ++frame, static_cast<double>(frame) * 0.001, out);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    const double jobsMs = MsSince(t0);
    std::printf("\n[TerrainBench] %zu level-0 nodes with erosion: serial %.1f ms (%.1f nodes/s), %d job workers %.1f ms (%.1f nodes/s)\n", keys.size(),
        serialMs, 1000.0 * static_cast<double>(keys.size()) / serialMs, cfg.maxInFlight, jobsMs, 1000.0 * static_cast<double>(keys.size()) / jobsMs);
}

TEST(TerrainBench, DISABLED_SelectionCost)
{
    for (const float factor : { 2.5f, 5.0f, 9.0f }) {
        TerrainSettings s;
        s.lodDistanceFactor = factor;
        s.frustumCulling = false;
        s.maxViewDistance = 200000.0f;
        LodSelector selector(Sanitize(s));
        std::vector<WantedNode> nodes;
        selector.Select(LodView{}, nodes);
        constexpr int kRuns = 200;
        const auto t0 = Clock::now();
        for (int i = 0; i < kRuns; ++i) {
            LodView v;
            v.cameraX = i * 3.0;
            v.cameraY = 300.0;
            selector.Select(v, nodes);
        }
        std::printf("[TerrainBench] factor %.1f: %zu leaves, %.4f ms per Select\n", factor, nodes.size(), MsSince(t0) / kRuns);
    }
}

TEST(TerrainBench, DISABLED_WorkingSetWithFrustum)
{
    const float t = std::tan(30.0f * 3.14159265f / 180.0f);
    const float aspect = 16.0f / 9.0f;
    const float nearZ = 0.5f;
    for (const float viewDistance : { 4000.0f, 8000.0f, 12000.0f, 20000.0f }) {
        for (const float factor : { 1.5f, 2.0f, 2.5f }) {
            float m[16] = {};
            m[0] = 1.0f / (aspect * t);
            m[5] = 1.0f / t;
            m[10] = viewDistance / (viewDistance - nearZ);
            m[11] = 1.0f;
            m[14] = -nearZ * viewDistance / (viewDistance - nearZ);
            TerrainSettings s;
            s.lodDistanceFactor = factor;
            s.maxViewDistance = viewDistance;
            LodSelector selector(Sanitize(s));
            LodView view;
            view.cameraY = 400.0;
            view.frustum = ExtractFrustumPlanes(m);
            std::vector<WantedNode> nodes;
            LodSelectionStats stats;
            selector.Select(view, nodes, &stats);
            const double bytesPerNode = static_cast<double>(NodeVertexCount(64)) * (sizeof(TerrainVertex) + 4.0);
            std::printf("[TerrainBench] view %5.0f m factor %.1f: wanted %4d, in view %4d, triangles in view %.2f M, resident MB %.0f\n", viewDistance, factor,
                stats.leaves, stats.inView, stats.inView * (NodeIndexCount(64) / 3.0) / 1.0e6, static_cast<double>(stats.leaves) * bytesPerNode / 1.0e6);
        }
    }
}

TEST(TerrainBench, DISABLED_ErosionStrength)
{
    TerrainSettings base;
    base.erosionEnabled = false;
    const TerrainHeightField plain(Sanitize(base));
    const std::vector<NodeKey> keys = LandNodes(plain, 0, 6);
    ASSERT_FALSE(keys.empty());
    std::printf("\n[TerrainBench] erosion strength over %zu land nodes (heights in metres)\n", keys.size());
    std::printf("%-9s %-6s %-6s %10s %10s %10s\n", "droplets", "strng", "-", "meanAbs", "maxAbs", "ms/node");
    for (const int drops : { 1800, 4000, 8000 }) {
        for (const float rate : { 3.0f, 6.0f, 12.0f }) {
            TerrainSettings s = base;
            s.erosionEnabled = true;
            s.erosionDropletCount = drops;
            s.erosionStrength = rate;
            const TerrainHeightField f(Sanitize(s));
            double sumAbs = 0.0;
            double maxAbs = 0.0;
            double ms = 0.0;
            for (const NodeKey& k : keys) {
                const NodeMesh a = BuildNode(plain, k);
                const auto t0 = Clock::now();
                const NodeMesh b = BuildNode(f, k);
                ms += MsSince(t0);
                for (std::size_t i = 0; i < GridVertexCount(64); ++i) {
                    const double d = std::fabs(a.vertices[i].position[1] - b.vertices[i].position[1]);
                    sumAbs += d;
                    maxAbs = std::max(maxAbs, d);
                }
            }
            std::printf("%-9d %-6.2f %-6.2f %10.4f %10.3f %10.3f\n", drops, rate, 0.3, sumAbs / (static_cast<double>(keys.size()) * GridVertexCount(64)), maxAbs,
                ms / static_cast<double>(keys.size()));
        }
    }
}

TEST(TerrainBench, DISABLED_HeightStatistics)
{
    const TerrainSettings s = Sanitize(TerrainSettings{});
    const TerrainHeightField f(s);
    std::vector<float> land;
    int ocean = 0;
    int total = 0;
    float steepSum = 0.0f;
    for (int j = 0; j < 256; ++j) {
        for (int i = 0; i < 256; ++i) {
            const double x = -16000.0 + i * 125.0;
            const double z = -16000.0 + j * 125.0;
            const float h = f.Height(x, z, 128.0);
            ++total;
            if (h <= 0.0f) {
                ++ocean;
            } else {
                land.push_back(h);
                steepSum += f.Slope(x, z, 16.0);
            }
        }
    }
    std::sort(land.begin(), land.end());
    const auto pct = [&](double p) { return land[static_cast<std::size_t>(p * static_cast<double>(land.size() - 1))]; };
    const float snowLine = s.snowMinHeightFraction * s.heightScale;
    const auto above = std::count_if(land.begin(), land.end(), [&](float h) { return h > snowLine; });
    std::printf("\n[TerrainBench] ocean %.1f%%, land heights m: p50 %.0f p90 %.0f p99 %.0f max %.0f, above snow line (%.0f m) %.2f%% of land, mean slope %.1f deg\n",
        100.0 * ocean / total, pct(0.5), pct(0.9), pct(0.99), land.back(), snowLine, 100.0 * static_cast<double>(above) / static_cast<double>(land.size()),
        steepSum / static_cast<float>(land.size()) * 57.2958f);
}

TEST(TerrainBench, DISABLED_Dump)
{
    const char* dir = std::getenv("GTE_TERRAIN_DUMP_DIR");
    if (dir == nullptr) {
        GTEST_SKIP() << "GTE_TERRAIN_DUMP_DIR not set";
    }
    const std::string base(dir);
    TerrainSettings s;
    const TerrainHeightField f(Sanitize(s));
    DumpRelief(f, base + "\\world_overview.png", 0.0, 0.0, 32768.0, 768, 64.0);
    DumpRelief(f, base + "\\region_8km.png", 0.0, 0.0, 8192.0, 768, 8.0);

    const std::vector<NodeKey> land = LandNodes(f, 0, 1);
    ASSERT_FALSE(land.empty());
    TerrainSettings noErosion = s;
    noErosion.erosionEnabled = false;
    const TerrainHeightField plain(Sanitize(noErosion));
    DumpNodes(plain, base + "\\nodes_plain.png", land[0], 4, 128);
    DumpNodes(f, base + "\\nodes_eroded.png", land[0], 4, 128);
    std::printf("[TerrainBench] wrote PNGs to %s\n", dir);
}

} // namespace gte::terrain
