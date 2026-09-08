# PHASE0_MASTER_STRATEGY (v2) — PMX-Accurate Rigid-Body-Driven Collision for the Verlet Dynamic-Chain Solver

campaign folder: `task_manager/verlet-integration-10/`
orchestrates: PHASE1, PHASE2, PHASE3, PHASE4, PHASE5 (all in this same folder)

This document is the **orchestrator**. It does not itself contain
implementation steps — each child phase file is a fully self-contained,
implementable work chunk. An AI programmer should read this file first for
the big picture, then execute PHASE1 → PHASE5 **in strict numeric order**
(each phase's code depends on the previous phase's code already existing and
compiling).

This campaign directly answers the user's own request, verbatim:

> "right now collision only occurs against static rigid bodies — please make
> it follow the pmx defined rigid body — like skirt/hair, i believe still
> using sphere collider? if yes, please follow pmx defined rigid body shape —
> and use pmx defined rigid body layer rule with collision map — also by
> default, turn collision to ON."

**This is v2 of this document.** v1 (2026-09-08, first pass) was independently
re-verified line-by-line against the live source tree as a "second iteration"
quality pass, on the same day. v1's own overall plan/architecture was found to
be sound and its factual investigation was overwhelmingly accurate — but the
re-verification pass found two genuine, concrete defects (one of which is a
guaranteed **compile error**, not a style nit, and one of which is a genuine
**undefined-behavior** risk) plus several smaller precision/documentation
gaps, all fixed in this revision. See "Revision Notes (v2)" at the very
bottom of this document for the full, itemized list — every child phase file
below has already been updated in place to match; this section exists so a
reader who already knows v1 can jump straight to what changed, without
re-reading every phase end to end.

---

## Step 1 — The Goal (Where are we going?)

Today (confirmed by direct source inspection, 2026-09-08, of the prior
`task_manager/verlet-integration-9` campaign's own finished work — the
campaign that first added Box/Capsule collision support), a Verlet-simulated
hair/skirt joint already collides correctly against **any** PMX `Static`
rigid-body shape (Sphere, Box, Capsule) belonging to its own model — but three
concrete gaps remain, exactly matching the user's three complaints:

