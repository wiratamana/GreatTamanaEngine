#include "TerrainOverlayData.h"

#include "../../Terrain/Debug/TerrainMapImage.h"

#include <algorithm>

namespace gte {

int OverlayBucketOf(const terrain::TerrainNodeInfo& node, bool colorByState) noexcept
{
    if (colorByState) {
        if (!node.enabled) {
            return 2;
        }
        return node.inView ? 0 : 1;
    }
    return kOverlayStateBuckets + std::clamp(static_cast<int>(node.key.level), 0, 11);
}

std::array<std::uint8_t, 4> OverlayBucketColor(int bucket) noexcept
{
    switch (bucket) {
    case 0: return { 70, 230, 100, 255 };
    case 1: return { 240, 90, 70, 255 };
    case 2: return { 130, 130, 150, 255 };
    default: break;
    }
    const std::array<std::uint8_t, 3> lod = terrain::MapLodColor(bucket - kOverlayStateBuckets);
    return { lod[0], lod[1], lod[2], 255 };
}

void AppendNodeBoxSegments(const terrain::TerrainNodeInfo& node, std::vector<FrustumSegment>& out)
{
    const float x[2] = { static_cast<float>(node.minX), static_cast<float>(node.maxX) };
    const float y[2] = { static_cast<float>(node.minY), static_cast<float>(node.maxY) };
    const float z[2] = { static_cast<float>(node.minZ), static_cast<float>(node.maxZ) };
    auto corner = [&](int i) { return Vec3(x[i & 1], y[(i >> 1) & 1], z[(i >> 2) & 1]); };
    for (int i = 0; i < 8; ++i) {
        for (int bit = 0; bit < 3; ++bit) {
            const int j = i | (1 << bit);
            if (j != i) {
                out.push_back({ corner(i), corner(j), FrustumEdgeKind::Side });
            }
        }
    }
}

} // namespace gte
