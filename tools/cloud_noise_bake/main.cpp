// cloud_noise_bake: offline bake and verify of tileable cloud noise assets.
// Usage: cloud_noise_bake bake --out DIR [--blue FILE.png] [--gen-blue]
//        cloud_noise_bake verify --dir DIR
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- GTVOL file format
constexpr std::uint32_t kFormatRgba8 = 0;
constexpr std::uint32_t kFormatR8 = 1;
constexpr std::uint32_t kFlagTileable = 1;

struct GtvHeader {
    char magic[4] = {'G', 'T', 'V', '1'};
    std::uint32_t width = 0, height = 0, depth = 0;
    std::uint32_t format = 0, flags = 0, reserved0 = 0, reserved1 = 0;
};
static_assert(sizeof(GtvHeader) == 32);

struct Volume {
    GtvHeader h;
    std::vector<std::uint8_t> data;
    int channels() const { return h.format == kFormatRgba8 ? 4 : 1; }
};

bool WriteGtvol(const fs::path& path, const Volume& v) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(&v.h), sizeof(GtvHeader));
    f.write(reinterpret_cast<const char*>(v.data.data()), static_cast<std::streamsize>(v.data.size()));
    return static_cast<bool>(f);
}

bool ReadGtvol(const fs::path& path, Volume& v) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(&v.h), sizeof(GtvHeader));
    if (!f || std::memcmp(v.h.magic, "GTV1", 4) != 0) return false;
    const std::size_t bytes = static_cast<std::size_t>(v.h.width) * v.h.height * v.h.depth * v.channels();
    v.data.resize(bytes);
    f.read(reinterpret_cast<char*>(v.data.data()), static_cast<std::streamsize>(bytes));
    return static_cast<bool>(f);
}

// ---------------------------------------------------------------- helpers
template <class F>
void ParallelFor(int n, F&& fn) {
    const unsigned workers = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < workers; ++t) {
        pool.emplace_back([&] {
            for (int i = next++; i < n; i = next++) fn(i);
        });
    }
    for (auto& t : pool) t.join();
}

std::uint32_t Fmix(std::uint32_t h) {
    h ^= h >> 16; h *= 0x85EBCA6Bu;
    h ^= h >> 13; h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

std::uint32_t Hash3(int x, int y, int z, std::uint32_t seed) {
    std::uint32_t h = seed * 0x9E3779B1u + 0x7F4A7C15u;
    h = Fmix(h ^ (static_cast<std::uint32_t>(x) * 0x85EBCA6Bu));
    h = Fmix(h ^ (static_cast<std::uint32_t>(y) * 0xC2B2AE35u + 0x165667B1u));
    h = Fmix(h ^ (static_cast<std::uint32_t>(z) * 0x27D4EB2Fu + 0x9E3779B9u));
    return h;
}

float U01(std::uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }

int Wrap(int c, int n) { return ((c % n) + n) % n; }

float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float Smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------- periodic noise
// p in [0,1) per axis; 'cells' lattice cells per period, so the period is exactly 1.0.
float PerlinPeriodic(float px, float py, float pz, int cells, std::uint32_t seed) {
    static const int kGrad[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                                     {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
    const float x = px * cells, y = py * cells, z = pz * cells;
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y)), iz = static_cast<int>(std::floor(z));
    const float fx = x - ix, fy = y - iy, fz = z - iz;
    float corner[2][2][2];
    for (int dz = 0; dz < 2; ++dz)
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const std::uint32_t h = Hash3(Wrap(ix + dx, cells), Wrap(iy + dy, cells), Wrap(iz + dz, cells), seed);
                const int* g = kGrad[h % 12u];
                corner[dz][dy][dx] = g[0] * (fx - dx) + g[1] * (fy - dy) + g[2] * (fz - dz);
            }
    const float u = Fade(fx), v = Fade(fy), w = Fade(fz);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00 = lerp(corner[0][0][0], corner[0][0][1], u);
    const float x10 = lerp(corner[0][1][0], corner[0][1][1], u);
    const float x01 = lerp(corner[1][0][0], corner[1][0][1], u);
    const float x11 = lerp(corner[1][1][0], corner[1][1][1], u);
    return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
}

// Distance to the nearest feature point (F1) in cell units, cell indices wrap modulo 'cells'.
float WorleyF1Periodic(float px, float py, float pz, int cells, std::uint32_t seed) {
    const float x = px * cells, y = py * cells, z = pz * cells;
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y)), iz = static_cast<int>(std::floor(z));
    const float fx = x - ix, fy = y - iy, fz = z - iz;
    float best = 1e9f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const std::uint32_t h = Hash3(Wrap(ix + dx, cells), Wrap(iy + dy, cells), Wrap(iz + dz, cells), seed);
                const float ox = dx + U01(h) - fx;
                const float oy = dy + U01(Fmix(h ^ 0xA511E9B3u)) - fy;
                const float oz = dz + U01(Fmix(h ^ 0x63D83595u)) - fz;
                best = std::min(best, ox * ox + oy * oy + oz * oz);
            }
    return std::sqrt(best);
}

// Inverted Worley: 1 at feature points (blobs are bright), 0 at cell borders.
float WorleyInv(float px, float py, float pz, int cells, std::uint32_t seed) {
    return 1.0f - std::min(WorleyF1Periodic(px, py, pz, cells, seed), 1.0f);
}

// Periodic 2D inverted Worley (1 - F1 at z = 0): 1 at cluster centers, 0 at cell borders.
float WorleyInv2DPoint(float px, float py, int cells, std::uint32_t seed) {
    const float x = px * cells, y = py * cells;
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const float fx = x - ix, fy = y - iy;
    float best = 1.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const std::uint32_t h = Hash3(Wrap(ix + dx, cells), Wrap(iy + dy, cells), 0, seed);
            const float ox = dx + U01(h) - fx;
            const float oy = dy + U01(Fmix(h ^ 0xA511E9B3u)) - fy;
            best = std::min(best, ox * ox + oy * oy);
        }
    return 1.0f - std::sqrt(best);
}

// FBM: every octave doubles the cell count, so every octave tiles with period 1.
template <class Fn>
float Fbm(float px, float py, float pz, int baseCells, int octaves, float gain, std::uint32_t seed, Fn&& noise) {
    float sum = 0, amp = 1, norm = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * noise(px, py, pz, baseCells << o, seed + 1000u * o);
        norm += amp;
        amp *= gain;
    }
    return sum / norm;
}

float PerlinFbm(float x, float y, float z, int base, int oct, std::uint32_t seed) {
    return Fbm(x, y, z, base, oct, 0.5f, seed, PerlinPeriodic);
}

// Octave weights 0.625 / 0.25 / 0.125 as in the Schneider shader recipe.
float WorleyFbm3(float x, float y, float z, int base, std::uint32_t seed) {
    return 0.625f * WorleyInv(x, y, z, base, seed) + 0.25f * WorleyInv(x, y, z, base * 2, seed + 1000u) +
           0.125f * WorleyInv(x, y, z, base * 4, seed + 2000u);
}

// ---------------------------------------------------------------- float volumes
using FVol = std::vector<float>;

template <class Fn>
FVol Fill(int w, int h, int d, Fn&& fn) {
    FVol out(static_cast<std::size_t>(w) * h * d);
    ParallelFor(d, [&](int z) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                out[(static_cast<std::size_t>(z) * h + y) * w + x] = fn(static_cast<float>(x) / w, static_cast<float>(y) / h, static_cast<float>(z) / d);
    });
    return out;
}

void Stretch(FVol& v) {
    const auto [mn, mx] = std::minmax_element(v.begin(), v.end());
    const float lo = *mn, inv = 1.0f / std::max(*mx - *mn, 1e-9f);
    for (float& f : v) f = (f - lo) * inv;
}

std::uint8_t Q(float f) { return static_cast<std::uint8_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); }

Volume Pack(int w, int h, int d, const std::vector<const FVol*>& ch) {
    Volume v;
    v.h.width = w; v.h.height = h; v.h.depth = d;
    v.h.format = ch.size() == 4 ? kFormatRgba8 : kFormatR8;
    v.h.flags = kFlagTileable;
    const std::size_t n = static_cast<std::size_t>(w) * h * d, c = ch.size();
    v.data.resize(n * c);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = 0; k < c; ++k) v.data[i * c + k] = Q((*ch[k])[i]);
    return v;
}

// ---------------------------------------------------------------- the assets
// Base shape 128^3: R Perlin-Worley, G/B/A single-octave inverted Worley at 8/16/32 cells.
Volume BakeShape(int n) {
    FVol perlin = Fill(n, n, n, [](float x, float y, float z) { return PerlinFbm(x, y, z, 4, 4, 11u); });
    FVol worley = Fill(n, n, n, [](float x, float y, float z) { return WorleyFbm3(x, y, z, 4, 21u); });
    Stretch(perlin);
    Stretch(worley);
    FVol r(perlin.size());
    for (std::size_t i = 0; i < r.size(); ++i) r[i] = worley[i] + perlin[i] * (1.0f - worley[i]);
    Stretch(r);
    auto single = [&](int cells, std::uint32_t seed) {
        FVol v = Fill(n, n, n, [=](float x, float y, float z) { return WorleyInv(x, y, z, cells, seed); });
        Stretch(v);
        return v;
    };
    FVol g = single(8, 31u), b = single(16, 41u), a = single(32, 51u);
    return Pack(n, n, n, {&r, &g, &b, &a});
}

