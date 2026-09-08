# PHASE3 (v2) — Turn Collision ON by Default ("also by default, turn collision to ON")

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1, PHASE2 (the collision path this default now activates
for every model out of the box must already be group/mask-aware and
radius-aware before flipping this default on, so any freshly-spawned model's
first-ever collision behavior is already the fully-correct PHASE1+2 behavior,
never an intermediate one)

**v2 change summary:** this phase's own plan (Step 3.1-3.3) was re-verified
against the live source tree and found completely accurate — no functional
change. This revision adds one new subsection, Step 4.1, explicitly stating
the accepted performance trade-off this default flip causes in
`Game/Physics/PhysicsSystem.cpp`'s own existing `anyChainWantsCollision`
guard (`PHASE0_MASTER_STRATEGY.md`'s Revision Notes, finding #4) — a
documentation addition only, not a code change, since the guard's own logic
is already correct as written.

---

## Step 1 — The Goal

Today, `DynamicChainDefinition::collisionEnabled` defaults to `false`. A
freshly-imported PMX model's every detected hair/skirt chain therefore
clips straight through the character's own body by default — a human must
open the Editor Inspector and manually tick "Enable Collision" per chain,
per model, before physics-driven collision ever activates at all. **Goal:**
flip this one default to `true`, so every freshly-detected chain collides
against its model's own auto-detected PMX colliders (now correctly
shape/group/mask-aware, per PHASE1/PHASE2) the moment the model is spawned —
matching the user's own explicit request, verbatim.

## Step 2 — The Situation

`src/Physics/DynamicChainDefinition.h`:

```cpp
    bool collisionEnabled = false;
```

This single line is the ENTIRE production-code change this phase makes.
`DetectDynamicChains()` (`DynamicChainDetection.cpp`) never explicitly writes
to this field anywhere in its own Step G (confirmed by direct inspection —
Step G seeds `gravityScale`/`windScale`/`constraintIterations`/
`jointSettings`/`maxPlausibleRootDelta`, never `collisionEnabled`), so every
freshly-detected `DynamicChainDefinition` is left at whatever this one
default member-initializer says — flipping it here is sufficient to flip the
behavior for every newly-detected chain in the engine, with no other
production file needing to change.

**Test-suite audit (already performed as part of this campaign's own
investigation — repeat it before touching anything, to catch any drift since
this document was written):** a full `search_in_dir` sweep for
`collisionEnabled` across `tests/` found it referenced in exactly three
files:

1. `tests/Physics/DynamicChainSolverTests.cpp`, test
   `CollidersAreIgnoredWhenCollisionEnabledIsFalse` (line ~279):
   ```cpp
   DynamicChainDefinition definition = BuildThreeJointChainDefinition(/*stiffness=*/0.0f, /*damping=*/0.05f);
   ASSERT_FALSE(definition.collisionEnabled);
   ```
   **This is the ONLY place in the entire test suite that reads the raw
   default value of a freshly-constructed `DynamicChainDefinition` without
   ever explicitly assigning it first.** This test's own NAME and PURPOSE
   ("collision must be a no-op when `collisionEnabled` is false") depend on
   the chain genuinely having `collisionEnabled == false` when
   `StepDynamicChain()` is called a few lines later — after this phase's
   default flip, that `ASSERT_FALSE` line will immediately fail, even though
   the test's own INTENT (verify the false-case no-op contract) is still
   completely valid and still needs to keep being tested. **Fix: explicitly
   set the field** instead of relying on the default:
   ```cpp
   DynamicChainDefinition definition = BuildThreeJointChainDefinition(/*stiffness=*/0.0f, /*damping=*/0.05f);
   definition.collisionEnabled = false; // Explicit - this test intentionally exercises the disabled case,
                                          // independent of whatever DynamicChainDefinition's own default is.
   ```
   (Delete the old `ASSERT_FALSE(...)` line entirely — it becomes a
   tautological assertion against a value the test itself just set on the
   previous line, which adds no verification value; the explicit assignment
   above already documents the intent at least as clearly.)

