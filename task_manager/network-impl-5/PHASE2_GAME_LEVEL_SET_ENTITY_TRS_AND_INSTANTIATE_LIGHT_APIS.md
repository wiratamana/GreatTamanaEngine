# PHASE2 — Game-Level `SetEntityTrs()` / `InstantiateLight()` APIs

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: nothing from Phase 1 (no
JSON anywhere in this phase) or anything networking-related. Reuses
`ECS/EntityQuery.h` (`FindEntityByName`/`MakeUniqueEntityName`),
`ECS/TransformHierarchy.h` (`SetParent`), and `Math/Quat.h`
(`Quat::FromEulerDegrees`), all unchanged.

## Step 1 — The Goal

Add two new public methods to `Game` (`src/Game/Game.h/.cpp`) that do the
ACTUAL ECS mutation this campaign is about — callable from completely
ordinary in-process C++ code (no HTTP, no bridge, no JSON):

- `Game::SetEntityTrs(const SetEntityTrsParams&)` — updates the LOCAL
  Transform of an existing entity, looked up by name.
- `Game::InstantiateLight(const InstantiateLightParams&)` — spawns a new
  light entity, this campaign's light-specific sibling of
  `InstantiatePrimitive()`.

Also perform ONE small, deliberately behavior-preserving refactor of the
ALREADY-EXISTING `Game::CreateDirectionalLightEntity()` (see Step 3.4) so
its own hardcoded default rotation and `InstantiateLight()`'s new default
rotation share a single source of truth instead of two independent copies
of the same two float literals.

## Step 2 — The Situation

