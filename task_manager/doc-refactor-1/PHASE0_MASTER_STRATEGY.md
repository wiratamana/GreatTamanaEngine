# PHASE0 — MASTER STRATEGY: `doc-refactor-1` Campaign

**Role of this document:** the orchestrator. Every other `PHASEn_*.md` file in
this same folder (`task_manager/doc-refactor-1/`) is a child of this one. Read
THIS file first, always, before opening any `PHASEn_*.md` file. Each child
phase document also links back here and must re-read this file's "Locked
Decisions" section before doing any work, because those decisions are binding
across every phase and are NOT repeated in full inside each child document.

## Step 1: The Goal (Where are we going?)

Two concrete outcomes, in this exact order of priority:

1. **Close out the already-in-flight `network-impl-7` campaign.** `GET
   /activate_tab` and `GET /list_tabs` are fully implemented, wired, and
   Tier-1/Tier-2-tested in `src/` and `tests/` on this branch already — the
   ONLY missing pieces are: the `AGENTS.md`/`README.md` documentation bullets
   Phase 5 of that campaign was supposed to add, and that campaign's own
   completion reports. This must be finished FIRST, on the CURRENT (still
   bloated) `README.md`/`AGENTS.md`, so the content naturally rides along when
   those two files get split apart in the phases that follow (see "Locked
   Decision #1" below).
2. **Turn `README.md` (133 KB / 1972 lines) and `AGENTS.md` (155 KB / 2109
   lines) into thin, GitHub-style entry-point files**, by extracting almost
   all of their prose into a new `docs/` folder (grouped into subfolders),
   leaving both root files as short indexes that link out to the full detail.
   `BUILDING.md`, `TESTING.md`, and `TODO.md` are already properly separated
   top-level files and are explicitly OUT of scope for this campaign — do not
   touch their content, only fix a link INTO them if one becomes stale.

Both outcomes are achieved purely by writing/editing files (Markdown, plus a
handful of `.cpp`/`.h`/`CMakeLists.txt` touch-points already built in a prior
campaign that this campaign only needs to document, not re-implement). No
phase in this campaign is a "just think about it" phase — every phase below
produces or edits real files.

## Step 2: The Situation (Where are we now?)

- Branch: `fix/doc-refactor`. Project root:
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`.
- `task_manager/network-impl-7/` contains the FULL 5-phase strategy for the
  `activate_tab`/`list_tabs` feature. Phases 1-4 are done and their own
  `PHASEn_COMPLETION_REPORT.md` files confirm it (route handlers in
  `src/Network/NetworkRoutes.h/.cpp` and `src/Network/NetworkServer.cpp`,
  `EditorUiCommandBridge` in `src/Application/`, `EditorPanelCatalog.h`, the
  ImGui tab-activation engine, and `Application::Run()` frame-loop wiring are
  ALL present and compiling today). Phase 5's own TEST file,
  `tests/Network/ActivateTabEndpointEndToEndTests.cpp`, is ALSO already
  written and already registered in `tests/CMakeLists.txt` — confirmed by
  direct inspection.
- What Phase 5 of `network-impl-7` did **NOT** finish (confirmed by grep — zero
  hits for `activate_tab`/`list_tabs`/`EditorUiCommandBridge` in `README.md` or
  `AGENTS.md`, and no `PHASE5_COMPLETION_REPORT.md` or
  `CAMPAIGN_COMPLETION_REPORT.md` file exists in `task_manager/network-impl-7/`
  yet):
  - The `AGENTS.md` "Networking" section bullet describing the new bridge/
    endpoints.
  - The `README.md` "Status" section bullet summarizing the feature.
  - `task_manager/network-impl-7/PHASE5_COMPLETION_REPORT.md` and
    `CAMPAIGN_COMPLETION_REPORT.md`.
  - The one full cross-configuration build+regression pass Phase 5 asked for
    (default / `-DGTE_ENABLE_EDITOR=OFF` / `-DGTE_ENABLE_PROJECT_PANEL=OFF`).
- `README.md` structure today (baseline line numbers BEFORE Phase 1 of THIS
  campaign edits anything — re-verify with `search_in_dir`/`read_line` before
  trusting any exact number, they will have shifted): `# GreatTamanaEngine`
  title (0), `## Goal` (4), `## Architecture` (9, with `### Event handling`
  29, `### Math` 54, `### Rendering` 63, `### Entity-Component-System (ECS)`
  150, `### Asset Pipeline` 231, `### Editor / Debug UI` 437), `## Building`
  (831, one line, already just links to `BUILDING.md`), `## Testing` (835, one
  line, already just links to `TESTING.md`), `## Status` (839 — a ~1128-line
  reverse-chronological project history, one bullet per past campaign), `##
  Roadmap` (1967, already just links to `TODO.md`). Total 1972 lines.
- `AGENTS.md` structure today (same caveat about line numbers shifting after
  Phase 1): `# AGENTS.md` title (0), `## Coding Guidelines` (4, short, ~19
  lines), `## GPU Resource Memory Tracking` (23), `## CPU Dependency Memory
  Tracking` (86), `## Profiling` (159), `## Job System` (443), `## Networking`
  (936, with a nested `### Named Texture Capture (GET /get_texture)` at 1148),
  `## Render Target Format Matching` (1321), `## Skeletal Animation Pose
  Resolution` (1378), `## GPU Vertex Skinning` (1427), `## Atmosphere
  Scattering` (1496), `## Entity-Component-System (ECS)` (1746), `## Scene
  Serialization` (1899), `## Editor Module Structure` (1940), `## Testability
  & Regression Safety` (2062, short, ~47 lines). Total 2109 lines.
- **This is the hard part of the whole campaign**: roughly 250 source-code
  comments across `src/` and `tests/` cite `AGENTS.md` (and ~12 cite
  `README.md`) by section name, e.g. `// see AGENTS.md, "Networking"` or `//
  see AGENTS.md, "Job System"`. A naive full-content move that deletes those
  headings would strand every one of those comments. Locked Decision #2 below
  is how this campaign avoids that.
- **Important nuance for Phase 4's own audit (found during this campaign's own
  second-iteration review pass):** unlike the ~250 `AGENTS.md`-citing comments
  above — every one of which cites a section by its HEADING NAME, and is
  therefore fully, structurally protected by Locked Decision #2 (the heading
  itself is never deleted, only shortened) — a few of the ~12 `README.md`-
  citing comments instead quote specific PROSE from inside the `## Status`
  section (e.g. `src/Game/Animation/AnimationSystem.cpp` quotes the exact
  phrase "A spawned MMD model can now actually be ANIMATED"). Locked Decision
  #4 keeps only the LAST 3-5 `## Status` bullets inline in `README.md` —
  everything older is relocated VERBATIM (never deleted) to
  `docs/CHANGELOG.md`. A comment quoting an older bullet's prose is therefore
  still only one hop away (via the `## Status` heading's own link to
  `docs/CHANGELOG.md`), the same as every other extracted section — but it is
  no longer found by searching `README.md` alone. This is an accepted, natural
  consequence of moving (not deleting) content, not a bug to fix by rewriting
  the comments themselves (still out of scope, per Locked Decision #2/
  Workflow Rules) — but Phase 4's own Step 3.6 must explicitly enumerate and
  check each of these ~12 comments individually, not just spot-check 3-5 of
  them, precisely because this class of citation behaves differently from the
  heading-name kind.