// Detail 64^3, four channels, all periodic (the shader reads them with one trilinear fetch):
//   R billowy low   : inverted Worley, 4/8/16 cells mixed 0.55/0.30/0.15 (16/8/4 texels per cell).
//   G billowy high  : inverted Worley, 16/24 cells mixed 0.7/0.3 (4 and 2.7 texels per cell; no octave below 2.5 texels).
//   B wispy low     : ridged Perlin (1 - |2p - 1|)^2, 4 cells, 3 octaves, domain warped by a Perlin vector field.
//   A wispy high    : the same at 12 cells, 2 octaves.
// A billow is bright at the cell centres (erode the borders to get cauliflower lobes); a wisp is bright on thin
// sheets and strands (erode everything else to get ragged fibres).
Volume BakeDetail(int n) {
    auto worleyMix = [&](std::initializer_list<std::pair<int, float>> octaves, std::uint32_t seed) {
        FVol v = Fill(n, n, n, [&](float x, float y, float z) {
            float sum = 0, norm = 0;
            std::uint32_t s = seed;
            for (const auto& [cells, weight] : octaves) {
                sum += weight * WorleyInv(x, y, z, cells, s);
                norm += weight;
                s += 1000u;
            }
            return sum / norm;
        });
        Stretch(v);
        return v;
    };
    auto ridged = [&](int base, int octaves, std::uint32_t seed) {
        // Warp: three independent periodic Perlin fields move the lookup position by up to 0.06 of the tile.
        FVol raw = Fill(n, n, n, [=](float x, float y, float z) {
            const float wx = 0.06f * PerlinPeriodic(x, y, z, 3, seed + 7001u);
            const float wy = 0.06f * PerlinPeriodic(x, y, z, 3, seed + 7002u);
            const float wz = 0.06f * PerlinPeriodic(x, y, z, 3, seed + 7003u);
            return PerlinFbm(x + wx, y + wy, z + wz, base, octaves, seed);
        });
        Stretch(raw);
        for (float& f : raw) {
            const float r = 1.0f - std::fabs(2.0f * f - 1.0f);
            f = r * r;
        }
        Stretch(raw);
        return raw;
    };
    FVol r = worleyMix({{4, 0.55f}, {8, 0.30f}, {16, 0.15f}}, 61u);
    FVol g = worleyMix({{16, 0.7f}, {24, 0.3f}}, 71u);
    FVol b = ridged(4, 3, 81u);
    FVol a = ridged(12, 2, 91u);
    return Pack(n, n, n, {&r, &g, &b, &a});
}

// Curl 128^2: the curl of a periodic Perlin potential, a divergence free 2D field used to push the detail lookup
// around. R and G are the two curl components (0.5 = zero), B is a second independent field for the height shift.
Volume BakeCurl(int n) {
    auto potential = [&](std::uint32_t seed) {
        FVol v = Fill(n, n, 1, [=](float x, float y, float) { return PerlinFbm(x, y, 0.37f, 3, 3, seed); });
        Stretch(v);
        return v;
    };
    const FVol p1 = potential(201u), p2 = potential(211u);
    FVol r(static_cast<std::size_t>(n) * n), g(r.size()), b(r.size()), a(r.size(), 1.0f);
    auto at = [&](const FVol& v, int x, int y) { return v[static_cast<std::size_t>(Wrap(y, n)) * n + Wrap(x, n)]; };
    float maxMag = 1e-9f;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * n + x;
            r[i] = (at(p1, x, y + 1) - at(p1, x, y - 1)) * 0.5f;
            g[i] = -(at(p1, x + 1, y) - at(p1, x - 1, y)) * 0.5f;
            b[i] = at(p2, x, y) - 0.5f;
            maxMag = std::max({maxMag, std::fabs(r[i]), std::fabs(g[i])});
        }
    for (std::size_t i = 0; i < r.size(); ++i) {
        r[i] = 0.5f + 0.5f * r[i] / maxMag;
        g[i] = 0.5f + 0.5f * g[i] / maxMag;
        b[i] = 0.5f + b[i];
    }
    return Pack(n, n, 1, {&r, &g, &b, &a});
}

// Periodic separable 2D Gaussian blur wrapped modulo n on both axes.
FVol BlurPeriodic2D(const FVol& src, int n, int radius, float sigma) {
    std::vector<float> kernel(static_cast<std::size_t>(2 * radius + 1));
    float ksum = 0.0f;
    const float inv2Sigma2 = 1.0f / (2.0f * sigma * sigma);
    for (int d = -radius; d <= radius; ++d) {
        const float w = std::exp(-static_cast<float>(d * d) * inv2Sigma2);
        kernel[static_cast<std::size_t>(d + radius)] = w;
        ksum += w;
    }
    for (float& w : kernel) w /= ksum;

    const std::size_t stride = static_cast<std::size_t>(n);
    FVol tmp(src.size()), dst(src.size());
    ParallelFor(n, [&](int y) {
        const std::size_t row = static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < n; ++x) {
            float acc = 0.0f;
            for (int d = -radius; d <= radius; ++d)
                acc += src[row + static_cast<std::size_t>(Wrap(x + d, n))] * kernel[static_cast<std::size_t>(d + radius)];
            tmp[row + static_cast<std::size_t>(x)] = acc;
        }
    });
    ParallelFor(n, [&](int y) {
        const std::size_t row = static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < n; ++x) {
            float acc = 0.0f;
            for (int d = -radius; d <= radius; ++d)
                acc += tmp[static_cast<std::size_t>(Wrap(y + d, n)) * stride + static_cast<std::size_t>(x)] * kernel[static_cast<std::size_t>(d + radius)];
            dst[row + static_cast<std::size_t>(x)] = acc;
        }
    });
    return dst;
}

// Fast parallel 2D periodic inverted Worley field (1 at cluster centers, 0 at cell borders).
FVol WorleyInv2D(int n, int cells, std::uint32_t seed, const FVol* warpX = nullptr, const FVol* warpY = nullptr,
                 float warp = 0.0f) {
    FVol out(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
    const float invN = 1.0f / static_cast<float>(n);
    const bool doWarp = warpX != nullptr && warpY != nullptr && warp != 0.0f;
    ParallelFor(n, [&](int y) {
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(n);
        const float baseY = static_cast<float>(y) * invN;
        for (int x = 0; x < n; ++x) {
            const std::size_t i = row + static_cast<std::size_t>(x);
            const float px = static_cast<float>(x) * invN + (doWarp ? warp * ((*warpX)[i] - 0.5f) : 0.0f);
            const float py = baseY + (doWarp ? warp * ((*warpY)[i] - 0.5f) : 0.0f);
            out[i] = WorleyInv2DPoint(px, py, cells, seed);
        }
    });
    return out;
}

struct VoronoiCell2D {
    float d1 = 0.0f;
    float wall = 0.0f;
    int cx = 0;
    int cy = 0;
};

// Periodic 2D Voronoi nearest distance and smooth cell-wall distance in cell units.
VoronoiCell2D VoronoiSmoothWall2D(float px, float py, int cells, std::uint32_t seed, float jitter, float cornerK) {
    const float x = px * static_cast<float>(cells), y = py * static_cast<float>(cells);
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy);
    const float pad = 0.5f * (1.0f - jitter);
    float ox[9], oy[9];
    int wx[9], wy[9];
    float bestD2 = 1e9f;
    int bestIdx = 0, idx = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        const int cy = Wrap(iy + dy, cells);
        for (int dx = -1; dx <= 1; ++dx) {
            const int cx = Wrap(ix + dx, cells);
            const std::uint32_t h = Hash3(cx, cy, 0, seed);
            const float rx = static_cast<float>(dx) + pad + jitter * U01(h) - fx;
            const float ry = static_cast<float>(dy) + pad + jitter * U01(Fmix(h ^ 0xA511E9B3u)) - fy;
            const float d2 = rx * rx + ry * ry;
            ox[idx] = rx;
            oy[idx] = ry;
            wx[idx] = cx;
            wy[idx] = cy;
            if (d2 < bestD2) {
                bestD2 = d2;
                bestIdx = idx;
            }
            ++idx;
        }
    }
    const float v1x = ox[bestIdx], v1y = oy[bestIdx];
    float wall = 8.0f;
    for (int j = 0; j < 9; ++j) {
        if (j == bestIdx) continue;
        const float ddx = ox[j] - v1x, ddy = oy[j] - v1y;
        const float dl = std::sqrt(ddx * ddx + ddy * ddy);
        if (dl > 1e-5f) {
            const float dist = 0.5f * ((v1x + ox[j]) * ddx + (v1y + oy[j]) * ddy) / dl;
            const float h = std::max(cornerK - std::fabs(wall - dist), 0.0f) / cornerK;
            wall = std::min(wall, dist) - h * h * cornerK * 0.25f;
        }
    }
    return {std::sqrt(bestD2), std::max(0.0f, wall), wx[bestIdx], wy[bestIdx]};
}