2. `tests/Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp` — every
   reference either explicitly assigns `chain.collisionEnabled = collisionEnabled;`
   inside its own `RegisterAttachSeedPoseAndConfigureChain()` helper (called
   with an explicit `true`/`false` literal at every call site), or is a
   comment. **No fix needed** — every actual runtime value is set explicitly,
   never inferred from the raw struct default.

3. `tests/Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp` — same
   pattern as above: `RegisterAttachAndConfigure()` explicitly assigns
   `chain.collisionEnabled = collisionEnabled;` at every call site (including
   the one test asserting `ASSERT_FALSE(model->chains[0].collisionEnabled)`
   at line ~354 — that assertion is checking the value the helper JUST
   explicitly set two lines above, with `/*collisionEnabled=*/false`, not the
   raw struct default at all). **No fix needed.**

No other test file in the repository references `collisionEnabled` at all
(confirmed by the same sweep) — in particular, `DynamicChainDetectionTests.cpp`
(which tests `DetectDynamicChains()` directly, the function whose output this
phase's default flip actually changes) never asserts anything about
`collisionEnabled` on any detected chain today, so it needs no change.

## Step 3 — The Plan

### 3.1 — Flip the default

`src/Physics/DynamicChainDefinition.h`:

```cpp
    // task_manager/verlet-integration-10, PHASE3 - defaults to true as of
    // this phase (was false from this field's introduction in
    // verlet-integration-9's PHASE2 through the rest of that campaign) - a
    // freshly-detected chain now collides against its model's own
    // auto-detected PMX colliders immediately, with no manual Editor opt-in
    // required, per this campaign's own explicit user request ("by default,
    // turn collision to ON"). A human may still explicitly disable
    // collision per chain via the Editor Inspector's existing "Enable
    // Collision" checkbox (Panels/InspectorPanel.cpp) - this default only
    // changes what a BRAND NEW, never-before-tuned chain starts at. See
    // Game/Physics/PhysicsSystem.cpp's own `anyChainWantsCollision` guard
    // doc comment for the accepted per-frame performance consequence of this
    // default flip (task_manager/verlet-integration-10, PHASE3, Step 4.1).
    bool collisionEnabled = true;
```

### 3.2 — Fix the one affected test

Apply the exact edit from Step 2, point 1 above to
`tests/Physics/DynamicChainSolverTests.cpp`.

### 3.3 — Update stale doc-comment mentions of the old default (no code change)

A `search_in_dir` for the phrase `"the default"` near `collisionEnabled`
across `src/` finds these doc-comment mentions of the OLD default value that
must be updated to avoid misleading a future reader (grep for
`collisionEnabled` and `"opt-in-only default"` / `"default, matching"` style
phrasing):

- `src/Physics/DynamicChainDefinition.h`'s own pre-existing doc comment above
  `collisionEnabled` (the block inherited from verlet-integration-9,
  currently reading "...when false (the default, matching the old
  hasHeadCollider's own opt-in-only default)...") — update the parenthetical
  to reflect the new default; keep the rest of that historical explanation
  intact (it is still accurate about WHY the field exists and how it
  replaced the old head-collider trio).
- `src/Editor/Panels/InspectorPanel.cpp`'s two "Enable Collision" checkbox
  sites (PHASE4 revisits these files anyway for group/mask/radius visibility
  — if PHASE4 has not yet run, at minimum confirm neither site's own doc
  comment asserts the field "defaults to false" anywhere; none currently do,
  per direct inspection — both existing comments only describe WHAT the
  checkbox replaced, not its default value, so no change is strictly required
  here, but re-confirm during PHASE4 regardless since that phase touches
  these exact call sites next).
- `src/Game/Physics/PhysicsSystem.cpp`'s own comment "...an entity with
  collision disabled everywhere (today's default for every chain) pays zero
  extra ComputeBoneWorldMatrix() calls..." (in the `anyChainWantsCollision`
  guard's comment block, confirmed at the real file's lines 375-384) — update
  "today's default for every chain" to reflect that collision is now enabled
  by default; the surrounding performance-guard LOGIC itself (skip resolving
  colliders when `anyChainWantsCollision == false`) is completely unaffected
  and needs no code change — only the comment's own factual claim about what
  "default" means changes. **See Step 4.1 below (new in v2) for the fuller
  explanation this comment update should reference.**

### 3.4 — Re-verify the full `collisionEnabled` test sweep (repeat Step 2's audit)

Re-run the same `search_in_dir` sweep for `collisionEnabled` across `tests/`
one more time AFTER making the Step 3.2 edit, to confirm no other test
silently depended on the old default (this document's own Step 2 audit is
believed complete and current as of this campaign's own investigation, but
re-confirming immediately before/after this specific edit costs nothing and
catches any drift if PHASE1/PHASE2's own new tests happened to introduce a
fresh raw-default dependency — they should not, per their own Step 4
"Confirmed zero regression" sections, but this is the cheap, final
confirmation).

## Step 4 — Why this is safe to do now (not earlier, not without PHASE1/PHASE2)

Flipping this default BEFORE PHASE1 (group/mask filtering) would mean every
freshly-spawned model's hair/skirt immediately collides against EVERY
detected Static body regardless of PMX-authored collision-group intent — for
a real MMD model where groups exist specifically so, e.g., a skirt's own
inner chain does NOT collide with an inner-thigh Static body the model's
author deliberately excluded via `collisionGroupMask`, turning collision on
by default WITHOUT group filtering already working could look visually WORSE
than today's opt-in-required default (a skirt joint fighting a collider its
own author explicitly meant to exclude). Sequencing PHASE3 after PHASE1
(and PHASE2, so the joint's own physical thickness is also already accounted
for) guarantees that the moment collision activates by default for the first
time in this engine's history, it activates with the FULLY correct,
PMX-faithful behavior the user asked for — not a partially-correct
intermediate state.

### 4.1 — (New in v2) Accepted performance consequence of this default flip

`src/Game/Physics/PhysicsSystem.cpp` already contains an existing
optimization guard (confirmed real code, lines 375-388):

```cpp
        std::vector<Collider> resolvedColliders;
        const bool anyChainWantsCollision = std::any_of(model->chains.begin(), model->chains.end(),
            [](const DynamicChainDefinition& c) { return c.collisionEnabled; });
        if (anyChainWantsCollision && !model->colliders.empty()) {
            resolvedColliders.reserve(model->colliders.size());
            for (const ModelColliderDefinition& colliderDef : model->colliders) {
                // ... one ComputeBoneWorldMatrix() call per collider ...
            }
        }
```

Before this campaign, `collisionEnabled` defaulted to `false` for every
chain, so `anyChainWantsCollision` was `false` for essentially every model in
existence unless a human had manually opted in — this guard therefore almost
always fired and skipped this per-collider `ComputeBoneWorldMatrix()` work
entirely, for almost every model, every frame. **After this phase,
`anyChainWantsCollision` will be `true` for the overwhelming majority of
freshly-detected chains (the new default), so nearly every model that has at
least one detected chain AND at least one detected Static collider will now
pay this resolution cost every single frame, unconditionally.**

This is an intentional, accepted, and entirely expected consequence of "turn
collision on by default" — it is exactly what the user asked for (hair/skirt
should avoid clipping through the body automatically, which necessarily
requires resolving the colliders it needs to avoid) — **not a regression and
not a defect**, and the guard's own existing logic needs no code change: it
is still correctly named, still correctly skips the work for the (now rarer)
case of a chain/model with collision genuinely disabled or with zero
detected colliders. This subsection exists purely so a future performance
investigation into "why does every model with hair now cost slightly more
per frame than it used to" finds an already-recorded, deliberate answer
instead of having to rediscover this trade-off from scratch. No action item
results from this note beyond the doc-comment wording update in Step 3.3
above.
