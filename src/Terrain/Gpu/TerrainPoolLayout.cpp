#include "TerrainPoolLayout.h"

#include "../TerrainTypes.h"

#include <limits>

namespace gte::terrain::gpu {

std::optional<PoolLayout> MakePoolLayout(int quads, std::uint32_t slotCount, std::string* why)
{
    const auto fail = [why](const char* text) -> std::optional<PoolLayout> {
        if (why != nullptr) {
            *why = text;
        }
        return std::nullopt;
    };
    if (quads < 1 || quads > 512) {
        return fail("quads per node is out of range");
    }
    if (slotCount == 0 || slotCount > kPoolMaxSlots) {
        return fail("slot count is out of range");
    }
    PoolLayout layout;
    layout.slotCount = slotCount;
    layout.vertsPerSlot = NodeVertexCount(quads);
    layout.indicesPerSlot = NodeIndexCount(quads);
    // Absolute vertex indices must fit 32 bits, and one index buffer must stay under 4 GiB.
    if (layout.TotalVertices() >= std::numeric_limits<std::uint32_t>::max()
        || layout.IndexBytes() >= (std::uint64_t{ 1 } << 32)) {
        return fail("the pool is too large for 32-bit indices");
    }
    return layout;
}

void WriteSlotIndices(std::span<const std::uint32_t> local, std::uint32_t baseVertex, std::span<std::uint32_t> out) noexcept
{
    const std::size_t count = local.size() < out.size() ? local.size() : out.size();
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = local[i] + baseVertex;
    }
}

std::vector<std::uint32_t> BuildPoolIndices(const PoolLayout& layout, std::span<const std::uint32_t> local)
{
    std::vector<std::uint32_t> all(static_cast<std::size_t>(layout.TotalIndices()));
    for (std::uint32_t slot = 0; slot < layout.slotCount; ++slot) {
        const std::span<std::uint32_t> out(all.data() + static_cast<std::size_t>(slot) * layout.indicesPerSlot, layout.indicesPerSlot);
        WriteSlotIndices(local, slot * layout.vertsPerSlot, out);
    }
    return all;
}

} // namespace gte::terrain::gpu
