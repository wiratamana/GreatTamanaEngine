#pragma once

#include "Entity.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gte {

class Registry;

// Parent -> ordered children table of every Transform; Children() matches GetChildren().
// Rebuild() redoes the table only when a Transform's raw hierarchy fields differ from the last build.
class HierarchySnapshot {
public:
    // Returns true when the table was rebuilt, false when the hierarchy is unchanged.
    bool Rebuild(Registry& registry);

    // Children of `parent` in SiblingLess order. kInvalidEntity = the roots. Unknown or stale entity = empty.
    std::span<const Entity> Children(Entity parent) const noexcept;

    // Number of listed entities.
    std::size_t Size() const noexcept { return m_entities.size(); }

    // Counts real rebuilds only.
    std::uint64_t Revision() const noexcept { return m_revision; }

private:
    struct Range {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
        std::uint32_t generation = 0; // generation of the parent that owns this slot
    };

    // Raw hierarchy fields of one Transform in dense storage order, plus the group derived from them.
    struct Key {
        Entity entity;
        Entity parent;
        std::uint32_t sibling;
        std::uint32_t group; // effective parent entity index, or the root group
    };

    struct Row {
        Entity entity;
        std::uint32_t sibling;
    };

    std::vector<Entity> m_entities;              // grouped by parent, each group in SiblingLess order
    std::vector<Range> m_rangeByParentIndex;     // indexed by the parent's entity index
    Range m_roots;
    std::vector<Key> m_keys;                     // fingerprint of the last build
    std::vector<Row> m_grouped;                  // scratch: grouped order
    std::vector<std::uint32_t> m_cursor;         // scratch: write position per group
    std::uint64_t m_revision = 0;
};

} // namespace gte
