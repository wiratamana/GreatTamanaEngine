# PHASE3 — Shared Reconstruction Function & HOOK POINT B: Restore

Parent: `PHASE0_MASTER_STRATEGY.md` (Section 2.3 is the load-bearing
context for this whole phase — read it again now). Read `PHASE1_...md`/
`PHASE2_...md`'s own completion reports first.

Depends on: PHASE1 AND PHASE2 (both fully done).
Blocks: PHASE4/PHASE5 (nothing to verify state-persistence against until
restore is real).

**This is the highest-risk, heaviest phase in this whole campaign** — it
moves a large, already-thoroughly-tested, real production function
(`Editor::LoadScene()`, `src/Editor/SceneIO.cpp` lines 55-386) across a
library-tier boundary. Read the WHOLE current body of that function
(`read_file`, not a partial `read_line`) before writing a single line of
this phase's own change — this file describes the extraction boundary
precisely, but the exact, current line numbers WILL have drifted by the
time this phase is implemented; re-confirm them.

---

## STEP 1 — WHY this specific extraction boundary, and where the new
function lives

Confirmed (`PHASE0_MASTER_STRATEGY.md` Section 2.3): every type
`Editor::LoadScene()`'s own recipe-aware reconstruction logic touches —
`Game`, `Renderer`, `Registry`, `Entity`, `Transform`, `Name`,
`ComponentTypeRegistry`, `TransformHierarchy`, `AssetDatabase`,
`SceneDocument` — is gte_core-tier already. The ONLY gte_editor-tier
things `LoadScene()` itself does are: (a) resolving
`ResolveProjectRootDirectory()`, (b) reading+parsing the `*.gtscene` file
from disk (`std::ifstream`), and (c) scanning a FRESH `AssetDatabase`
against that resolved root. **None of these three things are needed by
the reconstruction logic itself** — they only produce the two VALUES
(`SceneDocument`, `AssetDatabase`) that logic actually consumes.

**The new function, `ReconstructSceneFromDocument(Game&, Renderer&, const
SceneDocument&, const AssetDatabase&) -> void`, is added to `src/Scene/
SceneBuilder.h/.cpp` (gte_core-tier)** — sitting alongside
`BuildSceneDocumentFromRegistry()`/`ClearEntireScene()`, its own natural
siblings (the SAVE half, and the "make room" half, of this exact same
ECS-facing bridge). `SceneBuilder.h`'s own current header comment
(confirmed, lines 8-15) claims this logic must stay in `Editor/SceneIO.h`
because it "needs a live `Renderer`" — **this reasoning must be corrected
as part of this phase**, since `Renderer` has never been gte_editor-tier;
update that comment to state the TRUE, current reasoning (this function IS
now here, is genuinely core-tier, and is reused by both the Editor's own
`Ctrl+O` path and the Project Assembly Hot Reload restore path — mirrors
Unity's own `SceneManager.LoadScene()` being a runtime/Player capability,
not merely an Editor one).

`SceneBuilder.h`'s new function declaration (append after `ClearEntireScene()`):