// Weather 512^2: R cumulus clusters and fair-weather puffs, G blurred R mass, B domed cloud type, A core density.
Volume BakeWeather(std::uint32_t size, const Volume& curl) {
    const int n = static_cast<int>(size);
    const std::size_t total = static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
    const double invTotal = 1.0 / static_cast<double>(total);
    const float invN = 1.0f / static_cast<float>(n);
    const float zf = 0.37f;
    auto pf = [&](int base, int oct, std::uint32_t seed) {
        FVol v = Fill(n, n, 1, [=](float x, float y, float) { return PerlinFbm(x, y, zf, base, oct, seed); });
        Stretch(v);
        return v;
    };
    auto smax = [](float a, float b, float k) {
        const float h = std::max(k - std::fabs(a - b), 0.0f) / k;
        return std::max(a, b) + h * h * k * 0.25f;
    };

    // Periodic 2-octave Perlin domain warps and detail fields keep cumulus clusters organic and seamless.
    const FVol warpX = pf(5, 2, 201u), warpY = pf(5, 2, 211u);
    const FVol warpFX = pf(10, 2, 221u), warpFY = pf(10, 2, 231u);
    const FVol pd = pf(11, 3, 141u);
    const int coneBlurR = std::max(1, static_cast<int>(std::lround(static_cast<double>(size) * 2.0 / 512.0)));
    const float coneBlurSigma = 1.1f * (static_cast<float>(coneBlurR) / 2.0f);
    const int speckBlurR = std::max(1, static_cast<int>(std::lround(static_cast<double>(size) * 5.0 / 512.0)));
    const float speckBlurSigma = 2.2f * (static_cast<float>(speckBlurR) / 5.0f);

    // Exact VolumetricCloud.comp weather UV lookup at windOffset=(0,0), windShearM=280, windDirection=45 deg.
    constexpr float kHalfShearM = -0.5f * 280.0f * 0.70710678f;
    constexpr float kWeatherWarpTileM = 50000.0f;
    constexpr float kWeatherWarpM = 1200.0f;
    constexpr float kWeatherScaleM = 25000.0f;
    const int cw = static_cast<int>(curl.h.width), ch = static_cast<int>(curl.h.height);
    const int m2 = 2 * n;
    auto fract01 = [](float v) { return v - std::floor(v); };
    auto bilerpGrid = [&](const FVol& grid, float u, float v) {
        const float fx = fract01(u) * static_cast<float>(n);
        const float fy = fract01(v) * static_cast<float>(n);
        const int ix0 = static_cast<int>(std::floor(fx)), iy0 = static_cast<int>(std::floor(fy));
        const float tx = fx - static_cast<float>(ix0), ty = fy - static_cast<float>(iy0);
        const int x0 = Wrap(ix0, n), x1 = Wrap(ix0 + 1, n);
        const int y0 = Wrap(iy0, n), y1 = Wrap(iy0 + 1, n);
        const float v00 = grid[static_cast<std::size_t>(y0) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x0)];
        const float v10 = grid[static_cast<std::size_t>(y0) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x1)];
        const float v01 = grid[static_cast<std::size_t>(y1) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x0)];
        const float v11 = grid[static_cast<std::size_t>(y1) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x1)];
        return (1.0f - ty) * ((1.0f - tx) * v00 + tx * v10) + ty * ((1.0f - tx) * v01 + tx * v11);
    };
    auto sampleCurlRG = [&](float u, float v, float& cr, float& cg) {
        const float fx = fract01(u) * static_cast<float>(cw) - 0.5f;
        const float fy = fract01(v) * static_cast<float>(ch) - 0.5f;
        const int ix0 = static_cast<int>(std::floor(fx)), iy0 = static_cast<int>(std::floor(fy));
        const float tx = fx - static_cast<float>(ix0), ty = fy - static_cast<float>(iy0);
        const int x0 = Wrap(ix0, cw), x1 = Wrap(ix0 + 1, cw);
        const int y0 = Wrap(iy0, ch), y1 = Wrap(iy0 + 1, ch);
        auto fetch = [&](int x, int y, int c) {
            return static_cast<float>(curl.data[(static_cast<std::size_t>(y) * cw + x) * 4 + c]) * (1.0f / 255.0f);
        };
        cr = (1.0f - ty) * ((1.0f - tx) * fetch(x0, y0, 0) + tx * fetch(x1, y0, 0)) +
             ty * ((1.0f - tx) * fetch(x0, y1, 0) + tx * fetch(x1, y1, 0));
        cg = (1.0f - ty) * ((1.0f - tx) * fetch(x0, y0, 1) + tx * fetch(x1, y0, 1)) +
             ty * ((1.0f - tx) * fetch(x0, y1, 1) + tx * fetch(x1, y1, 1));
    };
    auto shaderTexel = [&](float px, float pz) -> std::pair<int, int> {
        float wx = px + kHalfShearM, wz = pz + kHalfShearM;
        float cr = 0.5f, cg = 0.5f;
        sampleCurlRG(wx / kWeatherWarpTileM, wz / kWeatherWarpTileM, cr, cg);
        wx += (cr * 2.0f - 1.0f) * kWeatherWarpM;
        wz += (cg * 2.0f - 1.0f) * kWeatherWarpM;
        const float wu = fract01(wx / kWeatherScaleM);
        const float wv = fract01(wz / kWeatherScaleM);
        const int ix = Wrap(static_cast<int>(std::lround(wu * static_cast<float>(n) - 0.5f)), n);
        const int iy = Wrap(static_cast<int>(std::lround(wv * static_cast<float>(n) - 0.5f)), n);
        return {ix, iy};
    };

    // Ground shadow windows evaluate max weather R across 4 cloud-bearing altitudes along the sun slant ray.
    struct Tap4 { std::uint32_t o[4]; };
    struct SampleSet {
        std::vector<Tap4> r2Off;
        std::vector<Tap4> r1Idx;
        double invCount = 1.0;
    };
    auto makeCloudSet = [&](float px0, float px1, float pz0, float pz1, int nx, int nz) {
        SampleSet s;
        s.r2Off.reserve(static_cast<std::size_t>(nx) * nz);
        s.r1Idx.reserve(static_cast<std::size_t>(nx) * nz);
        for (int j = 0; j < nz; ++j) {
            const float pz = pz0 + (pz1 - pz0) * (static_cast<float>(j) / static_cast<float>(std::max(nz - 1, 1)));
            for (int i = 0; i < nx; ++i) {
                const float px = px0 + (px1 - px0) * (static_cast<float>(i) / static_cast<float>(std::max(nx - 1, 1)));
                const auto [ix, iy] = shaderTexel(px, pz);
                const auto o2 = static_cast<std::uint32_t>(iy * m2 + ix);
                const auto o1 = static_cast<std::uint32_t>(iy * n + ix);
                s.r2Off.push_back({{o2, o2, o2, o2}});
                s.r1Idx.push_back({{o1, o1, o1, o1}});
            }
        }
        s.invCount = 1.0 / static_cast<double>(s.r2Off.size());
        return s;
    };
    constexpr float kGroundY = 16.0f;
    constexpr float kRayHeights[4] = {1650.0f, 2050.0f, 2500.0f, 3000.0f};
    auto makeGroundSet = [&](float gx0, float gx1, float gz0, float gz1, int nx, int nz,
                             float sunX, float sunY, float sunZ) {
        SampleSet s;
        s.r2Off.reserve(static_cast<std::size_t>(nx) * nz);
        s.r1Idx.reserve(static_cast<std::size_t>(nx) * nz);
        float along[4];
        for (int k = 0; k < 4; ++k) along[k] = (kRayHeights[k] - kGroundY) / sunY;
        for (int j = 0; j < nz; ++j) {
            const float gz = gz0 + (gz1 - gz0) * (static_cast<float>(j) / static_cast<float>(std::max(nz - 1, 1)));
            for (int i = 0; i < nx; ++i) {
                const float gx = gx0 + (gx1 - gx0) * (static_cast<float>(i) / static_cast<float>(std::max(nx - 1, 1)));
                Tap4 t2{}, t1{};
                for (int k = 0; k < 4; ++k) {
                    const auto [ix, iy] = shaderTexel(gx + sunX * along[k], gz + sunZ * along[k]);
                    t2.o[k] = static_cast<std::uint32_t>(iy * m2 + ix);
                    t1.o[k] = static_cast<std::uint32_t>(iy * n + ix);
                }
                s.r2Off.push_back(t2);
                s.r1Idx.push_back(t1);
            }
        }
        s.invCount = 1.0 / static_cast<double>(s.r2Off.size());
        return s;
    };

    constexpr float kSun2X = -0.5265f, kSun2Y = 0.573576f, kSun2Z = -0.6275f;
    constexpr float kSun3X = 0.866025f, kSun3Y = 0.500000f, kSun3Z = 0.0f;
    const SampleSet setFgMeadow = makeGroundSet(-2020.0f, -1780.0f, -940.0f, -660.0f, 12, 12, kSun2X, kSun2Y, kSun2Z);
    const SampleSet setLeftHill = makeGroundSet(-1950.0f, -1500.0f, -660.0f, -380.0f, 16, 12, kSun2X, kSun2Y, kSun2Z);
    SampleSet setFgUnion = setFgMeadow;
    setFgUnion.r2Off.insert(setFgUnion.r2Off.end(), setLeftHill.r2Off.begin(), setLeftHill.r2Off.end());
    setFgUnion.r1Idx.insert(setFgUnion.r1Idx.end(), setLeftHill.r1Idx.begin(), setLeftHill.r1Idx.end());
    setFgUnion.invCount = 1.0 / static_cast<double>(setFgUnion.r2Off.size());
    const double fgCnt = static_cast<double>(setFgMeadow.r2Off.size());
    const double hillCnt = static_cast<double>(setLeftHill.r2Off.size());
    const SampleSet setMgLake = makeGroundSet(-1660.0f, -1300.0f, -920.0f, -640.0f, 13, 10, kSun2X, kSun2Y, kSun2Z);
    const SampleSet setBeach = makeGroundSet(-1220.0f, -920.0f, -920.0f, -640.0f, 12, 10, kSun2X, kSun2Y, kSun2Z);
    const SampleSet setCoast = makeGroundSet(-850.0f, 2500.0f, -1800.0f, 400.0f, 24, 16, kSun2X, kSun2Y, kSun2Z);
    const SampleSet setNearEast = makeCloudSet(-1400.0f, 1600.0f, -1800.0f, 200.0f, 18, 14);
    const SampleSet setMidEast = makeCloudSet(2800.0f, 11000.0f, -3500.0f, 2500.0f, 28, 20);
    const SampleSet setV03 = makeGroundSet(-2600.0f, -1400.0f, -1050.0f, -550.0f, 18, 12, kSun3X, kSun3Y, kSun3Z);

    auto rayMaxR = [](const float* base, const Tap4& t) -> float {
        return std::max(std::max(base[t.o[0]], base[t.o[1]]), std::max(base[t.o[2]], base[t.o[3]]));
    };
    FVol r2(static_cast<std::size_t>(m2) * static_cast<std::size_t>(m2), 0.0f);
    struct WinStats { double mean, mn, mx, clr08, clr12, shd50, shd55, core70; };
    auto evalSet = [&](int sx, int sy, const SampleSet& s) -> WinStats {
        const float* base = &r2[static_cast<std::size_t>(sy) * static_cast<std::size_t>(m2) + static_cast<std::size_t>(sx)];
        double sum = 0.0;
        float mn = 1.0f, mx = 0.0f;
        int cClr08 = 0, cClr12 = 0, cShd50 = 0, cShd55 = 0, cCore70 = 0;
        for (const Tap4& t : s.r2Off) {
            const float v = rayMaxR(base, t);
            sum += static_cast<double>(v);
            mn = std::min(mn, v);
            mx = std::max(mx, v);
            if (v <= 0.08f) ++cClr08;
            if (v <= 0.12f) ++cClr12;
            if (v >= 0.50f) ++cShd50;
            if (v >= 0.55f) ++cShd55;
            if (v >= 0.70f) ++cCore70;
        }
        return {sum * s.invCount, static_cast<double>(mn), static_cast<double>(mx),
                static_cast<double>(cClr08) * s.invCount,
                static_cast<double>(cClr12) * s.invCount,
                static_cast<double>(cShd50) * s.invCount,
                static_cast<double>(cShd55) * s.invCount,
                static_cast<double>(cCore70) * s.invCount};
    };

    auto sq = [](double d) { return d * d; };
    auto hingeLo = [](double v, double lo) { return std::max(0.0, lo - v); };
    auto hingeHi = [](double v, double hi) { return std::max(0.0, v - hi); };
    auto scoreShift = [&](int sx, int sy, double baseLoss, double cutoff) -> double {
        const WinStats fm = evalSet(sx, sy, setFgMeadow);
        double loss = baseLoss + 350.0 * sq(hingeHi(fm.mean, 0.055)) + 130.0 * sq(hingeLo(fm.clr08, 0.86));
        if (loss >= cutoff) return loss;
        const WinStats fh = evalSet(sx, sy, setLeftHill);
        const double fguMean = (fm.mean * fgCnt + fh.mean * hillCnt) * setFgUnion.invCount;
        const double fguClr08 = (fm.clr08 * fgCnt + fh.clr08 * hillCnt) * setFgUnion.invCount;
        loss += 350.0 * (sq(hingeHi(fh.mean, 0.055)) + sq(hingeHi(fguMean, 0.055))) +
                130.0 * (sq(hingeLo(fh.clr08, 0.86)) + sq(hingeLo(fguClr08, 0.88)));
        if (loss >= cutoff) return loss;
        const WinStats mg = evalSet(sx, sy, setMgLake);
        const WinStats bc = evalSet(sx, sy, setBeach);
        loss += 70.0 * sq(hingeLo(mg.mean, 0.42)) + 70.0 * sq(hingeLo(mg.shd55, 0.40)) +
                70.0 * sq(hingeLo(mg.mx, 0.72)) + 90.0 * sq(hingeHi(bc.mean, 0.22));
        if (loss >= cutoff) return loss;
        const WinStats ne = evalSet(sx, sy, setNearEast);
        const WinStats me = evalSet(sx, sy, setMidEast);
        loss += 55.0 * (sq(hingeLo(ne.mean, 0.20)) + sq(hingeHi(ne.mean, 0.42))) +
                45.0 * sq(hingeLo(ne.clr12, 0.28)) + 55.0 * sq(hingeLo(me.core70, 0.14));
        if (loss >= cutoff) return loss;
        const WinStats co = evalSet(sx, sy, setCoast);
        const WinStats v3 = evalSet(sx, sy, setV03);
        loss += 50.0 * (sq(hingeLo(co.mean, 0.24)) + sq(hingeHi(co.mean, 0.56))) +
                55.0 * (sq(hingeLo(co.clr12, 0.28)) + sq(hingeLo(co.shd50, 0.28))) +
                50.0 * (sq(hingeLo(v3.mean, 0.24)) + sq(hingeHi(v3.mean, 0.56))) +
                45.0 * sq(hingeLo(v3.mx, 0.65)) + 45.0 * sq(hingeHi(v3.mn, 0.12));
        return loss + 0.02 * fguMean + 0.004 * bc.mean +
               0.002 * (sq(0.54 - mg.mean) + sq(0.38 - co.mean) + sq(0.32 - ne.mean));
    };

    auto sym2D = [&](const FVol& src, int sym) {
        if (sym == 0) return src;
        FVol dst(src.size());
        for (int y = 0; y < n; ++y) {
            const int ty = (sym & 2) ? (n - 1 - y) : y;
            for (int x = 0; x < n; ++x) {
                const int tx = (sym & 1) ? (n - 1 - x) : x;
                const int sx0 = (sym & 4) ? ty : tx;
                const int sy0 = (sym & 4) ? tx : ty;
                dst[static_cast<std::size_t>(y) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x)] =
                    src[static_cast<std::size_t>(sy0) * static_cast<std::size_t>(n) + static_cast<std::size_t>(sx0)];
            }
        }
        return dst;
    };

    // Multi-scale periodic Voronoi clusters: ~15% hero congestus, ~45% medium cumulus, ~40% fair-weather humilis.
    constexpr int kMacroCells = 8;
    constexpr int kDomeCells = 12;
    constexpr int kLobeCells = 20;
    constexpr int kPuffCells = 25;
    constexpr int kMacroTot = kMacroCells * kMacroCells;
    constexpr int kPuffTot = kPuffCells * kPuffCells;
    constexpr int kHeroCount = 7;
    constexpr int kMedCount = 25;

    FVol bestRUnshifted(total, 0.0f), bestBUnshifted(total, 0.0f);
    FVol shapeField(total, 0.0f), vigorField(total, 0.0f), bPeakField(total, 0.0f), bDomeField(total, 0.0f), rRaw(total, 0.0f);
    std::vector<char> isHumField(total, 0);
    double bestLoss = 1e30;
    int bestSym = 0, bestSx = 0, bestSy = 0;
    const int gridSteps = std::min(128, n);
    const int polishR = std::max(1, static_cast<int>(std::lround(static_cast<double>(size) * 4.0 / 512.0)));
    constexpr int kSeedTrials = 12;

    for (int trial = 0; trial < kSeedTrials; ++trial) {
        const std::uint32_t sm = 6375u + 73u * static_cast<std::uint32_t>(trial);
        const std::uint32_t sp = 583u + 27u * static_cast<std::uint32_t>(trial);
        const FVol street = pf(6, 2, sm + 1u);
        const FVol w12 = WorleyInv2D(n, kDomeCells, sp + 41u, &warpFX, &warpFY, 0.018f);
        const FVol w20 = WorleyInv2D(n, kLobeCells, sp + 51u, &warpFX, &warpFY, 0.018f);

        int cellTypes[kMacroTot];
        for (int k = 0; k < kMacroTot; ++k) cellTypes[k] = -1;
        std::vector<int> order(kMacroTot);
        for (int k = 0; k < kMacroTot; ++k) order[k] = k;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const float ua = U01(Hash3(a % kMacroCells, a / kMacroCells, 11, sm));
            const float ub = U01(Hash3(b % kMacroCells, b / kMacroCells, 11, sm));
            return (ua != ub) ? (ua < ub) : (a < b);
        });
        int pickedHero = 0;
        for (int k : order) {
            if (pickedHero >= kHeroCount) break;
            const int cx = k % kMacroCells, cy = k / kMacroCells;
            bool conflict = false;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (cellTypes[Wrap(cy + dy, kMacroCells) * kMacroCells + Wrap(cx + dx, kMacroCells)] == 2)
                        conflict = true;
                }
            if (!conflict) {
                cellTypes[k] = 2;
                ++pickedHero;
            }
        }
        for (int k : order) {
            if (pickedHero >= kHeroCount) break;
            if (cellTypes[k] < 0) {
                cellTypes[k] = 2;
                ++pickedHero;
            }
        }
        std::vector<int> rem;
        rem.reserve(kMacroTot);
        for (int k = 0; k < kMacroTot; ++k)
            if (cellTypes[k] < 0) rem.push_back(k);
        std::sort(rem.begin(), rem.end(), [&](int a, int b) {
            const float sa = U01(Hash3(a % kMacroCells, a / kMacroCells, 23, sm)) +
                             0.32f * static_cast<float>(((a % kMacroCells) + (a / kMacroCells)) & 1);
            const float sb = U01(Hash3(b % kMacroCells, b / kMacroCells, 23, sm)) +
                             0.32f * static_cast<float>(((b % kMacroCells) + (b / kMacroCells)) & 1);
            return (sa != sb) ? (sa < sb) : (a < b);
        });
        for (std::size_t idxR = 0; idxR < rem.size(); ++idxR)
            cellTypes[rem[idxR]] = (static_cast<int>(idxR) < kMedCount) ? 1 : 0;

        float cellPeakB[kMacroTot], cellMedRad[kMacroTot];
        for (int k = 0; k < kMacroTot; ++k) {
            const int cx = k % kMacroCells, cy = k / kMacroCells;
            const float ub = U01(Hash3(cx, cy, 37, sm));
            cellMedRad[k] = 0.45f + 0.08f * U01(Hash3(cx, cy, 53, sm));
            if (cellTypes[k] == 2) cellPeakB[k] = 0.86f + 0.13f * ub;
            else if (cellTypes[k] == 1) cellPeakB[k] = 0.44f + 0.20f * ub;
            else cellPeakB[k] = 0.09f + 0.15f * ub;
        }

        std::vector<char> puffOk(kPuffTot, 0);
        std::vector<float> puffRad(kPuffTot, 0.0f), puffPeakB(kPuffTot, 0.0f);
        for (int scy = 0; scy < kPuffCells; ++scy) {
            for (int scx = 0; scx < kPuffCells; ++scx) {
                const std::uint32_t h = Hash3(scx, scy, 0, sp + 61u);
                const float jx = 0.30f + 0.40f * U01(h);
                const float jy = 0.30f + 0.40f * U01(Fmix(h ^ 0xA511E9B3u));
                const float ux = (static_cast<float>(scx) + jx) / static_cast<float>(kPuffCells);
                const float uy = (static_cast<float>(scy) + jy) / static_cast<float>(kPuffCells);
                const float wx0 = ux - 0.018f * (bilerpGrid(warpFX, ux, uy) - 0.5f);
                const float wy0 = uy - 0.018f * (bilerpGrid(warpFY, ux, uy) - 0.5f);
                const float mpx = wx0 + 0.028f * (bilerpGrid(warpX, wx0, wy0) - 0.5f);
                const float mpy = wy0 + 0.028f * (bilerpGrid(warpY, wx0, wy0) - 0.5f);
                const VoronoiCell2D mc = VoronoiSmoothWall2D(mpx, mpy, kMacroCells, sm + 21u, 0.54f, 0.07f);
                const int mcid = mc.cy * kMacroCells + mc.cx;
                const int mct = cellTypes[mcid];
                const std::uint32_t sh = Hash3(scx, scy, 71, sp);
                const float su = U01(sh);
                const float su2 = U01(Fmix(sh ^ 0x9E3779B9u));
                const bool ok = (mct == 0 && mc.wall >= 0.065f) ||
                                (mct == 1 && mc.d1 >= cellMedRad[mcid] + 0.11f && mc.wall >= 0.065f);
                const int pidx = scy * kPuffCells + scx;
                puffOk[pidx] = ok ? 1 : 0;
                puffRad[pidx] = 0.38f + 0.07f * su2;
                const float basePb = (mct == 0) ? cellPeakB[mcid] : (0.09f + 0.15f * su);
                puffPeakB[pidx] = std::clamp(basePb + 0.05f * (su2 - 0.5f), 0.06f, 0.24f);
            }
        }

        ParallelFor(n, [&](int y) {
            const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(n);
            const float baseY = static_cast<float>(y) * invN;
            for (int x = 0; x < n; ++x) {
                const std::size_t i = row + static_cast<std::size_t>(x);
                const float baseX = static_cast<float>(x) * invN;
                const float mpx = baseX + 0.028f * (warpX[i] - 0.5f);
                const float mpy = baseY + 0.028f * (warpY[i] - 0.5f);
                const VoronoiCell2D mc = VoronoiSmoothWall2D(mpx, mpy, kMacroCells, sm + 21u, 0.54f, 0.07f);
                const int cid = mc.cy * kMacroCells + mc.cx;
                const int ctype = cellTypes[cid];
                const float pb = cellPeakB[cid];
                const float macroMoat = Smoothstep(0.036f, 0.092f, mc.wall);

                const float scallop = 0.56f * std::pow(w12[i], 1.15f) + 0.44f * std::pow(w20[i], 1.25f);
                const float st = 0.90f + 0.10f * street[i];

                float shVal = 0.0f, vigVal = 0.80f, pbVal = 0.0f, dmVal = 0.0f;
                char humFlag = 0;
                if (ctype == 2) {
                    const float rad = 1.0f - std::min(mc.d1 / 0.78f, 1.0f);
                    shVal = smax(rad * 0.95f, 0.56f * rad + 0.64f * scallop, 0.08f) * macroMoat * st;
                    vigVal = 0.88f + 0.10f * Smoothstep(0.25f, 0.75f, rad);
                    pbVal = pb * Smoothstep(0.015f, 0.075f, mc.wall);
                    dmVal = 0.86f + 0.14f * Smoothstep(0.0f, 0.58f, rad * 0.72f + 0.28f * w12[i]);
                } else if (ctype == 1) {
                    const float radLim = cellMedRad[cid];
                    const float rad = 1.0f - std::min(mc.d1 / radLim, 1.0f);
                    const float medMoat = Smoothstep(0.044f, 0.105f, mc.wall);
                    shVal = smax(rad * 0.88f, 0.54f * rad + 0.62f * scallop, 0.07f) * medMoat * st *
                            Smoothstep(0.0f, 0.14f, rad + 0.16f * w12[i]);
                    vigVal = 0.80f + 0.14f * Smoothstep(0.25f, 0.75f, rad);
                    pbVal = pb * Smoothstep(0.015f, 0.075f, mc.wall);
                    dmVal = 0.82f + 0.18f * Smoothstep(0.0f, 0.58f, rad * 0.68f + 0.32f * w12[i]);
                }

                const float fpx = baseX + 0.018f * (warpFX[i] - 0.5f);
                const float fpy = baseY + 0.018f * (warpFY[i] - 0.5f);
                const VoronoiCell2D sc = VoronoiSmoothWall2D(fpx, fpy, kPuffCells, sp + 61u, 0.40f, 0.06f);
                const int pidx = sc.cy * kPuffCells + sc.cx;
                if (puffOk[pidx] && ctype != 2) {
                    const float sRadLim = puffRad[pidx];
                    const float sMoat = Smoothstep(0.078f, 0.145f, sc.wall) * macroMoat;
                    const float medSep = (ctype == 1) ? Smoothstep(cellMedRad[cid] + 0.04f, cellMedRad[cid] + 0.10f, mc.d1) : 1.0f;
                    const float sRad = 1.0f - std::min((sc.d1 + 0.02f * (1.0f - w20[i])) / sRadLim, 1.0f);
                    const float shH = medSep * sMoat * sRad;
                    if (shH > shVal) {
                        shVal = shH;
                        vigVal = 0.85f + 0.10f * Smoothstep(0.20f, 0.70f, sRad);
                        pbVal = puffPeakB[pidx];
                        dmVal = 0.72f + 0.28f * Smoothstep(0.0f, 0.75f, sRad);
                        humFlag = 1;
                    }
                }
                if (!humFlag && shVal > 0.0f) shVal = std::min(1.0f, shVal + 0.045f);

                shapeField[i] = shVal;
                vigorField[i] = vigVal;
                bPeakField[i] = pbVal;
                bDomeField[i] = dmVal;
                isHumField[i] = humFlag;
            }
        });

        float edge = 0.045f, gamma = 1.45f, vigScale = 0.96f;
        double fClear = 0.0, fShoulder = 0.0, fCore = 0.0, meanR = 0.0;
        FVol rCandidate;
        for (int cal = 0; cal < 7; ++cal) {
            const float invSpan = 1.0f / std::max(0.88f - edge, 0.15f);
            ParallelFor(n, [&](int y) {
                const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(n);
                for (int x = 0; x < n; ++x) {
                    const std::size_t i = row + static_cast<std::size_t>(x);
                    const float sh = shapeField[i];
                    if (sh <= edge) {
                        rRaw[i] = 0.0f;
                    } else {
                        const float t = std::min(1.0f, (sh - edge) * invSpan);
                        if (isHumField[i]) {
                            rRaw[i] = std::min(1.0f, Smoothstep(0.0f, 0.38f, t) * vigorField[i]);
                        } else {
                            rRaw[i] = std::min(1.0f, std::pow(Smoothstep(0.0f, 0.84f, t), gamma) * vigorField[i] * vigScale);
                        }
                    }
                }
            });
            rCandidate = BlurPeriodic2D(rRaw, n, coneBlurR, coneBlurSigma);
            const FVol speck = BlurPeriodic2D(rCandidate, n, speckBlurR, speckBlurSigma);
            for (std::size_t i = 0; i < total; ++i) rCandidate[i] *= Smoothstep(0.055f, 0.135f, speck[i]);

            std::size_t cntClear = 0, cntShoulder = 0, cntCore = 0;
            double sumR = 0.0;
            for (float rv : rCandidate) {
                sumR += static_cast<double>(rv);
                if (rv <= 0.04f) ++cntClear;
                if (rv > 0.30f) ++cntShoulder;
                if (rv >= 0.70f) ++cntCore;
            }
            meanR = sumR * invTotal;
            fClear = static_cast<double>(cntClear) * invTotal;
            fShoulder = static_cast<double>(cntShoulder) * invTotal;
            fCore = static_cast<double>(cntCore) * invTotal;
            if (cal + 1 < 7) {
                edge = std::clamp(edge + static_cast<float>((0.405 - fClear) * 0.28), 0.015f, 0.25f);
                gamma = std::clamp(gamma + static_cast<float>((meanR - 0.295) * 1.8), 0.65f, 2.20f);
                vigScale = std::clamp(vigScale - static_cast<float>((meanR - 0.295) * 0.6), 0.82f, 1.06f);
            }
        }

        FVol bCandidate(total, 0.0f);
        for (std::size_t i = 0; i < total; ++i) {
            bCandidate[i] = std::clamp(bPeakField[i] * bDomeField[i] * Smoothstep(0.02f, 0.16f, rCandidate[i]), 0.0f, 1.0f);
        }

        const double histLoss = 25.0 * (sq(hingeLo(fClear, 0.355)) + sq(hingeHi(fClear, 0.445)) +
                                        sq(hingeLo(meanR, 0.270)) + sq(hingeHi(meanR, 0.320)) +
                                        sq(hingeLo(fShoulder, 0.350)) + sq(hingeHi(fShoulder, 0.450)) +
                                        sq(hingeLo(fCore, 0.140)) + sq(hingeHi(fCore, 0.240)));
        double bestTrialSymLoss = bestLoss;
        int bestTrialSym = 0, bestTrialSx = 0, bestTrialSy = 0;
        for (int sym = 0; sym < 8; ++sym) {
            const FVol rSym = sym2D(rCandidate, sym);
            for (int yy = 0; yy < n; ++yy) {
                const float* src = &rSym[static_cast<std::size_t>(yy) * static_cast<std::size_t>(n)];
                float* dst0 = &r2[static_cast<std::size_t>(yy) * static_cast<std::size_t>(m2)];
                float* dst1 = &r2[static_cast<std::size_t>(yy + n) * static_cast<std::size_t>(m2)];
                std::memcpy(dst0, src, static_cast<std::size_t>(n) * sizeof(float));
                std::memcpy(dst0 + n, src, static_cast<std::size_t>(n) * sizeof(float));
                std::memcpy(dst1, dst0, static_cast<std::size_t>(m2) * sizeof(float));
            }
            struct Cand { double loss; int sx, sy; };
            constexpr int kTopCoarse = 10;
            Cand topCands[kTopCoarse];
            for (Cand& c : topCands) c = {1e30, 0, 0};
            for (int ky = 0; ky < gridSteps; ++ky) {
                const int sy = (ky * n) / gridSteps;
                for (int kx = 0; kx < gridSteps; ++kx) {
                    const int sx = (kx * n) / gridSteps;
                    const double loss = scoreShift(sx, sy, histLoss, topCands[kTopCoarse - 1].loss);
                    if (loss < topCands[kTopCoarse - 1].loss) {
                        topCands[kTopCoarse - 1] = {loss, sx, sy};
                        for (int p = kTopCoarse - 1; p > 0 && topCands[p].loss < topCands[p - 1].loss; --p)
                            std::swap(topCands[p], topCands[p - 1]);
                    }
                }
            }
            double symLoss = topCands[0].loss;
            int symSx = topCands[0].sx, symSy = topCands[0].sy;
            for (const Cand& base : topCands) {
                if (base.loss >= 1e29) continue;
                for (int dy = -polishR; dy <= polishR; ++dy) {
                    const int sy = Wrap(base.sy + dy, n);
                    for (int dx = -polishR; dx <= polishR; ++dx) {
                        const int sx = Wrap(base.sx + dx, n);
                        const double loss = scoreShift(sx, sy, histLoss, symLoss);
                        if (loss < symLoss) {
                            symLoss = loss;
                            symSx = sx;
                            symSy = sy;
                        }
                    }
                }
            }
            if (symLoss < bestTrialSymLoss) {
                bestTrialSymLoss = symLoss;
                bestTrialSym = sym;
                bestTrialSx = symSx;
                bestTrialSy = symSy;
            }
        }
        if (bestTrialSymLoss < bestLoss) {
            bestLoss = bestTrialSymLoss;
            bestSym = bestTrialSym;
            bestSx = bestTrialSx;
            bestSy = bestTrialSy;
            bestRUnshifted = std::move(rCandidate);
            bestBUnshifted = std::move(bCandidate);
        }
    }

    auto roll2D = [&](const FVol& src, int sx, int sy) {
        FVol dst(src.size());
        for (int y = 0; y < n; ++y) {
            const std::size_t srcRow = static_cast<std::size_t>(Wrap(y + sy, n)) * static_cast<std::size_t>(n);
            const std::size_t dstRow = static_cast<std::size_t>(y) * static_cast<std::size_t>(n);
            for (int x = 0; x < n; ++x)
                dst[dstRow + static_cast<std::size_t>(x)] = src[srcRow + static_cast<std::size_t>(Wrap(x + sx, n))];
        }
        return dst;
    };
    const int finalSx = bestSx, finalSy = bestSy;
    const FVol r = roll2D(sym2D(bestRUnshifted, bestSym), finalSx, finalSy);
    const FVol bRaw = roll2D(sym2D(bestBUnshifted, bestSym), finalSx, finalSy);
    const FVol pdRolled = roll2D(sym2D(pd, bestSym), finalSx, finalSy);

    // True periodic Gaussian blur of R drives G; cluster-domed type drives B; core modulation drives A.
    const int blurRadius = std::max(1, static_cast<int>(std::lround(static_cast<double>(size) * 15.0 / 512.0)));
    const float blurSigma = 5.3f * (static_cast<float>(blurRadius) / 15.0f);
    const FVol g = BlurPeriodic2D(r, n, blurRadius, blurSigma);

    const int turretRadius = std::max(1, static_cast<int>(std::lround(static_cast<double>(size) * 3.0 / 512.0)));
    const float turretSigma = 1.4f * (static_cast<float>(turretRadius) / 3.0f);
    FVol b = BlurPeriodic2D(bRaw, n, turretRadius, turretSigma);
    FVol a(total);
    for (std::size_t i = 0; i < total; ++i) {
        b[i] = std::clamp(b[i], 0.0f, 1.0f);
        a[i] = 0.68f + 0.32f * Smoothstep(0.15f, 0.80f, 0.5f * pdRolled[i] + 0.5f * r[i]);
    }

    auto measure = [&](const SampleSet& s) -> WinStats {
        double sum = 0.0, mn = 1.0, mx = 0.0;
        int cClr08 = 0, cClr12 = 0, cShd50 = 0, cShd55 = 0, cCore70 = 0;
        for (const Tap4& t : s.r1Idx) {
            const float vf = rayMaxR(r.data(), t);
            const double v = static_cast<double>(vf);
            sum += v;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
            if (vf <= 0.08f) ++cClr08;
            if (vf <= 0.12f) ++cClr12;
            if (vf >= 0.50f) ++cShd50;
            if (vf >= 0.55f) ++cShd55;
            if (vf >= 0.70f) ++cCore70;
        }
        return {sum * s.invCount, mn, mx,
                static_cast<double>(cClr08) * s.invCount,
                static_cast<double>(cClr12) * s.invCount,
                static_cast<double>(cShd50) * s.invCount,
                static_cast<double>(cShd55) * s.invCount,
                static_cast<double>(cCore70) * s.invCount};
    };

    std::size_t totClear = 0, totShoulder = 0, totCore = 0, totCore25 = 0, totLowB = 0, totMedB = 0, totHeroB = 0;
    double sumR = 0.0, sumG = 0.0;
    float maxR = 0.0f;
    int histB[8] = {};
    for (std::size_t i = 0; i < total; ++i) {
        const float rv = r[i];
        const float bv = b[i];
        sumR += static_cast<double>(rv);
        sumG += static_cast<double>(g[i]);
        if (rv <= 0.04f) ++totClear;
        if (rv > 0.30f) ++totShoulder;
        if (rv >= 0.70f) ++totCore;
        maxR = std::max(maxR, rv);
        ++histB[std::min(7, static_cast<int>(Q(bv)) / 32)];
        if (rv > 0.25f) {
            ++totCore25;
            if (bv <= 0.28f) ++totLowB;
            else if (bv < 0.75f) ++totMedB;
            else ++totHeroB;
        }
    }
    const double invCore25 = 100.0 / static_cast<double>(std::max<std::size_t>(totCore25, 1));
    const WinStats stFg = measure(setFgMeadow), stHill = measure(setLeftHill), stFgAll = measure(setFgUnion);
    const WinStats stMg = measure(setMgLake), stBeach = measure(setBeach), stCoast = measure(setCoast);
    const WinStats stNearE = measure(setNearEast), stMidE = measure(setMidEast), stV3 = measure(setV03);
    std::printf(
        "weather stats: mean(wm.r)=%.4f r>0.30=%.1f%% r>=0.70=%.1f%% clear=%.1f%% max=%.2f mean(wm.g)=%.4f\n"
        "weather wm.b: hist8(%%)=[%.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f] | core(r>0.25) hum/med/hero=%.1f%%/%.1f%%/%.1f%%\n"
        "weather rays: fg(mean=%.3f clr08=%.2f) hill(mean=%.3f clr08=%.2f) fgAll(mean=%.3f clr08=%.2f) "
        "mg(mean=%.2f shd55=%.2f max=%.2f) beach(mean=%.3f) coast(mean=%.2f clr12=%.2f shd50=%.2f) "
        "nearE(mean=%.3f clr12=%.2f) midE(r70=%.2f) v03(mean=%.2f max=%.2f min=%.2f)\n",
        sumR * invTotal, 100.0 * static_cast<double>(totShoulder) * invTotal,
        100.0 * static_cast<double>(totCore) * invTotal, 100.0 * static_cast<double>(totClear) * invTotal,
        static_cast<double>(maxR), sumG * invTotal,
        100.0 * static_cast<double>(histB[0]) * invTotal, 100.0 * static_cast<double>(histB[1]) * invTotal,
        100.0 * static_cast<double>(histB[2]) * invTotal, 100.0 * static_cast<double>(histB[3]) * invTotal,
        100.0 * static_cast<double>(histB[4]) * invTotal, 100.0 * static_cast<double>(histB[5]) * invTotal,
        100.0 * static_cast<double>(histB[6]) * invTotal, 100.0 * static_cast<double>(histB[7]) * invTotal,
        static_cast<double>(totLowB) * invCore25, static_cast<double>(totMedB) * invCore25,
        static_cast<double>(totHeroB) * invCore25,
        stFg.mean, stFg.clr08, stHill.mean, stHill.clr08, stFgAll.mean, stFgAll.clr08,
        stMg.mean, stMg.shd55, stMg.mx, stBeach.mean, stCoast.mean, stCoast.clr12, stCoast.shd50,
        stNearE.mean, stNearE.clr12, stMidE.core70, stV3.mean, stV3.mx, stV3.mn);

    return Pack(n, n, 1, {&r, &g, &b, &a});
}

