# PHASE2 — HOOK POINT A: Capture State Snapshot

Parent: `PHASE0_MASTER_STRATEGY.md`. Read `PHASE1_...md`'s own completion
report first — this phase assumes `Core::GetAssetDatabase()` and
`PerformProjectAssemblyHotReload()`'s new 7th parameter
(`projectRootDirectory`) already exist, compile, and were live-verified as
a zero-regression change.

Depends on: PHASE1 (fully done).
Blocks: PHASE3 (HOOK POINT B's own restore logic reads the SAME
`HotReloadStateSnapshot::document` field this phase gives real content).

End state of this phase: `HotReloadStateSnapshot` carries a real
`SceneDocument`; `CaptureProjectAssemblyHotReloadState()` builds it from
the live world, via `Core`'s own, now-refreshed `AssetDatabase`; a live
cross-check against the already-working `GET
/project_assembly/debug/scene_snapshot` endpoint proves the new capture
logic produces the SAME data that endpoint already reports for the same
live world. **HOOK POINT B remains a no-op stub this phase** — the
snapshot is captured correctly, but nothing yet uses it to restore
anything (deliberately, so this phase's own correctness can be verified in
total isolation from PHASE3's much larger refactor).

---

## STEP 1 — `HotReloadStateSnapshot` gets a real field

`src/Core/Plugins/ProjectAssemblyHotReload.h` — confirmed current,
line 19: `struct HotReloadStateSnapshot {};`. Replace with:

```cpp
// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE2 - the real snapshot value type. `document` is
// exactly what Scene/SceneBuilder.h's BuildSceneDocumentFromRegistry()
// already produces for the live world - the SAME value type
// Editor/SceneIO.cpp's own SaveScene() and
// EditorHotReloadDebugCapability::BuildSceneSnapshotJson() (GET
// /project_assembly/debug/scene_snapshot) both already build, from the
// SAME function. Captures every entity, every built-in AND
// Project-Assembly-defined custom reflected component - see
// PHASE0_MASTER_STRATEGY.md Section 2 for the full "why this is already
// almost the whole feature" reasoning. Deliberately does NOT capture
// anything outside the ECS Registry - see the permanent honest-boundary
// documentation this campaign's own PHASE5 adds to
// docs/conventions/project-assembly-system.md.
#include "../../Scene/SceneDocument.h"

struct HotReloadStateSnapshot {
    SceneDocument document;
};
```

(Place the new `#include` alongside this header's own existing includes,
at the top of the file, not inline where shown above — shown inline here
only for readability.)

---

## STEP 2 — `CaptureProjectAssemblyHotReloadState()`'s real body

`src/Core/Plugins/ProjectAssemblyHotReload.h` — update this function's own
signature to accept the new `projectRootDirectory` parameter (PHASE1
already threads it through the enclosing `PerformProjectAssemblyHotReload()`
— this phase is what actually CONSUMES it):

```cpp
// HOOK POINT A - called BEFORE anything is torn down. Builds a full,
// generic ECS-Registry snapshot via Scene/SceneBuilder.h's
// BuildSceneDocumentFromRegistry() - THE SAME function
// EditorHotReloadDebugCapability::BuildSceneSnapshotJson() (GET
// /project_assembly/debug/scene_snapshot) already uses for the identical
// live world, which is this phase's own primary live cross-check tool
// (Step 3 below). Refreshes core.GetAssetDatabase() exactly ONCE per
// cycle (PHASE0_MASTER_STRATEGY.md, LDD-HR7) - RestoreProjectAssemblyHotReloadState()
// (PHASE3) reuses this SAME, already-refreshed instance later in the SAME
// cycle, without refreshing it a second time.
HotReloadStateSnapshot CaptureProjectAssemblyHotReloadState(Core& core, const std::filesystem::path& projectRootDirectory);
```

`ProjectAssemblyHotReload.cpp` — real body, replacing the existing no-op
stub (lines 23-28):

```cpp
#include "../../Scene/SceneBuilder.h"

HotReloadStateSnapshot CaptureProjectAssemblyHotReloadState(Core& core, const std::filesystem::path& projectRootDirectory)
{
    // LDD-HR7 - refreshed here, exactly once per cycle. Safe even if
    // projectRootDirectory doesn't exist (RefreshFromDirectory()'s own
    // existing, already-relied-upon tolerant behavior - mirrors
    // Editor/SceneIO.cpp's own SaveScene()'s identical comment).
    core.GetAssetDatabase().RefreshFromDirectory(projectRootDirectory);

    HotReloadStateSnapshot snapshot;
    snapshot.document = BuildSceneDocumentFromRegistry(core.GetGame().GetRegistry(), core.GetAssetDatabase());

    GTE_LOG_INFO("ProjectAssemblyHotReload",
        "CaptureProjectAssemblyHotReloadState: captured " + std::to_string(snapshot.document.entities.size()) + " entities.");
    return snapshot;
}
```

**Verify concretely, do not guess**: confirm `Core::GetGame()` returns
`Game&` (confirmed, `Core.h` line 188) and `Game::GetRegistry()` returns
`Registry&` (confirmed, used identically at `SceneIO.cpp` line 32/69 and
`EditorHotReloadDebugCapability.cpp` line 94) — this exact accessor chain
is ALREADY proven correct by both of those existing, already-working call
sites; this phase's own new call site is a third, identical use of the
same chain, not a novel one.

`PerformProjectAssemblyHotReload()`'s own call site (`.cpp`, currently
line 94: `const HotReloadStateSnapshot snapshot =
CaptureProjectAssemblyHotReloadState(core);`) — update to pass the new
parameter through:

