# PHASE8 — Completion Report: Live Verification, Docs, Full Regression, and Campaign Closeout

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file:
`PHASE8_LIVE_VERIFICATION_DOCS_AND_REGRESSION_CLOSEOUT.md`.

This is the FINAL phase of the `editor-core-separation-24` campaign, and of
the entire, 2-BIG-STEP "Project Assembly On-Screen Render Feature
Compositing" effort (`editor-core-separation-23` + `editor-core-separation-24`).

## Step 2 — pre-flight re-read confirmed no drift

Re-read PHASE1 through PHASE7's own completion reports in full before
starting (see the parent task's own summary). Confirmed, before touching
anything:

- PHASE4/PHASE5 actually shipped `src/Editor/ScreenPassAutoWire.h/.cpp` and
  `src/Editor/ScreenPassPriorityAssignment.h/.cpp` exactly as planned, with
  external linkage, both registered in `CMakeLists.txt`/`tests/CMakeLists.txt`.
- PHASE6/PHASE7 extended the two real, pre-existing test files
  (`tests/Editor/AssetScaffoldTemplateTests.cpp`,
  `tests/Network/CreateAssetEndpointEndToEndTests.cpp`) — no parallel/
  duplicate test file was ever created.
- PHASE4 discovered and fixed two real deviations from this campaign's own
  written Step 3.1/3.2 text (the anchor comments are 7-line wrapped blocks,
  not single lines; insertion happens after the WHOLE comment block, not
  literally the next physical line) — both confirmed via `ask_questions`
  before implementation. This phase's own verification is checked against
  that REAL, final behavior, never the strategy doc's original sketch.
- `Projects/ProjectAssemblyProbe/`'s own Game-half source file is
  `Assets/HelloGame.cpp`, never `Assets/ProjectAssemblyProbeGame.cpp` (which
  has never existed) — confirmed again, live, in Step 3.2 item 8 below.

## Step 3.1 — Permanent fixture created

`Projects/ScreenPassAutoWireProbe/` — the suggested name was free (confirmed
via `browse_dir` before creating), so no alternative name was needed. Created
via `POST /project_assembly/create_project?name=ScreenPassAutoWireProbe`
against the freshly (incrementally) built `GreatTamanaEditor.exe`. Confirmed,
by direct `read_file`, that its `Assets/ScreenPassAutoWireProbeGame.cpp` has
both anchor comments and a NAMED `gte::Core& core` parameter — genuine proof
this project was created after PHASE2's template change shipped, unlike
`ProjectAssemblyProbe/`. Kept permanently on disk per Locked Decision 10 —
never deleted after verification, mirroring `ProjectAssemblyProbe/`'s own
precedent. `Projects/` remains `.gitignore`d, so this fixture has zero
git-visible footprint, exactly like every other project under that folder.

## Step 3.2 — Live verification, item by item (real evidence, not code review)

1. **Genuine, mouse-driven UI check** (real `SetCursorPos`/`mouse_event`/
   `SendKeys` Win32 calls via a throwaway PowerShell helper script, deleted
   afterward — the interactive desktop session was confirmed UNLOCKED via a
   real, timestamped full-desktop screenshot before attempting any input,
   unlike PHASE1's own locked-session blocker). Right-clicked the
   "[Active Project] ScreenPassAutoWireProbe" row (real screen coordinates,
   confirmed via `GET /get_swapchain` screenshots between every step),
   hovered "Create" (ImGui submenus open on hover, not click — confirmed
   empirically, matching PHASE6's own prior finding) — the real submenu
   showed **"Render Pass...", "Compute Shader...", "Vertex/Fragment Shader
   Pair...", "Screen Post-Process Pass..."** as the fourth option. Clicked it
   — a real dialog titled **"Create New Screen Post-Process Pass"** appeared.
   Both `TitleForKind()` (PHASE1) and the 4th `MenuItem` (PHASE1) confirmed
   genuinely, visually correct via real mouse input, for the second time in
   this whole campaign (after PHASE6's own smoke test).
2. Typed `RedTint` into the Name field (confirmed via screenshot), clicked
   "Create" — dialog closed, Project panel's file tree showed the new
   `RedTintScreenPass.cpp` row. `GET /get_logs?category=ProjectLifecycle`
   confirmed the exact auto-wired success log line. `read_file`'d both
   `RedTintScreenPass.cpp` (template correctly substituted, `/*priority=*/0,`)
   and `ScreenPassAutoWireProbeGame.cpp` (forward declaration + call line
   correctly inserted at both anchors, every other line byte-for-byte
   unchanged from the pre-scaffold original) — confirmed both via the ImGui
   path directly.
3. `POST /project_assembly/create_asset?kind=screen_post_process_pass&name=redtint`
   (same name, different case) — `400`, `"a file named 'redtintScreenPass.cpp'
   already exists"` (Windows' own case-insensitive filesystem is what makes
   this collision-check correct without any explicit case-folding in this
   branch). `browse_dir` confirmed zero new files.
4. A 60-character-name-of-`A`s request — `400`,
   `"name is too long - ... would exceed the engine's 63-character render
   feature name limit"`. `browse_dir` confirmed zero new files.
5. Scaffolded `BlueTint` — `/*priority=*/1,` (confirmed by `read_file`). No
   collision-warning log entry was present at scaffold time (collision
   detection only fires once the feature is actually registered at runtime,
   confirmed structurally correct — see item 10's `RenderFeatureCompositor`
   evidence below for the real, live confirmation once compiled).
6. Scaffolded `GreenTint` (priority 2), deleted `BlueTintScreenPass.cpp`
   directly from disk (outside the Editor) to simulate an out-of-band
   deletion, removed its now-dangling forward declaration/call from
   `ScreenPassAutoWireProbeGame.cpp` by hand, then scaffolded `YellowTint` —
   `read_file`/`search_in_dir` confirmed `YellowTintScreenPass.cpp` reads
   `/*priority=*/3,` (one more than the highest surviving priority, `2` from
   `GreenTint`) — never `2` again, exactly as PHASE5's own
   `ComputeNextScreenPassPriority()` is designed to behave.
7. **Auto-wire happy path, byte-for-byte**: confirmed for `RedTint` in item 2
   above — the forward declaration line and call line are the ONLY two new
   lines in the whole file; every other line (including both full anchor
   comment blocks) is untouched.
8. **Graceful-fallback path, on the OLD project**: `read_file`'d
   `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` in full (252 lines)
   BEFORE touching anything, then scaffolded a Screen Post-Process Pass named
   `OldProjectFallbackCheck` into it (`POST /project_assembly/create_asset`)
   — response confirmed the fallback wording (`"could not auto-wire it in
   (this project may pre-date auto-wiring support)..."`), and `read_file`'d
   `HelloGame.cpp` again afterward — completely, byte-for-byte identical, all
   252 lines. No `ProjectAssemblyProbeGame.cpp` was ever created (confirmed
   via `browse_dir` — only `HelloGame.cpp`/`ProbeCompute.comp`/the throwaway
   `OldProjectFallbackCheckScreenPass.cpp` existed). The throwaway `.cpp` was
   deleted afterward, restoring this permanent BIG-STEP-1 fixture to its
   exact original state.
9. **Idempotency, live**: the commented-out-call re-wire scenario was
   exercised for real, not merely re-confirmed from PHASE4's own test suite —
   manually commented out `RegisterGreenTintScreenPass(core);` in
   `ScreenPassAutoWireProbeGame.cpp`, deleted `GreenTintScreenPass.cpp`, then
   re-scaffolded `GreenTint` via the Create flow — the auto-wire helper
   correctly treated the commented-out line as NOT wired and inserted a
   FRESH, active call line immediately below the anchor, leaving the old,
   now-orphaned commented line completely untouched (confirmed by
   `read_file`: two occurrences of the `RegisterGreenTintScreenPass` call
   substring, one active, one still commented). This is the real project
   file behaving exactly as PHASE4's own dedicated test fixture predicted.
10. **The literal acceptance test — the single most important checkbox**.
    Zero manual C++ editing performed by the tester at this point (only the
    Create menu/HTTP route were ever used to add new features; the ONLY hand
    edits made anywhere in this whole verification were the deliberate,
    explicitly-called-out ones simulating "I deleted a file by hand" in
    items 6/9/11, which the design itself requires as the realistic manual
    half of that specific workflow). Triggered a real, synchronous
    `POST /project_assembly/hot_reload?name=ScreenPassAutoWireProbe`
    (discovered live that a SEPARATE `compile_only` call against an
    ALREADY-LOADED project's own `.dll` fails with a real linker error —
    `ld.exe: cannot open output file ...: Permission denied` — this is the
    documented, permanent, pre-existing "cannot recompile an already-loaded
    DLL" limitation, not a new bug; `hot_reload` is the correct route since it
    unloads first) — returned `{"last_outcome":"Success", ...}`.
    `GET /get_game_view` confirmed a real, visible translucent-red-tinted
    image (consistent with the same salmon/pink baseline `editor-core-
    separation-23`'s own PHASE6 screenshot evidence established for this
    exact `AlphaOver` 0.15-alpha blend). Structurally confirmed via a saved
    `GET /render_graph` JSON dump (`render_features[]` array): `"RedTint.
    ScreenTint"` (priority 0), `"YellowTint.ScreenTint"` (priority 3, correctly
    surviving after the delete-then-recreate churn), both `"is_project_feature":
    true`, sitting alongside the pre-existing `"ProjectAssemblyProbe.
    ScreenTint"` (priority 100) and the demo plugins — zero stale/duplicate
    entries. `GET /get_logs?keyword=collision` returned zero matches
    (the one benign, EXPECTED warning that DID appear elsewhere — `"DemoRender
    FeatureV3 and RedTint.ScreenTint both declared priority 0..."` — is
    exactly the documented, safe, cross-system plugin-vs-project-feature
    collision this same template's own generated comment predicts, tie-broken
    lexically, never crashing).
11. **Iteration realism check — 5 full cycles, back to back, in the same
    running Editor session.** On `ScreenPassAutoWireProbe` (with `RedTint`/
    `YellowTint` left as an unchanged control group throughout): deleted
    `GreenTintScreenPass.cpp`, removed its declaration/call by hand, scaffolded
    `IterA`, hot-reloaded — repeated 5 times total (`IterA` → `IterB` →
    `IterC` → `IterD` → `IterE`, each cycle deleting the previous one and
    scaffolding a differently-named replacement). All 5 `hot_reload` calls
    returned `"last_outcome":"Success"` (`cycle_id` 3 through 7). Final
    `GET /render_graph` confirmed only `IterE.ScreenTint` (priority 4) survives
    of the whole churn sequence — zero stale/duplicate entries for `IterA`
    through `IterD`. **The bounded GPU-state slot proof, mechanical, not
    assumed**: `GET /get_logs?category=RenderFeatureCompositor` showed every
    single one of `IterA` through `IterE`'s own `RegisterProjectFeature(...)`
    calls claiming the EXACT SAME slot, `14`, every time, across all 5 cycles
    — while the untouched control group (`RedTint`→slot 12, `YellowTint`→slot
    13, the pre-existing `ProjectAssemblyProbe.ScreenTint`→slot 15) stayed
    completely stable throughout — mirroring `editor-core-separation-23`'s own
    20-rename-cycle proof methodology exactly, just with 5 cycles instead of
    20 (still comfortably exceeding the phase file's own "at least FIVE"
    requirement).
12. `docs/conventions/project-assembly-system.md` updated — see "Documentation"
    below.
13. `AGENTS.md` updated — see "Documentation" below.
14. Full regression — see "Full clean build + full regression" below.

## Documentation

- **`docs/conventions/project-assembly-system.md`**: the existing
  `### On-screen Game View compositing` section's own closing paragraph
  (previously "**Explicitly, still BIG-STEP 1 only...**") was replaced,
  in place, with a corrected, honest statement that BIG-STEP 2 is now also
  shipped, pointing at the new section immediately below — a small, targeted,
  in-place fix, per the phase file's own explicit instruction that this is
  NOT a conflict with Locked Decision 9 ("add a new section, don't rewrite the
  existing one" applies to the substantial technical content above it, which
  was left completely untouched). A brand-new
  `### Screen Post-Process Pass scaffolding — Editor "Create" menu +
  auto-wire (BIG-STEP 2)` section was added immediately after it, documenting
  the new scaffold kind, the generated template's shape, the priority
  auto-assignment scheme, the auto-wire mechanism's own honest fresh-project-
  vs-old-project boundary (explicitly naming both permanent fixtures and the
  `HelloGame.cpp` naming quirk), and the live 5-cycle churn proof.
- **`AGENTS.md`**: the existing "Project Assembly On-Screen Render Feature
  Compositing" section's own "BIG-STEP 1 of 2 only" sentence was marked, in
  place, as historically accurate for when `editor-core-separation-23`
  itself shipped (mirroring the "Project Assembly Hot Reload" section's own
  established "AT THE TIME this campaign shipped" precedent) — never silently
  rewritten to imply the whole effort landed in one campaign. A new paragraph
  was added immediately after it, stating the entire 2-BIG-STEP effort is now
  CLOSED, summarizing the scaffolding tool and auto-wire mechanism, and citing
  this campaign's own `CAMPAIGN_COMPLETION_REPORT.md`.

## Full clean build + full regression (Step 3.3)

1. `cmake --build build --target clean` — 641 files removed.
2. `cmake --build build` — **623/623 steps succeeded, zero errors** (this
   repo's build enables no `-Wall`/`-Wextra` for its own code, so "zero new
   warnings" and "zero warnings at all" are the same statement here). A
   follow-up `cmake --build build` confirmed `ninja: no work to do.` — a
   genuinely clean, fully-caught-up build tree.
3. `ctest -C Debug --output-on-failure` from `build/` — **2081 tests total,
   100% of executed tests passing, 25 legitimate environment-gated skips** —
   up from `editor-core-separation-23`'s own documented 2055/25 baseline, a
   clean **+26 tests, +0 skips**, exactly matching this campaign's own new
   test cases by name: PHASE4's `ScreenPassAutoWireTests.cpp` (9), PHASE5's
   `ScreenPassPriorityAssignmentTests.cpp` (10), PHASE6's extension of
   `AssetScaffoldTemplateTests.cpp` (5 new), PHASE7's extension of
   `CreateAssetEndpointEndToEndTests.cpp` (2 new; one pre-existing test was
   renamed, not duplicated) — 9+10+5+2 = 26. **No test failed at any point
   during this full-suite run** — the "diagnose and fix it yourself"/
   `delegate_task` contingency (Step 3.3 item 3) was never triggered.

## No `delegate_task` needed

The full build/regression run surfaced zero failures, so PHASE0's own Step
3.3 item 3 contingency (diagnose root cause, `delegate_task(position:
"next")` to fix, report back before writing this report) was never
triggered. No `ask_questions` call was needed either — every ambiguity the
phase file itself anticipated (the fixture name, the naming-quirk gotcha, the
`compile_only`-vs-`hot_reload` distinction discovered live) was already
resolved either by the phase file's own explicit instructions or by direct
investigation of the real, observed behavior.

## Files touched

- `Projects/ScreenPassAutoWireProbe/` (new, permanent fixture — `Projects/`
  is `.gitignore`d, zero git-visible footprint)
- `docs/conventions/project-assembly-system.md` (closing-paragraph fix +
  new sub-section)
- `AGENTS.md` (historical-accuracy note + new follow-up paragraph)
- `task_manager/editor-core-separation-24/PHASE8_COMPLETION_REPORT.md` (this
  file)
- `task_manager/editor-core-separation-24/CAMPAIGN_COMPLETION_REPORT.md`
  (written alongside this report)

No file under `Projects/ProjectAssemblyProbe/` was left modified from before
this phase started — the throwaway `OldProjectFallbackCheckScreenPass.cpp`
was deleted, and `HelloGame.cpp` was confirmed byte-for-byte identical
before and after. Every temporary verification artifact created outside
`Projects/` (the PowerShell helper script, full-desktop screenshots, saved
`render_graph`/log JSON dumps) was deleted after use.