// ---------------------------------------------------------------- blue noise
// Void-and-cluster (Ulichney) on a torus; output is a rank image scaled to 0..255.
std::vector<std::uint8_t> VoidAndCluster(int n) {
    const int total = n * n, radius = 9;
    const float sigma = 1.9f;
    std::vector<float> kernel((2 * radius + 1) * (2 * radius + 1));
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            kernel[(dy + radius) * (2 * radius + 1) + dx + radius] = std::exp(-(dx * dx + dy * dy) / (2 * sigma * sigma));
    auto splat = [&](std::vector<float>& e, int idx, float sign) {
        const int cx = idx % n, cy = idx / n;
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx)
                e[Wrap(cy + dy, n) * n + Wrap(cx + dx, n)] += sign * kernel[(dy + radius) * (2 * radius + 1) + dx + radius];
    };
    auto argmaxOnes = [&](const std::vector<float>& e, const std::vector<char>& bp) {
        int best = -1;
        for (int i = 0; i < total; ++i)
            if (bp[i] && (best < 0 || e[i] > e[best])) best = i;
        return best;
    };
    auto argminZeros = [&](const std::vector<float>& e, const std::vector<char>& bp) {
        int best = -1;
        for (int i = 0; i < total; ++i)
            if (!bp[i] && (best < 0 || e[i] < e[best])) best = i;
        return best;
    };
    std::vector<char> bp(total, 0);
    std::vector<float> energy(total, 0.0f);
    const int ones = total / 10;
    std::uint32_t rng = 12345u;
    for (int i = 0; i < ones; ++i) {
        int idx;
        do { rng = Fmix(rng + 0x9E3779B9u); idx = static_cast<int>(rng % static_cast<std::uint32_t>(total)); } while (bp[idx]);
        bp[idx] = 1;
        splat(energy, idx, 1.0f);
    }
    for (int iter = 0; iter < 100000; ++iter) {
        const int c = argmaxOnes(energy, bp);
        bp[c] = 0; splat(energy, c, -1.0f);
        const int v = argminZeros(energy, bp);
        bp[v] = 1; splat(energy, v, 1.0f);
        if (v == c) break;
    }
    std::vector<int> rank(total, 0);
    {
        std::vector<char> b2 = bp;
        std::vector<float> e2 = energy;
        for (int r = ones - 1; r >= 0; --r) {
            const int c = argmaxOnes(e2, b2);
            b2[c] = 0; splat(e2, c, -1.0f);
            rank[c] = r;
        }
    }
    for (int r = ones; r < total; ++r) {
        const int v = argminZeros(energy, bp);
        bp[v] = 1; splat(energy, v, 1.0f);
        rank[v] = r;
    }
    std::vector<std::uint8_t> out(total);
    for (int i = 0; i < total; ++i) out[i] = static_cast<std::uint8_t>(std::lround(255.0 * rank[i] / (total - 1)));
    return out;
}

