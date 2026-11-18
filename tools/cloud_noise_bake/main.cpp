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

// Weather 512^2: R broken-cumulus coverage, G wide-blurred R mass (~1.27 km radius), B dome peak, A core density.
Volume BakeWeather(std::uint32_t size) {
    const int n = static_cast<int>(size);
    const std::size_t total = static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
    const float invN = 1.0f / static_cast<float>(n);
    const float zf = 0.37f;
    auto pf = [&](int base, int oct, std::uint32_t seed) {
        FVol v = Fill(n, n, 1, [=](float x, float y, float) { return PerlinFbm(x, y, zf, base, oct, seed); });
        Stretch(v);
        return v;
    };
    auto normalize = [](FVol& v) {
        double sum = 0.0;
        for (float f : v) sum += static_cast<double>(f);
        const float mean = static_cast<float>(sum / static_cast<double>(v.size()));
        double var = 0.0;
        for (float f : v) {
            const double d = static_cast<double>(f - mean);
            var += d * d;
        }
        const float invSd = 1.0f / static_cast<float>(std::sqrt(std::max(var / static_cast<double>(v.size()), 1e-12)));
        for (float& f : v) f = (f - mean) * invSd;
    };

    // Gentle 2-octave periodic warp (<= 0.05 tile) keeps cumulus footprints rounded without folding contours.
    const FVol warpX = pf(3, 2, 201u), warpY = pf(3, 2, 211u);
    auto warped = [&](int base, int oct, std::uint32_t seed, float warp) {
        FVol v(total);
        ParallelFor(n, [&](int y) {
            const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(n);
            for (int x = 0; x < n; ++x) {
                const std::size_t i = row + static_cast<std::size_t>(x);
                const float fx = static_cast<float>(x) * invN + warp * (warpX[i] - 0.5f);
                const float fy = static_cast<float>(y) * invN + warp * (warpY[i] - 0.5f);
                v[i] = PerlinFbm(fx, fy, zf, base, oct, seed);
            }
        });
        normalize(v);
        return v;
    };

    // Multi-scale cumulus: gentle macro variety (3), rounded meso & cumulus cells (7, 14, 24), and companion cells (16).
    FVol macroF = warped(3, 2, 401u, 0.045f);
    for (float& f : macroF) f = 1.35f * std::tanh(f * (1.0f / 1.35f));
    FVol mesoF = warped(7, 2, 411u, 0.040f);
    for (float& f : mesoF) f = 1.85f * std::tanh(f * (1.0f / 1.85f));
    const FVol cellF = warped(14, 2, 421u, 0.032f);
    const FVol puffF = warped(24, 2, 431u, 0.022f);
    const FVol compF = warped(16, 2, 441u, 0.030f);
    const FVol cap = pf(5, 3, 111u), pt = pf(4, 3, 131u), pd = pf(7, 3, 141u);

    FVol combined(total), compMix(total);
    for (std::size_t i = 0; i < total; ++i) {
        combined[i] = 0.14f * macroF[i] + 0.32f * mesoF[i] + 0.34f * cellF[i] + 0.20f * puffF[i];
        compMix[i] = 0.55f * compF[i] + 0.58f * cellF[i] + 0.60f * puffF[i];
    }
    normalize(combined);
    normalize(compMix);

    FVol rRaw(total);
    for (std::size_t i = 0; i < total; ++i) {
        const float z = combined[i];
        const float shoulder = Smoothstep(-0.12f, 0.85f, z);
        const float dome = Smoothstep(-0.04f, 0.54f, z);
        const float core = Smoothstep(0.55f, 1.75f, z);
        const float peakCap = 0.85f + 0.07f * cap[i];
        const float mainCloud = peakCap * (0.45f * shoulder + 0.39f * dome + 0.16f * core);
        // Fair-weather cumulus cells across all regions keep every 2.5 km patch dappled with crisp cores and sky gaps.
        const float companion = peakCap * (0.92f * Smoothstep(0.76f, 1.44f, compMix[i]));
        rRaw[i] = std::max(mainCloud, companion);
    }

    // Radius-3 periodic smoothing (~145 m) softens horizontal cloud borders into rounded cumulus gradients.
    FVol r = BlurPeriodic2D(rRaw, n, 3, 1.4f);

    // Wide periodic blur (~26 texels on 512, ~1.27 km) keeps G varying gradually without ceiling spikes.
    const int blurRadius = std::max(1, static_cast<int>(std::lround(static_cast<double>(size) * 26.0 / 512.0)));
    const float blurSigma = static_cast<float>(blurRadius) / 2.8f;
    const FVol blurredR = BlurPeriodic2D(r, n, blurRadius, blurSigma);

    FVol g(total), b(total), a(total);
    for (std::size_t i = 0; i < total; ++i) {
        g[i] = std::clamp(blurredR[i] * 1.25f, 0.0f, 1.0f);
        b[i] = Smoothstep(0.18f, 0.65f, g[i]) * (0.60f + 0.40f * Smoothstep(0.25f, 0.75f, pt[i]));
        a[i] = 0.62f + 0.38f * Smoothstep(0.15f, 0.80f, 0.5f * pd[i] + 0.5f * r[i]);
    }
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
        Volume weather = BakeWeather(512);
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
        Volume weather = BakeWeather(512);
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
