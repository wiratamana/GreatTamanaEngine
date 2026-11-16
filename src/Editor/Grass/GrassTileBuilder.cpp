#include "GrassTileBuilder.h"

#include "../../Terrain/TerrainMaterial.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace gte {

namespace {

constexpr double kSnapTexels = 8.0;
constexpr float kNoOcean = -1.0e6f;
constexpr double kMinDriftMetres = 8.0;   // Smallest camera drift that may rebuild the tile.
constexpr double kDriftSafetyMetres = 8.0;

// Noise height on a lattice of kCoarseStep texels, filled on demand.
class CoarseNoise {
public:
    CoarseNoise(const terrain::TerrainHeightField& field, double originX, double originZ, float texel, std::uint32_t size)
        : m_field(field)
        , m_originX(originX)
        , m_originZ(originZ)
        , m_texel(static_cast<double>(texel))
        , m_side(size / kCoarseStep + 2)
        , m_values(static_cast<std::size_t>(m_side) * m_side, std::numeric_limits<float>::quiet_NaN())
    {
    }

    // Bilinear noise height at fractional texel coordinates.
    float Height(double i, double j)
    {
        const double u = i / kCoarseStep;
        const double v = j / kCoarseStep;
        const auto iu = static_cast<std::uint32_t>(u);
        const auto iv = static_cast<std::uint32_t>(v);
        const auto fu = static_cast<float>(u - iu);
        const auto fv = static_cast<float>(v - iv);
        const float a = Lattice(iu, iv);
        const float b = Lattice(iu + 1, iv);
        const float c = Lattice(iu, iv + 1);
        const float d = Lattice(iu + 1, iv + 1);
        return (a + fu * (b - a)) + fv * ((c + fu * (d - c)) - (a + fu * (b - a)));
    }

private:
    static constexpr std::uint32_t kCoarseStep = 4;

    float Lattice(std::uint32_t iu, std::uint32_t iv)
    {
        float& value = m_values[static_cast<std::size_t>(iv) * m_side + iu];
        if (std::isnan(value)) {
            value = m_field.Height(m_originX + static_cast<double>(iu * kCoarseStep) * m_texel,
                m_originZ + static_cast<double>(iv * kCoarseStep) * m_texel, 2.0);
        }
        return value;
    }

    const terrain::TerrainHeightField& m_field;
    double m_originX;
    double m_originZ;
    double m_texel;
    std::uint32_t m_side;
    std::vector<float> m_values;
};

} // namespace

double SnapGrassTileOrigin(double center, std::uint32_t size, float texelSize) noexcept
{
    const double step = static_cast<double>(texelSize) * kSnapTexels;
    const double half = 0.5 * static_cast<double>(size) * static_cast<double>(texelSize);
    return std::floor((center - half) / step) * step;
}

float GrassOceanLevel(const water::WaterBodySet* water) noexcept
{
    if (water != nullptr) {
        for (const water::WaterBody& body : water->Bodies()) {
            if (body.kind == water::WaterBodyKind::Ocean) {
                return body.level;
            }
        }
    }
    return kNoOcean;
}

