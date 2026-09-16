# PHASE3_INSTANTIATE_MESH_ASSET_ENGINE_COMMAND.md

**Campaign:** `stl-parser-2` (parent: `PHASE0_MASTER_STRATEGY.md` — READ FIRST)
**Branch:** `feature/stl-parser-impl`
**Depends on:** nothing from `PHASE1`/`PHASE2` (this phase is fully
independent of the import-side bridge — it only extends the ALREADY-EXISTING
`EngineCommandBridge`). Can, in principle, be implemented in parallel with
`PHASE1`/`PHASE2`, but this campaign's own delegation flow still runs it
sequentially after them (see `PHASE0`).

## Step 1: The Goal (Where are we going?)

A new, bare-bones `Game::InstantiateMeshAssetFromGtaFile()` method — an
`Outcome`-returning wrapper around the ALREADY-EXISTING, UNCHANGED
`Game::CreateMeshEntityFromGtaFile()` — plumbed through as a fifth value on
the already-existing `EngineCommandBridge`/`EngineCommandKind`. By the end
of this phase, a manually-constructed `EngineCommandRequest` (e.g. from a
unit test) can already fully spawn a Mesh `*.gta` end to end, with a rich
success/failure outcome — the only thing missing after this phase is the
HTTP route itself (`PHASE4`).

## Step 2: The Situation (Where are we now?)

