#include "DrawStats.h"

namespace gte {

DrawStats CountDrawStats(std::span<const CountableDrawItem> items) noexcept
{
    DrawStats stats;
    for (const CountableDrawItem& item : items) {
        AccumulateDrawStats(stats, item.hasIndexBuffer, item.vertexCount, item.indexCount);
    }
    return stats;
}

} // namespace gte
