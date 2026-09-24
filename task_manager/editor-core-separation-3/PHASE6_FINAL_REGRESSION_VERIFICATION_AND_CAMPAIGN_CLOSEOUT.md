# PHASE6 — Final Full Regression Verification and Campaign Closeout

**Parent**: `PHASE0_MASTER_STRATEGY.md`. **Predecessor**: every prior phase
(`PHASE1`-`PHASE5`) must already be complete, each with its own
`PHASEn_COMPLETION_REPORT.md` committed.

This is the ONE phase in this campaign allowed to run a full clean build and
the full `ctest` regression suite (Universal Rule 4 of
`PHASE0_MASTER_STRATEGY.md` explicitly reserves this for here).

---

## Step 1: The Goal

Prove, mechanically, with fresh evidence (not inherited assumptions from any
prior phase's own narrower checks), that this entire campaign's real,
substantial deliverable — a working, end-to-end, dynamic plugin system,
Milestones 0-3 — is genuinely correct AND has introduced zero regression
anywhere else in this large, pre-existing codebase. Write
`CAMPAIGN_COMPLETION_REPORT.md`, mirroring
`task_manager/editor-core-separation-2/CAMPAIGN_COMPLETION_REPORT.md`'s own
shape/tone/honesty level exactly (read it again, in full, immediately
before writing this phase's own report, as the direct style template).

---

## Step 2: The Situation

Every phase before this one only ever ran a targeted, incremental compile
check (Universal Rule 4). This is the FIRST point in this campaign a full,
clean rebuild of the ENTIRE repository (both the main `build/` tree and
every one of the (now four) manually-invocable CI-style probe projects) has
ever been attempted. Real, previously-undetected cross-phase interactions
(e.g. a CMake `target_sources()`/`add_subdirectory()` ordering issue, an
`#include` that only fails in a from-scratch configure) are realistically
possible and must be treated as genuine bugs to fix, not waved away.

---

## Step 3: The Plan

### 3.1 — Full clean rebuild, main tree

Delete `build/` (or use a fresh sibling build directory if deleting is
undesirable on this machine — confirm with the user only if genuinely
unsure, otherwise proceed, mirroring `editor-core-separation-2` PHASE5's own
precedent of deleting and reconfiguring fresh). `cmake -S . -B build -G
Ninja` (default options — `GTE_ENABLE_PLUGINS` defaults `ON`, confirmed
Locked Design Decision #6). `cmake --build build`. Record the exact
step-count/error-count, mirroring every prior campaign's own
`"NNN/NNN steps, zero errors"` reporting convention. Confirm, via
`browse_dir`, that `build/plugins/demo_hello_world.dll`,
`build/plugins/demo_render_feature.dll`, `build/plugins/demo_editor_panel.dll`
all genuinely exist, plus (Locked Design Decision #4) `libstdc++-6.dll`/
`libgcc_s_seh-1.dll`/`libwinpthread-1.dll` staged next to
`build/GreatTamanaEditor.exe`.

### 3.2 — Full `ctest` regression pass

`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C
Debug --output-on-failure`. Compare the new total/passing/skip counts
against `editor-core-separation-2`'s own final baseline
(**"1774 total, 1772 passed, 2 legitimate environment-gated skips, zero
failures"**) — this campaign is not expected to have added any new `tests/`
GoogleTest file itself (every new class this campaign adds —
`PluginHost`, the adapters, `EditorPanelRegistry` — is exercised by the
manual/HTTP-driven smoke checks each phase already ran, and by the
dedicated standalone probes, not by new GoogleTest cases; if any phase
DID add a genuine Tier-1-testable pure-logic unit, per `AGENTS.md`'s own
"Testability & Regression Safety" mandatory rule, confirm its own new test
is present and passing here) — so the expected, correct outcome is the
EXACT SAME baseline count, unchanged. **Any count drift at all — up or
down, passing or skipped — must be explained concretely, by name, not
waved past.** A genuine, newly-introduced failure is a real regression:
use `delegate_task` to spin off a dedicated fix task (this is the ONE
place in this whole campaign `delegate_task` may be used from an
"implementation phase," and only for this exact purpose — diagnosing and
fixing a real regression Step 3.2 itself surfaces, per
`PHASE0_MASTER_STRATEGY.md`'s Universal Rule 8's own narrow carve-out,
mirroring `editor-core-separation-2` PHASE5's own identical precedent
exactly).

### 3.3 — Re-run every probe, fresh, from a clean build directory

- `tools/ci/gte_core_standalone_probe` (pre-existing, untouched by this
  campaign — confirm it still passes, unaffected).
- `tools/ci/gte_core_player_link_probe` (extended by PHASE5) — confirm its
  own original checks still pass AND PHASE5's own new bonus check runs or
  cleanly self-skips.
- `tools/ci/gte_plugin_abi_handshake_probe` (new, PHASE2).
- `tools/ci/gte_plugin_isolation_probe` (new, PHASE5) — confirm
  `LoadedModuleCount() == 3` exactly, `renderFeatureCount == 1` exactly.

### 3.4 — Live, HTTP-driven end-to-end smoke test

Boot `build/GreatTamanaEditor.exe` via `run_app_background`, drive it via
`gte_send_request`, mirroring `editor-core-separation-2`'s own smoke-test
table shape:

| Endpoint | Expected |
|---|---|
| `GET /get_swapchain` | `200`, real rendered PNG, every pre-existing panel present |
| `GET /list_tabs` | `200` — includes `"Demo Plugin Panel"` alongside every pre-existing built-in name |
| `GET /get_logs?limit=50` | `200` — real `"Loaded plugin '...'"` lines for all 3 demo plugins |
| `GET /activate_tab?name=Game` → `GET /get_game_view` | Visibly solid magenta (`DemoRenderFeaturePlugin`'s own clear pass) |
| `GET /activate_tab?name=Scene` → `GET /get_swapchain` | Scene View ALSO shows the same magenta clear |
| `GET /activate_tab?name=Demo Plugin Panel` → `GET /get_swapchain` | Panel visible, docked, showing "Hello from a plugin!" |
| Every pre-existing endpoint each prior campaign already smoke-tested (`/save_scene`, `/load_scene`, `/frame_debugger/*`, `/clear_logs`) | Unchanged behavior — spot-check at least 3 of these, not the full exhaustive list every prior campaign already covered in depth |

`stop_app_background` when done.

### 3.5 — Re-check every one of `PHASE0_MASTER_STRATEGY.md`'s own Locked
Design Decisions against the real, final code

For each of the 10 Locked Design Decisions in `PHASE0_MASTER_STRATEGY.md`'s
Step 3, write one line in the completion report confirming it is genuinely,
mechanically true in the final code (mirroring
`editor-core-separation-2`'s own "Four Hard Rules, restated with fresh
evidence" section) — e.g. "`gte_core.a`/`gte_editor.a` are still `STATIC`
libraries: confirmed, `search_in_dir(CMakeLists.txt, 'add_library(gte_core'`/
`'add_library(gte_editor'`) shows `STATIC` on both, unchanged."

### 3.6 — `CAMPAIGN_COMPLETION_REPORT.md`

Write it, in this same folder, covering: what this campaign set out to do;
what shipped, phase by phase (one paragraph per phase, mirroring
`editor-core-separation-2`'s own per-phase summary depth); the full
build/`ctest`/probe/smoke-test evidence from Steps 3.1-3.4 above; the Locked
Design Decision re-check from Step 3.5; every real deviation from this
strategy any phase's own completion report already flagged, consolidated
here; what remains genuinely open (Milestone 4/hot-reload, the owned-handle
mechanism, real feature migration — all explicitly, honestly restated as
still out of scope, exactly as `editor-core-separation-1`/`-2` each did for
their own remaining open items).

---

## Universal Rules

Follow `PHASE0_MASTER_STRATEGY.md`'s Universal Rules exactly — this phase is
the one explicit exception to Universal Rule 4 (full build/`ctest` IS
required here) and the one explicit, narrow exception to Universal Rule 8
(`delegate_task` may be used here, ONLY to spin off a fix for a genuine
regression this phase's own Step 3.2 surfaces).

## Non-Goals for this phase specifically

- Do not use this phase to add any new feature/capability — it is
  verification and closeout only. Any genuine gap discovered here that is
  NOT a regression (e.g. "the owned-handle mechanism really should exist
  now") gets written into the completion report's "what remains genuinely
  open" section, not implemented ad-hoc.
