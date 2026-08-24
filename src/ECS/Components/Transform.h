#pragma once

#include "../Entity.h"
#include "Math/Mat4.h"
#include "Math/Quat.h"
#include "Math/Vec3.h"

#include <cstdint>

namespace gte {

// The engine's first real component (see AGENTS.md, "Entity-Component-
// System") - plain position/rotation/scale data, ALWAYS relative to `parent`
// below (Unity's own Transform.localPosition/localRotation/localScale
// semantics) - an entity with no parent (parent == kInvalidEntity, the
// default) is implicitly in world space, exactly as every Transform used to
// be before parenting existed. Deliberately a plain struct with no behavior
// beyond the one pure-math helper below (LocalToWorldMatrix() only ever
// composes THIS entity's own local T/R/S - it has no way to reach a parent's
// Transform, since components must never depend on Registry/other entities -
// see AGENTS.md), same spirit as every gte::Math type (Vec3/Quat/Mat4) it's
// built from. Walking the actual parent CHAIN to get a true world matrix -
// and every other hierarchy operation (attach/detach, cycle-safe reparenting,
// sibling reordering) - lives in the free functions in ECS/TransformHierarchy.h
// instead, the same "only code that already depends on Registry may also
// depend on Transform relationships" boundary this engine already draws
// elsewhere (see AGENTS.md, RenderSystem).
struct Transform {
    Vec3 position = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 scale = Vec3::One();

    // kInvalidEntity (the default) means "no parent - this Transform's own
    // position/rotation/scale above are already in WORLD space", matching
    // every Transform's behavior before parenting existed. A non-invalid
    // value means position/rotation/scale above are relative to THAT
    // entity's own world transform instead (see
    // ECS/TransformHierarchy.h's ComputeWorldMatrix()) - set exclusively
    // through TransformHierarchy.h's SetParent() (never assigned directly),
    // since that's what keeps reparenting cycle-safe and keeps
    // siblingIndex/world-position-preservation consistent.
    Entity parent = kInvalidEntity;

    // This entity's position among the OTHER entities that share the exact
    // same `parent` value above (0 = first) - purely a DISPLAY/ordering
    // concern (which order "Hierarchy" lists siblings in, and the order a
    // future Editor "reorder" drag ends up producing - see
    // ECS/TransformHierarchy.h's GetChildren()/SetSiblingIndex()), with zero
    // effect on the actual computed world transform. Mirrors Unity's own
    // Transform.GetSiblingIndex()/SetSiblingIndex(). Not guaranteed unique
    // or densely packed until SetSiblingIndex()/SetParent() has normalized
    // this entity's sibling group at least once (ties are broken by
    // creation/dense-storage order - see GetChildren()) - never assign this
    // directly, always go through TransformHierarchy.h.
    std::uint32_t siblingIndex = 0;

    // Translate * Rotate * Scale, via Mat4::TRS() - see Mat4.h for the exact
    // composition order/convention this engine uses. This is this entity's
    // LOCAL (parent-relative) matrix only - see ECS/TransformHierarchy.h's
    // ComputeWorldMatrix() for the actual world matrix once `parent` is set.
    Mat4 LocalToWorldMatrix() const noexcept
    {
        return Mat4::TRS(position, rotation, scale);
    }
};

} // namespace gte
