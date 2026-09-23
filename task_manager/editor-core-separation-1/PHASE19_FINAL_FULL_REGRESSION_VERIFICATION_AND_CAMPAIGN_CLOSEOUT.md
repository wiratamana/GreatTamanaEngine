# PHASE19 — Final Full Regression, Verification Checklist, Campaign Closeout

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on ALL prior phases (1-18). This is
the LAST phase of the campaign.

## Step 1: The Goal

Mechanically confirm every item in the design doc's own Section 10
("Verification Checklist") is genuinely true, run the final full clean
build + full `ctest` regression pass, perform a live HTTP-driven smoke test
across every feature this campaign touched, and write the
`CAMPAIGN_COMPLETION_REPORT.md` that ties all 19 phases together — matching
this repo's own established convention (see `task_manager/render-pass-7/`
or `task_manager/mrt-1/` for the shape/tone to match).

## Step 2: The Situation / The Problem

By this point, `gte_core`/`gte_editor` should be two real, correctly
one-way-dependent static libraries; `Core`/`EditorHost` should be the real
public contract and composition root; `Application` should no longer
exist; the executable should be renamed; a standalone-core probe and a
headless test fixture should both exist and pass. This phase is the single
place that checks ALL of this together, end-to-end, rather than trusting
each phase's own narrower verification in isolation.

## Step 3: The Plan

1. Read every `PHASEn_COMPLETION_REPORT.md` in this folder (1 through 18)
   in full — collect every deviation, discovered fact, or deferred item
   each one flagged.
2. Mechanically re-check EVERY bullet in the design doc's Section 10:
   - `gte_core`'s `target_link_libraries()` never lists `gte_editor`,
     `SDL3`, `imgui`, or `imguizmo` — confirm by reading the final
     `CMakeLists.txt` directly.
   - `gte_core`'s own `.cpp`/`.h` files contain zero `#include` of anything
     under `src/Editor/` except `EditorLayer.h` itself, and zero `#if`/
     macro conditioned on "is this an Editor build" — confirm via a final
     `search_in_dir` for `GTE_ENABLE_EDITOR` (expect ZERO results anywhere
     now, including the CMake placeholder from Phase 8 — Phase 9 should
     have already replaced it) and for `#include ".*Editor/` scoped to
     files outside `src/Editor/`.
   - `gte_core` builds and links standalone via the Phase 18 probe — re-run
     it now, fresh, one more time.
   - `GreatTamanaEngineTests`/its renamed equivalent no longer requires
     `SDL3.dll` — re-confirm Phase 14's flipped regression test still
     passes.
   - The headless `ISurfaceProvider` test (Phase 18) actually runs and
     passes as part of the real test suite (not just once manually).
   - No object file inside `gte_core.a` carries an unresolved external
     symbol only `gte_editor.a` defines — the Phase 18 probe IS this
     check; confirm it actually failed BEFORE any of this campaign's fixes
     would have made sense, i.e. sanity-confirm the probe is not a
     trivially-always-passing no-op (if genuinely unsure, deliberately
     introduce a temporary, throwaway violation — e.g. a stray `#include`
     of an Editor header inside a `gte_core` file — confirm the probe
     ACTUALLY fails to build, then revert the temporary violation. This is
     the one legitimate reason to touch code in this closeout phase beyond
     pure verification.)
3. Full clean rebuild of the ENTIRE repo (delete/reconfigure `build`
   fully). Full `ctest` regression pass
   (`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`).
   Record the exact final test count and pass rate, compared against the
   pre-campaign baseline (whatever Phase 9's report recorded as its
   "before" count).
4. Live, HTTP-driven end-to-end smoke test via `run_app_background` +
   `gte_send_request`: boot the renamed executable, capture a Game View
   screenshot and a Scene View screenshot, exercise
   `/save_scene`/`/load_scene`, `/activate_tab`/`/list_tabs`,
   `/spawn_gpu_driven_test_batch`, `/frame_debugger/enable`+`/capture`,
   `/get_logs`. Confirm every single one still behaves identically to this
   campaign's pre-Phase-1 baseline. `stop_app_background` when done.
5. Write `CAMPAIGN_COMPLETION_REPORT.md` in this folder, in the same style
   as this repo's other completed campaigns (`task_manager/render-pass-7/
   CAMPAIGN_COMPLETION_REPORT.md` is a good reference for tone/structure):
   summarize what shipped, phase by phase, any deviations from this
   strategy's original plan (discovered during real execution), the final
   test counts, and explicitly restate the Four Hard Rules (design doc
   Section 1.3) with a plain YES/NO + evidence for each.
6. Update `AGENTS.md` with a new short section (mirroring every other
   subsystem's existing entry format) summarizing this campaign's outcome
   and linking to `task_manager/editor-core-separation-1/PHASE0_MASTER_STRATEGY.md`
   and `CAMPAIGN_COMPLETION_REPORT.md` — this is the ONE place outside this
   campaign's own folder that needs a durable update, matching how every
   prior campaign in this repo left a trace in `AGENTS.md`.

## Files Touched

- Final `CMakeLists.txt` state (verification only, plus the deliberate
  temporary-violation-then-revert from Step 2's last bullet, if performed)
- NEW `CAMPAIGN_COMPLETION_REPORT.md`
- `AGENTS.md` (new section)

## Definition of Done

- Every Section 10 checklist bullet confirmed true, with evidence, in the
  campaign report.
- Full clean build + full `ctest` pass, 100% (or the same pre-existing
  skip count this repo already tolerates, never a NEW failure).
- Live smoke test confirms zero regression across every touched feature.
- `AGENTS.md` updated. `CAMPAIGN_COMPLETION_REPORT.md` written. Final git
  commit for the whole campaign.

## Out of Scope

The Player Build Pipeline remains explicitly, permanently out of scope —
state this plainly, one more time, in the campaign report's own "what
remains genuinely open" section, exactly as prior campaigns in this repo
do.
