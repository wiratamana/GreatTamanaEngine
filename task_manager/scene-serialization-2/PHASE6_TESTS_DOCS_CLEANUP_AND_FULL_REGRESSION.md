# PHASE6 — Tests, Docs, Cleanup, and Full Regression

_Part of `task_manager/scene-serialization-2/`. **Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.**_
Depends on: PHASE1–PHASE5 (all must be done first — this is the closeout phase).
Branch: `feature/scene-serialization`.

## Step 1: The Goal

Close out the campaign: make sure every doc that describes the OLD system
now accurately describes the NEW one, make sure test coverage is complete
across every phase (not just the isolated unit tests each phase already
added), remove any remaining dead references to the deleted TEXT format, and
— ONLY NOW, for the first time in this whole campaign — run the FULL build
and the FULL `ctest` regression suite, treating any newly-failing test as a
real regression to root-cause and fix, per `AGENTS.md`'s own Testability
rule.

## Step 2: The Situation / The Problem

Every prior phase deliberately ran only a FAST compile check, per this
campaign's own workflow rules — by design, to keep each phase's own
turnaround short. That means several cross-cutting concerns were
deliberately deferred to this exact phase:

- `docs/conventions/scene-serialization.md` still describes the OLD system
  in detail (root-only, `PrimitiveSource`/`MeshAssetSource`-only, the TEXT
  grammar, the OLD Design Decision #3 this campaign explicitly supersedes).
- `AGENTS.md`'s own "Scene Serialization" section summary line is now stale
  (still says "`SceneTextFormat.h/.cpp`").
- `TODO.md` may still list "Scene serialization" as an open/partial item, or
  may need a NEW entry for the explicitly-deferred `SkeletalAnimator`
  reflection follow-up (PHASE2's own Step 3.3 note).
- No phase so far has run the FULL test suite together — a passing
  standalone `ComponentTypeRegistryTests.cpp` plus a passing standalone
  `SceneJsonFormatTests.cpp` does not guarantee the FULL suite (every OTHER
  existing test file, e.g. anything touching `Game`/`Registry`/
  `TransformHierarchy`) still passes together, since PHASE4 changed
  `Game::EnsureDefaultCameraExists()`'s own guard logic and PHASE3/4 deleted
  a component from `Registry`-clearing behavior other tests might rely on.

## Step 3: The Plan

### 3.1 — `docs/conventions/scene-serialization.md`: full rewrite

Rewrite this file's body (keep its existing header/intro style) to
accurately describe the POST-campaign system:

- The on-disk format is now JSON (`Scene/SceneJsonFormat.h/.cpp`,
  version 2), not the old hand-rolled TEXT grammar — name-drop
  `SceneTextFormat.h/.cpp` explicitly as "REMOVED by the
  `scene-serialization-2` campaign", so a reader who remembers the old name
  isn't left confused about where it went.
- `Scene/SceneDocument.h`'s `SceneEntityRecord` is hierarchy-aware (parent by
  array index) and carries a GENERIC `nlohmann::json components` bag, not a
  fixed field set.
- The NEW extensibility story: `src/ECS/Reflection/` (`ComponentTypeRegistry`,
  `GTE_REFLECT_FIELD`/`GTE_REFLECT_ENUM_FIELD`) is how a component becomes
  serializable — describe `BuiltinComponentReflection.cpp` as the one place
  a future component's registration is added, and restate PHASE2's own
  explicit "why NOT these five" reasoning (`MeshRenderer`/`MeshAssetSource`/
  `SkeletalAnimator`/`DynamicChainRig`/`ResolvedAnimationPose`) so this
  policy is discoverable from the docs, not only from a code comment buried
  in one `.cpp` file.
- EVERY entity in the Registry is now walked (full hierarchy, not just
  roots) — explicitly state that this SUPERSEDES the old bullet about
  "only ROOT entities .../only recognizes PrimitiveSource/MeshAssetSource"
  and the old bullet about "a multi-part asset's own child entities never
  round-trip" (PHASE4's reconciliation algorithm is the replacement
  behavior — summarize it at a level a future maintainer can act on without
  re-reading the full PHASE4 strategy doc, with a cross-reference to it for
  the full detail).
