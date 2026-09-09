#pragma once

#include "../ECS/Entity.h"

#include <cstdint>
#include <string>

namespace gte {

// Outcome of one Game::InstantiatePrimitive() call - see that method's own
// doc comment (Game.h) for the full contract. `success == false` means
// `errorMessage` explains why and NO entity was created at all (every other
// field is meaningless in that case). `success == true` always means an
// entity WAS created, even if `parentRequestedButNotFound` is also true (a
// not-found parent is a non-fatal WARNING, never a reason to fail the whole
// request or roll back the just-created entity - see network-impl-3's
// PHASE0_MASTER_STRATEGY.md's own Locked Design Decision #2).
struct InstantiatePrimitiveOutcome {
    bool success = false;
    std::string errorMessage;

    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    // The entity's ACTUAL display name after Unity-style auto-de-duplication
    // (EntityQuery.h's MakeUniqueEntityName()) - may differ from whatever
    // name the caller originally requested.
    std::string resolvedName;

    // True only when the caller explicitly requested a parent BY NAME and no
    // live entity currently has that name - the entity was still created,
    // just left unparented (world space). Always false when the caller
    // requested no parent at all.
    bool parentRequestedButNotFound = false;
    std::string requestedParentName; // meaningful only when the flag above is true
};

// Outcome of one Game::DeleteEntityByName() call. `success == false` means
// `errorMessage` explains why (empty name, or no live entity currently has
// that name) and NOTHING was destroyed. `success == true` means the named
// entity (and every descendant of it) was destroyed -
// deletedEntityIndex/Generation identify exactly which entity that was
// (useful for a caller that wants to confirm/log which physical entity a
// name resolved to, since names are not enforced globally unique outside of
// InstantiatePrimitive()'s own auto-dedup - see network-impl-3's Locked
// Design Decision #3).
struct DeleteEntityOutcome {
    bool success = false;
    std::string errorMessage;
    std::uint32_t deletedEntityIndex = 0;
    std::uint32_t deletedEntityGeneration = 0;
};

} // namespace gte
