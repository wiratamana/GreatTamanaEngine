# CAMPAIGN COMPLETION REPORT — `scene-serialization-2`

_Six phases, `task_manager/scene-serialization-2/PHASE0_MASTER_STRATEGY.md`.
See each phase's own `PHASEn_COMPLETION_REPORT.md` in this same folder for
full detail - this is a short, top-level summary of the whole campaign._

## Goal recap

Three deliverables (`PHASE0_MASTER_STRATEGY.md`, Step 1):

1. `POST /save_scene` / `POST /load_scene` HTTP endpoints.
2. Fix the two reported serialization gaps: Transform not reliably
   preserved for every entity, and Camera's `nearZ`/`farZ`/`fovYDegrees`
   never serialized at all.
3. Make the whole system scale like Unity's `[SerializeField]` - a new
   component becomes serializable by registering its own fields once, in
   one place, never by hand-editing three different files per field.

## What shipped, phase by phase

- **Phase 1** - `src/ECS/Reflection/` core: `FieldDescriptor`/
  `ComponentTypeDescriptor`/`ComponentTypeRegistry`, `GTE_REFLECT_FIELD`/
  `GTE_REFLECT_ENUM_FIELD` macros, `Vec3`/`Quat` JSON adapters. Pure new
  infrastructure, zero engine behavior change, fully unit-tested (9 tests)
  against a throwaway test-only component.
- **Phase 2** - Registered the real engine components:
  `Transform`/`Name`/`Camera`/`DirectionalLight`/`PrimitiveSource`, via a
  new, lazily self-bootstrapping `ComponentTypeRegistry::Instance()`. Wrote
  down, in actual code comments, exactly why `MeshAssetSource`/
  `MeshRenderer`/`SkeletalAnimator`/`DynamicChainRig`/`ResolvedAnimationPose`
  are deliberately NOT registered. This is what fixes reported gap #2
  (`Camera::nearZ`/`farZ`). 9 more tests, including a regression guard that
  the five excluded types stay excluded.
- **Phase 3** - Replaced `SceneDocument.h`'s old flat, root-only,
  fixed-schema shape with a hierarchy-aware, generic-component-bag shape;
  deleted `SceneTextFormat.h/.cpp` (the old hand-rolled TEXT grammar) and
  replaced it with `SceneJsonFormat.h/.cpp` (JSON, version 2);
  `SceneBuilder.cpp`'s save half now walks EVERY entity in the Registry,
  full parent/child hierarchy, generically. A working (if simplified)
  generic `LoadScene()` landed too - a plain Camera/Light/empty-node scene
  already round-trips correctly after this phase. Found and fixed one real
  discrepancy against its own strategy file (`SetParent()`'s real
  precondition needing a `Transform` on both sides, not honored by the
  file's own literal Pass-A pseudocode).
- **Phase 4** - The recipe-spawn reconciliation algorithm:
  `PrimitiveSource`/`MeshAssetSource` entities are reconstructed via the
  EXISTING `Game::CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()`
  spawn helpers (so `MeshRenderer` comes back correctly, a real GPU
  resource, never reflected directly), then reconciled by NAME against the
  saved document so a hand-edited (including unnamed) child part's
  Transform survives a round trip - superseding `scene-serialization-1`'s
  old "child transforms never round-trip" limitation. Also replaced
  `ClearSerializableSceneObjects()` with an unconditional
  `ClearEntireScene()`, and fixed `Game::EnsureDefaultCameraExists()`'s
  guard (live `Camera` count, not a one-shot bool) so the scene never ends
  up cameraless after a Load. This is what fixes reported gap #1 (Transform
  not preserved for every entity/every child).