// ---------------------------------------------------------------- previews
bool WritePng(const fs::path& p, int w, int h, int comp, const std::vector<std::uint8_t>& px) {
    return stbi_write_png(p.string().c_str(), w, h, comp, px.data(), w * comp) != 0;
}

std::uint8_t Texel(const Volume& v, int x, int y, int z, int c) {
    const int ch = v.channels();
    return v.data[((static_cast<std::size_t>(z) * v.h.height + y) * v.h.width + x) * ch + c];
}

// Rows = channels, columns = evenly spaced z slices (nearest upscaled to at least 128 px).
void PreviewSlices(const Volume& v, const fs::path& path) {
    const int w = v.h.width, h = v.h.height, d = v.h.depth, ch = v.channels();
    const int cols = std::min(d, 8), s = std::max(1, 128 / w);
    const int W = cols * w * s, H = ch * h * s;
    std::vector<std::uint8_t> img(static_cast<std::size_t>(W) * H);
    for (int c = 0; c < ch; ++c)
        for (int k = 0; k < cols; ++k) {
            const int z = d == 1 ? 0 : k * d / cols;
            for (int y = 0; y < h * s; ++y)
                for (int x = 0; x < w * s; ++x) img[static_cast<std::size_t>(c * h * s + y) * W + k * w * s + x] = Texel(v, x / s, y / s, z, c);
        }
    WritePng(path, W, H, 1, img);
}