- `src/Game/Game.h`'s `Entity CreateMeshEntityFromGtaFile(Renderer&,
  const std::string& absoluteGtaPath)` already does 100% of the actual
  spawning work this phase needs (see `PHASE0`'s own "What already exists"
  section) — it returns `kInvalidEntity` on any failure, with NO error
  message of any kind. This phase must NOT modify this method's body or
  signature at all — Locked Design Decision 4 (`PHASE0`) requires the new
  method to be a pure, additive wrapper.
- `src/Game/EngineCommandResults.h` already holds four outcome structs
  (`InstantiatePrimitiveOutcome`, `DeleteEntityOutcome`, `SetEntityTrsOutcome`,
  `InstantiateLightOutcome`) — `InstantiatePrimitiveOutcome`'s own shape
  (`success`/`errorMessage`/`entityIndex`/`entityGeneration`/`resolvedName`)
  is the closest existing template for this phase's new outcome type.
- `src/Application/EngineCommandBridge.h`'s `EngineCommandKind` enum
  (`InstantiatePrimitive`/`DeleteEntity`/`SetEntityTrs`/`InstantiateLight`)
  and `src/Application/EngineCommandDispatch.cpp`'s `ExecuteEngineCommand()`
  switch statement are exactly where this phase's fifth value slots in —
  read both files in full; the pattern is completely mechanical (one more
  enum value, one more `struct ...Command` field on `EngineCommandRequest`,
  one more `switch` case).
- `src/ECS/Registry.h`'s `TryGetComponent<T>(Entity)` (returns `T*`, or
  `nullptr`) is how this phase's new method reads back the freshly-spawned
  root entity's `Name` component (`src/ECS/Components/Name.h`) for its
  `resolvedName` output field.

## Step 3: The Plan

### 3.1 — `src/Game/EngineCommandResults.h` changes

Add a new outcome struct, placed right after `InstantiateLightOutcome`:

```cpp
// task_manager/stl-parser-2 campaign, PHASE3 - outcome of one
// Game::InstantiateMeshAssetFromGtaFile() call. Deliberately the SAME
// SHAPE as InstantiatePrimitiveOutcome (success/errorMessage/entityIndex/
// entityGeneration/resolvedName) minus the parenting fields - this method
// never parents/positions/renames anything beyond what
// CreateMeshEntityFromGtaFile() already does today (PHASE0's Locked
// Design Decision #4 - "bare-bones wrapper", no world_position/name/parent
// input of any kind).
struct InstantiateMeshAssetOutcome {
    bool success = false;
    std::string errorMessage;

    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    // The spawned ROOT entity's actual Name component value (the source
    // file's own stem, e.g. "terrain" for "terrain.gta") - empty if the
    // root somehow has no Name component at all (should not happen in
    // practice - MeshInstantiationSystem::SpawnMeshAsset() always assigns
    // one - but defensively read back rather than assumed).
    std::string resolvedName;
};
```

### 3.2 — `src/Game/Game.h` changes

Add a new public method declaration, placed right after
`CreateMeshEntityFromGtaFile()`'s own declaration:

```cpp
// task_manager/stl-parser-2 campaign, PHASE3 - a network-command-friendly,
// Outcome-returning wrapper around CreateMeshEntityFromGtaFile() above -
// see that method's own doc comment for the FULL spawn behavior this
// preserves EXACTLY, unchanged (world origin, unparented, named after the
// file - PHASE0_MASTER_STRATEGY.md's Locked Design Decision #4, "bare-bones
// wrapper"). The only thing this method ADDS is a rich success/failure
// report: `outcome.success == false` (with `errorMessage` explaining why,
// and NO entity created) exactly when CreateMeshEntityFromGtaFile() would
// have returned kInvalidEntity - missing file, wrong/corrupt *.gta, or a
// decodable-but-empty (zero vertices/triangles) mesh. Never throws.
InstantiateMeshAssetOutcome InstantiateMeshAssetFromGtaFile(Renderer& renderer, const std::string& absoluteGtaPath);
```

### 3.3 — `src/Game/Game.cpp` changes

Implement it right after `CreateMeshEntityFromGtaFile()`'s own definition:

```cpp
InstantiateMeshAssetOutcome Game::InstantiateMeshAssetFromGtaFile(Renderer& renderer, const std::string& absoluteGtaPath)
{
    InstantiateMeshAssetOutcome outcome;

    const Entity root = CreateMeshEntityFromGtaFile(renderer, absoluteGtaPath);
    if (root == kInvalidEntity) {
        outcome.success = false;
        outcome.errorMessage = "failed to load or spawn a Mesh asset from \"" + absoluteGtaPath
            + "\" - the file may be missing, not a valid Mesh *.gta, or empty (zero vertices/triangles)";
        return outcome;
    }

    outcome.success = true;
    outcome.entityIndex = root.index;
    outcome.entityGeneration = root.generation;
    if (const Name* name = m_registry.TryGetComponent<Name>(root)) {
        outcome.resolvedName = name->value; // confirm the exact field name against ECS/Components/Name.h before assuming "value".
    }
    return outcome;
}
```

Confirm the exact field name on `Name` (`src/ECS/Components/Name.h`) before
writing `name->value` verbatim — read that header first; adjust if the
actual member is spelled differently.

**Do not** call `CreateMeshEntityFromGtaFile()`'s own internal
`SpawnMeshAsset()`/`MeshInstantiationSystem` machinery directly from this
new method — always go through the existing public
`CreateMeshEntityFromGtaFile()` method itself, so its own skinning/physics
hand-off side effects (see that method's own doc comment,
`RegisterSkinnedMesh()`/`RegisterDynamicChains()`/`AttachDynamicChainRigIfNeeded()`/
`RegisterGpuSkinnedMesh()`) still run unchanged for a skinned model spawned
this way too — `terrain.stl` itself is boneless/riggless (Locked Design
Decision from `stl-parser-1`), so this path isn't exercised by this
campaign's own smoke test, but a FUTURE caller instantiating a skinned PMX
model through this same new method must get identical behavior to dragging
it into Hierarchy by hand.

### 3.4 — `src/Application/EngineCommandBridge.h` changes

Add a fifth value to `EngineCommandKind`:

```cpp
enum class EngineCommandKind {
    InstantiatePrimitive,
    DeleteEntity,
    SetEntityTrs,
    InstantiateLight,
    // task_manager/stl-parser-2 campaign, PHASE3 - spawns an already-
    // imported Mesh *.gta asset (see Game::InstantiateMeshAssetFromGtaFile()) -
    // reuses this SAME bridge (not a new one) because this is exactly the
    // same shape of request InstantiatePrimitive/InstantiateLight already
    // are: an ECS+Renderer-mutating spawn - see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #9.
    InstantiateMeshAsset,
};
```

Add a plain request payload struct, placed right after
`DeleteEntityCommand`:

```cpp
// Plain request payload for one InstantiateMeshAsset command.
struct InstantiateMeshAssetCommand {
    std::string absoluteGtaPath;
};
```

Add it as a new field on both `EngineCommandRequest` and
`EngineCommandResult` (mirroring how `setEntityTrs`/`instantiateLight` were
added alongside the two older fields):

```cpp
// EngineCommandRequest:
InstantiateMeshAssetCommand instantiateMeshAsset;