- No `docs/` folder exists yet. It must be created fresh.

## Step 3: The Plan — Locked Decisions (binding on every phase below)

These were confirmed directly with the project owner before any phase
document was written. Every phase must follow them exactly; do not
re-litigate them mid-phase.

1. **Sequencing:** finish the `network-impl-7` documentation closeout FIRST
   (Phase 1, on the still-bloated files), THEN split the files apart
   (Phases 2-3). This means the new bullets naturally travel with the rest of
   the "Networking"/"Status" content when it gets extracted in Phases 2-3 —
   nobody has to draft that content twice or reconcile two divergent copies.
2. **Keep the same section headings.** The new thin `AGENTS.md` keeps every
   existing `## <Section Name>` heading (e.g. `## Networking`, `## Job
   System`, `## Entity-Component-System (ECS)`) exactly as spelled today, but
   each shrinks to a short 2-4 line summary ending in a link to the new full
   file under `docs/`. This is why the ~250 existing `// see AGENTS.md,
   "Networking"` -style source comments do **not** need to be rewritten as
   part of this campaign — the heading they cite still genuinely exists in
   `AGENTS.md`, it just now summarizes-and-links instead of containing
   everything inline. Do NOT delete any `## `/`### ` heading text from
   `AGENTS.md` during Phase 3. The same logic applies to `README.md`'s
   headings in Phase 2 (`## Architecture`, `## Status`, etc. all stay).