// Each channel's middle slice tiled 2x2, channels side by side: seams show up as lines.
void PreviewTiled(const Volume& v, const fs::path& path) {
    const int w = v.h.width, h = v.h.height, ch = v.channels();
    const int s = std::max(1, 128 / w), tw = w * s * 2, th = h * s * 2;
    const int W = tw * ch, z = v.h.depth / 2;
    std::vector<std::uint8_t> img(static_cast<std::size_t>(W) * th);
    for (int c = 0; c < ch; ++c)
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) img[static_cast<std::size_t>(y) * W + c * tw + x] = Texel(v, (x / s) % w, (y / s) % h, z, c);
    WritePng(path, W, th, 1, img);
}

// ---------------------------------------------------------------- verification
double MeanAbsDiff(const Volume& v, int axis, int shift, int c, int i) {
    const int dims[3] = {static_cast<int>(v.h.width), static_cast<int>(v.h.height), static_cast<int>(v.h.depth)};
    const int j = (i + shift) % dims[axis];
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    double sum = 0;
    long long count = 0;
    for (int q2 = 0; q2 < dims[a2]; ++q2)
        for (int q1 = 0; q1 < dims[a1]; ++q1) {
            int p[3], q[3];
            p[axis] = i; q[axis] = j;
            p[a1] = q[a1] = q1;
            p[a2] = q[a2] = q2;
            sum += std::abs(static_cast<int>(Texel(v, p[0], p[1], p[2], c)) - static_cast<int>(Texel(v, q[0], q[1], q[2], c)));
            ++count;
        }
    return sum / static_cast<double>(count);
}

