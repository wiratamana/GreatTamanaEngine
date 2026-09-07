# PHASE1 — Rigid Body Wireframe Geometry Module (`src/Editor/RigidBodyWireframe.h/.cpp`) — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit — no code anywhere turns a
`RigidBody`'s shape/size/rotation into real 3D geometry). Implements
`PHASE1_RIGID_BODY_WIREFRAME_GEOMETRY_MODULE.md` in full — no prior phase in
this campaign existed yet (this is the campaign's first executed phase).

## What was done

Added a new, pure, ImGui/Vulkan-free module answering exactly the question
Phase 0 identified as missing: "what are the actual 3D line segments that
trace this rigid body's real outline, positioned/oriented exactly like the
real physics body?"

- **`src/Editor/RigidBodyWireframe.h`** — new file. Declares `WireframeSegment`
  (a plain `{Vec3 a, b;}` pair) and
  `BuildRigidBodyWireframe(RigidBodyShape shape, const Vec3& shapeSize, const
  Vec3& translate, const Vec3& rotateRadians)`, exactly per the phase
  document's 3.1 (verbatim, including its full doc comment explaining the
  PMX Euler-order convention and the degenerate-size-handling contract).
- **`src/Editor/RigidBodyWireframe.cpp`** — new file, implementing all three
  shapes per the phase document's 3.2 (verbatim):
  - **Sphere** — 3 mutually-perpendicular great circles (XY/YZ/XZ planes),
    radius `shapeSize.x`, empty when radius `<= kEpsilon`.
  - **Box** — 12 oriented edges derived from 8 corners indexed by 3 bits
    (never a hand-written literal edge list, so it can't drift out of sync
    with the corner-indexing convention), half-extents `shapeSize.x/y/z`,
    empty only when ALL three half-extents are non-positive (a
    flattened-on-one-axis box still draws its real flat outline).
  - **Capsule** — 2 rings + 4 vertical sides + 4 hemisphere arcs (2 per end
    cap, one in the local XY plane, one in ZY), radius `shapeSize.x`,
    cylinder length `shapeSize.y` (capsule axis along local +Y, matching
    `PhysicsData.h`'s own doc comment), empty only when radius `<=
    kEpsilon` — a non-positive length degrades to a "pure sphere" capsule
    (two coincident hemispheres) rather than emptying the result.
  - `RotationFromPmxEuler()` calls the existing
    `Quat::FromEulerDegrees(RadToDeg(x), RadToDeg(y), RadToDeg(z))` exactly
    as Phase 0's Culprit #3 established as already correct — no new
    rotation-order math was invented.
  - Two small shared helpers, `AppendClosedLoop()`/`AppendOpenArc()`
    (function templates over a per-index local-point lambda), avoid
    duplicating the "sample N points, connect them into segments" loop
    across the sphere's 3 circles and the capsule's 2 rings/4 arcs.
- **`CMakeLists.txt`** (root) — added `src/Editor/RigidBodyWireframe.h`/`.cpp`
  to the existing `if(GTE_ENABLE_PROJECT_PANEL)` `target_sources(gte_core
  PRIVATE ...)` block, right alongside `ModelRigCache.h/.cpp`/
  `BoneViewerWindow.h/.cpp`, per 3.3.
- **`tests/Editor/RigidBodyWireframeTests.cpp`** — new file, added to the
  same `GTE_ENABLE_PROJECT_PANEL` test block in `tests/CMakeLists.txt`
  (right after `Editor/ModelRigCacheTests.cpp`), per 3.4/3.5. 8 test cases
  covering: degenerate-size empty-vector behavior for all three shapes,
  exact segment counts, per-shape geometric invariants (sphere points at
  exact radius from `translate`; box edge lengths matching half-extents;
  capsule ring/arc points within the capsule's own radius of its local Y
  axis), and a rotation+translation correctness check for Box.

## One deviation from the phase document's own draft test — a bug found and fixed

The phase document's own draft `RigidBodyWireframeTests.cpp` (Step 3.4)
included `BoxAppliesRotationAndTranslation`, asserting that some rotated
box corner would satisfy BOTH `|world.x - translate.x| < 0.01` AND
`||world.z| - 1.0| < 0.01` simultaneously. Running this verbatim test
against the freshly-compiled implementation failed
(`foundCornerNearWorldZAxis` stayed `false`) — independently re-derived via
Rodrigues' rotation formula (a 90-degree yaw around Y sends local `(sx*1,
sy*1, sz*3)` to `(sz*3, sy*1, -sx*1)`, i.e. world X offset is always `±3`
and world Z offset is always `±1` for EVERY corner of this specific
`shapeSize(1,1,3)` box), the two conditions the draft test combined can
**never** both hold for a box whose Z half-extent is 3 (not 0) — this was a
logic bug in the phase document's own draft assertion, not a bug in
`BuildRigidBodyWireframe()` itself (every OTHER assertion in that same test,
including the per-corner diagonal-distance check, already passed).

Fixed by replacing the flawed combined per-corner check with a correct,
equivalent invariant: track the MAX `|world.x - translate.x|` and MAX
`|world.z - translate.z|` across every corner, and assert they land at
`3.0`/`1.0` respectively — directly proving the local X/Z half-extents
swapped which world axis they span after the 90-degree yaw, the exact
property the original test intended to check. `#include <algorithm>` was
added for `std::max`. This is the ONLY deviation from the phase document's
literal draft in this session — every other file/line matches it verbatim.

## Verification

- **Fast compile check** (per this campaign's workflow rules): `cmake -S .
  -B build` (reconfigure, picked up the two new CMakeLists.txt source
  additions) → `cmake --build build --target gte_core` — succeeded, one
  file compiled (`RigidBodyWireframe.cpp`), `libgte_core.a` relinked
  cleanly, zero warnings/errors.
- **Test build + run**: `cmake --build build --target
  GreatTamanaEngineTests` — succeeded. Ran
  `GreatTamanaEngineTests.exe --gtest_filter=RigidBodyWireframeTest.*` — all
  8 new tests pass.
- **Full existing suite** (extra verification beyond this task's minimum
  "fast compile check" requirement, run to confirm no regression): 835
  tests from 108 suites, 832 passed, 1 pre-existing machine-gated skip
  (`PmxLoaderRealModelSmokeTest` — MMD test model directory not present on
  this machine, expected). **2 pre-existing, unrelated failures** were
  observed: `RenderSystemTest.EntityWithParentUsesComposedWorldMatrix` and
  `RenderSystemTest.ResolveActiveCameraViewProjectionFollowsParentTransform`
  — confirmed via `git status` that this session never touched
  `RenderSystem`/`Transform`/any ECS file (only `RigidBodyWireframe.h/.cpp`,
  `RigidBodyWireframeTests.cpp`, and the two `CMakeLists.txt` files were
  added/modified), so these two failures pre-exist on this branch,
  unrelated to this phase's own change, and are explicitly out of scope for
  this campaign (see `PHASE0_MASTER_STRATEGY.md`'s "What We Will NOT Do" —
  this campaign touches only the Rigid Body wireframe/gizmo). Left
  unaddressed, flagged here for whoever next works on `RenderSystem`/
  `Transform` parent-hierarchy code.

## What was NOT touched (per the phase document's own "What We Will NOT Do")

`src/Editor/BoneViewerWindow.h/.cpp` was not touched at all in this phase —
Phase 2 owns wiring `BuildRigidBodyWireframe()` into the viewport overlay.
The three per-shape builders (`BuildSphereWireframe()`/`BuildBoxWireframe()`/
`BuildCapsuleWireframe()`) stay anonymous-namespace implementation details,
not exposed in the header. No segment/arc resolution constant was made
runtime-configurable. `RigidBodyMotionType` is untouched/unread by this
module.

## Campaign status

This completes Phase 1 of `verlet-integration-3`
(`PHASE0_MASTER_STRATEGY.md`). Phase 2
(`PHASE2_BONE_VIEWER_SELECT_TO_REVEAL_WIREFRAME_INTEGRATION.md`) can now
proceed — `BuildRigidBodyWireframe()` exists, compiles, and is fully
Tier-1-tested, ready for `BoneViewerWindow::Build()` to call it directly for
whichever ONE rigid body is currently `Selection`-selected.