3. **Folder layout — `docs/` (not `doc/`), grouped into subfolders:**
   ```
   docs/
     README.md                        (Phase 4 — the docs/ landing index)
     CHANGELOG.md                     (Phase 2 — full Status history)
     architecture/
       event-handling.md              (Phase 2)
       math.md                        (Phase 2)
       rendering.md                   (Phase 2)
       ecs.md                         (Phase 2 — README's ECS architecture
                                        overview; distinct from, and must
                                        cross-link to, conventions/ecs.md)
       asset-pipeline.md              (Phase 2)
       editor-debug-ui.md             (Phase 2)
     conventions/
       gpu-resource-memory-tracking.md          (Phase 3)
       cpu-dependency-memory-tracking.md         (Phase 3)
       profiling.md                              (Phase 3)
       job-system.md                             (Phase 3)
       networking.md                             (Phase 3 — includes the
                                                    "Named Texture Capture"
                                                    subsection AND Phase 1's
                                                    new activate_tab/list_tabs
                                                    bullet)
       render-target-format-matching.md          (Phase 3)
       skeletal-animation-pose-resolution.md      (Phase 3)
       gpu-vertex-skinning.md                     (Phase 3)
       atmosphere-scattering.md                   (Phase 3)
       ecs.md                                     (Phase 3 — AGENTS.md's ECS
                                                    CONVENTION; cross-link
                                                    with architecture/ecs.md)
       scene-serialization.md                     (Phase 3)
       editor-module-structure.md                 (Phase 3)
   ```
   `AGENTS.md`'s `## Coding Guidelines` and `## Testability & Regression
   Safety` sections stay **inline, in full**, in the thin `AGENTS.md` — they
   are already short (~19 and ~47 lines) and are the two most universally
   cross-cutting rules in the whole document; extracting them would just add
   an extra click for the two things almost every task needs to re-read.
4. **`README.md`'s new "Status" section keeps its last 3-5 entries inline**
   (verbatim, including Phase 1's new bullet if it lands among the last 5),
   plus a link to the full `docs/CHANGELOG.md` for everything older.
5. **`BUILDING.md`, `TESTING.md`, `TODO.md` are untouched** (content-wise) —
   they already are exactly the kind of separated, focused file this campaign
   is trying to create more of. Only touch them if Phase 4's link-integrity
   audit finds an actually-broken link pointing at them.

## Step 3 (continued): Phase List

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_ACTIVATE_TAB_DOCUMENTATION_CLOSEOUT.md` | Finish `network-impl-7`: add the missing `AGENTS.md`/`README.md` bullets (on the still-bloated files) + write that campaign's own completion reports. |
| 2 | `PHASE2_README_ARCHITECTURE_AND_CHANGELOG_EXTRACTION.md` | Split `README.md`'s `## Architecture` subsections into `docs/architecture/*.md` and its `## Status` history into `docs/CHANGELOG.md`; rewrite `README.md` thin. |
| 3 | `PHASE3_AGENTS_CONVENTIONS_EXTRACTION.md` | Split `AGENTS.md`'s 12 subsystem sections into `docs/conventions/*.md`; rewrite `AGENTS.md` thin (pointer headings, per Locked Decision #2). |
| 4 | `PHASE4_CROSS_REFERENCE_LINK_INTEGRITY_AND_DOCS_INDEX.md` | Write `docs/README.md` (the new landing index), add a short "Documentation" pointer near the top of `README.md`/`AGENTS.md`, and audit every internal Markdown link/anchor across the repo's own `.md` files for breakage. |
| 5 | `PHASE5_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md` | The one full 3-configuration build+`ctest` pass (covering BOTH the `network-impl-7` feature AND the doc split), the manual live smoke test, and this campaign's own `CAMPAIGN_COMPLETION_REPORT.md`. |

