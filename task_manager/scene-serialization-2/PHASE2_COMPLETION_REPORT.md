# PHASE2 COMPLETION REPORT — Register the Engine's Real Components Into the Reflection System

_Campaign: `task_manager/scene-serialization-2/`. Parent: `PHASE0_MASTER_STRATEGY.md`.
Phase file implemented: `PHASE2_BUILTIN_COMPONENT_REFLECTION_REGISTRATION.md`._

Branch: `feature/scene-serialization` (unchanged, as required).

## Prerequisites followed

- Read `README.md` and `AGENTS.md` at the project root.
- Re-read `PHASE0_MASTER_STRATEGY.md` in full for overall campaign context.
- Read `PHASE1_COMPLETION_REPORT.md` (the only prior phase report in this
  folder) before starting — it confirmed Phase 1's reflection primitives
  (`ComponentTypeDescriptor.h`, `ComponentTypeRegistry.h/.inl/.cpp`,
  `ReflectFieldMacros.h`, `MathJsonAdapters.h`) all exist, compile, and pass
  their own 9 Tier-1 tests, with zero prior behavior change and no hint of
  anything deliberately left incomplete for this phase to pick up beyond
  what `PHASE2_BUILTIN_COMPONENT_REFLECTION_REGISTRATION.md` itself already
  describes.
- Read `PHASE2_BUILTIN_COMPONENT_REFLECTION_REGISTRATION.md` in full before
  writing any code.

## What was built

### 3.1 — Self-bootstrapping `ComponentTypeRegistry::Instance()`

`src/ECS/Reflection/ComponentTypeRegistry.cpp` — `Instance()` now
forward-declares `void RegisterBuiltinComponentReflections();` (defined only
in the new `BuiltinComponentReflection.cpp`, never included via a header, so
`ComponentTypeRegistry.h/.cpp` themselves stay free of any `#include` on a
real `ECS/Components/*.h` file) and calls it exactly once, the first time
`Instance()` is ever invoked by anything, anywhere in the process. Set
`bootstrapped = true` **before** calling
`RegisterBuiltinComponentReflections()` (not after), exactly as the phase
file requires — `RegisterComponentType<T>()`
(`ComponentTypeRegistry.inl`) calls `Instance()` again internally for every
single component type it registers, and that nested re-entrant call must see
`bootstrapped == true` already or this would recurse forever. Confirmed by
running the actual tests (below) with no stack overflow/infinite loop and
every real component correctly appearing in the table.

### 3.2 — New file: `src/ECS/Reflection/BuiltinComponentReflection.cpp`

