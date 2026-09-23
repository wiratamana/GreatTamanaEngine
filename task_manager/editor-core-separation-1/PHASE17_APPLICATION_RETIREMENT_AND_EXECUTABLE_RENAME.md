# PHASE17 — Retire `Application`, Rename the Executable Target

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phase 16 (`EditorHost` must already
be fully functionally equivalent). See `PHASE0`'s "Locked Design Decision
#3" — the rename IS in scope, as the final CODE step of this campaign.

## Step 1: The Goal

`Application.h`/`.cpp` are deleted entirely. The executable target is
renamed away from `GreatTamanaEngine` to a name that honestly reflects
"this is the authoring tool" (e.g. `GreatTamanaEditor`) — matching the
design doc's own Section 6.2 suggestion, now that `EditorHost` fully
replaces `Application`.

## Step 2: The Situation / The Problem

By this point, `Application` should be a thin, unused shell (its real
responsibilities all moved to `Core`/`EditorHost` across Phases 12-16).
Confirm this via `search_in_dir` for `Application` usage across `src/` —
identify every remaining reference (likely just `main.cpp`'s old,
now-dead construction path, if Phase 15 left it in place rather than
deleting it outright, plus any stray `#include "Application.h"` elsewhere).

## Step 3: The Plan

1. Read `main.cpp`'s current state. If it still has both an `Application`
   path and an `EditorHost` path (Phase 15 said to leave the old path
   intact for safety), delete the `Application`-constructing path entirely
   now — `main.cpp` should construct ONLY `EditorHost`.
2. Delete `src/Application/Application.h`/`.cpp` outright.
3. Confirm via `search_in_dir` for `"Application.h"` and `class Application`
   across the entire repo (excluding this campaign's own `task_manager`
   docs, which reference it historically and should NOT be edited) — zero
   remaining includes/references expected in `src/`.
4. Decide the exact new executable name via `ask_questions` if not already
   obvious from context (e.g. `GreatTamanaEditor` vs keeping
   `GreatTamanaEngine` — the design doc suggests the former but this is
   ultimately a product-naming decision, not a pure architecture one).
5. Update the root `CMakeLists.txt`'s `add_executable(...)` target name and
   every place that name is referenced (install rules, any packaging
   script, `BUILDING.md`/`README.md`'s own build-output mentions — search
   for the old name across `.md` files at the repo root and update
   accordingly, EXCLUDING historical `task_manager/*` campaign docs which
   should stay as an accurate historical record of what was true when they
   were written).
6. Compile-check: incremental build under the new target name. Confirm the
   renamed executable still boots correctly.
7. This phase does NOT require a full ctest run (not one of the two
   flagged checkpoints) — Phase 19 is the final full verification.

## Files Touched

- DELETE `src/Application/Application.h`/`.cpp`
- `src/main.cpp`
- Root `CMakeLists.txt`
- `README.md`/`BUILDING.md` (executable name references only)

## Definition of Done

- `Application.h`/`.cpp` no longer exist. Zero remaining reference to
  `class Application` anywhere in `src/`.
- The executable builds and boots under its new name.
- `PHASE17_COMPLETION_REPORT.md` (recording the exact new name chosen and
  why) + git commit.

## Out of Scope

Do not touch the Player Build Pipeline (still explicitly out of scope for
this entire campaign).
