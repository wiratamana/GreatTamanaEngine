#pragma once

#include "Entity.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gte {

class Registry;

// Parent -> ordered children table of every Transform. Rebuild once per use; buffers keep their capacity.
// Children() matches GetChildren() for every parent. Not valid after the hierarchy changes.
class HierarchySnapshot {
public:
    void Rebuild(Registry& registry);

    // Children of `parent` in SiblingLess order. kInvalidEntity = the roots. Unknown or stale entity = empty.
    std::span<const Entity> Children(Entity parent) const noexcept;

    // Number of listed entities.
    std::size_t Size() const noexcept { return m_entities.size(); }

private:
    struct Range {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
        std::uint32_t generation = 0; // generation of the parent that owns this slot
    };

    struct Row {
        std::uint32_t group;   // parent entity index, or the root group
        std::uint32_t sibling;
        Entity entity;
    };

    std::vector<Entity> m_entities;              // grouped by parent, each group in SiblingLess order
    std::vector<Range> m_rangeByParentIndex;     // indexed by the parent's entity index
    Range m_roots;
    std::vector<Row> m_rows;                     // scratch: dense order
    std::vector<Row> m_grouped;                  // scratch: grouped order
    std::vector<std::uint32_t> m_cursor;         // scratch: write position per group
};

} // namespace gte