// Prints stats, seam test and correlation; returns true if every seam ratio is within tolerance.
bool VerifyVolume(const fs::path& path, const fs::path& previewDir) {
    Volume v;
    if (!ReadGtvol(path, v)) { std::printf("FAIL cannot read %s\n", path.string().c_str()); return false; }
    const int w = v.h.width, h = v.h.height, d = v.h.depth, ch = v.channels();
    const std::size_t n = static_cast<std::size_t>(w) * h * d;
    std::printf("\n=== %s  %dx%dx%d  format=%u flags=%u  bytes=%zu\n", path.filename().string().c_str(), w, h, d, v.h.format, v.h.flags, v.data.size() + sizeof(GtvHeader));
    std::vector<double> mean(ch, 0), sd(ch, 0);
    for (int c = 0; c < ch; ++c) {
        int mn = 255, mx = 0, hist[8] = {};
        double s = 0, s2 = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const int t = v.data[i * ch + c];
            mn = std::min(mn, t); mx = std::max(mx, t);
            s += t; s2 += static_cast<double>(t) * t;
            ++hist[t / 32];
        }
        mean[c] = s / n; sd[c] = std::sqrt(std::max(0.0, s2 / n - mean[c] * mean[c]));
        std::printf("ch%d min %3d max %3d mean %6.2f sd %6.2f  hist(8 bins, %%):", c, mn, mx, mean[c], sd[c]);
        for (int b = 0; b < 8; ++b) std::printf(" %4.1f", 100.0 * hist[b] / n);
        std::printf("\n");
    }
    bool ok = true;
    const char* axisName[3] = {"x", "y", "z"};
    const int dims[3] = {w, h, d};
    for (int a = 0; a < 3; ++a) {
        if (dims[a] < 2) continue;
        for (int c = 0; c < ch; ++c) {
            const int len = dims[a];
            double interior = 0, imin = 1e9, imax = 0;
            for (int i = 0; i < len - 1; ++i) {
                const double m = MeanAbsDiff(v, a, 1, c, i);
                interior += m; imin = std::min(imin, m); imax = std::max(imax, m);
            }
            interior /= (len - 1);
            const double wrap = MeanAbsDiff(v, a, 1, c, len - 1);
double farSum = 0;
            int farCount = 0;
            for (int i = 0; i < len; i += std::max(1, len / 8), ++farCount) farSum += MeanAbsDiff(v, a, len / 4, c, i);
            const double far_ = farSum / farCount;
            const double ratio = wrap / std::max(interior, 1e-9);
            const bool pass = wrap <= imax * 1.05 + 0.05;
            ok = ok && pass;
            std::printf("seam axis %s ch%d: interior mean|d| %.3f (min %.3f max %.3f)  wrap %.3f  ratio %.3f  far-pair(%d apart) %.3f  %s\n", axisName[a], c, interior, imin, imax, wrap, ratio, len / 4, far_, pass ? "OK" : "SEAM");
        }
    }
    if (ch > 1) {
        std::printf("correlation matrix (Pearson):\n");
        for (int a = 0; a < ch; ++a) {
            for (int b = 0; b < ch; ++b) {
                double cov = 0;
                for (std::size_t i = 0; i < n; ++i) cov += (v.data[i * ch + a] - mean[a]) * (v.data[i * ch + b] - mean[b]);
                std::printf(" %6.3f", cov / n / std::max(sd[a] * sd[b], 1e-9));
            }
            std::printf("\n");
        }
    }
    fs::create_directories(previewDir);
    const std::string stem = path.stem().string();
    if (stem.find("weather") != std::string::npos && ch == 4) {
        std::size_t cntGt30 = 0, cntClear = 0, cntCore = 0, cntLowB = 0, cntMedB = 0, cntHeroB = 0;
        int histB[8] = {};
        for (std::size_t i = 0; i < n; ++i) {
            const float rf = static_cast<float>(v.data[i * 4 + 0]) * (1.0f / 255.0f);
            const int bi = v.data[i * 4 + 2];
            const float bf = static_cast<float>(bi) * (1.0f / 255.0f);
            ++histB[bi / 32];
            if (rf > 0.30f) ++cntGt30;
            if (rf <= 0.04f) ++cntClear;
            if (rf > 0.25f) {
                ++cntCore;
                if (bf <= 0.28f) ++cntLowB;
                else if (bf < 0.75f) ++cntMedB;
                else ++cntHeroB;
            }
        }
        const double invCore = 100.0 / static_cast<double>(std::max<std::size_t>(cntCore, 1));
        std::printf("weather summary: mean(wm.r)=%.4f r>0.30=%.1f%% r<=0.04=%.1f%% mean(wm.g)=%.4f | wm.b hist(%%):",
                    mean[0] / 255.0, 100.0 * static_cast<double>(cntGt30) / static_cast<double>(n),
                    100.0 * static_cast<double>(cntClear) / static_cast<double>(n), mean[1] / 255.0);
        for (int b = 0; b < 8; ++b) std::printf(" %4.1f", 100.0 * static_cast<double>(histB[b]) / static_cast<double>(n));
        std::printf(" | core(r>0.25) wm.b hum/med/hero=%.1f%%/%.1f%%/%.1f%%\n",
                    static_cast<double>(cntLowB) * invCore, static_cast<double>(cntMedB) * invCore,
                    static_cast<double>(cntHeroB) * invCore);
    }
    PreviewSlices(v, previewDir / (stem + "_slices.png"));
    PreviewTiled(v, previewDir / (stem + "_tiled2x2.png"));
    return ok;
}

