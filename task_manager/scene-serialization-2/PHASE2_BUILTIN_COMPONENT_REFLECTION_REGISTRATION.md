# PHASE2 — Register the Engine's Real Components Into the Reflection System

_Part of `task_manager/scene-serialization-2/`. **Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.**_
Depends on: `PHASE1_REFLECTION_CORE_AND_MATH_JSON_ADAPTERS.md` (must be done first).
Branch: `feature/scene-serialization`.

## Step 1: The Goal

Use Phase 1's now-existing, generic, test-proven reflection primitives
(`ComponentTypeRegistry`/`RegisterComponentType<T>()`/`GTE_REFLECT_FIELD`) to
register the REAL ECS components this campaign needs — `Transform`, `Name`,
`Camera`, `DirectionalLight`, `PrimitiveSource` — and to explicitly, on the
record, decide NOT to register several others. This directly fixes reported
gap #2 (`Camera::nearZ`/`farZ` become reflectable here) and lays the exact
groundwork Phase 3/4 need for gap #1 (Transform for every entity). Still zero
changes to `src/Scene/`/`src/Editor/`/`src/Network/` — this phase only
answers "which components CAN be serialized", not "how a scene file uses
that".

## Step 2: The Situation / The Problem

Six real components exist under `src/ECS/Components/`:
`Camera.h`, `DirectionalLight.h`, `DynamicChainRig.h`, `MeshAssetSource.h`,
`MeshRenderer.h`, `Name.h`, `PrimitiveSource.h`, `ResolvedAnimationPose.h`,
`SkeletalAnimator.h`, `Transform.h` (ten total, once `Transform`, which lives
directly under `ECS/Components/`, is counted). Each was already read in full
before writing `PHASE0_MASTER_STRATEGY.md` — the exact field lists below are
taken directly from that reading, not guessed.

A registration call site is needed that runs EXACTLY ONCE, before the first
time anything reads `ComponentTypeRegistry::Instance()` for real (Phase 3's
Save/Load code) — but with NO reliance on static-initialization order across
translation units (a classic C++ footgun: the relative order two different
`.cpp` files' global/namespace-scope objects get constructed in is
UNSPECIFIED across TUs). The safest, simplest fix: make registration
LAZY, triggered the first time `ComponentTypeRegistry::Instance()` itself is
ever called — this needs no external call site anywhere in `Game`/
`Application` at all.

## Step 3: The Plan

### 3.1 — `ComponentTypeRegistry::Instance()` bootstraps itself, once

In `src/ECS/Reflection/ComponentTypeRegistry.cpp`, change `Instance()` from a
bare Meyer's singleton into a SELF-BOOTSTRAPPING one:

```cpp
// Forward-declared here, DEFINED in BuiltinComponentReflection.cpp (this
// phase, Section 3.2 below) - deliberately NOT included via a header, to
// keep ComponentTypeRegistry.h/.cpp themselves free of any #include on a
// real ECS/Components/*.h file (Phase 1's own module stays component-type-
// agnostic - only THIS one bootstrap call site knows about real components).
void RegisterBuiltinComponentReflections();

ComponentTypeRegistry& ComponentTypeRegistry::Instance()
{
    static ComponentTypeRegistry instance;
    static bool bootstrapped = false;
    if (!bootstrapped) {
        bootstrapped = true;
        RegisterBuiltinComponentReflections();
    }
    return instance;
}
```

This is safe/idempotent because `RegisterBuiltinComponentReflections()`
itself calls `RegisterComponentType<T>()`, which calls
`ComponentTypeRegistry::Instance()` again — but `bootstrapped` is already
`true` by the time that nested call happens (it was set BEFORE calling
`RegisterBuiltinComponentReflections()`), so there is no infinite recursion,
and the nested call correctly sees (and inserts into) the same, already-
under-construction `instance`. Double-check this exact ordering when
implementing — `bootstrapped = true` MUST be set before the call, not after.

### 3.2 — New file: `src/ECS/Reflection/BuiltinComponentReflection.cpp`

