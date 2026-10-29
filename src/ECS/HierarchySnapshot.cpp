#include "HierarchySnapshot.h"

#include "Components/Transform.h"
#include "Registry.h"
#include "TransformHierarchy.h"

#include <algorithm>
#include <limits>

namespace gte {

namespace {

constexpr std::uint32_t kRootGroup = std::numeric_limits<std::uint32_t>::max();

} // namespace

void HierarchySnapshot::Rebuild(Registry& registry)
{
    ComponentStorage<Transform>& storage = registry.Storage<Transform>();
    const std::size_t count = storage.Size();

    m_entities.clear();
    m_roots = Range{};
    if (count == 0) {
        m_rangeByParentIndex.clear();
        return;
    }

    std::uint32_t maxIndex = 0;
    for (std::size_t i = 0; i < count; ++i) {
        maxIndex = std::max(maxIndex, storage.EntityAt(i).index);
    }
    const std::size_t slotCount = static_cast<std::size_t>(maxIndex) + 1;
    m_rangeByParentIndex.assign(slotCount, Range{});

    // Pass 1: effective parent per Transform, group sizes, owner generation per slot.
    const Registry& constRegistry = registry;
    m_rows.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Entity entity = storage.EntityAt(i);
        const Transform& transform = storage.ComponentAt(i);
        const Entity parent = EffectiveParent(constRegistry, transform);
        const std::uint32_t group = parent.IsValid() ? parent.index : kRootGroup;
        m_rows[i] = Row{ group, transform.siblingIndex, entity };
        if (group == kRootGroup) {
            ++m_roots.count;
        } else {
            ++m_rangeByParentIndex[group].count;
        }
        m_rangeByParentIndex[entity.index].generation = entity.generation;
    }

    // Pass 2: prefix sums. Roots first, then one range per parent slot.
    m_cursor.resize(slotCount + 1);
    m_roots.first = 0;
    m_cursor[slotCount] = 0;
    std::uint32_t running = m_roots.count;
    for (std::size_t slot = 0; slot < slotCount; ++slot) {
        Range& range = m_rangeByParentIndex[slot];
        range.first = running;
        m_cursor[slot] = running;
        running += range.count;
    }

    // Pass 3: counting-sort scatter.
    m_grouped.resize(count);
    for (const Row& row : m_rows) {
        const std::size_t group = row.group == kRootGroup ? slotCount : row.group;
        m_grouped[m_cursor[group]++] = row;
    }

    // Pass 4: order each group; the rows carry their own key.
    const auto less = [](const Row& a, const Row& b) noexcept {
        return SiblingLess(a.sibling, a.entity, b.sibling, b.entity);
    };
    const auto sortGroup = [&](const Range& range) {
        if (range.count < 2) {
            return;
        }
        const auto first = m_grouped.begin() + range.first;
        const auto last = first + range.count;
        if (!std::is_sorted(first, last, less)) {
            std::sort(first, last, less);
        }
    };
    sortGroup(m_roots);
    for (const Range& range : m_rangeByParentIndex) {
        sortGroup(range);
    }

    m_entities.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        m_entities[i] = m_grouped[i].entity;
    }
}

std::span<const Entity> HierarchySnapshot::Children(Entity parent) const noexcept
{
    if (!parent.IsValid()) {
        return m_roots.count == 0 ? std::span<const Entity>{}
                                  : std::span<const Entity>(m_entities.data() + m_roots.first, m_roots.count);
    }
    if (parent.index >= m_rangeByParentIndex.size()) {
        return {};
    }
    const Range& range = m_rangeByParentIndex[parent.index];
    if (range.generation != parent.generation || range.count == 0) {
        return {};
    }
    return std::span<const Entity>(m_entities.data() + range.first, range.count);
}

} // namespace gte
