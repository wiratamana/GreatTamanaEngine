# PHASE4 — Documentation, Full Regression, and Campaign Closeout — COMPLETION REPORT

**Campaign:** `editor-core-separation-10` — "ImGui Widget ID Uniqueness"
**Phase:** PHASE4 of 4
**Status:** DONE

---

## What was done

Exactly as specified by `PHASE4_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md` — this
phase's own Step 3.1-3.6, with no deviation.

### New files

- `docs/conventions/imgui-id-uniqueness.md` — mirrors `docs/conventions/logging.md`'s
  house style (problem statement -> real API -> "how to use it" with a good/bad code
  snippet pair -> permanent history/provenance table). Covers: the original bug
  (referencing the bug report file and `PHASE0_MASTER_STRATEGY.md` by path), the
  two-layer system (`ScopedUniqueId` structural prevention + `ImGuiIdConflictTracker`/
  `ImGuiIdConflictGuard` proactive detection) plus the still-untouched third layer
  (Dear ImGui's own `io.ConfigDebugHighlightIdConflicts`), the mandatory rule for all
  future code, an explicit "What this does NOT fix" section restating LDD7 (the
  Atmosphere pass-naming duplication is permanent, by design), and a permanent
  call-site-by-call-site record of every file this campaign touched.

### Edited files

- `AGENTS.md` — added a new "## ImGui Widget ID Uniqueness" section, inserted
  immediately after the existing "## Logging" section's own "Full convention:" line
  (found at line 468 as of this phase's start — re-verified fresh, not trusted from
  any earlier phase's own quoted line number, per this phase's own instructions) and
  before "## Render Target Format Matching" (originally line 470, now shifted down by
  the inserted section). Used the phase doc's own suggested section text verbatim.
  **Correction during this phase**: the first `edit_line` attempt used literal
  `"\n"` escape sequences inside the `contents` string instead of real embedded
  newlines, which the tool took literally rather than splitting into separate lines
  (the whole section landed on one single garbled line). Caught immediately by
  re-reading the file after the edit, and fixed with a second `edit_line` call using
  real newline characters in the `contents` parameter instead — the final file is
  correct (verified again below). This was a call-formatting mistake on this agent's
  own part, not a tool malfunction, so no `bug_report` was filed for it.
- `docs/README.md` — added one new bullet to the "Conventions" list (between
  "Logging" and "Render Target Format Matching", mirroring `AGENTS.md`'s own new
  section placement) pointing at `conventions/imgui-id-uniqueness.md`. Not explicitly
  listed in `PHASE0_MASTER_STRATEGY.md`'s file manifest, but a direct, minimal,
  documentation-only extension of this phase's own stated goal #1 ("discoverable by
  a future engineer/AI agent") — every other existing convention file already has a
  matching index entry, and leaving the new one out would make it an orphaned page
  nobody browsing the index would ever find.

No other file was touched this phase — PHASE1-3 already did every code retrofit;
this phase is documentation + verification only, per the master strategy's own Step
2 ("This phase does not add any more retrofit call sites").

## Verification performed

### 3.3 — Full clean build

Deleted the entire pre-existing `build/` directory (per the phase doc's own "if
this repo's convention is a genuinely fresh/clean configure+build... do that here"
instruction) and reconfigured from scratch:

```
cmake -S . -B build -G Ninja
```

Configure succeeded with zero network access needed (`SDL3`/`Vulkan-Headers`/`volk`/
`VMA`/`stb_image`/`cpp-httplib`/`nlohmann-json`/`KTX-Software`/`glm`/`saba`/`imgui`/
`imguizmo`/`GoogleTest` were all already present from a prior configure and were
reused as-is) — only the same pre-existing, unrelated warning already seen in every
earlier phase (KTX's own git-describe-version fallback).

```
cmake --build build
```

**Result: 561/561 build steps succeeded, zero errors, zero warnings related to this
campaign's own code.** Produced `GreatTamanaEditor.exe` and
`tests\GreatTamanaEngineTests.exe` from a completely clean object-file slate — this
is the first genuinely full (not incremental) build this whole four-phase campaign
has run, proving nothing else in the tree was silently broken by any earlier phase's
change.

### 3.4 — Full regression test

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

**Result: `100% tests passed out of 1888`** (`Total Test time (real) = 180.84 sec`).
Exactly 2 tests did not run, both legitimate, pre-existing, environment-gated skips
unrelated to this campaign (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
`CoreHeadlessConstructionTest.ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`
— the latter self-skips on any machine whose Vulkan driver lacks
`VK_EXT_headless_surface`, per `AGENTS.md`'s own `gte_core`/`gte_editor` Library
Separation section).

Baseline comparison (per this phase's own instructions — searched `AGENTS.md` for
"ctest regression pass" to find the latest campaign's own reported total before this
one): `editor-core-separation-9`'s own campaign entry reports **1882 tests, 100%
passing, 2 legitimate environment-gated skips**. This phase's own new total, **1888
tests**, is exactly `1882 + 6` — the 6 new `ImGuiIdConflictTrackerTest` cases PHASE1
added (`FirstRegistrationOfAFreshIdIsNotAConflict`,
`SecondRegistrationOfTheSameIdIsAConflict`, `ThirdRegistrationOfTheSameIdIsAlsoAConflict`,
`ResetClearsSeenIdsSoAPreviouslyConflictingIdIsFreshAgain`,
`TwoDifferentIdsNeverConflictWithEachOther`,
`DistinctIdCountReflectsAMixOfRepeatsAndFreshIds`), all present and passing in this
run (test IDs 220-225). **Zero previously-passing tests newly failed** — this
campaign introduced no regression anywhere in the tree.

### 3.5 — Final live, HTTP-driven end-to-end proof

1. `run_app_background`'d the freshly-built `build\GreatTamanaEditor.exe` (PID 19176).
2. `GET /list_tabs` → confirmed the exact tab name `"Render Graph"` is still present
   (alongside `"Hierarchy"`/`"Inspector"`/`"Scene"`/`"Game"`/`"Memory"`/`"Profiler"`/
   `"Jobs"`/`"Atmosphere"`/`"Log"`/`"Project"`/`"Demo Plugin Panel"` — this session
   has demo plugins loaded, same as PHASE2/PHASE3's own sessions).
3. `GET /activate_tab?name=Render%20Graph` → `{"activated_tab":"Render
   Graph","success":true}`.
4. `GET /get_swapchain` → a real 246 KB PNG screenshot (visually inspected) showing
   the Editor's docked layout with "Render Graph" active at the bottom, its "Pause"
   row, "GPU-Driven Batches" section (correctly reporting no live batch this frame),
   and "Plugin Render Features" section (both `DemoRenderFeatureV2`/`V3` checkboxes,
   priority steppers, and the `[v3]` badge) all rendering correctly and identically
   in shape to PHASE2/PHASE3's own screenshots — **zero visible regression** in this
   panel's rendered output from any phase of this campaign. **Same disclosed,
   pre-existing limitation restated honestly** (not a new one introduced this phase):
   the "Offscreen Regime (Game View + Scene View)" pass table — where the two
   duplicate-named Atmosphere pass rows live — sits below this fixed-resolution
   screenshot's visible viewport, since there is still no HTTP mechanism to scroll a
   panel or resize/maximize the docked layout. This does not weaken the proof below,
   since that table is being built every real frame regardless of scroll position
   (`ImGui::TableNextRow()`/`ScopedUniqueId` execute unconditionally).
5. Let the app run for a few real seconds with "Render Graph" active (its own
   real, live, genuinely-duplicate-named Atmosphere pass rows being built every
   frame underneath the scrolled-off viewport).
6. `GET /get_logs?category=ImGuiIdConflict` → `{"count":0,"entries":[],
   "latest_id":30,"logging_enabled":true}` — **EMPTY**, confirming zero conflicts
   detected against the real, live, duplicate-named production data.
7. `GET /get_logs?min_level=Warning` → 17 entries, all pre-existing/unrelated
   (plugin CRT-linkage/render-feature-priority-tie-break/GPU-timing-slot-budget
   notices) — **zero** `ImGuiIdConflict` entries among them.
8. `stop_app_background(pid: 19176)` closed the process cleanly.

**Honest, restated limitation** (identical in kind to PHASE2/PHASE3's own): no HTTP
endpoint exists to simulate a mouse hover, so this live check cannot directly
re-enact "hover the duplicate-name row and confirm Dear ImGui's own red box no
longer appears" end-to-end over the network. The authoritative proof that the
underlying mechanism is correct is PHASE1's deterministic Tier-1 test
(`ImGuiIdConflictTrackerTests.cpp`, 6/6 passing in this run); this live check's own
real job is only to prove (a) nothing rendered differently after the campaign's full
scope landed, and (b) no conflict was logged against real, live, genuinely
duplicate-named production data during an actual run — both confirmed above.

## Notes for the campaign as a whole

- No `delegate_task` sub-fix was ever needed — the full clean build and full
  regression pass both succeeded on the first attempt with zero compile errors and
  zero newly-failing tests, so Step 3.3/3.4's "if a fix is non-trivial, delegate a
  focused sub-task" contingency was never triggered.
- `CAMPAIGN_COMPLETION_REPORT.md` (this same folder) covers the full four-phase
  writeup per this phase's own Step 3.6.
