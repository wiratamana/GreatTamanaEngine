# PHASE4 — Recipe-Spawn Reconciliation and Full Load Correctness

_Part of `task_manager/scene-serialization-2/`. **Parent: `PHASE0_MASTER_STRATEGY.md` — read it first.**_
Depends on: PHASE1, PHASE2, PHASE3 (all must be done first — this phase
edits the SAME functions PHASE3 just wrote, it does not start from scratch).
Branch: `feature/scene-serialization`.

> **Revision note (post-strategy-review):** Section 3.2 below was stress-tested
> on paper against the tricky edge cases this phase's own closing note (see
> "A note on review depth" at the end of this file) explicitly flagged as
> worth double-checking. That review found the ORIGINAL draft of the
> algorithm had three real, concrete bugs (not just "unclear prose") — an
> insufficient Pass A dedup guard that could silently reprocess an already-
> decided record, an unresolvable asset root that failed to propagate its
> "skip me" decision down to its own saved children, and a Pass B1 guard
> that accidentally also skipped re-parenting a recipe ROOT record to ITS
> OWN saved parent. All three are fixed in place below, along with an
> explicit, unambiguous, worked-through answer for every edge case this
> phase's own closing note raised but never actually answered. Nothing in
> Step 1/Step 2 changes — only Section 3.2's algorithm, and a couple of
> small companion notes in 3.1/3.4, are affected.

## Step 1: The Goal

