#include "GrassTileBuilder.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace gte {

namespace {

constexpr double kSnapTexels = 8.0;
constexpr float kNoOcean = -1.0e6f;

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
    tile->data.resize(static_cast<std::size_t>(job.size) * job.size * 2u);

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
            const terrain::SurfaceHit hit = sampler.Sample(x, z, hint);
            float flag = 0.0f;
            if (job.water != nullptr) {
                const water::WaterSample w = job.water->SampleAt(x, z);
                if (w.hasWater && hit.height <= w.surfaceY + job.waterMargin) {
                    flag = 1.0f;
                }
            }
            const std::size_t index = (static_cast<std::size_t>(j) * job.size + i) * 2u;
            tile->data[index] = hit.height;
            tile->data[index + 1] = flag;
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

} // namespace gte
