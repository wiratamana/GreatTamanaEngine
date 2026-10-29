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

bool HierarchySnapshot::Rebuild(Registry& registry)
{
    ComponentStorage<Transform>& storage = registry.Storage<Transform>();
    const std::size_t count = storage.Size();

    // The effective parent depends only on these fields and on which entities own a Transform.
    if (count == m_keys.size()) {
        std::uint32_t difference = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const Entity entity = storage.EntityAt(i);
            const Transform& transform = storage.ComponentAt(i);
            const Key& key = m_keys[i];
            difference |= (key.entity.index ^ entity.index) | (key.entity.generation ^ entity.generation)
                | (key.parent.index ^ transform.parent.index) | (key.parent.generation ^ transform.parent.generation)
                | (key.sibling ^ transform.siblingIndex);
        }
        if (difference == 0) {
            return false;
        }
    }

    ++m_revision;
    m_entities.clear();
    m_roots = Range{};
    m_keys.clear();
    if (count == 0) {
        m_rangeByParentIndex.clear();
        return true;
    }

    // Pass 1: one read of the Transform storage. Writes the fingerprint and the effective parent group.
    const Registry& constRegistry = registry;
    m_keys.resize(count);
    std::uint32_t maxIndex = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Entity entity = storage.EntityAt(i);
        const Transform& transform = storage.ComponentAt(i);
        const Entity parent = EffectiveParent(constRegistry, transform);
        m_keys[i] = Key{ entity, transform.parent, transform.siblingIndex, parent.IsValid() ? parent.index : kRootGroup };
        maxIndex = std::max(maxIndex, entity.index);
    }

    // Pass 2: group sizes and the owner generation of every slot.
    const std::size_t slotCount = static_cast<std::size_t>(maxIndex) + 1;
    m_rangeByParentIndex.assign(slotCount, Range{});
    for (const Key& key : m_keys) {
        if (key.group == kRootGroup) {
            ++m_roots.count;
        } else {
            ++m_rangeByParentIndex[key.group].count;
        }
        m_rangeByParentIndex[key.entity.index].generation = key.entity.generation;
    }

    // Pass 3: prefix sums. Roots first, then one range per parent slot.
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

    // Pass 4: counting-sort scatter.
    m_grouped.resize(count);
    for (const Key& key : m_keys) {
        const std::size_t group = key.group == kRootGroup ? slotCount : key.group;
        m_grouped[m_cursor[group]++] = Row{ key.entity, key.sibling };
    }

    // Pass 5: order each group; the rows carry their own key.
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
    return true;
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
