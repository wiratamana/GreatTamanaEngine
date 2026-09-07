# PHASE3 — COMPLETION REPORT: Inspector "Verlet Joint" Model-Part Section

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE3_INSPECTOR_VERLET_JOINT_SECTION.md` exactly as written (v2) — every
exact struct/signature/branch shown in that document's Step 3 was applied
verbatim against the real, currently-compiling source tree
(`src/Editor/Panels/InspectorPanel.h/.cpp`) after re-verifying Phase 1's
`FindDynamicChainJointByBoneIndex()`/`DynamicChainJointLocation`
(`src/Physics/DynamicChainDefinition.h`) and Phase 2's Bone Viewer "Verlet"
mode (`src/Editor/BoneViewerWindow.h/.cpp`) were both already in place and
compiling, confirming this phase had something real to build on.

## What was done

1. **`src/Editor/Panels/InspectorPanel.h`** — no change needed, confirmed
   per the phase document's own Step 3.1: the outer `BuildInspectorPanel()`
   signature already carries `PhysicsSystem& physicsSystem` in BOTH build
   configurations (added by `verlet-integration-1`'s Phase 4 for
   `BuildEntityInspector()`'s "Dynamic Chain Physics" section) — verified by
   direct read, not just assumed.

2. **`src/Editor/Panels/InspectorPanel.cpp`**
   - `BuildModelPartInspector()`'s signature gained a new
     `PhysicsSystem& physicsSystem` parameter (previously
     `(Registry&, EditorContext&, ModelRigCache&)`), with its doc comment
     updated to note the Verlet case is the one genuinely-editable exception
     to this function's previous "every field is read-only" premise — a real,
     already-running simulation (`PhysicsSystem::Update()`) consumes
     `DynamicJointSettings`, unlike Bone/RigidBody/Joint's read-only asset
     data.
   - Its one call site (inside `BuildInspectorPanel()`) now reads
     `BuildModelPartInspector(registry, ctx, rigCache, physicsSystem);` —
     `physicsSystem` was already an in-scope parameter of the enclosing
     function in both build configurations, so this needed no further
     plumbing.
   - Added the new `case ModelPartKind::Verlet:` branch inside
     `BuildModelPartInspector()`'s existing
     `switch (ctx.selection.SelectedModelPartKind())`, immediately after the
     existing `case ModelPartKind::Joint: { ... break; }`:
     - Bounds-checks the selected index (a **skeleton bone index** for this
       `ModelPartKind`, per Phase 1's own scheme — not a flattened per-chain
       joint counter) against `rig->skeleton.bones.size()` and shows the
       bone's name/index.
     - Fetches `physicsSystem.GetDynamicChainRigCache().TryGetMutable(source->gtaPath)`
       — the exact same live, mutable, model-path-keyed cache the pre-existing
       "Dynamic Chain Physics" section already edits — and gracefully
       degrades ("No dynamic-chain physics data registered for this model.")
       if the model has none registered.
     - Resolves the selected bone index back to `(chainIndex,
       jointIndexInChain)` via Phase 1's
       `FindDynamicChainJointByBoneIndex()`, gracefully degrading ("This bone
       is not a physics-simulated joint in any detected chain...") if the
       bone is not a joint of any chain (e.g. it's an ordinary skinned bone,
       or a chain's own root/anchor bone).
     - Shows read-only context: chain index + joint count, position-in-chain,
       the chain's root/anchor bone name+index, and (bounds-checked) the
       joint's own bind-pose rest length to the previous joint.
     - Shows and **live-edits** that exact joint's `DynamicJointSettings`
       (Damping/Stiffness/Weight-Mass, with the `mass` slider's minimum bound
       kept at `0.01f` per the phase document's own Step 3.4 — never widened
       to `0.0f`, since `DynamicJointSettings::mass` is documented as "must
       be > 0" and feeds `VerletParticle::inverseMass`'s divide-by-mass
       seeding), guarded by an explicit bounds check against
       `chain.jointSettings.size()` before dereferencing (the v2 robustness
       fix from `PHASE0_MASTER_STRATEGY.md`'s own Revision Notes, finding
       #3 — `jointSettings` is documented as always index-aligned 1:1 with
       `jointBoneIndices` so this can never actually fire in practice, but
       every other single-part case in this same file bounds-checks its own
       index before dereferencing too, so this branch stays consistent with
       that established convention).
     - Shows and edits the chain-WIDE settings (Gravity Scale, Wind Scale,
       Head Collider toggle + collider bone index/radius when enabled) —
       writing directly into the same `DynamicChainDefinition&` reference,
       exactly like the pre-existing "Dynamic Chain Physics" section already
       does for these same fields.
   - No new `#include` was needed — `../../Game/Physics/PhysicsSystem.h` and
     `../../Physics/DynamicChainDefinition.h` were both already present at
     the top of the file (added by `verlet-integration-1`'s Phase 4 for the
     pre-existing "Dynamic Chain Physics" section), and `PhysicsSystem.h`
     itself already transitively includes
     `Game/Physics/DynamicChainRigCache.h` (confirmed by direct read), which
     is where `DynamicChainRigCache::ModelEntry`/`TryGetMutable()` are
     declared.

