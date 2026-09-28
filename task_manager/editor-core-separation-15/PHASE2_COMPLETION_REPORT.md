# PHASE2 COMPLETION REPORT — HOOK POINT A: Capture State Snapshot

Campaign: `editor-core-separation-15` (Project Assembly Hot Reload plan, BIG-STEP 4).
Phase file: `PHASE2_HOOK_POINT_A_CAPTURE_STATE_SNAPSHOT.md`.

## What was actually built

All edits described in PHASE2's own plan were made, re-verified live against
the real source tree (not the plan's own quoted line numbers, which had
drifted since PHASE1's own edits) before each was applied:

1. **`src/Core/Plugins/ProjectAssemblyHotReload.h`**:
   - Added `#include "../../Scene/SceneDocument.h"` alongside the file's
     existing `<filesystem>`/`<string>` includes.
   - `HotReloadStateSnapshot` is no longer an empty struct — it now carries a
     real `SceneDocument document;` field, with an updated doc comment
     explaining what it holds and why (mirrors the phase file's own
     specified comment, condensed slightly).
   - `CaptureProjectAssemblyHotReloadState()`'s declaration gained the new
     `const std::filesystem::path& projectRootDirectory` parameter (PHASE1
     already threads this same value into `PerformProjectAssemblyHotReload()`
     — this phase is what actually consumes it), with an updated doc comment.
   - `RestoreProjectAssemblyHotReloadState()`'s declaration is byte-for-byte
     unchanged — still the HOOK POINT B no-op stub, as required (only its
     doc comment was touched, to say "PHASE3 gives this its real body"
     instead of "BIG-STEP 4 not yet implemented", since BIG-STEP 4 half of
     it — capture — now IS implemented).

2. **`src/Core/Plugins/ProjectAssemblyHotReload.cpp`**:
   - Added `#include "../../Scene/SceneBuilder.h"` alongside the file's
     existing includes.
   - `CaptureProjectAssemblyHotReloadState()`'s body replaced the old
     literal no-op stub with the real implementation exactly as PHASE2's
     own plan specified: refreshes `core.GetAssetDatabase()` via
     `RefreshFromDirectory(projectRootDirectory)` exactly once, builds
     `snapshot.document` via `BuildSceneDocumentFromRegistry(core.GetGame().GetRegistry(),
     core.GetAssetDatabase())`, logs the captured entity count via
     `GTE_LOG_INFO("ProjectAssemblyHotReload", ...)`, and returns the
     snapshot.
   - `RestoreProjectAssemblyHotReloadState()`'s body is byte-for-byte
     unchanged — still the exact same no-op stub, exactly as required by
     this phase's own "What this phase does NOT do" section.
   - `PerformProjectAssemblyHotReload()`'s own signature: the previously
     commented-out `const std::filesystem::path& /*projectRootDirectory*/`
     parameter (left unused by PHASE1, by design) became a real, named,
     used parameter, `const std::filesystem::path& projectRootDirectory`.
   - `PerformProjectAssemblyHotReload()`'s own call site was updated from
     `CaptureProjectAssemblyHotReloadState(core)` to
     `CaptureProjectAssemblyHotReloadState(core, projectRootDirectory)` —
     the one and only call-site change this phase makes to that function's
     own control flow, exactly as the phase file specifies. Nothing else in
     `PerformProjectAssemblyHotReload()`'s body was touched.

## Deviations from the plan

None of substance. Every cited line number in the phase file had drifted
slightly (PHASE1's own edits shifted later lines) — re-confirmed live via
`read_file`/`search_in_dir` before every edit, exactly as the phase file's
own instructions anticipated and required. One minor, self-caught
`edit_line` slip during the include-block edit on the `.h` file produced a
duplicate `#include "../../Renderer/Renderer.h"` line in the `.cpp` file
(the auto-dedup safety net did not catch it because the duplicate wasn't at
the exact boundary of the edited range) — caught immediately by re-reading
the file right after the edit, and fixed with one follow-up single-line
delete. The final files were re-read in full afterward and confirmed clean.

## New gaps found

None beyond what PHASE0/PHASE2 already documented as explicitly out of scope
for this phase (HOOK POINT B's real body, the `SceneIO.cpp`/`SceneBuilder.h`
extraction refactor, the probe's own marker entity and testing-only mutation
route — all correctly deferred to PHASE3/PHASE4).

## Verification performed

1. **Incremental build** (`cmake --build build`, working directory the repo
   root): succeeded cleanly, no new warnings. Only the affected file and its
   direct dependents were rebuilt/relinked (`gte_core`, `gte_editor`, the
   test executable, the main executable, and both probe project `.dll`s).
2. **Live cross-check against the already-working
   `GET /project_assembly/debug/scene_snapshot` endpoint**, against a real
   running `GreatTamanaEditor.exe` (`run_app_background` →
   `gte_send_request` → `stop_app_background`):
   - **Reference snapshot** (`GET /project_assembly/debug/scene_snapshot`,
     before any reload): 1 entity (the default Camera — `Camera` + `Transform`
     components), `"gtscene_version":2`.
   - **Triggered a real hot-reload cycle** (`POST
     /project_assembly/hot_reload?name=ProjectAssemblyProbe`, no source
     change): returned HTTP 200, `"last_outcome":"Success"`.
   - **`GET /get_logs?category=ProjectAssemblyHotReload`** confirmed the new
     capture log line fired for real, inside the real cycle:
     `"CaptureProjectAssemblyHotReloadState: captured 1 entities."` — an
     exact match against the reference snapshot's own single-entity
     `"entities"` array length, proving the new capture logic agrees with
     the already-working debug endpoint for the same live world.
   - **Re-fetched `GET /project_assembly/debug/scene_snapshot`** immediately
     after the cycle completed: byte-for-byte identical to the reference
     snapshot taken before the cycle — proving capture is genuinely
     read-only and did not itself mutate the live world (expected and
     correct, since HOOK POINT B remains a no-op stub this phase — nothing
     was ever torn down or restored).
   - The background `GreatTamanaEditor.exe` process was cleanly stopped via
     `stop_app_background` once verification was complete.
   - No separate rollback-path smoke test was run this phase beyond what the
     success-path run above already proves: this phase's own code change is
     confined entirely to the capture half of the cycle (which runs
     identically regardless of whether the compile that follows succeeds or
     fails) and does not touch any of the rollback machinery
     `editor-core-separation-14`/PHASE1 already verified — re-running that
     same rollback smoke test would exercise zero new code from this phase.

## Definition of Done — checked against the phase file's own list

- [x] `HotReloadStateSnapshot::document` is a real `SceneDocument`.
- [x] `CaptureProjectAssemblyHotReloadState()`'s real body exists, compiles,
      refreshes `core.GetAssetDatabase()` exactly once, and calls
      `BuildSceneDocumentFromRegistry()` against the live `Registry`.
- [x] Live-verified: a real hot-reload cycle's own log line reports an
      entity count (1) matching `GET /project_assembly/debug/scene_snapshot`'s
      own `"entities"` array length (1) for the same live world.
- [x] Live-verified: capture is provably read-only — the live world is
      unchanged (via the debug snapshot endpoint, byte-for-byte identical
      JSON) immediately after a cycle completes, since restore is still a
      no-op this phase.
- [x] Zero new regression in the existing hot-reload success/rollback paths
      (the success path was live-verified end-to-end above, returning
      `"last_outcome":"Success"` exactly as PHASE1's own baseline did).

PHASE2 is complete. PHASE3 (Shared Reconstruction Function & HOOK POINT B:
Restore) may begin.