// EngineCommandResult (needs "#include "../Game/EngineCommandResults.h""
// already present - confirm it is):
InstantiateMeshAssetOutcome instantiateMeshAsset;
```

### 3.5 — `src/Application/EngineCommandDispatch.cpp` changes

Add a new `case` to `ExecuteEngineCommand()`'s `switch`, right after the
existing `InstantiateLight` case:

```cpp
case EngineCommandKind::InstantiateMeshAsset: {
    result.instantiateMeshAsset = game.InstantiateMeshAssetFromGtaFile(
        renderer, request.instantiateMeshAsset.absoluteGtaPath);
    break;
}
```

### 3.6 — Tests

**Do NOT create a new `tests/Game/InstantiateMeshAssetFromGtaFileTests.cpp`
file — mirroring `PHASE1`'s own established precedent for
`NullEditorLayer::ImportExternalAssetIntoProject()` (an accepted gap,
verified by inspection, with no test file invented just to have something to
register in `tests/CMakeLists.txt`).** **Confirmed by directly reading the
real source tree during this campaign's Iteration 2 double-check** (not just
"likely", as an earlier draft of this section hedged):
`Game::CreateMeshEntityFromGtaFile()` itself has NO automated test coverage
anywhere today — the only hit for its name under `tests/` is a comment in
`tests/Game/GameLoopPhysicsWithoutAnimationTests.cpp` explaining that THAT
file hand-mirrors its skinning/physics hand-off logic without actually
calling it; nothing anywhere constructs a real `Renderer` and calls either
method. This phase's new wrapper inherits the exact same limitation, and
this phase's own completion report must say so explicitly rather than
silently skip writing any test at all.

**Do NOT attempt to isolate the "missing file" `kInvalidEntity` branch as a
Renderer-free Tier-1 test — this is confirmed NOT achievable, not merely
"worth double-checking" as an earlier draft of this section suggested.**
It's true that `MeshAssetGpuCatalog::EnsureMeshAsset()` reads and validates
the `*.gta` file (via `ReadGtaFile()`) BEFORE ever touching its own
`Renderer&`/`RenderSystem&` parameters — but that is irrelevant here, because
the blocker is one call-signature layer further OUT: both
`CreateMeshEntityFromGtaFile()` and this phase's new
`InstantiateMeshAssetFromGtaFile()` take `Renderer&` (a reference, never a
pointer that could be null/stubbed), and `Renderer`'s own constructor is
`explicit Renderer(Window&)` (`src/Renderer/Renderer.h`) — it unconditionally
stands up a real SDL window and a real Vulkan instance/device/swapchain.
There is no way to obtain a `Renderer&` to pass to either method at all
without a live, real windowing/GPU environment, regardless of how early the
callee itself bails out internally. This is a hard Tier-2 wall at the
call-site level, not a data-flow question about which branch runs first —
document it as the same accepted, permanent Tier-2 gap
`Game::InstantiatePrimitive()`/`CreateMeshEntityFromGtaFile()` already have,
and do not spend implementation time trying to work around it (e.g. via an
unsafe `reinterpret_cast`-constructed fake `Renderer&` — nothing in this
codebase does that anywhere, for good reason).

**Do not invent a new `EngineCommandDispatchTests.cpp` file either — none
exists today, for anyone.** Confirmed by browsing `tests/Application/`: there
is no dispatch-level test file there at all (only
`EngineCommandBridgeTests.cpp`, which tests the bridge's own mutex/timeout
plumbing, never `EngineCommandDispatch.cpp`'s `ExecuteEngineCommand()`
switch itself) — searching all of `tests/` for `ExecuteEngineCommand`
likewise finds zero hits. `ExecuteEngineCommand()` takes a live `Renderer&`
unconditionally (regardless of which `EngineCommandKind` is actually
dispatched), so EVERY ONE of its existing four cases already shares this
same un-tested, accepted Tier-2 gap — this phase's fifth case
(`InstantiateMeshAsset`) inherits it identically. This phase's own
completion report should say so plainly (mirroring the note above) rather
than either silently skipping the topic or trying to conjure a dispatch-level
test file that has no precedent anywhere in this codebase.

If, contrary to the analysis above, this phase's own implementer finds a
genuinely new pure-logic seam worth testing (e.g. a future refactor that
splits some part of this wrapper into a live-`Renderer`-free helper), it must
still be registered in `tests/CMakeLists.txt` like any other new test file —
but the DEFAULT, EXPECTED outcome of this phase, per the analysis above, is
zero new test files and an honest note in the completion report, not a
forced/contrived test.

## Definition of Done (this phase's slice)

- `InstantiateMeshAssetOutcome` exists in `EngineCommandResults.h`.
- `Game::InstantiateMeshAssetFromGtaFile()` exists, is a pure additive
  wrapper (verified by inspection: it calls `CreateMeshEntityFromGtaFile()`
  exactly once, with no other spawn-affecting logic of its own).
- `EngineCommandKind::InstantiateMeshAsset` is a real, dispatched fifth
  value on the EXISTING `EngineCommandBridge` — not a new bridge.
- `cmake --build build` succeeds; `ctest -C Debug --output-on-failure`
  passes with zero regressions, and any newly-added tests (or an explicit,
  honest note about which Tier-2 gaps could not be covered) are included.
- No `POST /instantiate_asset` HTTP route exists yet — that is explicitly
  out of scope for this phase (`PHASE4`'s job).