- `Game::CreateDirectionalLightEntity()` (`src/Game/Game.h/.cpp`, already
  exists, used today by `Editor/Panels/HierarchyPanel.cpp`'s "Create
  Directional Light" menu item) is the exact spawn machinery this phase's
  `InstantiateLight()` builds on top of - a `Transform` (given a
  `Quat::FromEulerDegrees(45.0f, -30.0f, 0.0f)` default rotation) plus a
  `DirectionalLight` component, named `"Directional Light"` via
  `MakeUniqueEntityName()`. **This phase must not change this method's own
  observable behavior at all** - the Editor's existing menu item must keep
  producing an identical entity afterward (same default rotation, same
  fallback name, same auto-dedup) - see Step 3.4's own explicit
  "behavior-preserving" requirement.
- `Game::InstantiatePrimitive()` (`network-impl-3` campaign, unchanged by
  this phase) is the closest existing precedent for `InstantiateLight()`'s
  own shape: parse/validate a type-like string, resolve a unique name via
  `MakeUniqueEntityName()`, set `Transform::position`, optionally reparent
  via `FindEntityByName()` + `SetParent(..., worldPositionStays = true)`,
  and report a `parentRequestedButNotFound` warning (never a failure) for
  an unresolvable parent name. `InstantiateLight()` reuses every one of
  these steps, just swapping "spawn a mesh via `CreatePrimitiveEntity()`"
  for "spawn a light via a small inline block mirroring
  `CreateDirectionalLightEntity()`'s own body."
- `Game::DeleteEntityByName()` (`network-impl-3` campaign, unchanged) is the
  closest existing precedent for `SetEntityTrs()`'s own "look up by name,
  fail gracefully if not found" shape - reuse `FindEntityByName()`
  identically.
- `ECS/Components/Transform.h`: `position`/`rotation`/`scale` are plain,
  directly-mutable public fields - `Registry::TryGetComponent<Transform>(entity)`
  (already used elsewhere, e.g. `InstantiatePrimitive()`'s own
  `m_registry.GetComponent<Transform>(entity)` call - note `SetEntityTrs()`
  below uses `TryGetComponent`, NOT `GetComponent`, because unlike
  `InstantiatePrimitive()` - which just created the entity itself and KNOWS
  it has a `Transform` - `SetEntityTrs()` is handed an arbitrary
  already-live entity by name and must NOT assume it has one) gives direct,
  in-place mutable access.
- `Math/Quat.h`'s `Quat::FromEulerDegrees(pitchX, yawY, rollZ)` takes
  DEGREES and returns a normalized `Quat` - safe to call directly with
  whatever floats a caller supplied, no separate normalization step needed
  for the Euler path. (A raw quaternion input path does not exist anywhere
  in this campaign - see `PHASE0_MASTER_STRATEGY.md`'s Locked Design
  Decision #1 - so there is no "defensively re-normalize an arbitrary
  caller-supplied quaternion" concern to handle here at all.)
- `src/Game/EngineCommandResults.h` is a small, plain-data-only header (no
  Renderer/Registry dependency beyond `Entity.h`) - already `#include`d by
  BOTH `Game.h` (this phase) and `EngineCommandBridge.h` (Phase 3, an
  `Application`-layer file). This phase adds its new REQUEST structs here
  too (not only outcome structs) - see Step 3.1's own rationale for this
  explicit, deliberate departure from `InstantiatePrimitive()`'s own
  flat-parameter-list convention.

## Step 3 — The Plan

### 3.1 — Extend `src/Game/EngineCommandResults.h`

Add `#include "../Math/Vec3.h"` and `#include "../Math/Quat.h"` (this file
currently only includes `../ECS/Entity.h` - both new includes are needed for
the structs below; verify the exact relative include path other files under
`src/Game/` already use for these two headers, e.g. `Game.cpp`'s own
existing includes, before finalizing - `EngineCommandBridge.h` already uses
exactly `"../Math/Vec3.h"` from ITS OWN location, `src/Application/`, one
directory away from `src/Game/` in the same way, so the identical
`"../Math/..."` relative form is very likely correct here too).

Add these four new structs, immediately after the existing
`DeleteEntityOutcome`:

```cpp
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
```

**CRITICAL — CMakeLists.txt registration:** `EngineCommandResults.h` is
already listed in the root `CMakeLists.txt`'s hand-maintained
`target_sources(gte_core PRIVATE ...)` list (added by `network-impl-3`) -
since this phase only ADDS content to an already-registered header (no new
file), **no CMakeLists.txt change is needed for this file at all.**

### 3.2 — Extend `src/Game/Game.h`

Add near `InstantiatePrimitive()`/`DeleteEntityByName()`'s own declarations
(keep the four visually adjacent - they are this campaign's whole public
surface):

```cpp
// network-impl-5 campaign
// (PHASE2_GAME_LEVEL_SET_ENTITY_TRS_AND_INSTANTIATE_LIGHT_APIS.md) - updates
// the LOCAL (parent-relative) Transform of an EXISTING live entity, looked
// up by Name (EntityQuery.h's FindEntityByName() - same lookup
// DeleteEntityByName() already uses). Operates ONLY on
// Transform::position/rotation/scale directly - NEVER a hierarchy-aware
// world-space conversion (ECS/TransformHierarchy.h's ComputeWorldMatrix()/
// ComputeWorldTransform() are never called here) - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #3. For an entity with
// no parent (every entity InstantiatePrimitive()/InstantiateLight() produce,
// unless explicitly parented) local IS world, so this already covers the
// common "move/rotate the thing I just spawned" case exactly.
//
// Each of params.hasTranslation/hasRotationEulerDegrees/hasScale is applied
// INDEPENDENTLY - a call with only hasScale == true leaves
// position/rotation completely untouched, and vice versa. A call with ALL
// THREE false is not an error - it changes nothing and simply returns the
// entity's current transform (see SetEntityTrsOutcome's own doc comment,
// EngineCommandResults.h, and PHASE0's Locked Design Decision #6).
//
// Never throws. Returns success == false (nothing changed) when:
// params.name is empty or does not resolve to any live entity at all
// (SetEntityTrsOutcome::entityNotFound == true in that specific case), OR
// the resolved entity exists but has no Transform component at all
// (entityNotFound stays false in THAT case - see that outcome field's own
// doc comment for why this distinction exists).
SetEntityTrsOutcome SetEntityTrs(const SetEntityTrsParams& params);

// network-impl-5 campaign - spawns a new light entity BY PARAMETERS, the
// light-specific sibling of InstantiatePrimitive() above, built on the same
// spawn machinery CreateDirectionalLightEntity() above already established
// (a Transform + a DirectionalLight component), plus the same additive
// naming/positioning/parenting steps InstantiatePrimitive() itself already
// established (MakeUniqueEntityName()/FindEntityByName()/SetParent(...,
// worldPositionStays = true)). Needs no `Renderer&` parameter, for the
// exact same reason CreateDirectionalLightEntity() itself doesn't - a light
// has nothing to rasterize. (This is also exactly why, unlike
// InstantiatePrimitive(), this method is fully Tier-1-testable - see
// PHASE0_MASTER_STRATEGY.md's own Step 2 note.)
//
// params.lightType (case-insensitive; empty also means "directional") only
// ever recognizes "directional" today - this engine's only implemented
// light component (ECS/Components/DirectionalLight.h). ANY other
// non-empty value fails the whole call (success == false, NO entity
// created) with errorMessage explaining only "directional" is supported -
// see PHASE0's Locked Design Decision #9 for why this field exists at all
// today.
//
// Unlike InstantiatePrimitive()'s "rotation is always identity" behavior,
// this method gives a light with NO explicit rotation
// (params.hasRotationEulerDegrees == false) the SAME "late-afternoon"
// default rotation CreateDirectionalLightEntity() itself uses
// (Quat::FromEulerDegrees(45, -30, 0)) - see PHASE0's Locked Design
// Decision #5 for the rationale, and Game.cpp's own shared
// DefaultDirectionalLightRotation() helper (Step 3.4 below) for how this
// stays a SINGLE literal shared with CreateDirectionalLightEntity(), never
// two independently-drifting copies.
//
// Never throws. Returns success == false (and creates NO entity at all)
// only for an unrecognized params.lightType.
InstantiateLightOutcome InstantiateLight(const InstantiateLightParams& params);
```

### 3.3 — Implement `SetEntityTrs()` in `src/Game/Game.cpp`

```cpp
SetEntityTrsOutcome Game::SetEntityTrs(const SetEntityTrsParams& params)
{
    SetEntityTrsOutcome outcome;

    const Entity entity = FindEntityByName(m_registry, params.name);
    if (entity == kInvalidEntity) {
        outcome.success = false;
        outcome.entityNotFound = true;
        outcome.errorMessage = params.name.empty()
            ? "name must not be empty"
            : ("no live entity found with name '" + params.name + "'");
        return outcome;
    }

    Transform* transform = m_registry.TryGetComponent<Transform>(entity);
    if (transform == nullptr) {
        outcome.success = false;
        outcome.errorMessage = "entity '" + params.name + "' has no Transform component";
        return outcome;
    }

    if (params.hasTranslation) {
        transform->position = params.translation;
        outcome.translationChanged = true;
    }
    if (params.hasRotationEulerDegrees) {
        transform->rotation = Quat::FromEulerDegrees(
            params.rotationEulerDegrees.x, params.rotationEulerDegrees.y, params.rotationEulerDegrees.z);
        outcome.rotationChanged = true;
    }
    if (params.hasScale) {
        transform->scale = params.scale;
        outcome.scaleChanged = true;
    }

    outcome.success = true;
    outcome.entityIndex = entity.index;
    outcome.entityGeneration = entity.generation;
    outcome.resultingPosition = transform->position;
    outcome.resultingRotation = transform->rotation;
    outcome.resultingScale = transform->scale;
    return outcome;
}
```

(Suggested-correct reference implementation, not a copy-paste mandate -
verify `Transform`/`Quat` are already visible in `Game.cpp` via its existing
includes, e.g. `ECS/Components/Transform.h` is already `#include`d there per
`InstantiatePrimitive()`'s own existing code; `Math/Quat.h` may need an
explicit new `#include` if not already transitively available - check
before finalizing.)

### 3.4 — Extract `DefaultDirectionalLightRotation()` + implement `InstantiateLight()` in `src/Game/Game.cpp`

First, the behavior-preserving refactor: add a small, file-local (anonymous
namespace) helper immediately above `Game::CreateDirectionalLightEntity()`'s
existing implementation, then change that existing method's own body to
call it instead of repeating the literal inline:

```cpp
namespace {
// Shared between Game::CreateDirectionalLightEntity() (the Editor's
// "Create Directional Light" menu path) and Game::InstantiateLight() (the
// network-impl-5 campaign's own new path) so the two can never silently
// drift apart - see PHASE0_MASTER_STRATEGY.md's Locked Design Decision #5.
// Late-afternoon-ish sun: pitched down toward the ground plus a bit of
// yaw so it isn't perfectly axis-aligned - purely a sensible visual
// default (see Quat::FromEulerDegrees()'s own "pitch around Right()"
// convention), not physically derived. This is the EXACT SAME literal
// Game::CreateDirectionalLightEntity() already used before this refactor -
// extracting it here must not change that method's own observable
// behavior at all.
Quat DefaultDirectionalLightRotation() noexcept
{
    return Quat::FromEulerDegrees(45.0f, -30.0f, 0.0f);
}
} // namespace

Entity Game::CreateDirectionalLightEntity()
{
    const Entity entity = m_registry.CreateEntity();

    Transform& transform = m_registry.AddComponent<Transform>(entity);
    transform.rotation = DefaultDirectionalLightRotation();

    m_registry.AddComponent<DirectionalLight>(entity);

    const std::string uniqueName = MakeUniqueEntityName(m_registry, "Directional Light");
    m_registry.AddComponent<Name>(entity, Name{ uniqueName });

    return entity;
}
```

Then implement `InstantiateLight()` immediately after it:

```cpp
InstantiateLightOutcome Game::InstantiateLight(const InstantiateLightParams& params)
{
    InstantiateLightOutcome outcome;

    std::string normalizedType = params.lightType;
    std::transform(normalizedType.begin(), normalizedType.end(), normalizedType.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!normalizedType.empty() && normalizedType != "directional") {
        outcome.success = false;
        outcome.errorMessage = "unsupported light_type '" + params.lightType
            + "' - only 'directional' is currently supported";
        return outcome;
    }

    const Entity entity = m_registry.CreateEntity();

    Transform& transform = m_registry.AddComponent<Transform>(entity);
    transform.position = params.worldPosition;
    transform.rotation = params.hasRotationEulerDegrees
        ? Quat::FromEulerDegrees(params.rotationEulerDegrees.x, params.rotationEulerDegrees.y, params.rotationEulerDegrees.z)
        : DefaultDirectionalLightRotation();

    DirectionalLight& light = m_registry.AddComponent<DirectionalLight>(entity);
    light.color = params.color;
    light.illuminanceLux = params.illuminanceLux;
    light.active = params.active;

    const std::string baseName = params.requestedName.empty() ? std::string("Directional Light") : params.requestedName;
    const std::string uniqueName = MakeUniqueEntityName(m_registry, baseName);
    m_registry.AddComponent<Name>(entity, Name{ uniqueName });

    outcome.success = true;
    outcome.entityIndex = entity.index;
    outcome.entityGeneration = entity.generation;
    outcome.resolvedName = uniqueName;

    if (params.hasParent) {
        const Entity parentEntity = FindEntityByName(m_registry, params.parentName);
        if (parentEntity == kInvalidEntity) {
            outcome.parentRequestedButNotFound = true;
            outcome.requestedParentName = params.parentName;
        } else {
            SetParent(m_registry, entity, parentEntity, /*worldPositionStays=*/true);
        }
    }

    return outcome;
}
```

Needs `#include <algorithm>` (for `std::transform`) and `#include <cctype>`
(for `std::tolower`) in `Game.cpp` if not already present - verify against
the file's current includes (`InstantiatePrimitive()`'s own shape-name
handling delegates lower-casing to `TryParsePrimitiveTypeName()` internally,
so `Game.cpp` itself may not currently need either header - check before
assuming they're already there).

### 3.5 — Testability note (be explicit about this in the completion report)

Unlike `InstantiatePrimitive()` (Tier 2 - touches a live `Renderer`/GPU mesh
cache via `CreatePrimitiveEntity()`), **both new methods in this phase are
fully Tier-1-testable** - `SetEntityTrs()` touches only `Registry`/
`Transform` (exactly like `DeleteEntityByName()` already is), and
`InstantiateLight()` touches only `Registry`/`Transform`/`DirectionalLight`/
`Name` - no `Renderer&` parameter, no GPU mesh/pipeline cache, nothing
`CreatePrimitiveEntity()`-shaped at all. State this explicitly in the
completion report as a positive, worth-noting difference from
`InstantiatePrimitive()`'s own accepted Tier-2 gap - it means THIS phase's
own dedicated unit tests (see Verification below) fully exercise the real
production logic, not a stand-in.

## Verification for this phase

- Fast compile check.
- New test cases in `tests/Game/GameEntityCommandsTests.cpp` (existing file
  - extend it, do not create a new one) covering, at minimum, for
  `SetEntityTrs()`: changing only translation (rotation/scale
  untouched, correct `*Changed` flags); changing all three at once; a no-op
  call (name only, nothing else) still returning `success == true` with the
  entity's actual current transform echoed back; a name that resolves to no
  live entity (`entityNotFound == true`); an entity that exists but has no
  Transform component (`entityNotFound == false`, `success == false`,
  distinct error message). For `InstantiateLight()`: a fully-default call
  (empty `lightType`, no rotation) produces the SAME rotation
  `CreateDirectionalLightEntity()` itself would (a direct, worked
  cross-check between the two methods' own rotations, proving the shared
  helper actually is shared); an explicit `rotation_euler_degrees` overrides
  that default; an unrecognized `lightType` fails with NO entity created
  (assert `registry.AliveEntityCount()` is unchanged before/after); a
  `parent` name that doesn't resolve sets `parentRequestedButNotFound` but
  still reports `success == true`; color/illuminance/active values round-trip
  onto the spawned entity's own `DirectionalLight` component correctly (read
  it back via `registry.GetComponent<DirectionalLight>()` after the call).
  Also add (or confirm/extend) a direct test proving
  `CreateDirectionalLightEntity()`'s own OWN observable behavior is
  completely unchanged after the Step 3.4 refactor (same default rotation,
  same name, same component set) - this is the one regression this phase
  must be paranoid about, since it edits an already-shipped, already-used
  method's implementation.
- Write `PHASE2_COMPLETION_REPORT.md`, `git add`/`git commit`.