- **Phase 5** - `POST /save_scene`/`POST /load_scene`: two new
  `EngineCommandKind` values, `SaveSceneOutcome`/`LoadSceneOutcome`,
  explicit-path `SaveScene()`/`LoadScene()` overloads, `NetworkRoutes.h/.cpp`
  parsing/response-building (with one deliberate exception to the usual
  "malformed body is a 400" rule - a malformed/empty body means "use the
  default path", not an error), `NetworkServer.cpp` route registration
  (Save failure -> `500`, Load failure -> `400`, the one deliberate
  asymmetry), and the `GTE_ENABLE_EDITOR`-off `503` precedent. Verified live
  against a real running engine in BOTH `GTE_ENABLE_EDITOR` configurations.
  This is deliverable #1.
- **Phase 6** (this phase) - Closed out the campaign: rewrote
  `docs/conventions/scene-serialization.md` and `AGENTS.md`'s summary to
  describe the post-campaign system accurately; updated `TODO.md` (the
  `DirectionalLight`/`AtmosphereSettings` bullet and the "Engine Roadmap"
  scene-serialization bullet, plus the new `SkeletalAnimator` follow-up
  entry); audited every test file from every prior phase (all present,
  registered, passing) and extended `SceneRoundTripIntegrationTests.cpp`
  with a third, independent `PrimitiveSource` root so the campaign's own
  required "Camera + child + PrimitiveSource root, all in one document" case
  is genuinely covered; confirmed zero leftover dead code referencing the
  deleted TEXT-format types; ran the full build + full `ctest` regression
  suite for the first time this campaign (**1543/1543 tests passed**, zero
  regressions); and ran a live, HTTP-driven save -> delete -> load smoke
  test against a real running engine, confirming a deleted
  `PrimitiveSource` entity correctly reappears after `POST /load_scene`.

## Final state

- On-disk format: JSON (`Scene/SceneJsonFormat.h/.cpp`, version 2),
  replacing the old hand-rolled TEXT grammar, still at `*.gtscene`
  (`Editor/SceneIO.h`'s `DefaultScenePath()`, unchanged).
- Scope: EVERY entity in the Registry, full parent/child hierarchy - not
  just `PrimitiveSource`/`MeshAssetSource` roots.
- Extensibility: a new component becomes serializable via ~10 lines in
  `src/ECS/Reflection/BuiltinComponentReflection.cpp` - nothing in
  `src/Scene/` or `src/Network/` needs to change again for it.
- Both originally reported gaps are fixed: Transform round-trips for every
  entity (including a multi-part mesh's own child parts), and Camera's
  `nearZ`/`farZ`/`fovYDegrees` now round-trip too.
- Two new HTTP endpoints exist and work end to end, in both
  `GTE_ENABLE_EDITOR` configurations.
- Full clean build + full `ctest` regression pass (1543/1543, 1 pre-existing
  machine-gated skip) + a live HTTP smoke test all confirm the system works,
  as of this phase's own closeout.

## Known, deliberately-scoped-out remaining gaps (see `TODO.md`)

- `SkeletalAnimator` is not yet reflectable/serializable - needs re-running
  `Game::PlayAnimationOnEntity()`'s own cache-registration side effects at
  Load time, not just a field copy (see PHASE2's Step 3.3).
- `DynamicChainRig`/`ResolvedAnimationPose` (live simulation/derived state)
  are deliberately never serialized.
- `AtmosphereSettings` (owned by `Application`, not any ECS entity) is still
  not scene-serialized at all - out of scope for this campaign.
- A live, end-to-end smoke test of the recipe-spawn reconciliation
  algorithm's multi-part-IMPORTED-MESH branch specifically (a hand-edited/
  unnamed child part Transform, a reparented asset root) has still never
  been run against a real running engine in any session of this campaign -
  no suitable `*.gta` multi-part mesh asset has been available in this
  environment. The simpler `PrimitiveSource` recipe path IS verified live
  (Phase 6's own smoke test); the by-Name child-reconciliation branch is
  only verified via automated Tier-1 tests so far.
- Undo/redo and multi-scene support remain unimplemented, as before this
  campaign.