```cpp
const HotReloadStateSnapshot snapshot = CaptureProjectAssemblyHotReloadState(core, projectRootDirectory);
```

---

## STEP 3 — Live cross-check against the ALREADY-WORKING debug endpoint
(mandatory, this phase's own primary correctness proof)

Because `CaptureProjectAssemblyHotReloadState()` and
`EditorHotReloadDebugCapability::BuildSceneSnapshotJson()` now both call
the IDENTICAL underlying function (`BuildSceneDocumentFromRegistry()`)
against the SAME live `Registry`, they MUST produce equivalent
`SceneDocument` content for the SAME instant (module-ordering/AssetDatabase-
refresh-timing details aside — both scan the SAME project root). This
phase's own live proof:

1. Launch `GreatTamanaEditor.exe` (`run_app_background`).
2. `GET /project_assembly/debug/scene_snapshot` — record this JSON as the
   "reference" snapshot.
3. Trigger a real hot-reload cycle (`POST
   /project_assembly/hot_reload?name=ProjectAssemblyProbe`, no source
   change) — this exercises `CaptureProjectAssemblyHotReloadState()` for
   real, on the main thread, inside a real cycle (its own
   `GTE_LOG_INFO` line, added above, is directly checkable via `GET
   /get_logs?category=ProjectAssemblyHotReload` — confirm it reports the
   SAME entity count the reference snapshot's own `"entities"` array
   length shows).
4. `GET /project_assembly/debug/scene_snapshot` again, AFTER the cycle
   completes — confirm it is unchanged from step 2's reference (since
   HOOK POINT B is still a no-op stub this phase, the live world itself
   was never actually cleared/restored — this is expected and correct;
   this step only proves the CAPTURE half ran correctly and did not
   itself corrupt or mutate the live world it was reading from — capture
   must be a pure, read-only operation).
5. `stop_app_background` when done.

---

## Definition of Done — this phase only

- [ ] `HotReloadStateSnapshot::document` is a real `SceneDocument`.
- [ ] `CaptureProjectAssemblyHotReloadState()`'s real body exists, compiles,
      refreshes `core.GetAssetDatabase()` exactly once, and calls
      `BuildSceneDocumentFromRegistry()` against the live `Registry`.
- [ ] Live-verified: a real hot-reload cycle's own log line reports an
      entity count matching `GET /project_assembly/debug/scene_snapshot`'s
      own `"entities"` array length for the same live world.
- [ ] Live-verified: capture is provably read-only — the live world is
      unchanged (via the debug snapshot endpoint) immediately after a
      cycle completes, since restore is still a no-op this phase.
- [ ] Zero new regression in the existing hot-reload success/rollback
      paths (a quick smoke test of both, mirroring PHASE1's own Step 4).

## What this phase does NOT do

- Does NOT implement `RestoreProjectAssemblyHotReloadState()`'s real body
  — it remains the existing no-op stub, byte-for-byte, this phase.
- Does NOT touch `Editor::LoadScene()`/`SceneIO.cpp` at all — that whole
  refactor is PHASE3's job.
- Does NOT add the probe's own marker entity or the testing-only mutation
  route — PHASE4's job. This phase's own live cross-check (Step 3) only
  needs entity COUNT to agree, not any specific custom-component field
  value, since nothing anywhere yet has a live entity carrying
  `ProbeHotReloadMarker`.
