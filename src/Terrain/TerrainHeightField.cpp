#include "TerrainHeightField.h"

#include <FastNoiseLite.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

namespace gte::terrain {

namespace {

constexpr float kNyquistFraction = 0.35f; // An octave is dropped once its wavelength nears the sample spacing.

inline float Saturate(float v) noexcept { return std::clamp(v, 0.0f, 1.0f); }

inline float SmoothStep(float e0, float e1, float v) noexcept
{
    const float t = Saturate((v - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

struct TerrainHeightField::Impl {
    // Noise that depends on the LOD level (fewer octaves when coarse).
    struct LevelNoise {
        FastNoiseLite peaks;
        FastNoiseLite detail;
        float detailAmplitude = 0.0f;
    };

    FastNoiseLite warp;
    FastNoiseLite continental;
    bool useWarp = false;
    std::vector<LevelNoise> levels;
    float heightScale = 1.0f;
    float seaLevel = 0.0f;
    float shaping = 0.25f;
    float peaksWeight = 1.0f;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    double spacingLevel0 = 2.0;

    // Shape from the warped position (x, z) and the continentalness `c` there.
    float Shape(const LevelNoise& level, float x, float z, float c) const
    {
        const float xc = c - seaLevel;
        const float land = SmoothStep(-shaping, shaping, xc);
        const float inland = Saturate(xc / std::max(0.05f, 1.0f - seaLevel));
        const float oceanT = Saturate(-xc / 0.6f);
        const float ocean = -0.2f * oceanT * oceanT * (3.0f - 2.0f * oceanT);
        const float landBase = 0.04f + 0.30f * inland * inland * (3.0f - 2.0f * inland);
        float n = ocean + (landBase - ocean) * land;

        const float mountainMask = SmoothStep(0.0f, 1.0f, (xc - 0.05f) / 0.45f);
        if (mountainMask > 0.0f && peaksWeight > 0.0f) {
            const float r01 = level.peaks.GetNoise(x, z) * 0.5f + 0.5f;
            n += peaksWeight * 0.66f * mountainMask * r01 * std::sqrt(r01);
        }
        float h = n * heightScale;
        if (land > 0.0f && level.detailAmplitude > 0.0f) {
            h += level.detail.GetNoise(x, z) * level.detailAmplitude * (0.3f + 0.7f * land);
        }
        return std::clamp(h, minHeight, maxHeight);
    }

    float Sample(const LevelNoise& level, float wx, float wz) const
    {
        float x = wx;
        float z = wz;
        if (useWarp) {
            warp.DomainWarp(x, z);
        }
        return Shape(level, x, z, continental.GetNoise(x, z));
    }

    // Lattice spacing for the smooth layers (warp, continentalness): about 16 m, in whole samples.
    static int LatticeStride(double spacing) noexcept
    {
        int stride = 1;
        while (stride < 8 && spacing * static_cast<double>(stride * 2) <= 16.0) {
            stride *= 2;
        }
        return stride;
    }

    static std::int64_t FloorDiv(std::int64_t a, std::int64_t b) noexcept
    {
        std::int64_t q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) {
            --q;
        }
        return q;
    }
    const LevelNoise& LevelFor(double spacing) const noexcept
    {
        int level = 0;
        if (spacing > spacingLevel0) {
            level = static_cast<int>(std::lround(std::log2(spacing / spacingLevel0)));
        }
        level = std::clamp(level, 0, static_cast<int>(levels.size()) - 1);
        return levels[static_cast<std::size_t>(level)];
    }
};

TerrainHeightField::TerrainHeightField(const TerrainSettings& settings)
    : m_settings(settings)
    , m_layout(MakeLayout(settings))
    , m_impl(std::make_unique<Impl>())
{
    Impl& impl = *m_impl;
    const int seed = static_cast<int>(settings.seed);
    impl.heightScale = settings.heightScale;
    impl.seaLevel = settings.continentalSeaLevel;
    impl.shaping = settings.continentalShaping;
    impl.peaksWeight = settings.peaksWeight;
    impl.minHeight = MinHeight(settings);
    impl.maxHeight = MaxHeight(settings);
    impl.spacingLevel0 = m_layout.spacingLevel0;

    impl.useWarp = settings.domainWarpStrength > 0.0f;
    impl.warp.SetSeed(seed + 1013);
    impl.warp.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    impl.warp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
    impl.warp.SetFractalType(FastNoiseLite::FractalType_DomainWarpProgressive);
    impl.warp.SetFractalOctaves(2);
    impl.warp.SetFractalGain(0.5f);
    impl.warp.SetFractalLacunarity(2.0f);
    impl.warp.SetFrequency(settings.domainWarpFrequency);
    impl.warp.SetDomainWarpAmp(settings.domainWarpStrength);

    impl.continental.SetSeed(seed + 2027);
    impl.continental.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    impl.continental.SetFractalType(FastNoiseLite::FractalType_FBm);
    impl.continental.SetFractalOctaves(3);
    impl.continental.SetFractalGain(0.5f);
    impl.continental.SetFractalLacunarity(2.0f);
    impl.continental.SetFrequency(settings.baseFrequency * settings.continentalFrequencyScale);

    const float peaksFreq = settings.baseFrequency * settings.peaksFrequencyScale;
    const float detailFreq = settings.baseFrequency * settings.detailFrequencyScale;
    const int count = std::max(1, settings.lodLevelCount);
    impl.levels.resize(static_cast<std::size_t>(count));
    for (int level = 0; level < count; ++level) {
        Impl::LevelNoise& ln = impl.levels[static_cast<std::size_t>(level)];
        const float spacing = static_cast<float>(m_layout.spacingLevel0 * static_cast<double>(1u << level));

        // Keep octaves whose finest wavelength is still resolved by this level's grid.
        int peakOctaves = settings.octaves;
        const float ratio = kNyquistFraction / std::max(1.0e-9f, peaksFreq * spacing);
        if (ratio > 1.0f) {
            const int fit = 1 + static_cast<int>(std::floor(std::log(ratio) / std::log(settings.lacunarity)));
            peakOctaves = std::clamp(fit, 1, settings.octaves);
        } else {
            peakOctaves = 1;
        }
        ln.peaks.SetSeed(seed + 3041);
        ln.peaks.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ln.peaks.SetFractalType(FastNoiseLite::FractalType_Ridged);
        ln.peaks.SetFractalOctaves(peakOctaves);
        ln.peaks.SetFractalGain(settings.gain);
        ln.peaks.SetFractalLacunarity(settings.lacunarity);
        ln.peaks.SetFrequency(peaksFreq);

        const float fade = 1.0f - SmoothStep(0.08f, 0.30f, spacing * detailFreq);
        ln.detailAmplitude = settings.detailAmplitude * fade;
        if (fade < 0.002f) {
            ln.detailAmplitude = 0.0f;
        }
        ln.detail.SetSeed(seed + 4057);
        ln.detail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ln.detail.SetFractalType(FastNoiseLite::FractalType_FBm);
        ln.detail.SetFractalOctaves(settings.detailOctaves);
        ln.detail.SetFractalGain(0.5f);
        ln.detail.SetFractalLacunarity(2.0f);
        ln.detail.SetFrequency(detailFreq);
    }
}

TerrainHeightField::~TerrainHeightField() = default;

int TerrainHeightField::LevelForSpacing(double spacing) const noexcept
{
    int level = 0;
    if (spacing > m_layout.spacingLevel0) {
        level = static_cast<int>(std::lround(std::log2(spacing / m_layout.spacingLevel0)));
    }
    return std::clamp(level, 0, static_cast<int>(m_impl->levels.size()) - 1);
}

float TerrainHeightField::Height(double x, double z, double sampleSpacing) const
{
    return m_impl->Sample(m_impl->LevelFor(sampleSpacing), static_cast<float>(x), static_cast<float>(z));
}

void TerrainHeightField::FillGrid(double originX, double originZ, double spacing, int countX, int countZ, float* out) const
{
    const Impl& impl = *m_impl;
    const Impl::LevelNoise& level = impl.LevelFor(spacing);
    const double gxExact = originX / spacing;
    const double gzExact = originZ / spacing;
    const std::int64_t gx0 = std::llround(gxExact);
    const std::int64_t gz0 = std::llround(gzExact);
    const bool aligned = std::fabs(gxExact - static_cast<double>(gx0)) < 1.0e-6 && std::fabs(gzExact - static_cast<double>(gz0)) < 1.0e-6;
    const int stride = aligned ? Impl::LatticeStride(spacing) : 1;

    if (stride <= 1) {
        for (int j = 0; j < countZ; ++j) {
            const float wz = static_cast<float>(originZ + static_cast<double>(j) * spacing);
            float* row = out + static_cast<std::size_t>(j) * static_cast<std::size_t>(countX);
            for (int i = 0; i < countX; ++i) {
                row[i] = impl.Sample(level, static_cast<float>(originX + static_cast<double>(i) * spacing), wz);
            }
        }
        return;
    }

    // Warp and continentalness are smooth: evaluate them on a world-aligned lattice and interpolate.
    // The lattice is anchored to global sample indices, so neighbouring tiles agree exactly.
    struct Lattice {
        std::vector<float> dx;
        std::vector<float> dz;
        std::vector<float> c;
        std::vector<int> cell;    // Lattice column of each sample column.
        std::vector<float> frac;  // Position inside the lattice cell, 0..1.
    };
    thread_local Lattice lat;
    const std::int64_t lx0 = Impl::FloorDiv(gx0, stride);
    const std::int64_t lx1 = Impl::FloorDiv(gx0 + countX - 1, stride) + 1;
    const std::int64_t lz0 = Impl::FloorDiv(gz0, stride);
    const std::int64_t lz1 = Impl::FloorDiv(gz0 + countZ - 1, stride) + 1;
    const int lw = static_cast<int>(lx1 - lx0 + 1);
    const int lh = static_cast<int>(lz1 - lz0 + 1);
    lat.dx.resize(static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh));
    lat.dz.resize(lat.dx.size());
    lat.c.resize(lat.dx.size());
    for (int lj = 0; lj < lh; ++lj) {
        const float z = static_cast<float>(static_cast<double>((lz0 + lj) * stride) * spacing);
        for (int li = 0; li < lw; ++li) {
            const float x = static_cast<float>(static_cast<double>((lx0 + li) * stride) * spacing);
            float wx = x;
            float wz = z;
            if (impl.useWarp) {
                impl.warp.DomainWarp(wx, wz);
            }
            const std::size_t k = static_cast<std::size_t>(lj) * static_cast<std::size_t>(lw) + static_cast<std::size_t>(li);
            lat.dx[k] = wx - x;
            lat.dz[k] = wz - z;
            lat.c[k] = impl.continental.GetNoise(wx, wz);
        }
    }
    lat.cell.resize(static_cast<std::size_t>(countX));
    lat.frac.resize(static_cast<std::size_t>(countX));
    for (int i = 0; i < countX; ++i) {
        const std::int64_t g = gx0 + i;
        const std::int64_t cell = Impl::FloorDiv(g, stride);
        lat.cell[static_cast<std::size_t>(i)] = static_cast<int>(cell - lx0);
        lat.frac[static_cast<std::size_t>(i)] = static_cast<float>(g - cell * stride) / static_cast<float>(stride);
    }

    for (int j = 0; j < countZ; ++j) {
        const std::int64_t g = gz0 + j;
        const std::int64_t cellZ = Impl::FloorDiv(g, stride);
        const int rowA = static_cast<int>(cellZ - lz0) * lw;
        const float fz = static_cast<float>(g - cellZ * stride) / static_cast<float>(stride);
        const float wz = static_cast<float>(static_cast<double>(g) * spacing);
        float* row = out + static_cast<std::size_t>(j) * static_cast<std::size_t>(countX);
        for (int i = 0; i < countX; ++i) {
            const std::size_t a = static_cast<std::size_t>(rowA + lat.cell[static_cast<std::size_t>(i)]);
            const std::size_t b = a + static_cast<std::size_t>(lw);
            const float fx = lat.frac[static_cast<std::size_t>(i)];
            const float w00 = (1.0f - fx) * (1.0f - fz);
            const float w10 = fx * (1.0f - fz);
            const float w01 = (1.0f - fx) * fz;
            const float w11 = fx * fz;
            const float dx = lat.dx[a] * w00 + lat.dx[a + 1] * w10 + lat.dx[b] * w01 + lat.dx[b + 1] * w11;
            const float dz = lat.dz[a] * w00 + lat.dz[a + 1] * w10 + lat.dz[b] * w01 + lat.dz[b + 1] * w11;
            const float c = lat.c[a] * w00 + lat.c[a + 1] * w10 + lat.c[b] * w01 + lat.c[b + 1] * w11;
            const float wx = static_cast<float>(static_cast<double>(gx0 + i) * spacing);
            row[i] = impl.Shape(level, wx + dx, wz + dz, c);
        }
    }
}

float TerrainHeightField::Slope(double x, double z, double spacing) const
{
    const double hx = Height(x + spacing, z, spacing) - Height(x - spacing, z, spacing);
    const double hz = Height(x, z + spacing, spacing) - Height(x, z - spacing, spacing);
    const double grad = std::sqrt(hx * hx + hz * hz) / (2.0 * spacing);
    return static_cast<float>(std::atan(grad));
}

} // namespace gte::terrain