Registers exactly the 5 real component types the phase file lists, with the
exact field lists specified — every field name/type was re-verified against
the real headers before writing this file (not assumed from the strategy
doc's own summary), per the phase file's own explicit instruction:

- **`Transform`** (`src/ECS/Components/Transform.h`) — `position: Vec3`,
  `rotation: Quat`, `scale: Vec3`. Confirmed `parent: Entity` and
  `siblingIndex: std::uint32_t` are NOT reflected (captured separately at the
  SceneDocument entity-record level in a later phase, per PHASE0 Appendix A).
- **`Name`** (`src/ECS/Components/Name.h`) — `value: std::string`.
- **`Camera`** (`src/ECS/Components/Camera.h`) — `fovYDegrees: float`,
  `nearZ: float`, `farZ: float`, `active: bool`. This is what fixes the
  reported "Camera near/far never serialized" gap.
- **`DirectionalLight`** (`src/ECS/Components/DirectionalLight.h`) —
  `color: Vec3`, `illuminanceLux: float`, `active: bool`.
- **`PrimitiveSource`** (`src/ECS/Components/PrimitiveSource.h`) —
  `type: PrimitiveType`, registered via `GTE_REFLECT_ENUM_FIELD` against
  `Renderer/Primitives/PrimitiveMeshGenerator.h`'s own, already-existing
  `ToString(PrimitiveType)`/`TryParsePrimitiveTypeName()` (confirmed the
  exact signatures in that header before use) — no second, parallel string
  mapping introduced.

The file's own comments write out, in full, the exact per-type reasoning for
every component deliberately NOT registered (`MeshAssetSource`,
`MeshRenderer`, `SkeletalAnimator`, `DynamicChainRig`,
`ResolvedAnimationPose`) — matching the phase file's own Section 3.3 text
essentially verbatim, as instructed ("write this reasoning into the file's
own comments... not just this strategy doc").

### 3.4 — CMake wiring

- Root `CMakeLists.txt`: added
  `src/ECS/Reflection/BuiltinComponentReflection.cpp` immediately after the
  existing Phase 1 `ECS/Reflection/*` block inside `gte_core`'s source list.
- `tests/CMakeLists.txt`: added
  `ECS/Reflection/BuiltinComponentReflectionTests.cpp` immediately after
  Phase 1's own `ECS/Reflection/ComponentTypeRegistryTests.cpp` entry.

### 3.5 — Tests: `tests/ECS/Reflection/BuiltinComponentReflectionTests.cpp`

9 new `TEST()` cases, all passing:

1. `Find("Transform")` returns non-null with exactly 3 fields
   (`position`/`rotation`/`scale`), and explicitly confirms `parent`/
   `siblingIndex` are NOT present.
2. `Find("Name")` returns non-null with exactly 1 field (`value`).
3. `Find("Camera")` returns non-null with exactly 4 fields (`fovYDegrees`/
   `nearZ`/`farZ`/`active`), looked up by name (never assuming index order).
4. `Find("DirectionalLight")` returns non-null with exactly 3 fields
   (`color`/`illuminanceLux`/`active`).
5. `Find("PrimitiveSource")` returns non-null with exactly 1 field (`type`).
6. **Regression guard**: `Find("MeshRenderer")`, `Find("MeshAssetSource")`,
   `Find("SkeletalAnimator")`, `Find("DynamicChainRig")`, and
   `Find("ResolvedAnimationPose")` all return `nullptr`.
7. A real round trip against a real `Registry`: create an entity, add a
   `Camera` with non-default `fovYDegrees`/`nearZ`/`farZ`/`active`, serialize
   via `Find("Camera")`'s `tryGetConstComponent` + every field's own
   `writeJson`, apply that JSON back onto a FRESH entity's fresh, default
   `Camera` via `ensureDefaultComponent` + every field's own `readJson`, and
   confirm every field matches — exactly the mechanism Phase 3/4's generic
   Save/Load will use, proven here in isolation first, per the phase file's
   own instruction.
8. `PrimitiveSource`'s enum field round-trips correctly for every
   `PrimitiveType` value (`Cube`/`Sphere`/`Capsule`/`Cone`/`Plane`).
9. An unrecognized JSON string (`"NotAShape"`) makes `readJson` return
   `false` with a non-empty error message, confirmed via `EXPECT_NO_THROW`,
   and confirmed the component's `type` field is completely UNCHANGED after
   the failed call (never a partial/corrupt write).

## Verification

- Re-ran `cmake --build build --target GreatTamanaEngineTests` (fast compile
  check, per this phase's own workflow rule — no full build/regression) —
  succeeded with no compiler errors or warnings from any new code (the only
  build-log warning is the pre-existing, unrelated KTX git-describe
  "0.0.0-noversion" fallback warning, not caused by this change).
- Ran the new tests directly:
  `tests\GreatTamanaEngineTests.exe --gtest_filter=BuiltinComponentReflectionTest.*:ComponentTypeRegistryTest.*`
  — **18/18 passed** (9 new `BuiltinComponentReflectionTest` cases plus all 9
  pre-existing Phase 1 `ComponentTypeRegistryTest` cases, confirming the
  self-bootstrapping `Instance()` change didn't regress Phase 1's own
  coverage).
- `git status` confirms only the intended files changed:
  `CMakeLists.txt`, `tests/CMakeLists.txt`,
  `src/ECS/Reflection/ComponentTypeRegistry.cpp` (modified), plus two new
  files (`src/ECS/Reflection/BuiltinComponentReflection.cpp`,
  `tests/ECS/Reflection/BuiltinComponentReflectionTests.cpp`) — **zero**
  changes under `src/Scene/`, `src/Editor/`, or `src/Network/`, exactly as
  this phase's own Definition of Done requires.
- Per this phase's own rule and the campaign-wide workflow rule, did **not**
  run a full build or the full `ctest` regression suite (reserved for
  Phase 6).

## Definition of Done — checked against the phase file

- [x] `BuiltinComponentReflection.cpp` exists, registers exactly the 5 types
      listed in 3.2, with the exact field lists shown (verified against the
      real headers, not assumed).
- [x] `ComponentTypeRegistry::Instance()` self-bootstraps exactly once, with
      no reliance on cross-TU static-init order.
- [x] The "explicitly out of scope" reasoning (3.3) is written as actual
      code comments in `BuiltinComponentReflection.cpp`, not only in the
      strategy doc.
- [x] `tests/ECS/Reflection/BuiltinComponentReflectionTests.cpp` exists, is
      registered in `tests/CMakeLists.txt`, and passes (9/9) — including the
      "these five types are NOT registered" regression guard.
- [x] Zero changes to `src/Scene/`, `src/Editor/`, `src/Network/`.
- [x] Fast compile check passes.
- [x] This report written; `git add`/`git commit` follow next.

## Notes / discrepancies for later phases

None found. Everything in this phase's own `.md` file matched the real
codebase exactly as described — every field name/type on
`Transform`/`Name`/`Camera`/`DirectionalLight`/`PrimitiveSource`, the
`PrimitiveMeshGenerator.h` `ToString`/`TryParsePrimitiveTypeName` signatures,
and every field on the 5 deliberately-excluded components
(`MeshAssetSource`/`MeshRenderer`/`SkeletalAnimator`/`DynamicChainRig`/
`ResolvedAnimationPose`) were all re-confirmed by reading the actual header
files before writing any code, and all matched this phase's own text
exactly. No discrepancy to flag against `PHASE0_MASTER_STRATEGY.md` or any
later phase file this time.