```cpp
class Game; // forward-declare - do not #include Game/Game.h from this header if avoidable; SceneBuilder.cpp needs the full type, this header does not.
class Renderer;

// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE3 - the LOAD half of this ECS-facing bridge,
// extracted from Editor/SceneIO.cpp's own LoadScene() (which now becomes
// a thin wrapper: resolve the project root + a fresh AssetDatabase + parse
// the *.gtscene file, then call this SAME function). Genuinely gte_core-
// tier - every type this function touches already is (Game, Renderer,
// Registry, ComponentTypeRegistry, AssetDatabase) - moved here, per the
// user's own explicit direction, because "Load Scene is a core engine
// feature that can be controlled on the editor side also" (mirrors
// Unity's own SceneManager.LoadScene() being available in a Player build,
// not merely the Editor). Reused verbatim by
// Core/Plugins/ProjectAssemblyHotReload.cpp's own
// RestoreProjectAssemblyHotReloadState() (HOOK POINT B) - see that
// file for the second real caller.
//
// CALLER'S OWN RESPONSIBILITY, NOT this function's: calling
// ClearEntireScene(game.GetRegistry()) FIRST, exactly once, before this
// call - this function does NOT clear the scene itself (both of its two
// real callers already have their own, slightly different reasons for
// when/whether a fresh AssetDatabase needs scanning around that same
// clear step, so the ordering is left explicit at each call site rather
// than hidden inside this one shared function).
//
// Recipe-aware: a record carrying "PrimitiveSource" is spawned via
// Game::CreatePrimitiveEntity(); a record with a resolvable assetGuid is
// spawned via Game::CreateMeshEntityFromGtaFile() with by-Name child
// reconciliation; everything else becomes a bare entity. Every record's
// saved `components` fields are then applied generically via
// ComponentTypeRegistry, and its saved parent/sibling-index restored. See
// the ORIGINAL Editor/SceneIO.cpp LoadScene() body (now superseded/thinned
// by this extraction) for the full, exact, already-proven algorithm - do
// NOT re-derive it from scratch; copy its real, working logic verbatim,
// changing only the enclosing function's own name/signature/file.
//
// Also calls game.EnsureDefaultCameraExists() once, at the end - matches
// LoadScene()'s own existing final guarantee.
void ReconstructSceneFromDocument(Game& game, Renderer& renderer, const SceneDocument& document, const AssetDatabase& assetDatabase);
```

