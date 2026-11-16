#pragma once

// Cluster table of the node pool: one record per slot. A live slot has the exact box of its node; an inactive
// slot has a finite point far below the world, which every frustum test rejects (a non-finite box would be kept).

#include "TerrainPoolLayout.h"

#include "../../Assets/MeshCluster.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gte::terrain::gpu {

inline constexpr float kInactiveY = -1.0e5f;

struct BoxF {
    float min[3] = { 0.0f, 0.0f, 0.0f };
    float max[3] = { 0.0f, 0.0f, 0.0f };
};

// Record of a slot holding a node: index range of the slot, `world` box, no back-face cone (cutoff 1).
MeshCluster MakeLiveRecord(std::uint32_t slot, const PoolLayout& layout, const BoxF& world) noexcept;

// Record of an unused slot: same index range, a zero-size box at (0, kInactiveY, 0).
MeshCluster MakeInactiveRecord(std::uint32_t slot, const PoolLayout& layout) noexcept;

// Box that holds every live box of a world with this half extent and the inactive point. Constant for the pool.
BoxF PoolBounds(double worldHalfExtent, float minHeight, float maxHeight) noexcept;

struct SlotRange {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

// CPU copy of the table with dirty tracking, so only the changed records are written to the GPU.
class ClusterTableShadow {
public:
    explicit ClusterTableShadow(const PoolLayout& layout);

    const PoolLayout& Layout() const noexcept { return m_layout; }
    std::span<const MeshCluster> Records() const noexcept { return m_records; }
    std::uint32_t LiveCount() const noexcept { return m_live; }
    bool IsLive(std::uint32_t slot) const noexcept { return slot < m_isLive.size() && m_isLive[slot] != 0; }

    // False (nothing changed) for an out-of-range slot or an unusable box.
    bool SetLive(std::uint32_t slot, const BoxF& world);
    bool SetInactive(std::uint32_t slot);

    bool AnyDirty() const noexcept { return m_dirtyCount != 0; }
    void MarkAllDirty();

    // Sorted ranges of dirty slots; ranges closer than `maxGap` slots are merged. Clears the dirty flags.
    std::vector<SlotRange> TakeDirtyRanges(std::uint32_t maxGap = 4);

private:
    void MarkDirty(std::uint32_t slot) noexcept;

    PoolLayout m_layout;
    std::vector<MeshCluster> m_records;
    std::vector<std::uint8_t> m_isLive;
    std::vector<std::uint8_t> m_dirty;
    std::uint32_t m_live = 0;
    std::uint32_t m_dirtyCount = 0;
};

// Box usable as a live record: finite and not inverted.
bool IsUsableBox(const BoxF& box) noexcept;

} // namespace gte::terrain::gpu