This is the ONE place that bridges Phase 1's component-agnostic reflection
core with this engine's real `ECS/Components/*.h` types. A future component
type is expected to add its OWN registration call here (or, if a future
contributor prefers, in its own similarly-shaped `.cpp` file that this file's
`RegisterBuiltinComponentReflections()` calls out to — either is fine, this
phase does not need to force one specific file-per-component convention, see
PHASE0's Locked Design Decision #3 for why "one obvious place to add ~10
lines" is what actually matters, not a specific file layout).

```cpp
#include "ComponentTypeRegistry.h"
#include "ReflectFieldMacros.h"
#include "MathJsonAdapters.h"

#include "../Components/Camera.h"
#include "../Components/DirectionalLight.h"
#include "../Components/Name.h"
#include "../Components/PrimitiveSource.h"
#include "../Components/Transform.h"
#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h" // ToString(PrimitiveType)/TryParsePrimitiveTypeName()

namespace gte {

void RegisterBuiltinComponentReflections()
{
    // --- Transform (ECS/Components/Transform.h) ---
    // Deliberately reflects ONLY position/rotation/scale - NEVER `parent`
    // (an Entity handle, meaningless across a save/load round trip - see
    // PHASE3/PHASE4 for how hierarchy is instead captured at the
    // SceneDocument level via each entity's own array-index-based "parent"
    // field) and NEVER `siblingIndex` (also captured separately, at the
    // SceneDocument entity-record level - see PHASE0's Appendix A - so it
    // sits alongside "parent", not inside the generic "components" bag,
    // keeping ordering/hierarchy concerns visually distinct from plain
    // component data in the saved JSON).
    RegisterComponentType<Transform>("Transform", {
        GTE_REFLECT_FIELD(Transform, position),
        GTE_REFLECT_FIELD(Transform, rotation),
        GTE_REFLECT_FIELD(Transform, scale),
    });

    // --- Name (ECS/Components/Name.h) ---
    RegisterComponentType<Name>("Name", {
        GTE_REFLECT_FIELD(Name, value),
    });

    // --- Camera (ECS/Components/Camera.h) ---
    // THIS is what fixes the reported "camera near-far value" gap.
    RegisterComponentType<Camera>("Camera", {
        GTE_REFLECT_FIELD(Camera, fovYDegrees),
        GTE_REFLECT_FIELD(Camera, nearZ),
        GTE_REFLECT_FIELD(Camera, farZ),
        GTE_REFLECT_FIELD(Camera, active),
    });

    // --- DirectionalLight (ECS/Components/DirectionalLight.h) ---
    RegisterComponentType<DirectionalLight>("DirectionalLight", {
        GTE_REFLECT_FIELD(DirectionalLight, color), // Vec3 - needs MathJsonAdapters.h, already included above.
        GTE_REFLECT_FIELD(DirectionalLight, illuminanceLux),
        GTE_REFLECT_FIELD(DirectionalLight, active),
    });

    // --- PrimitiveSource (ECS/Components/PrimitiveSource.h) ---
    // Enum field - uses PrimitiveMeshGenerator.h's OWN, already-existing
    // ToString(PrimitiveType)/TryParsePrimitiveTypeName() - never a second,
    // parallel string mapping.
    RegisterComponentType<PrimitiveSource>("PrimitiveSource", {
        GTE_REFLECT_ENUM_FIELD(PrimitiveSource, type, ToString, TryParsePrimitiveTypeName),
    });

    // --- Deliberately NOT registered here - see this phase's own Step 2/3.3
    // for the full, per-type reasoning already written down so a future
    // contributor does not "helpfully" add one of these back in without
    // re-reading why it was excluded:
    //   MeshAssetSource   - special-cased via the "asset_guid" entity-level
    //                       field instead (PHASE3/PHASE4) - see 3.3 below.
    //   MeshRenderer      - holds live GPU resource handles (MeshHandle/
    //                       PipelineHandle/TextureHandle) - session-local,
    //                       NEVER meaningful across a save/load round trip
    //                       (PHASE0's own Cross-Phase Invariant).
    //   SkeletalAnimator, DynamicChainRig, ResolvedAnimationPose - see 3.3.
}

} // namespace gte
```

Verify every field NAME/TYPE against the actual header before writing this
file (`Camera::fovYDegrees`/`nearZ`/`farZ`/`active` are all `float`/`float`/
`float`/`bool`; `DirectionalLight::color`/`illuminanceLux`/`active` are
`Vec3`/`float`/`bool`; `PrimitiveSource::type` is `PrimitiveType`;
`Transform::position`/`rotation`/`scale` are `Vec3`/`Quat`/`Vec3`;
`Name::value` is `std::string`) — do not trust this document's own summary
blindly, re-open each header file and confirm.

### 3.3 — Explicitly out of scope (write this reasoning into the file's own comments, as shown above, not just this strategy doc)

- **`MeshAssetSource` (`gtaPath: std::string`)** — NOT registered generically.
  A raw filesystem path is not a stable enough reference across machines/
  after a project is moved (this is exactly why `scene-serialization-1`
  already resolves it through `AssetDatabase` to a stable `Guid` instead —
  see `Scene/SceneBuilder.h`'s own existing doc comment). This campaign
  PRESERVES that Guid-resolution behavior, just moved to the SceneDocument's
  own `asset_guid` entity-level field (PHASE0's Appendix A) rather than
  inside the generic `"components"` bag — Phase 3/4 implement the actual
  save/load logic for this; this phase only needs to NOT accidentally
  register `MeshAssetSource` as a plain reflected component (which would
  serialize a raw, unresolved, potentially-stale path with no Guid
  indirection at all).
- **`MeshRenderer` (`mesh: MeshHandle, pipeline: PipelineHandle, texture:
  TextureHandle`)** — NOT registered. Every field is a live, session-local,
  opaque GPU-resource-pool index (`Renderer/ResourcePool.h`) — serializing
  one and reading it back in a LATER session would silently reference
  whatever unrelated resource happens to occupy that pool slot THIS time,
  a serious, silent-corruption-class bug. `MeshRenderer` is always rebuilt
  fresh by re-running `Game::CreatePrimitiveEntity()`/
  `CreateMeshEntityFromGtaFile()` (Phase 4), never restored from saved data.
- **`SkeletalAnimator` (`meshGtaPath, animationGtaPath: std::string;
  frame, speed: float; playing, loop: bool`)** — technically ALL plain data
  (no handles) and COULD be safely field-reflected in principle. Deliberately
  deferred to a FUTURE campaign anyway, for a narrower reason: correctly
  restoring it also needs re-running `Game::PlayAnimationOnEntity()`'s own
  cache-registration side effects (`AnimationSystem::Play()` — bone-name
  resolution against the model's skeleton), not just a field copy — the
  exact same "needs a spawn-time helper re-run, not a raw field copy" shape
  `MeshAssetSource ` needs, just for a component this specific campaign's
  reported requirements (Transform + Camera near/far) never asked for. Note
  this explicitly in `TODO.md` (Phase 6) as a well-scoped future follow-up,
  not a silent gap.
- **`DynamicChainRig` (`chainStates: std::vector<DynamicChainRuntimeState>`,
  `accumulatedSeconds: float`, ...)** — NOT registered. This is live physics
  SIMULATION state (particle positions mid-verlet-integration) — restoring
  stale simulation state from a save file would look like a physics glitch
  on the very next frame, not a feature. A freshly loaded scene should
  always start this component's physics from its own natural
  rest/bind-pose recomputation, exactly like a freshly SPAWNED (not loaded)
  model already does today.
- **`ResolvedAnimationPose` (`pose: std::vector<BoneLocalOffset>`)** — NOT
  registered. Purely DERIVED, per-frame-recomputed data (see its own doc
  comment: "Written EXCLUSIVELY by `AnimationSystem::EvaluatePoses()`,
  always OVERWRITING wholesale") — there is nothing here that is ever
  meaningful to persist; it is entirely recomputed from
  `SkeletalAnimator`/the model's own skeleton every single frame regardless.

### 3.4 — CMakeLists.txt wiring

Add `ECS/Reflection/BuiltinComponentReflection.cpp` to the same `gte_core`
source list Phase 1's new files were added to.

### 3.5 — Tests: `tests/ECS/Reflection/BuiltinComponentReflectionTests.cpp`

New Tier-1 test file (added to `tests/CMakeLists.txt`) covering, per
`AGENTS.md`'s Testability rule:

- `ComponentTypeRegistry::Instance().Find("Transform")` (and `"Name"`,
  `"Camera"`, `"DirectionalLight"`, `"PrimitiveSource"`) all return
  non-null, with the exact field-name list expected for each (e.g.
  `Find("Camera")->fields` has exactly 4 entries named `fovYDegrees`,
  `nearZ`, `farZ`, `active`, in any order — assert by name lookup within
  the vector, never assume a fixed index order).
- A REAL round trip against a REAL `Registry`: create an entity, add a
  `Camera` with non-default `nearZ`/`farZ` values, serialize via
  `Find("Camera")`'s `tryGetConstComponent` + every field's `writeJson`,
  then apply that same JSON back onto a FRESH entity's fresh, default
  `Camera` via `ensureDefaultComponent` + every field's `readJson`, and
  assert the fresh entity's `nearZ`/`farZ` now match the original — this is
  the exact mechanism Phase 3/4's generic Save/Load will use, proven here
  in isolation first.
- `PrimitiveSource`'s enum field round-trips correctly for EVERY
  `PrimitiveType` value (`Cube`/`Sphere`/`Capsule`/`Cone`/`Plane`), and an
  unrecognized JSON string (e.g. `"NotAShape"`) makes `readJson` return
  `false` with a non-empty error, WITHOUT mutating the component's
  `type` field at all (assert it still holds whatever value it had before
  the failed `readJson` call — a partial/corrupt write is never acceptable).
- `ComponentTypeRegistry::Instance().Find("MeshRenderer")` (and
  `"MeshAssetSource"`, `"SkeletalAnimator"`, `"DynamicChainRig"`,
  `"ResolvedAnimationPose"`) all return `nullptr` — a REGRESSION GUARD so a
  future accidental registration of a handle-bearing/derived/runtime
  component is caught immediately by a broken test, not discovered later as
  a silent corruption bug in the field.

## Definition of Done

- [ ] `BuiltinComponentReflection.cpp` exists, registers exactly the 5 types
      listed in 3.2, with the exact field lists shown (verified against the
      real headers, not assumed).
- [ ] `ComponentTypeRegistry::Instance()` self-bootstraps exactly once, with
      no reliance on cross-TU static-init order.
- [ ] The "explicitly out of scope" reasoning (3.3) is written as actual
      code comments in `BuiltinComponentReflection.cpp`, not only in this
      strategy doc.
- [ ] `tests/ECS/Reflection/BuiltinComponentReflectionTests.cpp` exists, is
      registered in `tests/CMakeLists.txt`, and passes — including the
      "these five types are NOT registered" regression guard.
- [ ] Zero changes to `src/Scene/`, `src/Editor/`, `src/Network/`.
- [ ] Fast compile check passes.
- [ ] Write `PHASE2_COMPLETION_REPORT.md`, then `git add`/`git commit`.