## Verification

- **Compile check**: `cmake --build build --target gte_core` — succeeds, 0
  errors/warnings; `Panels/InspectorPanel.cpp.obj` rebuilds cleanly with the
  new signature/branch.
- **Compile check**: `cmake --build build --target GreatTamanaEngine` — the
  real executable builds and links successfully end to end (confirms the
  full call chain — `ImGuiEditorLayer::BuildUI()` →
  `BuildInspectorPanel()` → `BuildModelPartInspector()`'s new
  `physicsSystem` parameter/branch — compiles correctly, not just the static
  library in isolation).
- Per the task workflow rules, no full build/full regression (`ctest`) was
  run — this phase's own scope is confined to
  `src/Editor/Panels/InspectorPanel.h/.cpp`, which has no Tier-1-testable
  pure-logic surface of its own (this whole file is ImGui-facing Inspector
  UI code, Tier 2 per `TESTING.md`) — Phase 1's own
  `FindDynamicChainJointByBoneIndex()`/`ModelPartKind::Verlet` tests already
  passed before this phase started and were not touched here.
- No manual/visual runtime verification against a live MMD model with
  physics-driven bone chains was performed in this session (would require
  launching the Editor interactively) — the phase document's own Step 5,
  item 4 checklist (selecting a Verlet joint and confirming the new section's
  fields/live-edit round-trip against the pre-existing "Dynamic Chain
  Physics" section, toggling the head collider and confirming Phase 2's own
  viewport wireframe updates, and the graceful "not a physics-simulated
  joint" edge case) is left as a follow-up sanity check the next time the
  Editor is run interactively, consistent with this session's "fast compile
  check only" workflow rule.

## What was deliberately NOT done (per this phase's own scope)

- The pre-existing "Dynamic Chain Physics" collapsible section in
  `BuildEntityInspector()` was not touched — it remains the "see every
  chain/joint of this model at once" view, unchanged since
  `verlet-integration-1`'s Phase 4.
- No live, currently-simulating particle position/velocity readout was
  added — this section shows/edits PARAMETERS only, consistent with Phase
  2's own static/bind-pose scope.
- No per-instance (per-entity) override editing was added — every edit in
  this new section writes into the model-PATH-keyed
  `DynamicChainRigCache::ModelEntry`, shared by every entity spawned from
  that same `*.gta`, exactly like the pre-existing "Dynamic Chain Physics"
  section already does.
- The Inspector's multi-selection summary switch (`kindNoun`'s 3-way
  ternary, still mislabeling a multi-selected set of Verlet joints as
  "Joints") was **not** touched — that is explicitly Phase 4's job (Culprit
  B), and this phase's own Step 5 checklist explicitly calls out that this
  is the CURRENT, EXPECTED, temporary state after Phase 3 alone, not a
  regression introduced here.

## Campaign status

This closes out Phase 3 of the `verlet-integration-5` campaign
(`PHASE0_MASTER_STRATEGY.md`). Selecting a Verlet joint (tree row or
viewport dot, from Phase 2) now shows a focused "Verlet Joint" Inspector
section that live-edits that exact joint's damping/stiffness/mass plus its
chain's rest-length/gravity-scale/wind-scale/head-collider settings, reusing
`PhysicsSystem::GetDynamicChainRigCache()`'s existing live-edit contract —
the same single source of truth the pre-existing "Dynamic Chain Physics"
section already reads/writes. Ready for Phase 4
(`PHASE4_MULTISELECT_AND_REGRESSION_CLOSURE.md`) to close the remaining
multi-selection summary gap, add the "Select All (Chain)" toolbar button,
and run the full manual cross-mode regression pass.

## Files touched

- `src/Editor/Panels/InspectorPanel.cpp`
- `task_manager/verlet-integration-5/PHASE3_COMPLETION_REPORT.md` (this file)
