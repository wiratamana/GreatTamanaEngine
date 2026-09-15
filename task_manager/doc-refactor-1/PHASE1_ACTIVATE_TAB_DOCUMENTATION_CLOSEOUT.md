# PHASE1 — `activate_tab`/`list_tabs` Documentation Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first, especially its
"Locked Decisions" section.
**Also read, in full, before starting:** every file in
`task_manager/network-impl-7/`, in this order: `PHASE0_MASTER_STRATEGY.md`,
`PHASE0_DOUBLE_CHECK_REPORT.md`, `PHASE1_COMPLETION_REPORT.md` through
`PHASE4_COMPLETION_REPORT.md`, then `PHASE5_TESTING_DOCS_AND_REGRESSION_
SAFETY.md` (this last one is the actual spec this phase finishes — Sections
3.2, 3.3, and 3.4 specifically; Section 3.1 is already done, see below).

## Step 1: The Goal

Finish the ONLY parts of the `network-impl-7` campaign's own Phase 5 that were
never completed: the two documentation bullets and the two completion
reports. Do this on the CURRENT, still-bloated `README.md`/`AGENTS.md` — do
NOT create any `docs/` file in this phase, that is Phases 2-3's job. Writing
the bullets here first means they automatically travel along when Phase 2/3
extract the surrounding section into `docs/`.

## Step 2: The Situation / The Problem

Confirmed by direct inspection of this branch (`fix/doc-refactor`) before this
strategy was written:

- **Already done, do NOT redo:** `src/Application/EditorUiCommandBridge.h/.cpp`,
  `src/Editor/EditorPanelCatalog.h`, the ImGui tab-activation engine
  (`IEditorLayer::ActivateTab()` and its real/null implementations),
  `Application::Run()`'s frame-loop drain step, `src/Network/NetworkRoutes.h/
  .cpp`'s `ParseActivateTabQuery`/`BuildActivateTabResponseJson`/
  `BuildUnknownTabNameResponseJson`/`BuildListTabsResponseJson`, and
  `src/Network/NetworkServer.cpp`'s `GET /activate_tab`/`GET /list_tabs`
  route registrations. All confirmed present and already covered by
  `tests/Network/NetworkRoutesTests.cpp` and `tests/Network/
  NetworkServerTests.cpp` (Tier-1 tests from Phase 4) AND by `tests/Network/
  ActivateTabEndpointEndToEndTests.cpp` (the Phase 5 Section 3.1 end-to-end
  test file — already written, already registered in `tests/CMakeLists.txt`
  at the `Network/ActivateTabEndpointEndToEndTests.cpp` entry). **Do not touch
  any of these files or write any new test file in this phase** — Section
  3.1 of the `network-impl-7` Phase 5 document is complete.
- **NOT done (confirmed by `search_in_dir` for `activate_tab`/`list_tabs`/
  `EditorUiCommandBridge`, case-insensitive, across `README.md` and
  `AGENTS.md` — zero hits in both):**
  - `AGENTS.md` "Networking" section bullet (Section 3.2 of the Phase 5 doc).
  - `README.md` "Status" section bullet (Section 3.3).
  - `task_manager/network-impl-7/PHASE5_COMPLETION_REPORT.md` (does not
    exist).
  - `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md` (Section 3.4;
    does not exist).
- The one full 3-configuration build+regression pass Section 3.5 of that
  document asks for is **deliberately deferred** to `PHASE5_FULL_REGRESSION_
  AND_CAMPAIGN_CLOSEOUT.md` of THIS (`doc-refactor-1`) campaign, so the whole
  `doc-refactor-1` effort only pays for one full build+regression pass instead
  of two. Only a fast compile check + a filtered `ctest`/direct-`.exe` run is
  required at the end of THIS phase.

## Step 3: The Plan

### 3.1 — `AGENTS.md`, "Networking" section bullet

Locate the current `## Networking` section (baseline: starts around line 936;
**re-verify with `search_in_dir "## Networking" AGENTS.md`** since this is a
fresh branch checkout snapshot). Find where its own existing chronological
bullet list ends (immediately before the nested `### Named Texture Capture
(GET /get_texture)` subsection, OR at the very end of the section if that
nested subsection itself has trailing bullets after it — inspect the real
current content with `read_line` around that boundary before deciding the
exact insertion point; match whatever placement convention every prior
addition to this section already used, i.e. append after the LAST bullet
that is directly under `## Networking` itself, before the `###` subheading).

Write a new bullet, in the same prose voice/level of detail as the
surrounding bullets (re-read the 2-3 bullets immediately before your
insertion point first, to match tone), that documents, at minimum:

- `EditorUiCommandBridge` — a new, separate bridge type from
  `EngineCommandBridge`/`FrameCaptureBridge`, and WHY it had to be separate
  (cite the exact rule this codebase already established for
  `FrameCaptureBridge` — find that bullet's own wording in this same section
  and mirror its reasoning: one bridge per distinct "thing the network thread
  is allowed to touch").
- `GET /activate_tab?name=<PanelName>`'s full contract: the query parameter,
  and every status code it can return (`200` success, `400` missing/invalid
  `name`, `404` unknown panel name, `409` known panel but not currently
  live/instantiated, `503` bridge unavailable or another command already in
  flight, `504` bridge timeout) — pull the authoritative mapping from
  `task_manager/network-impl-7/PHASE0_MASTER_STRATEGY.md`'s own locked
  "Endpoint contract" (as corrected by that campaign's own
  `PHASE0_DOUBLE_CHECK_REPORT.md` — that double-check report takes precedence
  over the original wording wherever they disagree).
- `GET /list_tabs`'s contract (no parameters, `200`, JSON array of every
  currently-known panel name).