- `ClearSerializableSceneObjects()` is gone; `ClearEntireScene()` replaces
  it, with the simpler unconditional semantics.
- The new `POST /save_scene`/`POST /load_scene` endpoints exist — cross-
  reference `docs/conventions/networking.md` rather than duplicating detail.

### 3.2 — `AGENTS.md` update

Find the existing "Scene Serialization" section (search for
`scene-serialization-1` inside `AGENTS.md`). Update its summary paragraph to
mention BOTH campaigns (`scene-serialization-1` for the ORIGINAL Save/Load
loop, `scene-serialization-2` for generic reflection + full-hierarchy
support + the network endpoints), and update the file-name list
(`SceneDocument.h`/`SceneJsonFormat.h/.cpp`/`SceneBuilder.h/.cpp`, plus the
new `src/ECS/Reflection/` module) to match reality. Keep the existing
"Full convention: [docs/conventions/scene-serialization.md]" link line
unchanged (it still points at the right file, just now updated per 3.1).

### 3.3 — `TODO.md` update

Search `TODO.md` for any existing "Scene serialization" entry and update/
remove it to match the now-much-more-complete feature set. ADD one new,
clearly-scoped entry (if not already implied elsewhere) for the explicitly
deferred follow-up PHASE2 already flagged: "`SkeletalAnimator` is not yet
reflectable/serializable — restoring it correctly needs re-running
`Game::PlayAnimationOnEntity()`'s own cache-registration side effects at
Load time, not just a field copy — see `task_manager/scene-serialization-2/
PHASE2_BUILTIN_COMPONENT_REFLECTION_REGISTRATION.md`'s Step 3.3 for the
full reasoning."

### 3.4 — Test-suite completeness audit (not new features — verifying existing coverage is real)

Re-open, and if necessary extend, EVERY test file this campaign's earlier
phases touched or should have touched:

- `tests/ECS/Reflection/ComponentTypeRegistryTests.cpp` (Phase 1),
  `BuiltinComponentReflectionTests.cpp` (Phase 2), `tests/Scene/
  SceneJsonFormatTests.cpp` (Phase 3), and the pre-existing (now twice-
  rewritten) `tests/Scene/SceneBuilderTests.cpp` (rewritten by PHASE3's own
  Section 3.6 against the new hierarchy-aware/generic-component shape, then
  partially updated AGAIN by PHASE4's own Section 3.3 test-follow-up note
  for the `ClearEntireScene()` rename) — confirm each still exists, is
  still registered in `tests/CMakeLists.txt`, and still passes standalone.
  For `SceneBuilderTests.cpp` specifically, double-check its
  `ClearLeavesUntaggedEntitiesUntouched`-descended test was actually renamed
  AND had its assertion inverted per PHASE4's note (an easy step to
  accidentally skip since the test still compiles and still passes either
  way if the assertion itself was never actually flipped).
