#pragma once

// Droplet-based hydraulic erosion on a small float tile. Deterministic for a given seed.

#include "TerrainSettings.h"

#include <cstdint>

namespace gte::terrain {

struct ErosionParams {
    float inertia = 0.05f;
    float sedimentCapacity = 4.0f;
    float minSlope = 0.01f;
    float erosionRate = 0.3f;
    float depositionRate = 0.3f;
    float evaporation = 0.01f;
    float gravity = 4.0f;
    std::int32_t maxLifetime = 30;
    std::int32_t brushRadius = 2;
};

ErosionParams MakeErosionParams(const TerrainSettings& s) noexcept;

// Heights are divided by this many "slope units" per sample so one parameter set fits every LOD level.
inline constexpr float kErosionSlopeUnits = 4.0f;

// Seed from the world seed and a node key (any three integers).
std::uint64_t ErosionSeed(std::uint32_t worldSeed, std::int32_t x, std::int32_t z, std::int32_t level) noexcept;

// Runs `dropletCount` droplets over the square tile `heights` (side x side, row major) and edits it in place.
// Droplets that start below `minStartHeight` are skipped (their random numbers are still drawn).
void ErodeTile(float* heights, int side, const ErosionParams& params, int dropletCount, std::uint64_t seed,
    float minStartHeight);

} // namespace gte::terrain
