#pragma once

#include "../ECS/Entity.h"

#include <cstdint>
#include <string_view>

namespace gte {

// What POST /hierarchy_action does to a Hierarchy row.
enum class HierarchyActionKind : std::uint8_t { Reparent, Reorder, Toggle };

// Plain request of POST /hierarchy_action. `dragged` is unused by Toggle.
struct HierarchyActionCommand {
    HierarchyActionKind kind = HierarchyActionKind::Reparent;
    Entity dragged;
    Entity target;
    bool insertAfter = false; // Reorder: land after the target row instead of before it
};

constexpr std::string_view HierarchyActionKindName(HierarchyActionKind kind) noexcept
{
    switch (kind) {
    case HierarchyActionKind::Reparent: return "reparent";
    case HierarchyActionKind::Reorder: return "reorder";
    case HierarchyActionKind::Toggle: return "toggle";
    }
    return "";
}

} // namespace gte
