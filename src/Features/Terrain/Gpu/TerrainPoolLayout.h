#pragma once

// Fixed layout of the GPU node pool: slot s owns vertices [s*V, (s+1)*V) and indices [s*I, (s+1)*I).
// The index values of a slot are absolute (they already include s*V), because the cull shader draws with base vertex 0.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte::terrain::gpu {

inline constexpr std::uint32_t kPoolVertexBytes = 32;   // Position, normal, uv.
inline constexpr std::uint32_t kPoolMaxSlots = 8192;    // Far above any real pool; guards the overflow maths.

struct PoolLayout {
    std::uint32_t slotCount = 0;
    std::uint32_t vertsPerSlot = 0;
    std::uint32_t indicesPerSlot = 0;

    std::uint64_t TotalVertices() const noexcept { return static_cast<std::uint64_t>(slotCount) * vertsPerSlot; }
    std::uint64_t TotalIndices() const noexcept { return static_cast<std::uint64_t>(slotCount) * indicesPerSlot; }
    std::uint64_t VertexBytes() const noexcept { return TotalVertices() * kPoolVertexBytes; }
    std::uint64_t IndexBytes() const noexcept { return TotalIndices() * sizeof(std::uint32_t); }
    std::uint64_t SlotVertexOffsetBytes(std::uint32_t slot) const noexcept
    {
        return static_cast<std::uint64_t>(slot) * vertsPerSlot * kPoolVertexBytes;
    }
    std::uint64_t SlotIndexOffsetBytes(std::uint32_t slot) const noexcept
    {
        return static_cast<std::uint64_t>(slot) * indicesPerSlot * sizeof(std::uint32_t);
    }
};

// Layout for nodes of `quads` quads per side. Empty (with `why`) when the pool would overflow 32-bit indices or
// the slot count is out of range.
std::optional<PoolLayout> MakePoolLayout(int quads, std::uint32_t slotCount, std::string* why = nullptr);

// Copies `local` (one node's indices) into `out` with `baseVertex` added to every value.
void WriteSlotIndices(std::span<const std::uint32_t> local, std::uint32_t baseVertex, std::span<std::uint32_t> out) noexcept;

// The whole pool index buffer (slotCount * indicesPerSlot values). For tests and tiny pools: large pools are
// written slot by slot instead.
std::vector<std::uint32_t> BuildPoolIndices(const PoolLayout& layout, std::span<const std::uint32_t> local);

} // namespace gte::terrain::gpu
