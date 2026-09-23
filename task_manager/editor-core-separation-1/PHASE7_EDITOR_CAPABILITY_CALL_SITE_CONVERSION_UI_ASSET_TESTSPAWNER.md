# PHASE7 — Bucket B, Part 2b: Convert Remaining Capability Call Sites

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE5` and `PHASE6` (same pattern,
applied to the remaining sites).

## Step 1: The Goal

Convert the remaining Bucket B `#if GTE_ENABLE_EDITOR` capability-check call
sites — `src/Application/EditorUiCommandBridge.h`,
`src/Application/AssetImportCommandBridge.h`,
`src/Editor/GpuDrivenBatchTestSpawner.h`'s CORE-side call site, and
`src/Game/Game.h`'s own fallback — to the same runtime-null-check pattern
Phase 6 already proved out for Scene IO.

## Step 2: The Situation / The Problem

Read all four files (and whatever CORE-side file actually calls into
`GpuDrivenBatchTestSpawner` behind a macro — find it via `search_in_dir` for
`GpuDrivenBatchTestSpawner` scoped OUTSIDE `src/Editor/`) in full, current
state, before writing anything.

## Step 3: The Plan

1. For `EditorUiCommandBridge.h`/`.cpp`: apply the exact same shape as
   Phase 6 — real implementation adapter
   (`src/Editor/EditorUiCapabilityImpl.h`/`.cpp` implementing
   `IEditorUiCapability` from Phase 5), CORE-side file only ever sees the
   interface, null-check replaces the macro, fallback behavior preserved.
2. For `AssetImportCommandBridge.h`/`.cpp`: same shape
   (`IAssetImportCapability`).
3. For `GpuDrivenBatchTestSpawner`: find the exact CORE-side call site
   first. If the ENTIRE spawner file already lives under `src/Editor/` and
   the ONLY macro-guarded thing is a single call site in, say,
   `Application.cpp` or a network route handler, that call site gets the
   `IGpuDrivenBatchTestCapability` treatment (Phase 5's interface). If, on
   inspection, this turns out to actually be a Bucket C case (i.e. the
   macro only guards code fully inside `src/Editor/` with no real
   Core-side caller), document that finding and defer it to Phase 8
   instead — do not force a Bucket B treatment onto something that turns
   out not to need it.
4. For `Game.h`'s own fallback: read its real current shape — confirm
   whether it needs its own new interface, or whether it is actually
   asking the SAME question one of the interfaces above already answers
   (e.g. if `Game.h`'s fallback is really about scene IO availability,
   reuse `ISceneIOCapability` rather than inventing a duplicate).
5. Wire each new adapter instance the same way Phase 6 did (temporary home
   in `Application` until Phase 16).
6. Confirm via a full-repo `search_in_dir` for `GTE_ENABLE_EDITOR` — the
   ONLY remaining matches after this phase should be: (a) Bucket C dead
   branches inside files entirely under `src/Editor/` (Phase 8's job), and
   (b) the CMake `option()`/`target_compile_definitions()` line itself
   (also Phase 8's job). If anything else still shows up, it means this
   strategy's file inventory missed a site — read it, classify it (Bucket
   A/B/C), and fix it here or flag it in the completion report for Phase 8
   to absorb.
7. Compile-check: incremental build. Live smoke check:
   `run_app_background`, exercise `GET /activate_tab`/`/list_tabs`,
   `POST /spawn_gpu_driven_test_batch` (or the real current endpoint name —
   confirm via `NetworkServer.cpp`'s route table), confirm all still work.

## Files Touched

- `src/Application/EditorUiCommandBridge.h`/`.cpp`
- `src/Application/AssetImportCommandBridge.h`/`.cpp`
- `src/Editor/GpuDrivenBatchTestSpawner.h`/`.cpp` + its real CORE-side
  caller (path confirmed during execution)
- `src/Game/Game.h` (+ `.cpp` if needed)
- NEW `src/Editor/EditorUiCapabilityImpl.h`/`.cpp`,
  NEW `src/Editor/EditorAssetImportCapabilityImpl.h`/`.cpp`,
  NEW `src/Editor/EditorGpuDrivenBatchTestCapabilityImpl.h`/`.cpp` (as
  needed, per Step 3's finding)

## Definition of Done

- A full-repo `search_in_dir` for `GTE_ENABLE_EDITOR` shows ONLY Bucket
  C/CMake-option matches remaining (documented exact count in the
  completion report for Phase 8 to consume as its own starting inventory).
- All exercised endpoints confirmed still working live.
- `PHASE7_COMPLETION_REPORT.md` + git commit.

## Out of Scope

Do not delete the `GTE_ENABLE_EDITOR` CMake option or touch Bucket C dead
branches yet — that is Phase 8, which needs this phase's exact remaining-
site count as its starting point.
