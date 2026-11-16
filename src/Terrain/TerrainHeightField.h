#pragma once

// Deterministic world-space height sampler. Const after construction, so any thread may sample it.

#include "TerrainSettings.h"

#include <memory>

namespace gte::terrain {

class TerrainHeightField {
public:
    // `settings` must already be sanitized.
    explicit TerrainHeightField(const TerrainSettings& settings);
    ~TerrainHeightField();
    TerrainHeightField(const TerrainHeightField&) = delete;
    TerrainHeightField& operator=(const TerrainHeightField&) = delete;

    const TerrainSettings& Settings() const noexcept { return m_settings; }
    const TerrainLayout& Layout() const noexcept { return m_layout; }

    // Height in metres at a world position. `sampleSpacing` picks the noise detail (coarse spacing skips fine octaves).
    float Height(double x, double z, double sampleSpacing) const;

    // Fills out[j * countX + i] with the height at (originX + i * spacing, originZ + j * spacing).
    void FillGrid(double originX, double originZ, double spacing, int countX, int countZ, float* out) const;

    // Slope angle in radians (0 flat) from central differences over one `spacing`.
    float Slope(double x, double z, double spacing) const;

    // LOD level whose vertex spacing matches `spacing` (clamped to the configured levels).
    int LevelForSpacing(double spacing) const noexcept;

private:
    struct Impl;
    TerrainSettings m_settings;
    TerrainLayout m_layout;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::terrain