1. **"Still using sphere collider?" — for the MOVING joint itself, effectively
   yes: a joint particle is a zero-radius mathematical point today, no matter
   what shape/size its OWN PMX Dynamic rigid body (the jiggle-bone's own
   authored collision volume, e.g. a hair strand's own small sphere/capsule)
   actually declares.** That per-joint shape/size data is parsed and stored
   (`Assets/PhysicsData.h::RigidBody`) but is **completely unused** for
   collision today — only `mass`/`linearDamping` are ever pulled from it
   (`DynamicChainDetection.cpp`, Step G). **Goal:** every joint particle gets
   its own physical collision radius, derived from its own PMX Dynamic rigid
   body's real shape/size, so a joint's simulated body doesn't visually clip
   as deeply into a Static collider as a mathematical point currently can.
2. **"Use pmx defined rigid body layer rule with collision map" — PMX's own
   collision-group/mask filtering system (`RigidBody::group`,
   `RigidBody::collisionGroupMask` — already parsed correctly from the PMX
   file, already correctly documented as "matches Bullet's own
   btCollisionObject group/mask convention exactly," even already partially
   surfaced in the Editor's Bone Viewer for an unrelated "Select All (Group)"
   button) is completely IGNORED by the actual collision solver.** Every
   joint of every collision-opted-in chain is tested against **every**
   detected Static collider unconditionally — there is no group/mask
   filtering at all today. **Goal:** wire this already-correctly-parsed PMX
   data into `StepDynamicChain()`'s own collision loop, using the exact same
   Bullet-style group/mask AND-test the reference `saba::MMDPhysics.cpp`
   backend itself uses (confirmed by direct inspection of
   `third_party/saba/src/Saba/Model/MMD/MMDPhysics.cpp`, line ~151-154:
   `m_world->addRigidBody(rb, 1 << mmdRB->GetGroup(), mmdRB->GetGroupMask())`
   — Bullet's own broad-phase-filter convention, `(groupA & maskB) &&
   (groupB & maskA)`).
3. **"By default, turn collision to ON."** `DynamicChainDefinition::collisionEnabled`
   defaults to `false` today — a human must open the Editor Inspector and
   manually tick "Enable Collision" per chain, per model, every time a new
   model is imported, before hair/skirt physics avoids clipping through the
   character's own body at all. **Goal:** flip this default to `true`, so
   collision "just works" for every freshly-detected chain the moment a model
   is spawned, matching the "should just work" spirit of the two goals above.

None of these three goals requires a new asset format, a new import step, or
any new authoring UI — every single byte of data this campaign needs
(`RigidBody::shape`/`shapeSize`/`group`/`collisionGroupMask`) is **already**
parsed correctly from the PMX file today (`src/Assets/PmxLoader.cpp`); this
campaign is purely about finally **reading** data that already exists and
routing it to where the solver can use it, plus one single default-value
flip.

---

## Step 2 — The Situation (Where are we now?)

Investigated and confirmed by direct source inspection (2026-09-08):

1. `src/Assets/PhysicsData.h::RigidBody` already carries `group`
   (`std::uint8_t`, PMX-authoring convention 0-15, single-bit membership) and
   `collisionGroupMask` (`std::uint16_t`, bitmask of groups this body IS
   ALLOWED to collide with) — correctly parsed by
   `src/Assets/PmxLoader.cpp::ConvertRigidBody()` directly from
   `saba::PMXRigidbody::m_group`/`m_collisionGroup` (confirmed lines 393-394:
   `out.group = body.m_group; out.collisionGroupMask = body.m_collisionGroup;`).
   **Nothing needs to change in the PMX import pipeline at all** — this data
   already survives into every `.gta` file's embedded `PhysicsData` today.
   **v2 correctness note (see Revision Notes, finding #2):** `m_group` is a
   raw `uint8_t` copied verbatim, byte-for-byte, straight off the wire from an
   untrusted `.pmx` file, with **zero range validation anywhere in the whole
   load pipeline** — PMX authoring tools always emit 0-15, but nothing in
   `saba` or this engine's own `PmxLoader.cpp` enforces that for a malformed/
   corrupted/adversarial file. Every phase in this campaign that turns a
   `group` value into a shift amount (`1u << group`) MUST mask it down to its
   documented 4-bit range FIRST — seeing 0-15 in a doc comment is not the same
   as the value actually always being 0-15 at runtime, and C++ shifting by an
   out-of-range amount is undefined behavior, not merely "a large wrong
   number." PHASE1 and PHASE4 (v2) both now do this explicitly.
2. `src/Physics/Collider.h::Collider` (the WORLD-space, shape-agnostic
   struct the solver actually iterates) has exactly four fields today:
   `shape`, `center`, `rotation`, `size` — **no group/mask fields at all.**
3. `src/Physics/ModelColliderDefinition.h` (the model-wide, bind-pose-relative
   collider list `DetectModelColliders()` builds once per model) also carries
   no group/mask fields — `Physics/ModelColliderDetection.cpp` never reads
   `RigidBody::group`/`collisionGroupMask` at all.
4. `src/Physics/DynamicChainDefinition.h::DynamicJointSettings` (the
   per-joint tuning struct: `damping`/`stiffness`/`mass` today) has no
   group/mask fields, and no collision-radius field either.
5. `src/Physics/DynamicChainDetection.cpp`, Step G (confirmed the real code,
   lines 444-451), already has the EXACT right place to pull more per-joint
   data from each joint's own matched `RigidBody` — it already does this for
   `mass`/`linearDamping`:
   ```cpp
   for (std::size_t j = 0; j < chain.jointBoneIndices.size(); ++j) {
       const auto rbIt = boneIndexToRigidBodyIndex.find(chain.jointBoneIndices[j]);
       if (rbIt != boneIndexToRigidBodyIndex.end()) {
           const RigidBody& body = physics->rigidBodies[static_cast<std::size_t>(rbIt->second)];
           chain.jointSettings[j].mass = body.mass;
           chain.jointSettings[j].damping = body.linearDamping;
       }
   }
   ```
   This is exactly where this campaign's new per-joint `group`/`collisionMask`/
   `collisionRadius` seeding belongs too — same loop, same matched `body`.
6. `src/Physics/VerletParticle.h::VerletParticle` has no collision-radius
   field — a particle is a pure zero-radius point (`position`,
   `previousPosition`, `inverseMass`, `pinned` only).
7. `src/Physics/SphereCollider.cpp`/`BoxCollider.cpp`/`CapsuleCollider.cpp`
   (the three per-shape collision-math primitives) never read anything from
   `particle` except `.pinned` and `.position` — no radius concept at all on
   the "thing being tested" side. `CapsuleCollider.cpp`'s own
   `SolveCapsuleCollision()` **delegates** to `SolveSphereCollision()`
   (`SolveSphereCollision(particle, SphereCollider{ closest, collider.radius })`)
   — an important reuse detail PHASE2 exploits directly (see that phase's own
   "why this design" note).
8. `src/Physics/DynamicChainSolver.cpp`, step 5 (Collision), does:
   ```cpp
   if (definition.collisionEnabled) {
       for (std::size_t i = 0; i < jointCount; ++i) {
           for (const Collider& collider : colliders) {
               SolveCollision(state.particles[i], collider);
           }
       }
   }
   ```
   Every particle vs. every collider, unconditionally — the exact place
   PHASE1's group/mask filter must be inserted, as a cheap guard BEFORE
   calling `SolveCollision()` (mirroring Bullet's own broad-phase filter
   running before narrow-phase).
9. `src/Physics/DynamicChainDefinition.h`:
   ```cpp
   bool collisionEnabled = false;
   ```
   This is the ONE line PHASE3 flips to `true`.
10. `tests/Physics/DynamicChainSolverTests.cpp`'s own
    `CollidersAreIgnoredWhenCollisionEnabledIsFalse` test (line ~279) contains
    `ASSERT_FALSE(definition.collisionEnabled);` against a freshly
    default-constructed `DynamicChainDefinition` that never explicitly sets
    the field — **this is the ONLY test in the whole repository that reads
    the raw default value without first explicitly overwriting it** (every
    other test/fixture that cares about a specific `collisionEnabled` value
    already sets it explicitly by hand — confirmed by a full repository-wide
    `search_in_dir` sweep for `collisionEnabled` across `tests/`, see PHASE3
    for the complete audit). PHASE3 fixes this one test; nothing else needs
    to change for the default flip.
11. Build system: this project has **no globbing** — every `.h`/`.cpp` file
    is listed explicitly in the root `CMakeLists.txt`
    (`add_library(gte_core STATIC ...)`) and every test file is listed
    explicitly in `tests/CMakeLists.txt` (`GTE_TEST_SOURCES`). This campaign
    adds a small number of brand-new test files only (no new production
    `.h`/`.cpp` files — every change to production code is an EDIT of an
    existing file) — every new test file still MUST be added to
    `tests/CMakeLists.txt`, or it will never compile/link/run.
12. **Aggregate-initializer safety (learned the hard way by
    verlet-integration-9's own PHASE0 v2, Revision Notes finding #5 — and
    RE-LEARNED, the even harder way, by THIS campaign's own v1→v2 review, see
    Revision Notes finding #1 below):** `Collider`, `DynamicJointSettings`,
    `ModelColliderDefinition`, **and any other plain-aggregate struct this
    campaign touches, anywhere in the codebase, including Editor-only
    structs like `BoneViewerWindow.h::RigidBodyEntry`** are all constructed at
    dozens of existing call sites via plain positional brace-init (e.g.
    `Collider{ ColliderShape::Sphere, center, rotation, size }`,
    `DynamicJointSettings{ damping, stiffness, mass }`,
    `RigidBodyEntry{ body.name, body.translate, body.rotateRadians,
    body.shape, body.shapeSize, body.boneIndex, body.group, body.motionType }`).
    **Every new field this campaign adds to ANY such struct MUST be appended
    as a new TRAILING member, strictly AFTER every field that already
    exists today — never inserted between two existing fields, no matter how
    "related" it looks to one of them** — each with its own sensible default
    member-initializer. Inserting a field in the middle of a struct that has
    even ONE existing positional-brace-init call site anywhere in the
    codebase does not just risk "a wrong value silently absorbed" (the v1
    concern this note originally only warned about) — if the type of the
    field that used to occupy that position differs from the type of the
    newly-inserted field (e.g. a `RigidBodyMotionType` enum class value being
    forced into a newly-inserted `std::uint16_t` slot), it is a guaranteed,
    unrecoverable **compile error** at every such call site, since C++
    performs no implicit conversion between an unrelated `enum class` and an
    integer type. PHASE4 v1 made exactly this mistake once (see Revision
    Notes finding #1) — PHASE4 v2 fixes it. **The correct, mechanical
    procedure, with zero exceptions, for this entire campaign:** (a) find
    EVERY field the target struct has today, in their EXISTING declared
    order; (b) append the new field(s) strictly after the LAST one; (c) find
    every positional-brace-init call site for that struct (a `search_in_dir`
    sweep for `StructName{` across the whole repository, never a partial/
    assumed search); (d) for the specific call site(s) that need the new
    field POPULATED with a real, non-default value (not merely left at its
    default), add the new value as a new TRAILING positional argument at
    that exact call site too, in the same edit — do not assume a
    field-by-field `entry.newField = ...;` assignment pattern exists just
    because it would be more convenient to describe; confirm which pattern
    the REAL file actually uses before writing the phase document's own
    instructions.
13. **Group-value robustness (new in v2 — see Revision Notes finding #2):**
    every function in this campaign that turns a `group` value (`RigidBody::group`,
    `DynamicJointSettings::group`, `Collider::group`, `ModelColliderDefinition::group`
    — all `std::uint8_t`) into a bitmask via `1u << group` MUST first mask
    `group` down to its documented 4-bit range (`group & 0x0Fu`) before the
    shift. A PMX file's `group` byte is copied completely unvalidated all the
    way from `PmxLoader.cpp::ConvertRigidBody()` (`out.group = body.m_group;`,
    no range check) — a malformed or adversarially-crafted `.pmx` file could
    contain any byte value 0-255 here, and `1u << 200` is undefined behavior
    in C++ (shifting by an amount >= the promoted-to type's own bit width,
    typically 32 for `unsigned int`), not merely "a surprising large mask."
    This engine's own established convention — "degrade gracefully, never
    crash," demonstrated throughout `ChainConstraints.cpp`,
    `SphereCollider.cpp`'s degenerate-center handling, `BoxCollider.cpp`'s
    degenerate half-extent handling, `IsDegenerateColliderShape()` — already
    requires this; PHASE1 v2 introduces the one shared masking convention
    (`GroupBit()`), and PHASE4 v2's independent Editor-only re-derivation
    mirrors it inline (per this campaign's own "never call the production
    static function from another translation unit" rule — see PHASE1's own
    test-writing guidance).

**Net conclusion:** this is a small, surgical, four-part campaign — no new
collision-math algorithm, no new asset format, no new import step. (a) thread
already-parsed PMX group/mask data through three existing structs and add one
filter check in the solver's existing collision loop, made robust against a
malformed/out-of-range `group` byte; (b) thread already-parsed PMX shape/size
data for each joint's OWN rigid body into one new scalar field, consumed by
the existing per-shape math with a minimal, additive, already-backward-
compatible change; (c) flip one boolean default and fix the one test that
depended on the old value; (d) update the two Editor surfaces that already
talk about collision so they stay accurate, WITHOUT breaking the one existing
Editor struct's own positional-init call site; (e) one end-to-end regression
test proving all of the above together, plus the mandatory CMakeLists.txt/
documentation audit.

---

## Step 3 — The Plan (Phase Index)

Execute **in this exact order** — PHASE4/PHASE5 read/verify state PHASE1-3
produce.

| # | File | One-line summary | Depends on |
|---|------|-------------------|------------|
| 1 | `PHASE1_PMX_COLLISION_GROUP_LAYER_FILTERING.md` | Adds `group`/`collisionMask` (trailing fields) to `Collider`, `ModelColliderDefinition`, and `DynamicJointSettings`; populates them from each body's real PMX `RigidBody::group`/`collisionGroupMask`; adds the Bullet-style group/mask AND-filter check to `DynamicChainSolver.cpp`'s existing collision loop, run BEFORE `SolveCollision()`, using a shift-safe `GroupBit()` helper. | none |
| 2 | `PHASE2_JOINT_OWN_RIGID_BODY_SHAPE_AS_COLLISION_RADIUS.md` | Adds `collisionRadius` (trailing field) to `VerletParticle` and `DynamicJointSettings`; derives each joint's own effective radius from its own PMX Dynamic/DynamicAndBoneMerge rigid body's real `shape`/`shapeSize`; seeds it into the particle at the existing seed point; inflates `SolveSphereCollision()`/`SolveBoxCollision()` by that radius (Capsule inherits it for free via its existing sphere-delegation). Unchanged since v1 — re-verified against the live source tree during this v2 review with zero findings. | 1 (touches the same files; sequenced after so PHASE2's tests can exercise PHASE1's filter too) |
| 3 | `PHASE3_COLLISION_ENABLED_ON_BY_DEFAULT.md` | Flips `DynamicChainDefinition::collisionEnabled`'s default from `false` to `true`; fixes the one test whose assertion depended on the old default; sweeps every other `collisionEnabled`-touching test (confirmed unaffected, but re-verified explicitly) and every doc comment that describes the old default; documents the accepted performance trade-off this default flip causes in `PhysicsSystem.cpp`'s own existing `anyChainWantsCollision` guard. | 1, 2 |
| 4 | `PHASE4_EDITOR_VISIBILITY_FOR_GROUP_AND_RADIUS.md` | Updates the Inspector's and Bone Viewer's existing collision-related text/overlays (`InspectorPanel.cpp`, `BoneViewerWindow.cpp`) so they now also reflect group/mask filtering and each chain's own effective particle radius, instead of silently going stale now that the underlying behavior changed — **v2 fixes a v1 defect that would not compile** (see Revision Notes finding #1). | 1, 2, 3 |
| 5 | `PHASE5_END_TO_END_REGRESSION_AND_BUILD_REGISTRATION.md` | New end-to-end regression tests proving group/mask filtering, joint-radius inflation, and default-on collision all work correctly together through the FULL `PhysicsSystem::Update()` pipeline; final CMakeLists.txt/documentation audit sweep; v2 adds one malformed-`group`-value robustness regression test. | 1-4 |

### Cross-cutting conventions every phase MUST follow (do not deviate)

- **No file globbing exists.** Any new **test** file added in ANY phase below
  MUST be added to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list. This
  campaign adds **no new production `.h`/`.cpp` files** — every production
  change is an in-place edit of an existing, already-registered file, so
  the root `CMakeLists.txt` needs no new entries (still worth a final
  confirmation sweep in PHASE5).
- **Append new struct fields at the END, always with their own default
  member-initializer, for EVERY struct in the codebase this campaign touches
  — production or Editor-only.** See Step 2, point 12 above — this is the
  single most important mechanical rule in this campaign; violating it can
  silently break dozens of existing positional-aggregate-init test call
  sites (a silent wrong-value bug in the best case; a guaranteed compile
  error in the worst case — see this campaign's own PHASE4 v1→v2 fix, and
  verlet-integration-9's own PHASE0 v2 finding #5, for the two concrete
  precedents).
- **Never trust a `group` byte's documented range without masking it before
  a shift.** See Step 2, point 13 above — every `1u << group` in this
  campaign (production or test code) must be `1u << (group & 0x0Fu)`, no
  exceptions.
- **Never silently regress an existing behavior.** Every existing test in
  `tests/Physics/{SphereCollider,BoxCollider,CapsuleCollider,Collider,
  DynamicChainSolver,DynamicChainDetection}Tests.cpp` and
  `tests/Game/Physics/PhysicsSystem*Tests.cpp` must keep passing, unmodified,
  UNLESS a phase document below explicitly names it and explains exactly why
  a specific, narrow edit is required (PHASE3 names exactly one such test).
- **Degrade gracefully, never crash.** Every new code path must follow the
  established convention seen throughout `DynamicChainSolver.cpp`,
  `ChainConstraints.cpp`, `SphereCollider.cpp`: a malformed/degenerate/
  out-of-range input is a documented no-op or safely-clamped value, never an
  exception, never an out-of-bounds read, never undefined behavior. A
  group/mask value of exactly `0` (meaning "no PMX data available", e.g. a
  hand-built test fixture that never sets it) MUST default to "collides with
  everything" (matching pre-campaign behavior), never "collides with
  nothing" — see PHASE1's own explicit default-value design for exactly how
  this is achieved. A `group` value outside its documented 0-15 range MUST
  never trigger undefined behavior (see point 13 above).
- **`previousPosition` is never touched by a collision response** — matches
  every existing `Solve*Collision()`'s own explicit contract; nothing in this
  campaign changes that.
- **A pinned particle (`particle.pinned == true`) is always left untouched**
  by every collision function — same existing convention; nothing in this
  campaign changes that either.
- **PMX Euler rotation convention.** Nothing in this campaign converts a new
  rotation — no change needed here — but if any phase's own verification
  code needs to reconstruct a rigid body's bind-pose transform by hand (for a
  test), it MUST reuse the exact same convention every other file in this
  codebase already uses: `Quat::FromEulerDegrees(RadToDeg(x), RadToDeg(y),
  RadToDeg(z))`.
- **Doc-comment discipline.** This codebase's header comments are long,
  precise, and cross-reference the exact originating phase/file/line of
  reasoning. Match that style in every edit this campaign makes — future
  maintainers (and future AI agents) rely on it exclusively.
- **Confirm every quoted line number/identifier against the LIVE file before
  editing, never trust a prior phase document's own quotation blindly.**
  This v2 review found one stale reference-file line-number citation
  (Revision Notes finding #3) purely from mechanically re-reading the real
  file — cheap insurance against documentation drift between when a phase
  document was written and when it is actually executed.
- **Bullet-style group/mask semantics, verified against this engine's own
  reference implementation** (`third_party/saba/src/Saba/Model/MMD/
  MMDPhysics.cpp`, confirmed lines 149-154 for the `addRigidBody(rb, 1 <<
  mmdRB->GetGroup(), mmdRB->GetGroupMask())` call, and lines 632-637 for
  `MMDRigidBody::GetGroup()`/`GetGroupMask()`'s own accessor bodies): two
  bodies A and B are allowed to collide if and only if `((1 << A.group) &
  B.collisionGroupMask) != 0 AND ((1 << B.group) & A.collisionGroupMask) !=
  0` — a SYMMETRIC AND-test, not an OR-test, and not a one-directional test.
  PHASE1 implements exactly this (with the group value safely masked first,
  per point 13 above), no simplification.

### Full list of every file this campaign touches

```
EDIT  src/Physics/Collider.h                              (Phase 1 - group/collisionMask fields)
EDIT  src/Physics/ModelColliderDefinition.h                (Phase 1 - group/collisionMask fields)
EDIT  src/Physics/ModelColliderDetection.cpp                (Phase 1 - populate group/collisionMask from Static RigidBody)
EDIT  src/Physics/DynamicChainDefinition.h                 (Phase 1 - DynamicJointSettings group/collisionMask; Phase 2 - collisionRadius; Phase 3 - collisionEnabled default)
EDIT  src/Physics/DynamicChainDetection.cpp                (Phase 1 - seed joint group/collisionMask; Phase 2 - seed joint collisionRadius)
EDIT  src/Physics/DynamicChainSolver.cpp                   (Phase 1 - group/mask filter in collision loop, via shift-safe GroupBit())
EDIT  src/Physics/DynamicChainSolver.h                     (Phase 1/2 - doc comments only)
EDIT  src/Game/Physics/PhysicsSystem.cpp                   (Phase 1 - copy group/collisionMask into resolved Collider; Phase 3 - doc-comment update only)
EDIT  src/Physics/VerletParticle.h                         (Phase 2 - collisionRadius field)
EDIT  src/Physics/SphereCollider.cpp                       (Phase 2 - inflate by particle.collisionRadius)
EDIT  src/Physics/BoxCollider.cpp                          (Phase 2 - inflate by particle.collisionRadius)
EDIT  src/Physics/SphereCollider.h                         (Phase 2 - doc comment only)
EDIT  src/Physics/BoxCollider.h                            (Phase 2 - doc comment only)
EDIT  src/Physics/CapsuleCollider.h                        (Phase 2 - doc comment only, no code change)
NEW   tests/Physics/DynamicChainSolverCollisionGroupFilterTests.cpp   (Phase 1)
NEW   tests/Physics/ModelColliderDetectionGroupMaskTests.cpp          (Phase 1)
EDIT  tests/Physics/SphereColliderTests.cpp                (Phase 2 - new radius-inflation cases appended)
EDIT  tests/Physics/BoxColliderTests.cpp                   (Phase 2 - new radius-inflation cases appended)
NEW   tests/Physics/DynamicChainDetectionJointRadiusTests.cpp         (Phase 2)
EDIT  tests/Physics/DynamicChainSolverTests.cpp            (Phase 3 - fix the one default-dependent assertion)
EDIT  src/Editor/Panels/InspectorPanel.cpp                 (Phase 4 - two call sites)
EDIT  src/Editor/BoneViewerWindow.h                        (Phase 4 - RigidBodyEntry gains collisionGroupMask, appended AFTER motionType)
EDIT  src/Editor/BoneViewerWindow.cpp                      (Phase 4 - two call sites, incl. the single RigidBodyEntry{...} construction site)
NEW   tests/Game/Physics/PhysicsSystemCollisionGroupAndRadiusEndToEndTests.cpp (Phase 5)
EDIT  tests/CMakeLists.txt                                 (every phase that adds a test file)
```

Proceed to `PHASE1_PMX_COLLISION_GROUP_LAYER_FILTERING.md`.

---

## Revision Notes (v2)

This section documents every finding from this campaign's own "second
iteration" quality-assurance pass (2026-09-08), performed by re-reading the
ENTIRE live source tree this campaign's v1 documents describe, line by line,
rather than trusting v1's own quotations. Every finding below has already
been fixed in the relevant child phase document — this list exists purely as
a change-log for anyone who already read v1.

1. **(Critical — would not compile) `PHASE4`'s v1 instructions for
   `BoneViewerWindow.h::RigidBodyEntry` inserted the new `collisionGroupMask`
   field BETWEEN the existing `group` and `motionType` fields, and assumed
   (incorrectly) that the struct is populated via individual
   `entry.group = body.group;`-style field assignments somewhere in
   `BoneViewerWindow.cpp`.** Direct inspection of the real file
   (`BoneViewerWindow.cpp`, line 412-413) shows the ONE real construction site
   uses plain 8-argument POSITIONAL aggregate-init:
   `RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape,
   body.shapeSize, body.boneIndex, body.group, body.motionType }`. Following
   v1's instruction literally would either (a) silently fail to find any
   `entry.group = body.group` line to anchor the edit near (there is none),
   or (b) if the struct field were inserted where v1 said, cause this exact
   call site to try to initialize the new `std::uint16_t collisionGroupMask`
   field with `body.motionType` (an unrelated `enum class
   RigidBodyMotionType` value) — a hard, unrecoverable **compile error**,
   since C++ performs no implicit conversion between an `enum class` and an
   integer type. **Fixed in PHASE4 v2:** the new field is appended strictly
   AFTER `motionType` (the struct's true last field), and the ONE real
   construction call site is updated with a 9th trailing positional argument,
   `body.collisionGroupMask`, in the same edit — both instructions now match
   the real file exactly, confirmed by direct re-inspection.
2. **(Robustness / undefined behavior) `PHASE1`'s v1 `GroupsMayCollide()` and
   `PHASE4`'s v1 `CountCollidersReachableByChain()`/overlay-dimming logic both
   computed `1u << group` with no range check on `group` first.** `group` is
   documented as "always 0-15" by `Assets/PhysicsData.h`'s own doc comment,
   but is copied completely unvalidated from a raw `.pmx` file byte
   (`PmxLoader.cpp::ConvertRigidBody()`, confirmed: `out.group =
   body.m_group;`, no range check anywhere in the load pipeline). A
   malformed/corrupted/adversarially-crafted `.pmx` file could contain any
   byte value 0-255 here; shifting `1u` by an amount `>= 32` (the promoted-to
   `unsigned int`'s own bit width) is undefined behavior in C++, not merely
   "an unexpectedly large mask" — a real risk this engine's own
   "degrade gracefully, never crash" convention (demonstrated everywhere else
   in this exact subsystem: `IsDegenerateColliderShape()`,
   `SphereCollider.cpp`'s degenerate-center fallback,
   `BoxCollider.cpp`'s degenerate half-extent handling) explicitly exists to
   prevent. **Fixed in PHASE1 v2:** a small shared `GroupBit()` helper masks
   `group & 0x0Fu` before shifting; **fixed in PHASE4 v2:** the independent
   Editor-only re-derivations (per this campaign's own "never call the
   production static function from another translation unit" rule) now
   perform the identical masking inline.
3. **(Documentation precision) v1's citation of
   `third_party/saba/src/Saba/Model/MMD/MMDPhysics.cpp`'s own
   `GetGroup()`/`GetGroupMask()` accessor bodies as "lines ~613-615" was
   stale/imprecise** — direct re-inspection places them at lines 632-637 in
   the actual vendored copy of this file in this repository. The
   `addRigidBody(...)` call site itself was correctly cited (lines ~149-154,
   confirmed accurate). **Fixed:** every phase document and this master
   strategy now cite the confirmed, re-verified line numbers.
4. **(Missing discussion, not a bug) v1 never discussed the interaction
   between PHASE3's default-to-`true` flip and `Game/Physics/PhysicsSystem.cpp`'s
   own existing `anyChainWantsCollision` performance guard** (confirmed real
   code: `resolvedColliders` is only ever populated `if (anyChainWantsCollision
   && !model->colliders.empty())`, an optimization that skips
   `ComputeBoneWorldMatrix()` calls entirely for a collision-disabled chain).
   Before this campaign, `anyChainWantsCollision` was `false` for essentially
   every model in existence (since `collisionEnabled` always defaulted to
   `false`), so this guard almost always fired and saved real per-frame work.
   After PHASE3, `anyChainWantsCollision` will be `true` for the overwhelming
   majority of freshly-detected chains, so nearly every model with at least
   one detected chain and at least one detected Static collider will now pay
   this resolution cost every frame, unconditionally — an intentional,
   accepted consequence of "turn collision on by default" (exactly what the
   user asked for), not a regression, but worth stating explicitly rather
   than leaving a future performance investigation to rediscover it from
   scratch. **Fixed:** PHASE3 v2 adds an explicit short note to this effect;
   no code changes result from this finding (the guard's own logic is
   already correct and needs no change — only its real-world hit rate
   changes, as an accepted side effect of this campaign's own explicit
   goal).
5. **(Minor precision improvement, no behavior change) `PHASE4`'s v1 own
   pseudo-code for the 3D-viewport collider-dimming feature (Step 3.4) used
   a placeholder/invented variable name, `m_verletModel`, and explicitly
   flagged it as unverified ("adapt the exact ... local-variable names ...
   this pseudo-code inlines a plausible variable name that must be verified
   against the live file").** This v2 review already performed that
   verification: the real, live local variable in the real enclosing
   function (`BoneViewerWindow.cpp`, confirmed around lines 895-1519) is
   named `verletModel` (a local `const DynamicChainRigCache::ModelEntry*`,
   no `m_` member prefix — it is not a member variable). **Fixed in PHASE4
   v2:** the hedge/placeholder is removed and the confirmed real name is used
   directly, removing one avoidable source of implementer error.
6. **(Minor robustness improvement to a v1-identified "acceptable
   fallback," not a defect) `PHASE4`'s v1 own reachable-collider matching for
   the 3D-viewport dimming feature (Step 3.4) matched a `RigidBodyEntry` to
   its corresponding `ModelColliderDefinition` by `boneIndex` alone.** Since
   `ModelColliderDefinition` does not retain which original PMX rigid-body
   index it came from, and a PMX model COULD (rarely, but validly) have more
   than one `Static` rigid body attached to the same bone, boneIndex-only
   matching is technically ambiguous in that edge case. **Fixed in PHASE4
   v2:** the matching heuristic is tightened to compare `(boneIndex, shape,
   shapeSize)` as a combined key, which resolves the ambiguity in every
   realistic case without requiring any new data-model field — documented as
   a best-effort heuristic for a purely cosmetic Editor overlay, consistent
   with PHASE4's own already-existing "acceptable smaller fallback" escape
   hatch for this same feature.

All five production/test source files re-read for this review
(`Collider.h`, `ModelColliderDefinition.h`, `ModelColliderDetection.cpp`,
`DynamicChainDefinition.h`, `VerletParticle.h`, `SphereCollider.cpp`,
`BoxCollider.cpp`, `CapsuleCollider.cpp`, `DynamicChainSolver.cpp/.h`,
`DynamicChainDetection.cpp`, `PhysicsSystem.cpp`, `PhysicsData.h`,
`PmxLoader.cpp`, `InspectorPanel.cpp`, `BoneViewerWindow.h/.cpp`,
`RigidBodyGroupSelection.h`, `tests/Physics/DynamicChainDetectionTests.cpp`,
`tests/CMakeLists.txt`) matched v1's own quotations exactly EXCEPT for the
six findings above — v1's underlying investigation was genuinely thorough;
this v2 pass is a targeted correction, not a rewrite of the plan's own
architecture, which remains unchanged and is re-confirmed sound.
