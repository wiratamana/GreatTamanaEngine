# PHASE6 — Bucket B, Part 2a: Convert Scene Save/Load Call Sites

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE5` (interfaces must already
be declared).

## Step 1: The Goal

`src/Application/EngineCommandDispatch.cpp`'s scene save/load handling and
`src/Network/NetworkServer.cpp`'s matching HTTP error response both
currently answer "is scene IO available" with `#if GTE_ENABLE_EDITOR`.
Convert both to a runtime null-check against `ISceneIOCapability*`
(Phase 5).

## Step 2: The Situation / The Problem

Read both files in full, current state. Confirm: does
`EngineCommandDispatch.cpp` call into `src/Editor/SceneIO.h`/`.cpp` directly
today (behind the macro)? Confirm the exact hardcoded fallback string
`"...requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)"`
and every place it's duplicated (`NetworkServer.cpp`'s own HTTP body/status
derivation).

## Step 3: The Plan

1. Read `src/Editor/SceneIO.h`/`.cpp` in full — this is the REAL
   implementation `ISceneIOCapability` needs to wrap.
2. Create `src/Editor/EditorSceneIOCapability.h`/`.cpp` (new, small
   `gte_editor`-owned adapter) implementing `ISceneIOCapability`,
   delegating to the real `SceneIO` functions.
3. In `EngineCommandDispatch.cpp`: replace the `#if GTE_ENABLE_EDITOR
   ... #else ... #endif` block with a single runtime branch:
   ```cpp
   if (sceneIOCapability != nullptr) {
       bool ok = sceneIOCapability->SaveScene(/* ... */);
       // success path, unconditional, was previously the ON branch
   } else {
       // was previously the OFF branch's exact fallback message/behavior
   }
   ```
   Keep the exact same OFF-branch fallback message/behavior — this is a
   behavior-preserving refactor, not a chance to change what a
   capability-less build reports.
4. In `NetworkServer.cpp`: same treatment for whatever HTTP status/body
   logic currently branches on the macro — it should ask the SAME
   `ISceneIOCapability*` instance (thread it through however
   `NetworkServer` already reaches `EngineCommandDispatch`'s call path
   today — read the real code to find the existing plumbing, do not invent
   a new global).
5. Wire the real instance: wherever `Application` constructs things today
   (temporary home until Phase 16 moves this into `EditorHost`), construct
   an `EditorSceneIOCapability` and call
   `SetSceneIOCapability(&editorSceneIOCapability)` on whatever host object
   Phase 5 decided owns the pointer.
6. Delete the `GTE_ENABLE_EDITOR` `#include` of `SceneIO.h` from
   `EngineCommandDispatch.cpp`/`NetworkServer.cpp` entirely — they should
   only ever see `ISceneIOCapability` (forward-declared or from
   `Core/EditorCapabilities.h`), never the real `SceneIO.h`.
7. Confirm via `search_in_dir` for `GTE_ENABLE_EDITOR` scoped to these two
   files — zero results expected after this phase.
8. Compile-check: incremental build. Live smoke check:
   `run_app_background`, `POST /save_scene` then `POST /load_scene` via
   `gte_send_request`, confirm both still work exactly as before. Pull
   `GET /get_logs` to confirm no new errors.

## Files Touched

- `src/Application/EngineCommandDispatch.cpp`/`.h`
- `src/Network/NetworkServer.cpp`
- NEW `src/Editor/EditorSceneIOCapability.h`/`.cpp`
- Wherever `Application` wires capabilities today (temporary, per Phase 5)

## Definition of Done

- Zero `GTE_ENABLE_EDITOR` reference in `EngineCommandDispatch.cpp`/
  `NetworkServer.cpp` related to scene IO.
- `/save_scene`/`/load_scene` confirmed working live.
- `PHASE6_COMPLETION_REPORT.md` + git commit.

## Out of Scope

Do not touch the other Bucket B sites (`EditorUiCommandBridge.h`,
`AssetImportCommandBridge.h`, `GpuDrivenBatchTestSpawner.h`, `Game.h`) — see
Phase 7.