`SceneBuilder.cpp` — append the `#include`s this new function needs
(`../Game/Game.h`, `../Renderer/Renderer.h`, `../ECS/Components/Name.h`,
`../Renderer/Primitives/PrimitiveMeshGenerator.h` — needed for
`PrimitiveType`/`TryParsePrimitiveTypeName()`, used by the copied Pass A
body's `"PrimitiveSource"` branch; technically already reachable
transitively through `Game.h` (confirmed, `Game.h` line 11, already
`#include`s this same header), but `SceneIO.cpp` itself explicitly
`#include`s it directly too (confirmed, that file's own current include
list) rather than relying on that transitive path — match that same
explicit-include discipline here, don't rely on a transitive include that
could silently break if `Game.h`'s own includes are ever cleaned up later
— and `<functional>` — needed for `std::function<void(std::size_t)>
markSubtreeUnresolved`; also already reachable transitively (via
`ECS/Registry.h` -> `ECS/Entity.h`, confirmed, and via
`ECS/Reflection/ComponentTypeRegistry.h` -> `ComponentTypeDescriptor.h`,
both already `#include`d by `SceneBuilder.cpp` today), but `SceneIO.cpp`
again explicitly `#include`s it directly (confirmed, line 15) rather than
relying on either transitive path — do the same here. `<fstream>` is NOT
needed here — file I/O stays in `SceneIO.cpp`), then
**copy `LoadScene()`'s own existing body VERBATIM**, from `Registry&
registry = game.GetRegistry();` through (but not including) `return true;`,
into this new function — with these EXACT, minimal, mechanical
adjustments only:

1. The function no longer reads/parses a file or returns `bool` — it
   receives an already-parsed `const SceneDocument& document` directly
   (rename every `document->...` dereference to `document....`, since the
   parameter is now a reference, not a `std::optional`), and returns
   `void` (drop the final `return true;`; the caller already knows
   parsing succeeded, since it is the one who parsed it).
2. The function no longer calls `ClearEntireScene(registry)` itself (per
   this function's own doc comment above — this responsibility moves to
   each of the two real callers, since `Editor::LoadScene()`'s own
   ordering — clear, THEN scan a fresh `AssetDatabase` — differs slightly
   from `RestoreProjectAssemblyHotReloadState()`'s own ordering — the
   `AssetDatabase` was already refreshed by `CaptureProjectAssemblyHotReloadState()`
   earlier in the SAME cycle, PHASE2 — so hard-coding one fixed order
   inside this shared function would be needlessly presumptuous).
3. The function no longer scans its own `AssetDatabase` — it receives one,
   already scanned, as a parameter (drop the `const std::filesystem::path
   projectRoot = ResolveProjectRootDirectory(); AssetDatabase
   assetDatabase; assetDatabase.RefreshFromDirectory(projectRoot);` block
   entirely — this is EXACTLY the gte_editor-tier dependency this whole
   extraction exists to remove).

Everything else — the `resultEntities`/`consumedByRecipe`/
`alreadyParentedByRecipe`/`childrenOf` bookkeeping, the
`markSubtreeUnresolved` recursive lambda, Pass A/B1/B2/B3, the final
`game.EnsureDefaultCameraExists()` call — copies across **completely
unchanged**. This is a pure code-motion refactor, not a rewrite; treat any
urge to "improve" the algorithm while moving it as a scope violation for
this phase (fix bugs, if any are found, only as their own, separately-
justified change, never silently folded into this move).

---

## STEP 2 — `Editor::LoadScene()` becomes a thin wrapper

`src/Editor/SceneIO.cpp` — replace the (now-extracted) body with:

```cpp
bool LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath)
{
    std::ifstream file(scenePath, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    const std::optional<SceneDocument> document = DeserializeSceneDocument(buffer.str());
    if (!document.has_value()) {
        return false; // Malformed file - do NOT touch the current scene at all.
    }

    // editor-core-separation-15 campaign, PHASE3 - the recipe-aware
    // reconstruction itself now lives in Scene/SceneBuilder.h/.cpp's
    // ReconstructSceneFromDocument() (gte_core-tier), shared with Project
    // Assembly Hot Reload's own restore path - see that function's own
    // doc comment. This wrapper's own remaining job: resolve the project
    // root, scan a fresh AssetDatabase (unchanged behavior from before
    // this refactor - "scanned fresh, right here, against
    // ResolveProjectRootDirectory() - never persisted/cached across
    // calls", this file's own pre-existing doc comment in SceneIO.h,
    // still true), clear the scene, then call the shared function.
    const std::filesystem::path projectRoot = ResolveProjectRootDirectory();
    AssetDatabase assetDatabase;
    assetDatabase.RefreshFromDirectory(projectRoot);

    ClearEntireScene(game.GetRegistry());
    ReconstructSceneFromDocument(game, renderer, *document, assetDatabase);
    return true;
}
```

**Verify concretely — this is the single most important check in this
whole phase**: this refactor MUST NOT change `LoadScene()`'s own
observable behavior at all. Confirm via:
1. A plain, full incremental build succeeds.
2. Launch `GreatTamanaEditor.exe`. Trigger a real `Ctrl+S`-equivalent
   (`POST /save_scene`) then a real `Ctrl+O`-equivalent (`POST
   /load_scene`) against a real, non-trivial scene (spawn a couple of
   primitives first via `POST /instantiate_primitive`, parent one under
   the other, then save/reload) — confirm, via `GET
   /project_assembly/debug/scene_snapshot`, the reloaded world is
   byte-for-byte equivalent to what was saved (hierarchy, Transform
   values, names).
3. Run this whole engine's existing scene-serialization Tier-1/Tier-2
   test suite (`search_in_dir` for the real test file names under
   `tests/` — e.g. anything under `tests/Editor/` or `tests/Scene/`
   covering `SceneIO`/`SceneBuilder` — confirm every one of those still
   passes with ZERO changes to their own test bodies; a test needing to
   change to keep passing is itself a signal this refactor changed real
   behavior, not just its own internal shape).

**Also required by this same Step, not optional cleanup**:
`docs/conventions/scene-serialization.md` currently documents this exact
logic as living in `Editor/SceneIO.cpp` and describes `Scene/SceneBuilder.h/.cpp`
as "the ECS-facing bridge, still Renderer-free" (its own "Module layout"
section) — both become FALSE the moment this Step lands, the exact same
class of "shipped code moved, doc never followed" drift Section 2.4 of
`PHASE0_MASTER_STRATEGY.md` already found and fixed once in
`project-assembly-system.md`. Update, in that same file:
- The `src/Scene/` bullet's own "still Renderer-free" claim (remove it;
  state instead that `ReconstructSceneFromDocument()` now lives here,
  genuinely gte_core-tier, and needs a live `Renderer` — reused by both
  `Editor::LoadScene()` and Project Assembly Hot Reload's own restore
  path).
- The `src/Editor/SceneIO.h/.cpp` bullet's own `LoadScene()` description
  (state it is now a thin wrapper: resolve the project root, scan a fresh
  `AssetDatabase`, parse the file, clear the scene, then call the shared
  function).
- The "Recipe-spawn reconciliation" section's own opening sentence
  (currently attributes the whole algorithm to `Editor/SceneIO.cpp`'s
  `LoadScene()` — correct this to name `ReconstructSceneFromDocument()`
  in `Scene/SceneBuilder.cpp` as the algorithm's real, current home,
  with `LoadScene()` merely calling into it).

Do not fix this same file's own, unrelated, pre-existing `GTE_ENABLE_EDITOR`
staleness (`GTE_ENABLE_EDITOR` no longer exists anywhere in this codebase,
per `AGENTS.md`'s "Editor Module Structure" section) while in here — that
staleness predates this campaign and is not something this Step's own
change causes; touch only the sentences this Step's own code move actually
invalidates, to keep this edit reviewable as "what this campaign changed."

**Note on test coverage, so nobody mistakes this for a missed obligation**:
`ReconstructSceneFromDocument()` needs a live `Renderer` (for
`Game::CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()`) exactly
like `LoadScene()` already did before this move — it is Tier-2 (GPU-
dependent), not Tier-1, so `AGENTS.md`'s "every Tier 1 change needs a
matching test change" rule does not apply to it directly. The existing
`tests/Scene/SceneRoundTripIntegrationTests.cpp` already covers the
underlying Pass A/B1/B2/B3 algorithm (minus recipe spawning, which needs a
live `Renderer`) against a plain `Registry` and must keep passing unchanged
(Step 2, item 3 above) — this remains the correct, permanent regression
guard for this logic; do not attempt to add a new GPU-dependent test for
`ReconstructSceneFromDocument()` itself as part of this phase.

---

## STEP 3 — HOOK POINT B: `RestoreProjectAssemblyHotReloadState()`'s real
body

`src/Core/Plugins/ProjectAssemblyHotReload.h` — update this function's own
signature to accept `projectRootDirectory` (PHASE1 already threads it
through the enclosing orchestrator):

```cpp
// HOOK POINT B - called AFTER the (new-or-rolled-back) code's own
// GTE_RegisterProject has already run (see PerformProjectAssemblyHotReload()'s
// own sequencing, editor-core-separation-14 PHASE4 - unchanged by this
// campaign), so every render-pass/panel/component-type registration the
// now-running code needs already exists BEFORE this call ever tries to
// apply saved component data referencing it - this ordering is what makes
// applying a saved custom-component field safe (ComponentTypeRegistry::Find()
// would return nullptr, and the field would be silently, confusingly
// dropped, if this ran too early). Real body, this campaign - reuses
// Scene/SceneBuilder.h's ClearEntireScene() + the new,
// ReconstructSceneFromDocument() (PHASE3, this file), sharing the SAME
// code the Editor's own Ctrl+O LoadScene() now goes through.
void RestoreProjectAssemblyHotReloadState(Core& core, Renderer& renderer, const HotReloadStateSnapshot& snapshot,
    const std::filesystem::path& projectRootDirectory);
```

(Note the new `Renderer&` parameter — `ReconstructSceneFromDocument()`
needs one, and `PerformProjectAssemblyHotReload()` already has a live
`Renderer& renderer` in scope at its own call site — pass it straight
through.)

`ProjectAssemblyHotReload.cpp` — real body, replacing the existing no-op
stub (lines 34-38):

```cpp
#include "../../Scene/SceneBuilder.h"

void RestoreProjectAssemblyHotReloadState(Core& core, Renderer& renderer, const HotReloadStateSnapshot& snapshot,
    const std::filesystem::path& projectRootDirectory)
{
    // LDD-HR7 - reuses the SAME core.GetAssetDatabase() instance
    // CaptureProjectAssemblyHotReloadState() already refreshed earlier in
    // THIS SAME cycle (PHASE2) - deliberately NOT refreshed a second time
    // here (nothing could have added a new on-disk asset during the
    // freeze - the whole engine, including any file-watching, was frozen
    // solid the entire time, LDD-HR4).
    ClearEntireScene(core.GetGame().GetRegistry());
    ReconstructSceneFromDocument(core.GetGame(), renderer, snapshot.document, core.GetAssetDatabase());

    GTE_LOG_INFO("ProjectAssemblyHotReload",
        "RestoreProjectAssemblyHotReloadState: restored " + std::to_string(snapshot.document.entities.size()) + " entities.");
}
```

`PerformProjectAssemblyHotReload()`'s own call site (`.cpp`, currently:
`RestoreProjectAssemblyHotReloadState(core, snapshot);`) — update to pass
the two new arguments through:

```cpp
RestoreProjectAssemblyHotReloadState(core, renderer, snapshot, projectRootDirectory);
```

(`renderer`/`projectRootDirectory` are already both in scope at this exact
call site — `renderer` is this whole function's own second parameter,
`projectRootDirectory` was threaded through by PHASE1.)

---

## STEP 4 — Isolation test for HOOK POINT B alone (before PHASE4/5's own
full round-trip test exists)

Since PHASE4 has not yet added a REAL, mutable marker entity, this phase's
own isolation proof is necessarily about MECHANISM, not yet about a
specific custom-component value:

1. Launch the engine. Spawn one extra primitive via `POST
   /instantiate_primitive` at a distinctive, non-default world position.
2. `GET /project_assembly/debug/scene_snapshot` — confirm the new
   primitive is present with that exact position.
3. Trigger a real hot-reload cycle (`POST
   /project_assembly/hot_reload?name=ProjectAssemblyProbe`, no source
   change).
4. `GET /project_assembly/debug/scene_snapshot` again — confirm the SAME
   primitive is STILL present, at the SAME position, with a plausibly
   DIFFERENT `Entity` numeric ID if the snapshot format exposes one
   (expected and harmless per LDD-HR8 — entity identity is not preserved,
   only entity DATA is).
5. Confirm, via `GET /get_swapchain`, normal rendering, no corruption, no
   crash.

---

## Definition of Done — this phase only

- [ ] `ReconstructSceneFromDocument()` exists in `Scene/SceneBuilder.h/.cpp`
      (gte_core-tier), compiles, and its own body is a faithful, unmodified
      copy of `LoadScene()`'s own prior recipe-aware logic (Pass A/B1/B2/B3
      + `markSubtreeUnresolved` + the final `EnsureDefaultCameraExists()`
      call), minus the file-I/O/`AssetDatabase`-scan/`ClearEntireScene()`
      responsibilities now left to each caller.
- [ ] `Editor::LoadScene()` is a thin wrapper calling the shared function;
      Step 2's own mandatory regression proof (byte-for-byte Ctrl+S/Ctrl+O
      round-trip, plus the FULL existing scene-serialization test suite
      unchanged and passing) is complete.
- [ ] `SceneBuilder.h`'s own stale "Deliberately still Renderer-free"
      header comment is corrected to describe the TRUE, current state.
- [ ] `HOOK POINT B`'s real body exists, compiles, and Step 4's own
      isolation test passes live: a runtime-spawned primitive's own
      Transform data genuinely survives a real hot-reload cycle.
- [ ] `PerformProjectAssemblyHotReload()`'s own call sites for both hook
      functions are updated to the new signatures; the enclosing
      orchestrator's own CONTROL FLOW is otherwise completely unchanged.
- [ ] `docs/conventions/scene-serialization.md`'s "Module layout"/"Recipe-
      spawn reconciliation" sections no longer attribute this logic to
      `Editor/SceneIO.cpp` alone — both now name `Scene/SceneBuilder.cpp`'s
      `ReconstructSceneFromDocument()` as its real, current home.

## What this phase does NOT do

- Does NOT yet prove a custom, Project-Assembly-defined component's value
  survives with a genuinely runtime-mutated value — PHASE4 (the marker
  entity + the one narrow mutation route) and PHASE5 (the full test) are
  what actually exercise that specific claim.
- Does NOT change anything about `DeserializeSceneDocument()`/
  `SerializeSceneDocument()` (`Scene/SceneJsonFormat.h`) — those are
  untouched, unmoved, already gte_core-tier.
- Does NOT attempt to preserve `Entity` numeric identity across a reload
  (LDD-HR8, `PHASE0_MASTER_STRATEGY.md`) — only entity DATA is preserved,
  by design.