- Add a NEW, genuinely end-to-end Tier-1(-ish) test — 
  `tests/Scene/SceneBuilderRoundTripTests.cpp` if one does not already
  exist from an earlier phase, else extend it — that builds a REAL
  `Registry` with a small mixed hierarchy (a Camera root with a non-default
  `nearZ`/`farZ`, a plain Transform+Name child under it, and a
  `PrimitiveSource` root), calls the REAL
  `BuildSceneDocumentFromRegistry()` -> `SerializeSceneDocument()` ->
  `DeserializeSceneDocument()` round trip (skipping the Renderer-dependent
  Load half, which is Tier 2 — see `AGENTS.md`'s own Testability rule for
  why `Game::CreatePrimitiveEntity()` itself has no automated coverage),
  and asserts the resulting `SceneDocument`'s shape (parent indices,
  component field values) exactly matches what was built, END TO END
  THROUGH THE REAL REGISTRY — not just through the reflection primitives in
  isolation (Phase 1/2's own tests already cover THOSE in isolation; this
  test's job is to catch an integration mistake between `SceneBuilder.cpp`
  and the registry that neither phase's own isolated tests could).
- Search the WHOLE `tests/` tree AND `src/` tree for any remaining reference
  to `SceneTextFormat`/`SceneObjectKind`/`SceneObjectRecord`/
  `ClearSerializableSceneObjects`/`m_defaultCameraEnsured` (`search_in_dir`
  across `tests/` and `src/`) — any hit is either genuinely dead code that
  must be deleted, or a sign an earlier phase missed a call site update;
  resolve every hit before proceeding. `m_defaultCameraEnsured` in
  particular should already have been fully removed by PHASE4's own Section
  3.4 — this is a regression check confirming that actually happened
  cleanly (e.g. no leftover doc-comment mention in `Game.h` referring to it
  by name), not an expectation that this phase itself needs to remove it.
- If `tests/Network/NetworkRoutesTests.cpp` exists (check — PHASE5 should
  have extended it), confirm the new `/save_scene`/`/load_scene` parser
  tests from PHASE5's own Definition of Done are actually present.

### 3.5 — Full regression pass (ONLY in this phase)

Per this campaign's own workflow rules, THIS is the one phase allowed (in
fact required) to run a full build + the full regression suite:

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Any newly-failing test is a REAL regression — root-cause it against this
campaign's own changes (the two most likely culprits, based on what this
campaign touched, are: (a) something elsewhere in the engine relied on
`ClearSerializableSceneObjects()`'s OLD, narrower "only destroy tagged
roots" behavior and now unexpectedly loses an entity it used to keep across
a Load — audit every OTHER call site of the old function name, if any
existed beyond `SceneIO.cpp`, before Phase 4 deleted it; (b) something
relied on `Game::EnsureDefaultCameraExists()`'s OLD one-shot-bool timing —
e.g. a test that manually destroys the default camera and expects it to
STAY gone, which the new self-healing guard would now silently undo). Fix
the root cause, never loosen a test's expectation just to make it pass
without first understanding exactly why it changed behavior.

If a real GPU/window is available in this environment, also do a manual,
visual smoke test via `run_app_background`/`gte_send_request`
(`/get_swapchain` or `/get_game_view`) mirroring PHASE4's own manual sanity
check (Primitive + multi-part asset with a hand-edited child + Camera +
plain empty node), this time triggered end-to-end over the network
(`POST /save_scene`, restart or clear the scene, `POST /load_scene`,
capture a frame to visually confirm everything reappeared correctly) —
this is the campaign's own final, most convincing proof that the two
original reported gaps (Transform not serializing, Camera near/far not
serializing) are genuinely fixed, and that the new network endpoints
genuinely work end-to-end, not just in isolated unit tests.

### 3.6 — Final cleanup sweep

- Confirm no leftover `// PHASE4 TODO:` marker comments (PHASE3's own
  Section 3.4 left one intentionally, expecting PHASE4 to remove it once
  it replaced that code) remain anywhere in `src/`.
- Confirm `git status` (`git_status` tool) shows a clean tree apart from
  this phase's own intended changes before committing.

## Definition of Done

- [ ] `docs/conventions/scene-serialization.md` fully rewritten to describe
      the post-campaign system accurately (3.1).
- [ ] `AGENTS.md`'s "Scene Serialization" section updated (3.2).
- [ ] `TODO.md` updated, including the new `SkeletalAnimator` follow-up
      entry (3.3).
- [ ] Every test file audited/extended per 3.4, including the new
      `SceneBuilderRoundTripTests.cpp` (or equivalent) end-to-end test.
- [ ] Zero remaining references to `SceneTextFormat`/`SceneObjectKind`/
      `SceneObjectRecord`/`ClearSerializableSceneObjects`/
      `m_defaultCameraEnsured` anywhere in `src/`/`tests/`.
- [ ] Full `cmake --build build` succeeds.
- [ ] Full `ctest -C Debug --output-on-failure` passes — any regression
      found was root-caused and genuinely fixed, not papered over.
- [ ] `PHASE6_COMPLETION_REPORT.md` written (and, given this closes the
      whole campaign, consider a short `CAMPAIGN_COMPLETION_REPORT.md`
      summarizing all six phases together, mirroring
      `task_manager/stl-parser-2/CAMPAIGN_COMPLETION_REPORT.md`'s own
      existing precedent).
- [ ] `git add`/`git commit` (final commit for this campaign).