Close PHASE3's own explicitly-documented gap: a `PrimitiveSource`/
`MeshAssetSource` entity must come back with a REAL, GPU-backed
`MeshRenderer` (visible mesh) after Load, AND (this is the NEW behavior this
campaign adds, superseding `scene-serialization-1`'s old Design Decision #3)
a multi-part imported mesh's own CHILD "submesh part" entities must have
their hand-edited Transform restored too, not silently discarded — this
must hold true EVEN for a part whose source material had no name at all
(see 3.2's dedicated discussion of the empty-Name case — this is a real,
documented, not merely hypothetical scenario per `ECS/Components/Name.h`'s
own doc comment). Also fix two smaller correctness issues found while
designing this: replace `ClearSerializableSceneObjects()` (too narrow now
that scope is "every entity") with a true `ClearEntireScene()`, and make
`Game::EnsureDefaultCameraExists()` self-healing so a Load can never leave
the engine with zero cameras.

## Step 2: The Situation / The Problem

After PHASE3, `Editor/SceneIO.cpp`'s `LoadScene()` Pass A creates every
record as a bare entity, unconditionally. For a record whose
`components["PrimitiveSource"]` key exists, this is wrong — it needs a REAL
GPU mesh, which only `Game::CreatePrimitiveEntity(Renderer&, PrimitiveType)`
knows how to build (it goes through `MeshInstantiationSystem`'s GPU mesh
cache). Same for a record carrying a non-empty `assetGuid` — it needs
`Game::CreateMeshEntityFromGtaFile(Renderer&, const std::string&)`, which
ALSO creates a whole SUB-HIERARCHY of its own child "part" entities (named
after the source `.pmx` model's materials, or left with NO `Name` component
at all when a given material itself had no name — see `EntityBlueprintNode`'s
own doc comment) — entities this document's OWN Save step (PHASE3) ALSO
already recorded as separate child records (since Save now walks EVERY
entity, including those auto-generated children). If Load naively created a
SECOND, separate set of bare entities for those saved child records on top
of the ones `CreateMeshEntityFromGtaFile()` already made, the result would be
visibly duplicated geometry.

The correct behavior: recognize that a saved child record UNDER an asset
root is describing the SAME live child entity `CreateMeshEntityFromGtaFile()`
is ABOUT to (re-)create — match them by NAME (the one stable, human-readable
key both sides agree on) rather than creating a duplicate.

## Step 3: The Plan

### 3.1 — `SceneBuilder.cpp`'s save half: fill in the `asset_guid` resolution PHASE3 deferred

Inside `BuildSceneDocumentFromRegistry()` (the ONE function PHASE3 already
wrote the walk for), add the resolution step right after the recursive walk
finishes, using the `entityOrder`/`assetDatabase` PHASE3 already threads
through:

```cpp
for (std::size_t i = 0; i < entityOrder.size(); ++i) {
    if (const MeshAssetSource* meshAssetSource = registry.TryGetComponent<MeshAssetSource>(entityOrder[i]);
        meshAssetSource != nullptr) {
        if (const AssetRecord* asset = assetDatabase.FindByPath(meshAssetSource->gtaPath); asset != nullptr) {
            document.entities[i].assetGuid = asset->guid.ToString();
        }
        // else: not (or no longer) a tracked asset - leave assetGuid empty,
        // EXACTLY like scene-serialization-1's own original behavior
        // (see SceneBuilder.h's pre-existing doc comment) - except this
        // campaign does NOT skip/omit the entity's own record entirely
        // (the old behavior) - it is still saved, generically, with
        // whatever Transform/Name it has; it will simply come back on
        // Load as a bare entity with no re-derived mesh, since Load has no
        // Guid to resolve. This is a DELIBERATE, small improvement over
        // scene-serialization-1's old "skip the whole entity silently" -
        // now at least ITS TRANSFORM/NAME still round-trips, even if its
        // mesh can't be rebuilt.
    }
}
```

### 3.2 — `Editor/SceneIO.cpp`'s `LoadScene()`: the full two-pass + reconciliation algorithm

Replace PHASE3's Pass A body with this. Read this section in full before
writing any code — the ORDER of operations matters and is easy to get
subtly wrong (a first draft of exactly this algorithm got three separate
things wrong here — see the inline comments marked "NOTE"/"fix" below for
each one, and the "Resolved edge-case behavior" list right after the
pseudocode for the worked-through reasoning).

```
resultEntities[N]           := kInvalidEntity for every record index (PHASE3, unchanged)

consumedByRecipe[N]         := false for every record index. Means: "this record's own
                                identity/creation was already fully decided by Pass A - either
                                spawned via a recipe (PrimitiveSource/MeshAssetSource), OR
                                reconciled (matched or explicitly left unmatched) as an asset
                                root's saved child, OR marked unreachable because an ancestor's
                                asset failed to resolve." Consulted by Pass A's OWN dedup guard
                                (below) and by Pass B2 (to skip re-applying the "PrimitiveSource"
                                key onto an already-recipe-spawned entity).

alreadyParentedByRecipe[N]  := false for every record index. Means: "this record's LIVE entity
                                is ALREADY correctly attached under its live parent, because
                                CreateMeshEntityFromGtaFile() parented it there itself." Set
                                ONLY on a saved child SUCCESSFULLY matched by-Name inside the
                                reconciliation loop below - NEVER on the recipe root record
                                itself (see Pass B1 for exactly why this distinction matters -
                                conflating it with consumedByRecipe was the second bug this
                                revision fixes).

childrenOf[N]                := for every index i, the list of every j where
                                 document.entities[j].parentIndex == i, in ascending j order.
                                 Built ONCE, up front, by a single linear scan over
                                 document.entities before Pass A starts - reused by the
                                 reconciliation loop AND by MarkSubtreeUnresolved() below. (This
                                 is a plain restatement of the parent/child links PHASE3's own
                                 Pass B1 already walks one record at a time - just pre-grouped by
                                 parent here for convenience/efficiency.)

function MarkSubtreeUnresolved(i):
    // Recursively marks i's ENTIRE saved subtree (i itself, plus every descendant, at any
    // depth) as "already decided by Pass A - do not create a bare entity for it, ever."
    // resultEntities stays kInvalidEntity for every one of them, forever. This is the fix for
    // the second bug found in this review: WITHOUT this call, an unresolvable asset root's
    // saved children (and grandchildren, if any) would each be reached later by Pass A's own
    // i=0..N-1 loop with resultEntities[] still kInvalidEntity AND consumedByRecipe[] still
    // false - indistinguishable from "never visited yet" - and would fall through to the
    // "plain entity" branch, spawning a stray, UNPARENTED bare entity for each one (Pass B1
    // cannot parent it under a parent that itself never resolved). This function is what makes
    // "the whole root and its saved children are skipped" actually true, not just asserted in a
    // comment.
    consumedByRecipe[i] = true
    for child_j in childrenOf[i]:
        MarkSubtreeUnresolved(child_j)

PASS A (recipe-aware entity creation):
for i in 0..N-1:
    if resultEntities[i] != kInvalidEntity or consumedByRecipe[i]: continue
        // FIX (bug #1): the original draft only checked "resultEntities[i] != kInvalidEntity"
        // here. That is NOT sufficient - an unmatched reconciled child (see the reconciliation
        // loop below: a saved child with no live counterpart gets resultEntities[j] left at
        // kInvalidEntity ON PURPOSE) looks EXACTLY like "not yet visited" to that check alone,
        // so when this SAME linear loop later reached that same index j again, it would
        // reprocess it from scratch and spawn a brand-new bare entity for it - silently
        // resurrecting a record this algorithm had already, deliberately, decided to drop.
        // consumedByRecipe[i] is the flag that actually remembers "already decided" regardless
        // of whether that decision happened to leave resultEntities[i] valid or invalid.
    record := document.entities[i]

    if record.components has key "PrimitiveSource":
        primitiveTypeJson := record.components["PrimitiveSource"]["type"]   // a string, e.g. "Cube"
        if TryParsePrimitiveTypeName(primitiveTypeJson, parsedType):
            resultEntities[i] = game.CreatePrimitiveEntity(renderer, parsedType)
            consumedByRecipe[i] = true
        // else: malformed/unrecognized primitive type string - leave resultEntities[i] ==
        // kInvalidEntity AND consumedByRecipe[i] == false (degrade gracefully, matches this
        // codebase's existing "malformed input for THIS one item never aborts the whole load"
        // convention). No MarkSubtreeUnresolved() call needed here - PrimitiveSource.h
        // documents "a primitive spawn is always a single node with no children", so there is
        // no subtree to worry about.
        continue

    if record.assetGuid is non-empty:
        guid = Guid::Parse(record.assetGuid)   // NEVER throws - Assets/AssetTypes.cpp's
                                                 // Guid::Parse() returns Guid::Invalid() for
                                                 // anything that isn't exactly 32 hex characters,
                                                 // rather than throwing/asserting - so this line
                                                 // is just as safe against a hand-corrupted
                                                 // assetGuid string in the *.gtscene file as it
                                                 // is against a validly-formatted one that simply
                                                 // no longer resolves.
        asset = assetDatabase.FindByGuid(guid)
        if asset == nullptr:
            MarkSubtreeUnresolved(i)   // FIX (bug #2): moved/deleted/malformed asset guid - this
                                        // WHOLE root AND every one of its saved children (and
                                        // grandchildren, if the source format ever produces any -
                                        // see the "one-level-deep assumption" note below) is
                                        // skipped entirely, matching scene-serialization-1's own
                                        // original per-object skip behavior - see
                                        // MarkSubtreeUnresolved()'s own doc comment above for
                                        // exactly what silently broke without this call.
            continue

        rootEntity = game.CreateMeshEntityFromGtaFile(renderer, asset->gtaPath)
        resultEntities[i] = rootEntity
        consumedByRecipe[i] = true
        // alreadyParentedByRecipe[i] is DELIBERATELY NOT set here - see Pass B1 below for why
        // the root record itself must still be free to run through the normal SetParent() step.

        // --- Reconciliation: match this record's OWN saved children (childrenOf[i]) against
        // the LIVE children CreateMeshEntityFromGtaFile() JUST created, by Name -
        // first-unused-match, walking saved children in ascending saved-sibling order against
        // live children in ascending GetChildren() order (ECS/TransformHierarchy.h documents
        // GetChildren() as sorted by siblingIndex, ties broken by stable creation order) - a
        // deterministic, reproducible pairing, not an arbitrary one. See "Resolved edge-case
        // behavior" below for #1/#2/#3 worked through explicitly against this exact loop. ---
        liveChildren := GetChildren(registry, rootEntity)
        usedLiveChild := array of bool, same length as liveChildren, all false

        for j in childrenOf[i]:
            savedChildName := document.entities[j].components has a "Name" key
                              ? document.entities[j].components["Name"]["value"] : ""
                              // "" both when the saved child record has NO Name block at all,
                              // and when it has one whose value is itself an empty string -
                              // both are treated identically by this loop (see below).
            match := kInvalidEntity
            for k in 0..liveChildren.size()-1 where not usedLiveChild[k]:
                liveName := registry.TryGetComponent<Name>(liveChildren[k]) != nullptr
                            ? registry.GetComponent<Name>(liveChildren[k]).value : ""
                            // A live part entity is NOT guaranteed to have a Name component at
                            // all - EntityBlueprintNode.h documents "empty means no Name
                            // component at all", which happens whenever the source .pmx left
                            // that material unnamed. TryGetComponent (never a bare
                            // GetComponent, which may assert/UB on a missing component) is
                            // mandatory here for exactly that reason.
                if liveName == savedChildName:
                    match = liveChildren[k]; usedLiveChild[k] = true; break
                    // FIX (bug #4 / edge case #3): an earlier draft required
                    // "savedChildName != ''" here, i.e. it NEVER matched an unnamed saved child
                    // to anything. That directly contradicted this phase's own Step 1 promise -
                    // see "Resolved edge-case behavior" #3 below for the full reasoning on why
                    // this exclusion was wrong and has been removed; empty string is now just
                    // another valid (if weak) matching key, paired positionally like any
                    // repeated name.
            resultEntities[j] = match           // kInvalidEntity if no unused live child of that
                                                  // exact name (possibly "") is left
            consumedByRecipe[j] = true           // either way - a saved child of an asset root is
                                                  // NEVER independently re-created as a bare
                                                  // entity, matched or not (this is exactly the
                                                  // flag Pass A's own dedup guard above now
                                                  // checks, closing bug #1)
            if match != kInvalidEntity:
                alreadyParentedByRecipe[j] = true  // ONLY on an actual match - see Pass B1
        continue

    // Neither PrimitiveSource nor a resolvable assetGuid - a plain entity
    // (Camera/Light/empty node/an unresolvable-asset's now-orphaned saved
    // child, etc) - PHASE3's original bare-entity behavior, unchanged:
    resultEntities[i] = registry.CreateEntity()

PASS B1 (hierarchy wiring) - UNCHANGED from PHASE3, with guards:
for i in 0..N-1:
    if resultEntities[i] == kInvalidEntity: continue   // skip anything left unresolved (an
                                                         // unmatched reconciled child, or
                                                         // anything MarkSubtreeUnresolved() touched)
    if alreadyParentedByRecipe[i]: continue
        // FIX (bug #3): ONLY true for a matched asset-root CHILD -
        // CreateMeshEntityFromGtaFile() already attached it under the SAME live root entity
        // Pass A just resolved; re-parenting it again would be harmless but pointless.
        //
        // Deliberately NOT gated on consumedByRecipe[i] the way an earlier draft of this
        // algorithm did. A PrimitiveSource/asset-root RECORD ITSELF must still run through the
        // normal SetParent() step below whenever it has its OWN saved parentIndex, because
        // CreatePrimitiveEntity()/CreateMeshEntityFromGtaFile() ALWAYS create their entity as a
        // brand-new top-level scene root with no parent at all - they know nothing about
        // whatever OTHER entity that root might have been manually dragged under in the
        // Hierarchy panel before it was saved. The earlier draft skipped SetParent() for every
        // consumedByRecipe[i] == true record, including the root itself - meaning an imported
        // mesh (or a primitive) that had been reparented under some organizational "Group" node
        // would silently revert to being a top-level scene root on every Load, a real, visible
        // regression. alreadyParentedByRecipe is a SEPARATE flag from consumedByRecipe
        // specifically so this distinction can be made correctly.
    if record.parentIndex has value:
        parentEntity = resultEntities[*record.parentIndex]
        if parentEntity != kInvalidEntity:
            SetParent(registry, resultEntities[i], parentEntity, worldPositionStays=false)
        // else: this record's OWN saved parent itself never resolved. In practice this is
        // already unreachable for a MarkSubtreeUnresolved()-affected record (its own
        // resultEntities[i] is kInvalidEntity too, so this whole iteration is already skipped by
        // the guard above) - this else branch is purely defensive, for a plain bare
        // registry.CreateEntity() parent somehow not existing, which registry.CreateEntity()
        // is not documented to ever fail.

PASS B2 (generic field application) - UNCHANGED from PHASE3, with guard:
for i in 0..N-1:
    if resultEntities[i] == kInvalidEntity: continue
    ... apply every record.components[...] field exactly as PHASE3 wrote,
        INCLUDING for consumedByRecipe[i] == true entities - this is what
        restores a hand-edited child part's Transform (superseding
        scene-serialization-1's old Design Decision #3), and what restores
        the asset ROOT's own Name/Transform too - EXCEPT the "PrimitiveSource"
        key itself is never reapplied here for a Pass-A-recipe-spawned
        entity (it was already correctly set by CreatePrimitiveEntity()
        itself - reapplying it is harmless/idempotent but unnecessary; skip
        it for clarity by checking consumedByRecipe[i] before applying that
        ONE specific key, applying every OTHER key normally) ...

PASS B3 (sibling ordering) - UNCHANGED from PHASE3, with the same
resultEntities[i] == kInvalidEntity guard added. No alreadyParentedByRecipe
check here - re-applying a matched child's already-correct saved sibling
index is harmless/idempotent, and a reparented recipe root (see the Pass B1
fix above) DOES need its saved sibling index re-applied like any other record.
```

Implement this exactly as pseudocode-to-C++, preserving every comment above
as a real code comment at the matching spot in `SceneIO.cpp` — a future
reader must be able to see WHY each guard/flag exists without re-reading this
strategy file.

#### Resolved edge-case behavior (read this before implementing — these are the exact scenarios this phase's own closing note asks an implementer to be sure about)

1. **Two saved child records under the same asset root sharing the exact
   same saved Name.** Handled by the reconciliation loop's "first unused
   match" rule: the FIRST saved record with that name (in ascending saved-
   sibling order) claims the FIRST unused live child with that same name (in
   ascending `GetChildren()` order); the SECOND saved record with that name
   claims the SECOND unused live child with that name; and so on. Both
   orderings are independently stable/reproducible for an unchanged source
   asset (saved order comes from the original live sibling order at Save
   time; live order at Load time comes from `GetChildren()`'s documented
   siblingIndex/creation-order sort) — so this is a deterministic, not
   arbitrary, pairing. If there are MORE saved records with that name than
   live children carry it, the extra saved records simply find no unused
   live child left and resolve to `kInvalidEntity` (dropped individually,
   per point 4 below) — this does not affect any other record.