// Blue-noise quality: variance of a 5x5 box blur over raw variance (white noise ~0.04, blue much lower).
void BlueNoiseMetrics(const Volume& v) {
    const int w = v.h.width, h = v.h.height;
    double s = 0, s2 = 0, b2 = 0, bs = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double t = Texel(v, x, y, 0, 0);
            s += t; s2 += t * t;
            double acc = 0;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) acc += Texel(v, Wrap(x + dx, w), Wrap(y + dy, h), 0, 0);
            acc /= 25.0;
            bs += acc; b2 += acc * acc;
        }
    const double n = static_cast<double>(w) * h;
    const double var = s2 / n - (s / n) * (s / n), bvar = b2 / n - (bs / n) * (bs / n);
    std::printf("blue noise check: blur5x5 variance / raw variance = %.4f (white noise ~0.04)\n", bvar / var);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: cloud_noise_bake bake --out DIR [--blue FILE.png] [--gen-blue] | verify --dir DIR\n"); return 2; }
    const std::string mode = argv[1];
    std::string out, bluePath;
    bool genBlue = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--out" || a == "--dir") && i + 1 < argc) out = argv[++i];
        else if (a == "--blue" && i + 1 < argc) bluePath = argv[++i];
        else if (a == "--gen-blue") genBlue = true;
    }
    if (out.empty()) { std::printf("missing --out/--dir\n"); return 2; }
    const fs::path dir(out);

    if (mode == "weather") {  // dev shortcut: only the weather map (+ preview png) into --out
        fs::create_directories(dir);
        Volume curl = BakeCurl(128);
        Volume weather = BakeWeather(512, curl);
        if (!WriteGtvol(dir / "cloud_weather_512.gtvol", weather)) { std::printf("write failed\n"); return 1; }
        WritePng(dir / "cloud_weather_512.png", 512, 512, 4, weather.data);
        std::printf("weather written to %s\n", dir.string().c_str());
        return 0;
    }

    if (mode == "bake") {
        using Clock = std::chrono::steady_clock;
        auto ms = [](Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); };
        fs::create_directories(dir);
        auto t0 = Clock::now();
        Volume shape = BakeShape(128);
        std::printf("shape 128^3 baked in %.0f ms\n", ms(t0));
        t0 = Clock::now();
        Volume detail = BakeDetail(64);
        std::printf("detail 64^3 baked in %.0f ms\n", ms(t0));
        t0 = Clock::now();
        Volume curl = BakeCurl(128);
        std::printf("curl 128^2 baked in %.0f ms\n", ms(t0));
        t0 = Clock::now();
        Volume weather = BakeWeather(512, curl);
        std::printf("weather 512^2 baked in %.0f ms\n", ms(t0));

        Volume blue;
        blue.h.width = blue.h.height = 128; blue.h.depth = 1; blue.h.format = kFormatR8; blue.h.flags = kFlagTileable;
        bool loaded = false;
        if (!genBlue && !bluePath.empty()) {
            int w, h, c;
            unsigned char* px = stbi_load(bluePath.c_str(), &w, &h, &c, 1);
            if (px && w == 128 && h == 128) {
                blue.data.assign(px, px + 128 * 128);
                loaded = true;
                std::printf("blue noise loaded from %s\n", bluePath.c_str());
            } else {
                std::printf("blue noise file unusable (%s), generating\n", px ? "wrong size" : "load failed");
            }
            if (px) stbi_image_free(px);
        }
        if (!loaded) {
            t0 = Clock::now();
            blue.data = VoidAndCluster(128);
            std::printf("blue noise generated by void-and-cluster in %.0f ms\n", ms(t0));
        }

        struct Item { const char* name; const Volume* v; };
        const Item items[] = {{"cloud_shape_128", &shape}, {"cloud_detail_64", &detail}, {"cloud_curl_128", &curl}, {"cloud_weather_512", &weather}, {"blue_noise_128", &blue}};
        for (const Item& it : items) {
            if (!WriteGtvol(dir / (std::string(it.name) + ".gtvol"), *it.v)) { std::printf("write failed %s\n", it.name); return 1; }
        }
        WritePng(dir / "cloud_weather_512.png", 512, 512, 4, weather.data);
        WritePng(dir / "blue_noise_128.png", 128, 128, 1, blue.data);
        std::printf("assets written to %s\n", dir.string().c_str());
        return 0;
    }

    if (mode == "verify") {
        bool ok = true;
        for (const char* name : {"cloud_shape_128", "cloud_detail_64", "cloud_curl_128", "cloud_weather_512", "blue_noise_128"}) {
            ok = VerifyVolume(dir / (std::string(name) + ".gtvol"), dir / "preview") && ok;
        }
        Volume blue;
        if (ReadGtvol(dir / "blue_noise_128.gtvol", blue)) BlueNoiseMetrics(blue);
        std::printf("\nOVERALL: %s\n", ok ? "ALL SEAM TESTS PASSED" : "SEAM TEST FAILED");
        return ok ? 0 : 1;
    }
    std::printf("unknown mode\n");
    return 2;
}
