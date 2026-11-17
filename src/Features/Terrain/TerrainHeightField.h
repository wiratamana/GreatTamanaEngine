#pragma once

// Deterministic world-space height sampler. Const after construction, so any thread may sample it.

#include "TerrainCarve.h"
#include "TerrainSettings.h"

#include <memory>

namespace gte::terrain {

class TerrainHeightField {
public:
    // `settings` must already be sanitized. `carve` (optional) reshapes heights in Height() and in node meshes.
    explicit TerrainHeightField(const TerrainSettings& settings, std::shared_ptr<const ITerrainCarve> carve = nullptr);
    ~TerrainHeightField();
    TerrainHeightField(const TerrainHeightField&) = delete;
    TerrainHeightField& operator=(const TerrainHeightField&) = delete;

    const TerrainSettings& Settings() const noexcept { return m_settings; }
    const TerrainLayout& Layout() const noexcept { return m_layout; }
    const ITerrainCarve* Carve() const noexcept { return m_carve.get(); }
    std::shared_ptr<const ITerrainCarve> CarveShared() const noexcept { return m_carve; }
    std::uint64_t CarveHash() const noexcept { return m_carve ? m_carve->Hash() : 0; }

    // Height in metres at a world position, carve included. `sampleSpacing` picks the noise detail (coarse spacing skips fine octaves).
    float Height(double x, double z, double sampleSpacing) const;

    // Same without the carve (the raw noise surface).
    float BaseHeight(double x, double z, double sampleSpacing) const;

    // Fills out[j * countX + i] with the raw (uncarved) height at (originX + i * spacing, originZ + j * spacing).
    void FillGrid(double originX, double originZ, double spacing, int countX, int countZ, float* out) const;

    // Slope angle in radians (0 flat) from central differences over one `spacing`.
    float Slope(double x, double z, double spacing) const;

private:
    struct Impl;
    TerrainSettings m_settings;
    TerrainLayout m_layout;
    std::shared_ptr<const ITerrainCarve> m_carve;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::terrain