GrassTileBuild BuildGrassTile(const GrassTileJob& job)
{
    const auto start = std::chrono::steady_clock::now();
    GrassTileBuild result;
    if (job.size < 2 || job.texelSize <= 0.0f || job.surface.field == nullptr) {
        return result;
    }
    const terrain::SurfaceSampler sampler(job.surface);
    auto tile = std::make_shared<GrassHeightTile>();
    tile->size = job.size;
    tile->texelSize = job.texelSize;
    tile->originX = job.originX;
    tile->originZ = job.originZ;
    tile->seaLevel = GrassOceanLevel(job.water.get());
    tile->revision = job.revision;
    tile->data.assign(static_cast<std::size_t>(job.size) * job.size * kGrassTileStride, 0.0f);

    // Where no node is resident the noise field is read on every 4th texel only and interpolated.
    CoarseNoise coarse(*job.surface.field, job.originX, job.originZ, job.texelSize, job.size);
    const terrain::TerrainSettings& terrainSettings = job.surface.field->Settings();

    const double reach2 = job.reach * job.reach;
    std::uint64_t inReach = 0;
    std::uint64_t nearLevel0 = 0;
    std::uint64_t nearNodes = 0;
    std::uint64_t submerged = 0;
    const terrain::NodeMesh* hint = nullptr;
    for (std::uint32_t j = 0; j < job.size; ++j) {
        const double z = job.originZ + static_cast<double>(j) * static_cast<double>(job.texelSize);
        for (std::uint32_t i = 0; i < job.size; ++i) {
            const double x = job.originX + static_cast<double>(i) * static_cast<double>(job.texelSize);
            terrain::SurfaceHit hit;
            if (!sampler.SampleNodes(x, z, hint, hit)) {
                // No resident node: a coarse noise guess (bilinear over a sparse lattice) keeps the build cheap.
                hit.height = coarse.Height(static_cast<double>(i), static_cast<double>(j));
                hit.level = -1;
                hit.color = terrain::PackRgba8(terrain::SurfaceColor(hit.height, 0.0f, terrainSettings));
            }
            float flag = 0.0f;
            if (job.water != nullptr) {
                const water::WaterSample w = job.water->SampleAt(x, z);
                if (w.hasWater && hit.height <= w.surfaceY + job.waterMargin) {
                    flag = 1.0f;
                }
            }
            const std::size_t index = (static_cast<std::size_t>(j) * job.size + i) * kGrassTileStride;
            tile->data[index] = hit.height;
            tile->data[index + 1] = GrassTileFlagsColor(flag > 0.5f, hit.color);
            submerged += flag > 0.5f ? 1u : 0u;
            const double dx = x - job.centerX;
            const double dz = z - job.centerZ;
            if (dx * dx + dz * dz <= reach2) {
                ++inReach;
                nearLevel0 += hit.level == 0 ? 1u : 0u;
                nearNodes += hit.level >= 0 ? 1u : 0u;
            }
        }
    }
    const auto share = [](std::uint64_t part, std::uint64_t whole) {
        return whole == 0 ? 0.0f : static_cast<float>(static_cast<double>(part) / static_cast<double>(whole));
    };
    result.coverageLevel0 = share(nearLevel0, inReach);
    result.coverageNodes = share(nearNodes, inReach);
    result.submergedShare = share(submerged, static_cast<std::uint64_t>(job.size) * job.size);
    result.tile = std::move(tile);
    result.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}

std::optional<GrassTileReason> DecideGrassTileRebuild(const GrassTileView& view, const GrassTileSeen& last, const GrassTileSeen& now)
{
    if (!view.haveTile) {
        return GrassTileReason::Initial;
    }
    if (now.settingsKey != last.settingsKey) {
        return GrassTileReason::Settings;
    }
    const double half = 0.5 * view.extent;
    const double allowed = std::max(half - view.reach - kDriftSafetyMetres, kMinDriftMetres);
    const double drift = std::max(std::abs(view.cameraX - (view.originX + half)), std::abs(view.cameraZ - (view.originZ + half)));
    if (drift > allowed) {
        return GrassTileReason::Moved;
    }
    if (now.waterHash != last.waterHash) {
        return GrassTileReason::Water;
    }
    if (now.carveHash != last.carveHash || (last.rebuilding > 0 && now.rebuilding == 0)) {
        return GrassTileReason::Carve;
    }
    if (now.terrainHash != last.terrainHash) {
        return GrassTileReason::Terrain;
    }
    // New fine nodes bring better heights. While a terrain edit settles, any rebuilt node counts.
    if (view.now < view.settleUntil && now.streamRevision != last.streamRevision) {
        return GrassTileReason::Stream;
    }
    for (const auto& [key, mesh] : now.fineNodes) {
        const auto before = last.fineNodes.find(key);
        if (before == last.fineNodes.end() || before->second != mesh) {
            return GrassTileReason::Stream;
        }
    }
    return std::nullopt;
}

} // namespace gte
