# PHASE8 — Bucket C Cleanup + Delete `GTE_ENABLE_EDITOR` Entirely

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phases 2-7 (every real Bucket A/B
site must already be fixed — this phase assumes ONLY dead branches and the
CMake switch itself remain).

## Step 1: The Goal

After this phase, `GTE_ENABLE_EDITOR` does not exist ANYWHERE in this
codebase — not as a CMake `option()`, not as a `target_compile_definitions`
macro, not as a single `#if` anywhere in `src/`. This is the last
preparation step before Phase 9's CMake target surgery — the design doc is
explicit that doing the CMake split first, with ANY site left as-is,
produces a build that either fails to link or silently keeps the exact
branching this whole design removes.

## Step 2: The Situation / The Problem

Design doc Bucket C lists files living entirely under `src/Editor/` whose
`#if GTE_ENABLE_EDITOR`/`#else` branches only ever describe/handle their own
OFF state, even though the whole file only compiles when the Editor exists
in the first place: `Logger.h`/`.cpp` (already largely handled by Phase 3 —
re-check its remaining shape), `EditorPanelCatalog.h`, `ProjectRootPath.h`,
`ProfilerPanelData.h`, `ImGuiEditorLayer.cpp`, `Panels/LogPanel.cpp`,
`Panels/ProjectPanel.h`, `EditorLayer.h`'s own comments. Phase 7's
completion report should have left an exact remaining-site count from its
own final `search_in_dir` sweep — read that report first.

## Step 3: The Plan

1. Read Phase 7's completion report for its exact final `GTE_ENABLE_EDITOR`
   site count/list. Re-run `search_in_dir` for `GTE_ENABLE_EDITOR` across
   all of `src/` yourself to confirm it still matches (code may have
   drifted since that report).
2. For every remaining site under `src/Editor/`: read the file, confirm the
   `#else`/OFF branch is genuinely dead (the file itself is only ever
   compiled as part of `gte_editor`), then delete the `#if`/`#else`/`#endif`
   wrapper entirely, keeping only the always-true body. Do this file by
   file: `Logger.h`/`.cpp`, `EditorPanelCatalog.h`, `ProjectRootPath.h`,
   `ProfilerPanelData.h`, `ImGuiEditorLayer.cpp`, `Panels/LogPanel.cpp`,
   `Panels/ProjectPanel.h`, `EditorLayer.h` (comments only — no functional
   change there, just prose cleanup).
3. If ANY site turns up that is NOT covered by Bucket A (Phase 4), Bucket B
   (Phases 5-7), or genuinely-dead Bucket C — i.e. a real, live branch this
   strategy's inventory missed entirely — STOP, read it fully, and use
   `ask_questions` to decide the right treatment before proceeding (do not
   invent a fix for an unclassified site under time pressure).
4. Once every `#if GTE_ENABLE_EDITOR` in `src/` is gone: open the root
   `CMakeLists.txt`. Delete the `option(GTE_ENABLE_EDITOR ...)` declaration
   and the `target_compile_definitions(gte_core PUBLIC
   GTE_ENABLE_EDITOR=$<BOOL:${GTE_ENABLE_EDITOR}>)` line outright. Do NOT
   add any replacement macro of the same or a similar name, anywhere.
5. At this point, the OLD `if(GTE_ENABLE_EDITOR) ... else() ... endif()`
   block in `CMakeLists.txt` that adds Editor sources into `gte_core`
   becomes DEAD (its condition no longer exists as a variable at all, since
   the `option()` is gone) — do NOT delete that block's file list yet in
   this phase; only delete the `option()`/`target_compile_definitions()`
   themselves and confirm CMake still configures (it will likely error on
   the now-undefined `GTE_ENABLE_EDITOR` variable inside the `if()` — fix
   this minimally by hardcoding the block to always take its "ON" branch
   for now, e.g. `if(TRUE) ... endif()`, as a deliberate, temporary,
   clearly-commented placeholder). Phase 9 does the REAL surgery of turning
   this into two actual targets — this phase's job is only to prove zero
   macro branching remains in the C++ code itself.
6. Full-repo final check: `search_in_dir` for `GTE_ENABLE_EDITOR` across the
   ENTIRE repository (not just `src/`) — the only remaining match after
   this phase should be the CMakeLists.txt's now-hardcoded `if(TRUE)` block
   comment explaining why (and possibly `tests/CMakeLists.txt` if it also
   referenced the option — check and fix identically).
7. Compile-check: incremental build with the hardcoded `if(TRUE)` in place —
   confirm this still produces byte-for-byte the same single archive as
   before (this phase changes zero C++ behavior, only deletes dead
   branches and one CMake variable).

## Files Touched

- Every Bucket C file listed in Step 2.
- Root `CMakeLists.txt` (delete `option()` + `target_compile_definitions`
  line; temporarily hardcode the old conditional block to `if(TRUE)` with a
  clear `# TODO(Phase 9)` comment).
- `tests/CMakeLists.txt` if it also references `GTE_ENABLE_EDITOR`.

## Definition of Done

- Zero occurrence of the string `GTE_ENABLE_EDITOR` anywhere in `src/` or
  `tests/`.
- The only remaining occurrence in the whole repo is the clearly-commented,
  temporary `if(TRUE)` placeholder in `CMakeLists.txt`, explicitly marked
  for Phase 9 to replace with the real target split.
- Incremental build succeeds, producing an unchanged single archive.
- `PHASE8_COMPLETION_REPORT.md` + git commit.

## Out of Scope

Do not create the `gte_editor` CMake target yet — that is Phase 9, the very
next phase, which this phase directly sets up for.