Each phase document is self-contained enough to hand to a fresh implementer
who has only read `PHASE0` (this file) plus the previous phases' own
completion reports — it does not require re-reading this file's full prose,
only its "Locked Decisions" above.

## Workflow Rules (apply to every phase)

1. **No full build/full `ctest` regression run except in Phase 5.** Phases 1-4
   use a fast compile check (`cmake --build build`) plus, where relevant, a
   `ctest`/direct-`.exe` run filtered down to the handful of tests that phase
   actually touches (e.g. `-R "ActivateTab|ListTabs"`). Phase 5 is explicitly
   where the one full clean build + full regression pass across all three
   configurations happens.
2. **Every phase writes its own `PHASEn_COMPLETION_REPORT.md`** inside
   `task_manager/doc-refactor-1/` (this same folder) when done, following the
   shape already established by `task_manager/network-impl-7/PHASE1_
   COMPLETION_REPORT.md` (what was done, deviations from the strategy
   document if any, verification performed, exact state left in). Note this
   is IN ADDITION to Phase 1's own job of writing `network-impl-7`'s reports
   inside `task_manager/network-impl-7/` — two different folders, two
   different sets of reports, do not confuse them.
3. **`git add`/`git commit` at the end of every phase**, staying on
   `fix/doc-refactor` throughout the whole campaign — never switch branches.
4. **Always read the previous phase's own `PHASEn_COMPLETION_REPORT.md`
   before starting** — line numbers quoted in these strategy documents are
   baselines only and WILL have shifted after earlier phases edit the same
   files; re-derive exact positions with `search_in_dir`/`read_line` at
   execution time, never trust a hardcoded line number blindly.
5. **Re-verify every quoted line-number range in this document and in every
   child phase document at execution time.** They are all snapshots taken
   before Phase 1 ran and are provided only to orient the implementer, not as
   ground truth to blindly `edit_line` against.

## Definition of Done (whole campaign)

- `network-impl-7` has both its missing doc bullets and both of its missing
  completion reports.
- `README.md` and `AGENTS.md` are both dramatically shorter, read like a
  typical thin GitHub root README/CONTRIBUTING-style file, and every
  extracted topic is one click away under `docs/`.
- Every `## `/`### ` heading that existed in `README.md`/`AGENTS.md` before
  this campaign still exists (Locked Decision #2) — nothing was silently
  deleted, only shortened-and-linked.
- `docs/README.md` exists and is a genuinely useful table of contents for the
  whole `docs/` tree.
- Zero broken internal Markdown links anywhere in the repository's own `.md`
  files (excluding `third_party/`, which is untouched and out of scope).
- One full, clean, 3-configuration build + `ctest` regression pass, plus a
  manual live smoke test of `GET /activate_tab`/`GET /list_tabs`, all green.
- `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md` (written in
  Phase 1) is amended in Phase 5 with a short addendum confirming its own
  deferred Sections 3.5/3.6 obligations were actually discharged, with a
  cross-reference back to `doc-refactor-1`'s own Phase 5 reports — closing
  that loop from BOTH directions, not only from this campaign's own side.
- `task_manager/doc-refactor-1/CAMPAIGN_COMPLETION_REPORT.md` ties every phase
  together.
