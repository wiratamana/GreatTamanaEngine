#pragma once

#include "../ECS/Entity.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

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

// network-impl-5 campaign
// (PHASE2_GAME_LEVEL_SET_ENTITY_TRS_AND_INSTANTIATE_LIGHT_APIS.md) - unlike
// InstantiatePrimitive()/DeleteEntityByName() above (which take flat,
// individual parameters), SetEntityTrs()/InstantiateLight() below each take
// ONE small plain request struct instead - a deliberate, one-time departure
// from the flat-parameter convention, made because each of these two
// methods has enough independently-optional fields (up to 4 distinct
// optional groups for SetEntityTrsParams) that a flat parameter list would
// be genuinely error-prone at the call site (which trailing bool
// corresponds to which trailing Vec3?). src/Application/EngineCommandBridge.h
// (Phase 3) reuses these EXACT SAME two structs directly as its own
// per-kind command payload (it already #includes this header for the
// Outcome structs below) rather than re-declaring an identical shape a
// second time - a deliberate simplification over the EXISTING
// InstantiatePrimitiveCommand/DeleteEntityCommand precedent in that file,
// which DOES duplicate Game's own flat parameter shape; this file's
// request structs are not required to follow that older duplication
// pattern, and existing code is NOT retroactively changed to match.

// Request parameters for one Game::SetEntityTrs() call - see that method's
// own doc comment (Game.h) for the full contract.
struct SetEntityTrsParams {
    // The entity to modify, looked up via EntityQuery.h's
    // FindEntityByName() - exactly the same lookup DeleteEntityByName()
    // already uses.
    std::string name;

    // Each of the three groups below is independently optional - see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #2 (an ALL-OR-
    // NOTHING group once its own hasX flag is true - there is no notion of
    // "change only the Y axis" anywhere in this struct; NetworkRoutes.h's
    // ParseSetEntityTrsRequest() (Phase 1) already enforces this at parse
    // time, so by the time a SetEntityTrsParams reaches this method, each
    // hasX flag being true already implies its own x/y/z(/w) fields are
    // ALL meaningful).
    bool hasTranslation = false;
    Vec3 translation;

    // Euler degrees, (pitchX, yawY, rollZ) - passed directly to
    // Quat::FromEulerDegrees(). There is no quaternion-input alternative
    // anywhere in this campaign - see PHASE0's Locked Design Decision #1.
    bool hasRotationEulerDegrees = false;
    Vec3 rotationEulerDegrees;

    bool hasScale = false;
    Vec3 scale;
};

// Outcome of one Game::SetEntityTrs() call. `success == false` means
// `errorMessage` explains why and NOTHING was changed - every *Changed flag
// stays false and resulting*/entityIndex/Generation are meaningless in that
// case (see `entityNotFound` below for the ONE distinction this outcome
// makes between two different failure reasons, needed by NetworkServer.cpp's
// own HTTP status-code mapping in Phase 4).
struct SetEntityTrsOutcome {
    bool success = false;
    std::string errorMessage;

    // True ONLY when `name` did not resolve to ANY live entity at all
    // (FindEntityByName() returned kInvalidEntity) - Phase 4's
    // /set_entity_trs route maps THIS specific case to HTTP 404. False
    // (while success is ALSO still false) for the OTHER failure case - the
    // name resolved to a live entity, but that entity has no Transform
    // component to edit at all - which Phase 4 instead maps to HTTP 409
    // (the entity positively exists, but this operation cannot apply to
    // it - a meaningfully different situation than "no such entity").
    bool entityNotFound = false;

    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;

    // Independently reports which of the three optional groups this
    // specific call actually changed - see PHASE0's Locked Design Decision
    // #7. A request that supplied NONE of translation/rotation_euler_degrees/
    // scale at all (a valid no-op per Locked Design Decision #6) still
    // reports success == true here, with every flag below false.
    bool translationChanged = false;
    bool rotationChanged = false;
    bool scaleChanged = false;

    // The entity's FULL local Transform state AFTER this call, whether or
    // not this call changed a given field - meaningful only when
    // success == true. This is what lets a caller confirm the resulting
    // state (or simply READ the current state, for a no-op call) in the
    // same round trip - see PHASE0's Locked Design Decision #6/#7. No
    // dedicated "get entity transform" endpoint exists (see PHASE0's own
    // Non-Goals) - this is that capability's de-facto stand-in.
    Vec3 resultingPosition;
    Quat resultingRotation;
    Vec3 resultingScale;
};

// Request parameters for one Game::InstantiateLight() call - see that
// method's own doc comment (Game.h) for the full contract.
struct InstantiateLightParams {
    // "" or (case-insensitively) "directional" both mean "the engine's one
    // implemented light kind, DirectionalLight" - ANY other non-empty value
    // fails the whole call (see PHASE0's Locked Design Decision #9). This
    // field exists now specifically to future-proof this request shape for
    // a later, currently-unimplemented light kind (point/spot) - it is not
    // dead weight.
    std::string lightType;

    // REQUIRED at the NetworkRoutes.h/Phase 1 parsing layer (see PHASE0's
    // Locked Design Decision #4) - this field still defaults to
    // "Directional Light" INSIDE Game::InstantiateLight() itself if handed
    // an empty string anyway, as defense-in-depth for any other, non-
    // network caller of this method, exactly mirroring
    // InstantiatePrimitive()'s own "requestedName falls back to the shape's
    // ToString()" identical split between "the network layer enforces
    // required-ness" and "the Game-layer method still degrades gracefully."
    std::string requestedName;

    Vec3 worldPosition;

    // Absent (false) means: apply the SAME "late-afternoon" default
    // rotation Game::CreateDirectionalLightEntity() already uses - NOT an
    // identity rotation - see PHASE0's Locked Design Decision #5, and this
    // method's own doc comment (Game.h) for exactly why this diverges from
    // InstantiatePrimitive()'s own "identity unless told otherwise"
    // philosophy.
    bool hasRotationEulerDegrees = false;
    Vec3 rotationEulerDegrees;

    // Maps 1:1 onto DirectionalLight::color/illuminanceLux/active - see
    // ECS/Components/DirectionalLight.h. Defaults match that component's
    // own field defaults exactly, so "field omitted" and "field explicitly
    // set to the component default" behave identically.
    Vec3 color = Vec3::One();
    float illuminanceLux = 100000.0f;
    bool active = true;

    bool hasParent = false;
    std::string parentName;
};

// Outcome of one Game::InstantiateLight() call - deliberately the exact
// same SHAPE as InstantiatePrimitiveOutcome above (success/errorMessage/
// entityIndex/entityGeneration/resolvedName/parentRequestedButNotFound/
// requestedParentName) - both endpoints report exactly the same five
// pieces of information about a newly-spawned, possibly-named/parented
// entity. Kept as its own, separately-named struct (rather than literally
// reusing InstantiatePrimitiveOutcome) purely for call-site clarity/type
// safety at Phase 3's EngineCommandResult - NOT because the fields
// themselves differ in any way. See NetworkRoutes.h's own Phase 1 comment
// on why BuildInstantiatePrimitiveResponseJson() is reused verbatim for
// BOTH outcome types' JSON response despite this being a separate C++ type.
struct InstantiateLightOutcome {
    bool success = false;
    std::string errorMessage;
    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    std::string resolvedName;
    bool parentRequestedButNotFound = false;
    std::string requestedParentName;
};

} // namespace gte
