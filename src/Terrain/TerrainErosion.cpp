#include "TerrainErosion.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gte::terrain {

namespace {

// SplitMix64: tiny, fast and well distributed.
struct Rng {
    std::uint64_t state;
    std::uint64_t Next() noexcept
    {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Uniform in [0, 1).
    float NextFloat() noexcept { return static_cast<float>(Next() >> 40) * (1.0f / 16777216.0f); }
};

struct BrushCell {
    int dx;
    int dz;
    float weight;
};

std::vector<BrushCell> MakeBrush(int radius)
{
    std::vector<BrushCell> cells;
    float sum = 0.0f;
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const float d = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            if (d < static_cast<float>(radius)) {
                const float w = 1.0f - d / static_cast<float>(radius);
                cells.push_back({ dx, dz, w });
                sum += w;
            }
        }
    }
    for (BrushCell& c : cells) {
        c.weight /= sum;
    }
    return cells;
}

} // namespace

ErosionParams MakeErosionParams(const TerrainSettings& s) noexcept
{
    ErosionParams p;
    p.inertia = s.erosionInertia;
    p.sedimentCapacity = s.sedimentCapacity;
    p.erosionRate = s.erosionRate;
    p.depositionRate = s.depositionRate;
    p.evaporation = s.erosionEvaporation;
    p.gravity = s.erosionGravity;
    p.maxLifetime = s.erosionMaxLifetime;
    p.brushRadius = s.erosionBrushRadius;
    return p;
}

std::uint64_t ErosionSeed(std::uint32_t worldSeed, std::int32_t x, std::int32_t z, std::int32_t level) noexcept
{
    Rng rng{ (static_cast<std::uint64_t>(worldSeed) << 32) ^ 0xA5A5F00DCAFEull };
    rng.state ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) * 0x9E3779B97F4A7C15ull;
    rng.Next();
    rng.state ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) * 0xC2B2AE3D27D4EB4Full;
    rng.Next();
    rng.state ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(level)) * 0x165667B19E3779F9ull;
    return rng.Next();
}

void ErodeTile(float* map, int side, const ErosionParams& p, int dropletCount, std::uint64_t seed, float minStartHeight)
{
    if (map == nullptr || side < 8 || dropletCount <= 0) {
        return;
    }
    const int radius = std::max(1, p.brushRadius);
    const std::vector<BrushCell> brush = MakeBrush(radius);
    std::vector<int> brushOffsets;
    std::vector<float> brushWeights;
    for (const BrushCell& c : brush) {
        brushOffsets.push_back(c.dz * side + c.dx);
        brushWeights.push_back(c.weight);
    }
    Rng rng{ seed };
    const float maxPos = static_cast<float>(side - 1) - 0.001f;

    for (int drop = 0; drop < dropletCount; ++drop) {
        float posX = rng.NextFloat() * maxPos;
        float posZ = rng.NextFloat() * maxPos;
        {
            const int sx = static_cast<int>(posX);
            const int sz = static_cast<int>(posZ);
            if (map[sz * side + sx] < minStartHeight) {
                continue;
            }
        }
        float dirX = 0.0f;
        float dirZ = 0.0f;
        float speed = 1.0f;
        float water = 1.0f;
        float sediment = 0.0f;

        for (int step = 0; step < p.maxLifetime; ++step) {
            const int nodeX = static_cast<int>(posX);
            const int nodeZ = static_cast<int>(posZ);
            const float ox = posX - static_cast<float>(nodeX);
            const float oz = posZ - static_cast<float>(nodeZ);
            const int idx = nodeZ * side + nodeX;

            const float h00 = map[idx];
            const float h10 = map[idx + 1];
            const float h01 = map[idx + side];
            const float h11 = map[idx + side + 1];
            const float gradX = (h10 - h00) * (1.0f - oz) + (h11 - h01) * oz;
            const float gradZ = (h01 - h00) * (1.0f - ox) + (h11 - h10) * ox;
            const float height = h00 * (1.0f - ox) * (1.0f - oz) + h10 * ox * (1.0f - oz)
                + h01 * (1.0f - ox) * oz + h11 * ox * oz;

            dirX = dirX * p.inertia - gradX * (1.0f - p.inertia);
            dirZ = dirZ * p.inertia - gradZ * (1.0f - p.inertia);
            const float len = std::sqrt(dirX * dirX + dirZ * dirZ);
            if (len < 1.0e-9f) {
                break;
            }
            dirX /= len;
            dirZ /= len;
            posX += dirX;
            posZ += dirZ;
            if (posX < 0.0f || posX >= static_cast<float>(side - 1) || posZ < 0.0f || posZ >= static_cast<float>(side - 1)) {
                break;
            }

            const int nx = static_cast<int>(posX);
            const int nz = static_cast<int>(posZ);
            const float nox = posX - static_cast<float>(nx);
            const float noz = posZ - static_cast<float>(nz);
            const int nidx = nz * side + nx;
            const float newHeight = map[nidx] * (1.0f - nox) * (1.0f - noz) + map[nidx + 1] * nox * (1.0f - noz)
                + map[nidx + side] * (1.0f - nox) * noz + map[nidx + side + 1] * nox * noz;
            const float deltaH = newHeight - height;

            const float capacity = std::max(-deltaH, p.minSlope) * speed * water * p.sedimentCapacity;
            if (sediment > capacity || deltaH > 0.0f) {
                const float deposit = deltaH > 0.0f ? std::min(deltaH, sediment) : (sediment - capacity) * p.depositionRate;
                sediment -= deposit;
                map[idx] += deposit * (1.0f - ox) * (1.0f - oz);
                map[idx + 1] += deposit * ox * (1.0f - oz);
                map[idx + side] += deposit * (1.0f - ox) * oz;
                map[idx + side + 1] += deposit * ox * oz;
            } else {
                const float erode = std::min((capacity - sediment) * p.erosionRate, -deltaH);
                if (nodeX >= radius && nodeX < side - radius && nodeZ >= radius && nodeZ < side - radius) {
                    // Interior: every brush cell is inside the tile, no bounds checks.
                    float* centre = map + idx;
                    for (std::size_t k = 0; k < brushOffsets.size(); ++k) {
                        float& h = centre[brushOffsets[k]];
                        const float take = std::min(h > 0.0f ? h : 0.0f, erode * brushWeights[k]);
                        h -= take;
                        sediment += take;
                    }
                } else {
                    for (const BrushCell& cell : brush) {
                        const int cx = nodeX + cell.dx;
                        const int cz = nodeZ + cell.dz;
                        if (cx < 0 || cx >= side || cz < 0 || cz >= side) {
                            continue;
                        }
                        float& h = map[cz * side + cx];
                        const float take = std::min(h > 0.0f ? h : 0.0f, erode * cell.weight);
                        h -= take;
                        sediment += take;
                    }
                }
            }
            speed = std::sqrt(std::max(0.0f, speed * speed + deltaH * p.gravity));
            water *= (1.0f - p.evaporation);
        }
    }
}

} // namespace gte::terrain
