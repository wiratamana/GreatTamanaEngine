#include "TerrainClusterRecords.h"

#include <algorithm>
#include <cmath>

namespace gte::terrain::gpu {

bool IsUsableBox(const BoxF& box) noexcept
{
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(box.min[axis]) || !std::isfinite(box.max[axis]) || box.min[axis] > box.max[axis]) {
            return false;
        }
    }
    return true;
}

namespace {

MeshCluster MakeRecord(std::uint32_t slot, const PoolLayout& layout, const BoxF& box) noexcept
{
    MeshCluster c{};
    float radiusSq = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        c.aabbMin[axis] = box.min[axis];
        c.aabbMax[axis] = box.max[axis];
        const float centre = 0.5f * (box.min[axis] + box.max[axis]);
        const float half = 0.5f * (box.max[axis] - box.min[axis]);
        c.sphere[axis] = centre;
        radiusSq += half * half;
    }
    c.sphere[3] = std::sqrt(radiusSq);
    c.cone[0] = 0.0f;
    c.cone[1] = 1.0f;
    c.cone[2] = 0.0f;
    c.cone[3] = 1.0f;
    c.firstIndex = slot * layout.indicesPerSlot;
    c.indexCount = layout.indicesPerSlot;
    return c;
}

} // namespace

MeshCluster MakeLiveRecord(std::uint32_t slot, const PoolLayout& layout, const BoxF& world) noexcept
{
    return MakeRecord(slot, layout, world);
}

MeshCluster MakeInactiveRecord(std::uint32_t slot, const PoolLayout& layout) noexcept
{
    BoxF point;
    point.min[1] = point.max[1] = kInactiveY;
    return MakeRecord(slot, layout, point);
}

BoxF PoolBounds(double worldHalfExtent, float minHeight, float maxHeight) noexcept
{
    const float xz = static_cast<float>(worldHalfExtent + 1.0);
    BoxF box;
    box.min[0] = box.min[2] = -xz;
    box.max[0] = box.max[2] = xz;
    box.min[1] = std::min(kInactiveY, minHeight - 1000.0f);
    box.max[1] = maxHeight + 1000.0f;
    return box;
}

ClusterTableShadow::ClusterTableShadow(const PoolLayout& layout)
    : m_layout(layout)
    , m_records(layout.slotCount)
    , m_isLive(layout.slotCount, 0)
    , m_dirty(layout.slotCount, 0)
{
    for (std::uint32_t slot = 0; slot < layout.slotCount; ++slot) {
        m_records[slot] = MakeInactiveRecord(slot, layout);
    }
    MarkAllDirty();
}

void ClusterTableShadow::MarkDirty(std::uint32_t slot) noexcept
{
    if (m_dirty[slot] == 0) {
        m_dirty[slot] = 1;
        ++m_dirtyCount;
    }
}

void ClusterTableShadow::MarkAllDirty()
{
    std::fill(m_dirty.begin(), m_dirty.end(), static_cast<std::uint8_t>(1));
    m_dirtyCount = static_cast<std::uint32_t>(m_dirty.size());
}

bool ClusterTableShadow::SetLive(std::uint32_t slot, const BoxF& world)
{
    if (slot >= m_records.size() || !IsUsableBox(world)) {
        return false;
    }
    m_records[slot] = MakeLiveRecord(slot, m_layout, world);
    if (m_isLive[slot] == 0) {
        m_isLive[slot] = 1;
        ++m_live;
    }
    MarkDirty(slot);
    return true;
}

bool ClusterTableShadow::SetInactive(std::uint32_t slot)
{
    if (slot >= m_records.size()) {
        return false;
    }
    m_records[slot] = MakeInactiveRecord(slot, m_layout);
    if (m_isLive[slot] != 0) {
        m_isLive[slot] = 0;
        --m_live;
    }
    MarkDirty(slot);
    return true;
}

std::vector<SlotRange> ClusterTableShadow::TakeDirtyRanges(std::uint32_t maxGap)
{
    std::vector<SlotRange> ranges;
    if (m_dirtyCount == 0) {
        return ranges;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(m_dirty.size());
    std::uint32_t lastDirty = 0;
    for (std::uint32_t slot = 0; slot < count; ++slot) {
        if (m_dirty[slot] == 0) {
            continue;
        }
        m_dirty[slot] = 0;
        if (!ranges.empty() && slot - lastDirty <= maxGap + 1) {
            ranges.back().count = slot - ranges.back().first + 1;
        } else {
            ranges.push_back({ slot, 1 });
        }
        lastDirty = slot;
    }
    m_dirtyCount = 0;
    return ranges;
}

} // namespace gte::terrain::gpu