2. **An asset root whose saved child COUNT no longer matches how many live
   children `CreateMeshEntityFromGtaFile()` actually produces this time**
   (fewer parts, more parts, or renamed parts, because the source `.gta`
   file changed since Save):
   - *Fewer live parts now:* one or more saved child records find no unused
     live child with a matching name at all — each such record is dropped
     individually (`kInvalidEntity`, see point 4). The rest of the root
     (and any children that DO still match) are entirely unaffected.
   - *More live parts now:* the extra, newly-appeared live children are
     never referenced by ANY saved record (the reconciliation loop only
     ever walks `childrenOf[i]`, i.e. saved records — it never iterates
     "leftover" live children looking for something to attach them to).
     They simply keep whatever Transform `CreateMeshEntityFromGtaFile()`
     just gave them (the asset's own current default) — correct, since a
     brand-new part that didn't exist at Save time cannot possibly have a
     saved hand-edited Transform to restore.
   - *Renamed parts:* functionally identical to "fewer old + more new" at
     once — the old name is dropped per the "fewer" case, the new name is
     an unreferenced extra per the "more" case.
   - None of these three ever destroys or duplicates a live entity — the
     only thing that changes is which saved Transform/Name values get
     applied to which live entities, exactly as intended.
3. **A saved child record with an empty/missing Name.** Fixed in this
   revision (bug #4 above): empty string (`""`) is now a legitimate, if
   weak, matching key, handled by the exact same first-unused-match rule as
   point 1 — the Nth saved child with no Name pairs with the Nth
   still-unmatched live child that ALSO has no Name, in iteration order.
   This is what makes a hand-edited Transform on an UNNAMED submesh part
   (a real, documented scenario — see `ECS/Components/Name.h`'s "when the
   source .pmx actually named that material", implying it might not)
   actually survive a save/load round trip, matching this phase's own Step
   1 promise. An earlier draft's explicit `savedChildName != ""` exclusion
   silently discarded every unnamed part's Transform on every single Load —
   a real bug this revision closes, not a merely theoretical one.
4. **Whether the `resultEntities[i] == kInvalidEntity` guards on Pass
   B1/B2/B3 are sufficient**, given every place `resultEntities[i]` can
   legitimately end up `kInvalidEntity`:
   - an unrecognized/malformed `PrimitiveSource` type string (record has no
     children — nothing further to guard),
   - an unresolvable `assetGuid` (moved/deleted/malformed) — now
     ALSO propagates to every descendant via `MarkSubtreeUnresolved()`,
   - an asset root's saved child that found no matching unused live child.
   The Pass B1/B2/B3 guards ALONE are sufficient for THOSE three passes —
   but the ORIGINAL draft was missing the corresponding guard in **Pass A
   itself** (bug #1 above): without also checking `consumedByRecipe[i]` at
   the top of Pass A's own loop, an unmatched reconciled child (or any
   descendant of a failed asset root) would be silently reprocessed as a
   brand-new bare entity later in that SAME loop, before Pass B1/B2/B3 ever
   even ran. This revision's Pass A guard fix is what actually closes this
   — the B1/B2/B3 guards were necessary but not, by themselves, sufficient.
5. **Whether `ClearEntireScene()`/`EnsureDefaultCameraExists()` (3.3/3.4
   below) interact correctly with all of the above.** `ClearEntireScene()`
   runs to completion BEFORE Pass A starts, so by the time Pass A calls
   `CreateMeshEntityFromGtaFile()` for any asset root, the Registry contains
   ONLY entities this SAME `LoadScene()` call has created so far —
   `GetChildren(registry, rootEntity)` for that root can therefore only ever
   return children THAT SAME call just spawned, never anything left over
   from the pre-Load scene (which no longer exists at all by that point).
   No interference with by-Name matching is possible, by construction — this
   needed no fix. `EnsureDefaultCameraExists()`'s fixed live-count guard
   (3.4) does mean a Load that results in zero `Camera` entities self-heals
   — but only on the NEXT `Game::Render()` call, not synchronously inside
   `LoadScene()` itself; `LoadScene()` returning `true` does not, by itself,
   guarantee a `Camera` exists in the Registry at that exact instant. See
   3.4's own closing note for the small, optional hardening this suggests.

A further, explicitly-scoped assumption worth writing down rather than
discovering later: the reconciliation loop above only ever matches DIRECT
children of an asset root (`childrenOf[i]`, one level) — it does not recurse
into a matched child's OWN children. This matches `CreateMeshEntityFromGtaFile()`'s
actual, current behavior (a flat root + one child per `.pmx` material, per
`EntityBlueprintNode`'s own doc comment), even though `EntityBlueprintNode`
is technically capable of representing deeper nesting (its `children` field
is itself a `std::vector<EntityBlueprintNode>`). If a future asset pipeline
ever produces genuine grandchildren under an imported mesh's own parts, this
algorithm would need to recurse — reconciling each matched pair's own
children the same way, one level down — rather than staying a single
root-only pass; a MANUALLY user-added grandchild under an already-resolved
part (e.g. an attachment empty node dragged there in the Editor, which is
NOT something `CreateMeshEntityFromGtaFile()` itself ever produces) is
already handled correctly today, since it simply falls through Pass A's
final "plain entity" branch and gets parented under the now-resolved part by
Pass B1 like any other ordinary child.

### 3.3 — Replace `ClearSerializableSceneObjects()` with `ClearEntireScene()`

In `src/Scene/SceneBuilder.h/.cpp`, replace the old, tag-filtered function:

```cpp
// Destroys EVERY root entity (and, via DestroyEntityAndDescendants(), every
// one of its descendants) - i.e. genuinely empties the ENTIRE Registry of
// every entity, no exceptions, no tag-based filtering. Correct now that
// this campaign's scope is "every entity is serializable" (see PHASE0's
// Locked Design Decision #2) - there is no longer any entity kind this
// feature does NOT own, so there is nothing left to selectively preserve.
// Call this BEFORE spawning entities from a freshly-loaded SceneDocument,
// exactly like ClearSerializableSceneObjects() (the function this
// replaces) used to.
void ClearEntireScene(Registry& registry)
{
    const std::vector<Entity> roots = GetChildren(registry, kInvalidEntity);
    for (const Entity root : roots) {
        DestroyEntityAndDescendants(registry, root);
    }
}
```

Update `Editor/SceneIO.cpp`'s `LoadScene()` to call `ClearEntireScene()`
instead of `ClearSerializableSceneObjects()`. Update `Scene/SceneBuilder.h`'s
own header doc comment (it currently documents the OLD, tag-filtered
behavior in detail — rewrite it to match the new, unconditional behavior).
Delete the old function entirely — do not leave it as unused dead code.

Because this call fully completes before Pass A begins, the by-Name
reconciliation in 3.2 above can never observe a stale, pre-Load entity —
see "Resolved edge-case behavior" point 5 above for the full reasoning; no
special interaction code is needed here, this is simply a consequence of
ordering the calls this way.

**Test follow-up (the other half of PHASE3's own Section 3.6 deferral):**
`tests/Scene/SceneBuilderTests.cpp`'s three `ClearSerializableSceneObjects()`-
named tests (`ClearDestroysPrimitiveAndAssetRootsPlusTheirChildren`/
`ClearLeavesUntaggedEntitiesUntouched`/`ClearOnEmptyRegistryIsASafeNoOp`) were
deliberately left calling the OLD function name, unchanged, by PHASE3's own
Section 3.6 — updating them is THIS phase's job, now that the function they
call is actually being deleted:
- Rename every call site from `ClearSerializableSceneObjects(registry)` to
  `ClearEntireScene(registry)` in all three tests.
- `ClearDestroysPrimitiveAndAssetRootsPlusTheirChildren`/
  `ClearOnEmptyRegistryIsASafeNoOp` need ONLY that rename — their existing
  assertions already hold true for the new unconditional behavior (a
  Primitive/Asset root and its children were always destroyed by the OLD
  function too; an empty Registry is still a safe no-op).
- `ClearLeavesUntaggedEntitiesUntouched` is now WRONG, not just stale — the
  new `ClearEntireScene()` is UNCONDITIONAL (see above), so a Camera-only
  root is NO LONGER left untouched. Rename this test (e.g.
  `ClearDestroysEveryEntityIncludingUntaggedOnes`) and INVERT its assertion:
  after `ClearEntireScene(registry)`, assert the Camera entity is ALSO no
  longer alive (`EXPECT_FALSE(registry.IsAlive(cameraEntity))`), not that it
  survived.

### 3.4 — Fix `Game::EnsureDefaultCameraExists()`'s self-healing guard

Because `ClearEntireScene()` now destroys the engine's own default Camera
entity too (it is an ordinary Camera+Transform entity like any other, no
special protection — and per PHASE0's Locked Design Decision #2, this is
correct: it is just ordinary serializable content now, saved and restored
like anything else if the loaded document happens to contain a Camera
record). The risk: `EnsureDefaultCameraExists()`'s existing guard
(`m_defaultCameraEnsured` bool, set `true` forever after the FIRST call)
would never re-trigger even if a `Load()` results in ZERO Camera entities in
the Registry (e.g. loading an otherwise-valid scene file that simply never
had a Camera in it) — leaving the scene permanently un-renderable
(`RenderSystem::ResolveActiveCameraViewProjection()`'s own `Mat4::Identity()`
fallback) with no way to recover except manually creating a Camera.

Fix, in `src/Game/Game.h`/`Game.cpp`: change the guard from the one-shot
bool to an actual live check —

```cpp
void Game::EnsureDefaultCameraExists()
{
    if (m_registry.Storage<Camera>().Size() > 0) {
        return; // A camera already exists (default OR loaded from a scene) - nothing to do.
    }
    // ... unchanged entity-creation body below ...
}
```

Remove the now-unused `m_defaultCameraEnsured` member field and its own
doc comment in `Game.h` (search for both — it is referenced in exactly the
two places already found while researching PHASE0: the guard itself, and
its declaration). This makes the function correctly idempotent AND
self-healing after ANY future scene-clearing operation, not just the
engine's own startup — a genuine, small correctness improvement worth
calling out in the completion report as a bug fix, not just plumbing.

**Small additional hardening worth doing in this same phase, while both
pieces of logic are already being touched together:** `Game::Render()` is
what calls `EnsureDefaultCameraExists()` today (once per frame, unchanged by
this phase), so the self-healing fix above only actually takes effect on the
NEXT rendered frame after a Load, not synchronously inside `LoadScene()`
itself. If anything ever needs a guaranteed-to-exist Camera immediately
after `LoadScene()` returns (e.g. a network response that reports camera
state, or a screenshot-on-load feature), that narrow window matters. Cheap
fix: have `LoadScene()` (`Editor/SceneIO.cpp`) call
`game.EnsureDefaultCameraExists()` itself, once, right before it returns
`true` — harmless/idempotent when a Camera record WAS present in the loaded
document (the live-count guard above makes it a no-op), and closes the gap
entirely when one wasn't. Not required for correctness of THIS phase's own
scope (nothing in this campaign yet depends on that narrow window), but
cheap enough that it is worth doing now rather than deferring.

### 3.5 — What "superseding Design Decision #3" means in practice (write this into `docs/conventions/scene-serialization.md` in Phase 6, but understand it now)

Before this campaign: saving/loading a multi-part imported mesh ALWAYS reset
every child part's Transform back to whatever the source `.pmx` file itself
originally specifies (children were never individually saved at all).
After this campaign: a hand-tweaked child part Transform (e.g. an artist
nudging one material's mesh part slightly via the Inspector) now DOES
survive a save/load round trip, via the by-Name reconciliation in 3.2 above
— including for a part whose material had no name at all (see 3.2's
dedicated discussion) — UNLESS the source asset file itself changed in a way
that removes/renames that specific material/part (a genuinely orphaned saved
child, silently skipped, matching every other "gracefully degrade"
precedent in this codebase).

## Definition of Done

- [ ] `SceneBuilder.cpp`'s save half resolves `assetGuid` for every
      `MeshAssetSource` root, exactly as in 3.1.
- [ ] `LoadScene()` implements the full recipe-aware Pass A +
      by-Name-reconciliation algorithm from 3.2, with every guard/flag/
      comment preserved in the actual code — specifically including: the
      `consumedByRecipe` check in Pass A's OWN dedup guard (not just
      Pass B1/B2/B3's), `MarkSubtreeUnresolved()`'s recursive propagation
      for an unresolvable asset root, the `alreadyParentedByRecipe` flag
      kept SEPARATE from `consumedByRecipe` so a recipe root record's own
      saved `parentIndex` is still honored in Pass B1, and matching on an
      empty/missing saved child Name exactly like any other name.
- [ ] `ClearSerializableSceneObjects()` is deleted; `ClearEntireScene()`
      exists and is used by `LoadScene()`.
- [ ] `tests/Scene/SceneBuilderTests.cpp`'s three Clear-related tests are
      updated per this phase's own Section 3.3 test-follow-up note (renamed
      call sites, `ClearLeavesUntaggedEntitiesUntouched` renamed and its
      assertion inverted) and pass.
- [ ] `Game::EnsureDefaultCameraExists()`'s guard is fixed to check live
      `Camera` component count instead of a one-shot bool;
      `m_defaultCameraEnsured` is removed. `LoadScene()` calls it once more
      itself, right before returning `true` (3.4's small hardening).
- [ ] Manual sanity check: save a scene with (a) a plain Primitive cube,
      (b) an imported multi-part mesh asset with one child's Transform
      hand-edited via the Inspector (including, if the test asset has one,
      a part whose material has no name at all), (c) a Camera, (d) a plain
      empty Transform+Name node parented under the Camera, (e) the SAME
      imported mesh asset's root manually reparented under a plain empty
      "Group" Transform+Name node. Reload. Confirm: the cube is visible
      with correct Transform, the mesh asset is visible with ALL parts in
      their correct positions INCLUDING the hand-edited one(s), the
      Camera's `nearZ`/`farZ` match, the empty node is still correctly
      parented under the Camera, AND the mesh asset's root is still
      correctly parented under "Group" (not reverted to a top-level scene
      root — this last check specifically exercises the Pass B1 fix above).
- [ ] Fast compile check passes.
- [ ] `PHASE4_COMPLETION_REPORT.md` written, `git add`/`git commit`.

## A note on review depth for this phase specifically

This phase's own Section 3.2 algorithm is the single most intricate piece of
new logic in this entire campaign (ordering-sensitive, index-based,
multi-pass). A focused, standalone review of exactly this section (against
two saved children sharing the same Name, an asset root whose saved child
count no longer matches its currently-live child count, a saved child with
an empty/missing Name, whether the `kInvalidEntity` guards are actually
sufficient everywhere they're needed, and whether `ClearEntireScene()`/
`EnsureDefaultCameraExists()` interact correctly with the rest of this
algorithm) has now been done — see the "Revision note" at the top of this
file and the "Resolved edge-case behavior" list in 3.2 above for the full
findings and fixes. If, once implemented, the actual C++ still behaves
differently from what 3.2 now describes for any of these cases, treat THAT
as a real implementation bug against this spec, not an open design question
— this spec is intended to be unambiguous on all five of them now. If some
OTHER tricky case not covered above still feels uncertain once implemented,
it remains reasonable to request another focused, standalone double-check of
ONLY this phase's diff before moving on to Phase 5 — see this campaign's own
PHASE0 for how the overall review workflow is organized.