- The exact frame-loop position `Application::Run()` drains this bridge at
  (`NewFrame()` -> drain `EditorUiCommandBridge` -> ... -> `BuildUI()`) and
  WHY (the one-frame-lag reasoning — read `PHASE3_APPLICATION_WIRING_AND_
  FRAME_LOOP_INTEGRATION.md`'s own Section 3 for the exact rationale to
  paraphrase, don't invent a new explanation).
- `EditorPanelCatalog.h`'s role as the single shared source of truth for
  known panel names, and that `DockLayout.cpp`'s default-layout logic AND
  both new endpoints all read from it now.

Keep it to roughly the same length as the two or three most recent bullets
already in this section (do not write a novel — match the house style).

### 3.2 — `README.md`, "Status" section bullet

Locate `## Status` (baseline: starts around line 839) and find its LAST
bullet before `## Roadmap` (baseline: around line 1966, currently the
`atmosphere-scattering-4` follow-up entry). Append one new bullet AFTER it,
in the exact same voice as every other Status entry (bold lead sentence
naming the concrete new capability, then 2-4 sentences of supporting detail
naming the actual new files/endpoints), summarizing the whole feature for a
reader who has never opened `task_manager/network-impl-7/` — mirror how the
`network-impl-6` and `atmosphere-scattering-3`/`-4` entries already summarize
THEIR own campaigns concisely (re-read those two entries immediately before
writing this one).

### 3.3 — `task_manager/network-impl-7/PHASE5_COMPLETION_REPORT.md`

Write this report following the exact shape of `PHASE4_COMPLETION_REPORT.md`
in that same folder (What was done / Deviations / Verification performed /
Exact state left in). Cover: the two doc bullets above (quote exactly where
each was inserted and why), that Section 3.1 (the end-to-end test file) was
found already complete and required no changes, that Section 3.5 (full
regression) is deliberately deferred to `doc-refactor-1`'s own `PHASE5_
FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md` (name it explicitly, with the
reason: avoid a duplicate full build+regression pass), and that Section 3.6
(manual live smoke test) is likewise deferred to that same final phase.

### 3.4 — `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md`

Write this report following the shape of
`task_manager/atmosphere-scattering-3/CAMPAIGN_COMPLETION_REPORT.md` (VERIFIED
to exist at the time this strategy was written — read it in full first). Use
IT, not `task_manager/network-impl-6/NETWORK_IMPL_6_CAMPAIGN_COMPLETION_
REPORT.md`, as the primary shape to copy: `network-impl-7`'s own target file
name is the plain, unprefixed `CAMPAIGN_COMPLETION_REPORT.md`, exactly
matching `atmosphere-scattering-3`'s own naming convention (`network-impl-6`'s
equivalent report uses a different, folder-name-prefixed file name,
`NETWORK_IMPL_6_CAMPAIGN_COMPLETION_REPORT.md`, which is a fine cross-check
for level of DETAIL but not the naming convention to mirror). Concretely,
follow `atmosphere-scattering-3`'s own section shape: an opening line naming
the parent phase docs it ties together, then numbered sections — "The
original problem", "Root cause" (or, for this feature, simply "What was
built"), "What each phase did" (one short paragraph per phase, citing that
phase's own `PHASEn_COMPLETION_REPORT.md`), "The final result", "Final
build/test pass result", and "Campaign disposition" — tying together all five
`PHASEn_COMPLETION_REPORT.md` files in that folder into one final summary:
what was built end to end, what was verified in each phase, the final
Tier-1/Tier-2 test count delta for this feature, and an explicit note that the
campaign's full regression pass + manual smoke test happened one level up, as
part of `doc-refactor-1`'s `PHASE5_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`
(cite that file by its full relative path so a future reader can find it).
**Note for whoever writes this report:** `doc-refactor-1`'s own `PHASE5_
FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md` will come back and append a short
addendum to THIS SAME file once the deferred regression pass/smoke test
actually run (see `PHASE0_MASTER_STRATEGY.md`'s own Definition of Done) — this
is expected and not a conflict; leave this report's own "deferred" wording
intact rather than trying to predict Phase 5's future results.

### 3.5 — Verification for this phase only

- `cmake --build build` — fast compile check. Since this phase only edits
  `.md` files, this should be a no-op rebuild (nothing under `src/`/`tests/`
  changed); run it anyway as a cheap sanity check that the branch itself is
  in a buildable state before you start layering more phases on top.
- Run the existing test suite FILTERED to just this feature's tests (do not
  run the full suite yet): e.g.
  `build\GreatTamanaEngineTests.exe --gtest_filter="*ActivateTab*:*ListTabs*:*Network*"`
  (adjust the exact filter syntax to whatever this project's existing test
  binary actually accepts — check `TESTING.md` for the precise invocation
  convention already documented there). Confirm zero regressions; this must
  already pass since no `.cpp`/`.h` file changed in this phase, but running it
  confirms the branch was already healthy before your edits.

## Step 4: Deliverables / Files Touched

- Modified: `README.md` (one new "Status" bullet), `AGENTS.md` (one new
  "Networking" bullet).
- Created: `task_manager/network-impl-7/PHASE5_COMPLETION_REPORT.md`,
  `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md`,
  `task_manager/doc-refactor-1/PHASE1_COMPLETION_REPORT.md` (this campaign's
  own report, per `PHASE0`'s Workflow Rule #2).
- No `src/`, `tests/`, or `CMakeLists.txt` file is touched in this phase.

## Step 5: Handoff Note for Phase 2

Phase 2 will extract `README.md`'s `## Status` section (which now includes
this phase's new bullet) into `docs/CHANGELOG.md`. Make sure your new bullet
in Step 3.2 is placed as a normal Markdown list item indistinguishable in
style from its neighbors, so Phase 2's extraction can move the whole section
verbatim without any special-casing.
