# PHASE4 Completion Report — Cross-Reference Link Integrity and Docs Index

**Parent:** `PHASE0_MASTER_STRATEGY.md`. Executed as specified in
`PHASE4_CROSS_REFERENCE_LINK_INTEGRITY_AND_DOCS_INDEX.md`, having first read
`PHASE0_MASTER_STRATEGY.md`, `PHASE2_COMPLETION_REPORT.md`, and
`PHASE3_COMPLETION_REPORT.md` in this same `task_manager/doc-refactor-1/`
folder, plus the real, current `README.md`/`AGENTS.md`/`docs/` tree (re-derived
fresh rather than trusting any baseline line number, per Workflow Rule #4/#5).

## What was done

### Baseline re-verification

Confirmed the real, post-Phase-3 state before touching anything: `README.md`
254 lines (Phase 2's final 248 plus this phase's own new section, see below),
`AGENTS.md` 215 lines (Phase 3's final 209 plus this phase's new section),
`docs/` containing `CHANGELOG.md`, `architecture/` (6 files), `conventions/`
(12 files) — 19 files total, no `docs/README.md` yet, matching Phase 3's own
handoff note exactly.

### 3.1 — `docs/README.md` (the landing index)

Created `docs/README.md` with the exact shape the strategy document
specifies: an intro paragraph explaining the two-way split
(`architecture/` mirrors the root `README.md`'s old "Architecture" section;
`conventions/` mirrors the root `AGENTS.md`'s old subsystem sections), a
`## Architecture` list (one bullet per file in `docs/architecture/`, title +
one-sentence description + relative link — descriptions written from each
file's own real opening sentences, re-read via `read_file`/`read_line`
rather than assumed), a `## Conventions` list (same shape, one bullet per
file in `docs/conventions/`), a `## Changelog` section linking to
`CHANGELOG.md`, an `## Other Project Documentation` section linking to
`../BUILDING.md`/`../TESTING.md`/`../TODO.md` (Locked Decision #5's
untouched files), and the closing line linking back to both root files.

### 3.2 / 3.3 — "Documentation" pointer sections

Added a `## Documentation` section immediately after the title/tagline in
both root files, before their first pre-existing heading:

- `README.md`: inserted between the tagline (line 2) and `## Goal` (now
  shifted from line 4 to line 10), using the exact wording the strategy
  document specifies, linking to `docs/README.md`.
- `AGENTS.md`: inserted between the tagline (line 2) and `## Coding
  Guidelines` (now shifted from line 4 to line 10), using the exact wording
  the strategy document specifies, linking to `docs/conventions/` via
  `docs/README.md`.

Both edits were single `edit_line` calls that replaced the pre-existing
first heading line with "new section + blank line + that same original
heading line" — confirmed by immediate re-read that no other line shifted
unexpectedly and every original heading is still present.

### 3.4 — Link-integrity audit

Ran `search_in_dir` with the regex `\]\([^)]+\)` across `README.md`,
`AGENTS.md` (root, non-recursive), and the whole `docs/` folder (recursive):
**30 link matches in 4 root-level `.md` files** (`AGENTS.md`, `BUILDING.md`,
`README.md`, `TESTING.md` — `TODO.md` had zero Markdown-syntax links) plus
**129 link matches in 20 files under `docs/`** — 159 total link occurrences
enumerated and individually resolved against the real folder structure via
`browse_dir`/`read_file`/`read_line`:

- Every `docs/conventions/*.md` / `docs/architecture/*.md` sibling-file link
  (e.g. `rendering.md` -> `ecs.md`, `job-system.md` -> `profiling.md`)
  resolves — confirmed by matching each link target against the real
  `browse_dir` listings of both folders (6 architecture files, 12
  conventions files, all present, no filename typos).
- Every `../README.md` link from inside `docs/architecture/*.md` and
  `docs/conventions/*.md` (used by every one of the 18 files' own
  breadcrumb line) now resolves, since `docs/README.md` was created this
  phase — previously a deliberate forward reference per Phases 2/3's own
  completion reports, now closed.
- Every `../../README.md` (from `docs/architecture/*.md`) and
  `../../AGENTS.md` (from `docs/conventions/*.md`) breadcrumb link resolves
  to the real root files.
- Every `../CHANGELOG.md` link (from `docs/architecture/*.md`) and
  `docs/CHANGELOG.md` link (from the root `README.md`) resolves to the real
  `docs/CHANGELOG.md`.
- The two intentional `docs/architecture/ecs.md` <-> `docs/conventions/ecs.md`
  cross-references were checked in BOTH directions by direct `read_file`
  inspection of both files: `architecture/ecs.md` line 88 links
  `../conventions/ecs.md` (real path: `docs/conventions/ecs.md`, exists);
  `conventions/ecs.md` line 166 links `../architecture/ecs.md` (real path:
  `docs/architecture/ecs.md`, exists). Both directions confirmed resolving.
- The six `../../AGENTS.md#coding-guidelines` / `../../AGENTS.md#testability--regression-safety`
  anchor links (`profiling.md` x1, `ecs.md` x2, `editor-module-structure.md`
  x2, `networking.md` x1, per Phase 3's own recount) were re-checked against
  the REAL, final `AGENTS.md` slugs: `## Coding Guidelines` and
  `## Testability & Regression Safety` are both still present, unchanged in
  text, and this phase's own new `## Documentation` section (inserted above
  both) does not affect either heading's own GitHub anchor slug
  (`coding-guidelines` / `testability--regression-safety` — lowercase,
  spaces to hyphens, `&` dropped producing the double-hyphen) since anchor
  slugs are computed purely from a heading's own text, not its position in
  the file. Both anchors still resolve.
- Every external (`https://...`) link (`benikabocha/saba`, `hoffstadt/pl-sky`,
  Vulkan SDK, GoogleTest) was left untouched — out of scope for an internal
  link-integrity audit.
- **Zero broken links found** across all 159 occurrences checked. No fix was
  needed anywhere beyond creating `docs/README.md` itself (which is what
  closed the only known-forward-reference gap Phases 2/3 had already flagged
  as expected).

### 3.5 — Sweep for stale content assumptions

Searched `BUILDING.md`, `TESTING.md`, `TODO.md` for `README.md`/`AGENTS.md`
mentions (20 + 13 hits respectively, across all three files combined with
`README.md`/`AGENTS.md` root/docs hits). **Confirmed zero of these are actual
Markdown-syntax links** (`[text](path#anchor)`) — every single one is a
plain-text/backtick citation of a heading name (e.g. `` `README.md`, "Status" ``,
`` see `AGENTS.md`, "Profiling" ``), the same heading-name-citation class
Locked Decision #2 already protects, not a link requiring path/anchor
resolution at all. Per the strategy document's own Step 3.5 trigger condition
("any link ... with an anchor"), there is no genuinely broken link in any of
these three files to fix.

Going one level further than a bare pass/fail (since several of these plain-
text citations do explicitly say "for the full rundown"/"the full feature
writeup", the same class of risk Step 3.5's prose flags even without a literal
href): five citations in `TODO.md` (lines ~170, ~231, ~305, ~428, ~564) point
at specific `README.md` "Status" bullets that are now OLDER than the last 5
kept inline (GPU Vertex Skinning, Scene Serialization, PMX/VMD import, and IK
solving entries) — these enitres now live only in `docs/CHANGELOG.md`, not
literally under `README.md`'s own "Status" heading. This is the exact same
"Status-prose, one-hop" class Step 3.6's own table already documents for
`AnimationSystem.cpp` (confirmed below) — the heading itself
(`## Status`) still exists and still links to `docs/CHANGELOG.md`, so the
referenced content is one hop further away, not gone or broken. Per Locked
Decision #5 ("only touch \[`BUILDING.md`/`TESTING.md`/`TODO.md`\] if Phase 4's
link-integrity audit finds an actually-broken link"), and since none of these
five are actual broken links (no href to break — just prose that is now one
hop less direct), **no edit was made to `TODO.md`**; this is recorded here as
a deliberate, documented acceptance, not a silently-skipped check.

Searched `task_manager/COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`,
`GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`,
`HAIR_SKIRT_PHYSICS_DRIVEN_ANIMATION_REQUIREMENTS.md`, and
`RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md` for `README.md`/
`AGENTS.md` mentions: 0, 3, 0, and 1 hits respectively, all plain-text
heading-name citations (`` AGENTS.md's "Profiling" section ``, `` README.md's
"Rendering" section ``, generic `AGENTS.md`/`TODO.md` mention) — zero actual
links, zero broken references, all still valid via Locked Decision #2. Also
confirmed the handful of `\]\(...README.md...\)` / `\]\(...AGENTS.md...\)`
style hits found anywhere under `task_manager/` (a targeted regex search) are
all self-contained inside this campaign's OWN strategy/completion-report
documents (`doc-refactor-1/PHASE2_*.md`, `PHASE3_*.md`, `PHASE4_*.md`),
quoting the exact breadcrumb text those phases instructed to be WRITTEN
elsewhere (i.e. illustrative "here is the text to put in the new file",
never intended to be a live, clickable link from inside
`task_manager/doc-refactor-1/` itself) — not a real broken link in the
repository's actual documentation tree.

### 3.6 — `src/`/`tests/` comment audit (read-only, no edits)

Re-ran `search_in_dir "AGENTS.md"` across `src/` (spot-check, not exhaustive)
and confirmed at least 6 distinct cited headings (`"Networking"`, `"Skeletal
Animation Pose Resolution"`, `"Job System"`, `"CPU Dependency Memory
Tracking"`, `"Profiling"`, `"Testability & Regression Safety"` via `AGENTS.md`
generically) all still literally exist as `## ` headings in the real, final
`AGENTS.md` — exceeding the strategy's own 3-5 spot-check minimum.

Re-ran `search_in_dir "README.md"` across `src/` and `tests/` and confirmed
**exactly 12 hits in 9 files, 0 in `tests/`** — the same count and file list
`PHASE0_MASTER_STRATEGY.md`'s own table already enumerated. Re-classified
every one against the real, final `README.md`/`docs/CHANGELOG.md`:

| File | Cites | Class | Confirmed |
|---|---|---|---|
| `src/Application/Application.cpp` (~617) | generic "AGENTS.md/README.md/TODO.md" | Generic | No specific phrase assumed — always resolves. |
| `src/Assets/AssetDatabase.h` (~81) | "future notes" | Pre-existing stale | Confirmed: this phrase is not, and was never, literal text in `README.md` — unaffected by this campaign. |
| `src/Assets/AssetImporter.h` (~160) | "text file can stay still for now" | Pre-existing stale | Same as above. |
| `src/Assets/AssetTypes.h` (~17, ~23) | "future notes" / "text file can stay still for now" | Pre-existing stale | Same as above (two hits, one file). |
| `src/Assets/GtaFile.h` (~22) | "README.md's original spec for the exact byte layout" | Heading-protected | The 64-byte `*.gta` header description now lives in `docs/architecture/asset-pipeline.md`, confirmed present there (re-read in full this phase) — one hop away via the still-present "Asset Pipeline" heading/link, same protection as every `AGENTS.md` citation. |
| `src/Assets/GtaFile.h` (~77) | "text file can stay still for now" | Pre-existing stale | Same as `AssetImporter.h` above. |
| `src/Editor/EditorCamera.h` (~15) | generic historical mention | Generic/historical | No specific searchable text assumed. |
| `src/Editor/Panels/ProjectPanel.h` (~117) | "text file can stay still for now" | Pre-existing stale | Same as above. |
| `src/Game/Animation/AnimationSystem.cpp` (~497) | **"A spawned MMD model can now actually be ANIMATED"** | **Status-prose, one-hop** | **Re-confirmed by direct `search_in_dir` this phase: the phrase is ABSENT from the real, final `README.md` (0 hits) and PRESENT verbatim in `docs/CHANGELOG.md` line 407** — exactly the accepted, documented one-hop relocation `PHASE0`'s own nuance note predicted. `docs/architecture/asset-pipeline.md` (line 130) already links directly to this exact `CHANGELOG.md` entry too. |
| `src/Game/Animation/AnimationSystem.cpp` (~510) | generic "README.md/TODO.md" | Generic | No specific phrase assumed. |
| `src/Scene/SceneTextFormat.h` (~9) | "text file can stay still for now" | Pre-existing stale | Same as above. |

All ten "Pre-existing stale"/"Generic"/"Generic/historical" rows required no
action, confirmed unaffected either way (as Phase 0 itself already
established) — recorded here again so a future reader does not need to
re-derive this. The one "Status-prose, one-hop" row was independently
re-verified against the REAL post-Phase-2 files (not just re-asserted from
the strategy document's table) — confirmed still holds. No `src/`/`tests/`
file was edited this phase, per the strategy document's explicit instruction.

## Deviations from the strategy document

- Step 3.5's TODO.md finding (five "Status-prose, one-hop" citations) is
  reported as a **documented, deliberate non-fix** rather than an edit,
  since none of the five are an actual Markdown link with a path/anchor that
  could be "broken" in the mechanical sense Locked Decision #5 gates edits
  to `BUILDING.md`/`TESTING.md`/`TODO.md` on — they are prose citing a
  heading name, the exact class Locked Decision #2 already declares
  out-of-scope-to-rewrite. This is consistent with, not a deviation from,
  the strategy's own instruction to "fix any such found; if none exist, note
  that explicitly" — none of the STRICT (link-shaped) cases exist, and the
  broader prose-only cases are explicitly out of scope per Locked Decision
  #5 unless genuinely broken.
- No other deviations. Every deliverable in Step 3.1-3.6 was produced/audited
  exactly as specified.

## Verification performed

- `cmake --build build` — `ninja: no work to do.` (clean no-op rebuild,
  confirming only `.md` files were touched this phase, same as every prior
  phase in this campaign).
- No test filter run performed this phase, per Workflow Rule #1 (no
  `.cpp`/`.h`/`CMakeLists.txt` file touched).
- **159 total Markdown link occurrences** enumerated (30 across root-level
  `.md` files, 129 across `docs/`) and individually resolved against the real
  folder structure; **zero broken links found** (the only prior gap —
  eighteen files' breadcrumb links to `docs/README.md`, a known forward
  reference from Phases 2/3 — was closed by this phase creating that file).
- Both `docs/architecture/ecs.md` <-> `docs/conventions/ecs.md`
  cross-references confirmed resolving in both directions by direct
  `read_file` inspection of both files' real link lines.
- All six `../../AGENTS.md#coding-guidelines`/`../../AGENTS.md#testability--regression-safety`
  anchor links re-checked against the real, final `AGENTS.md` — both target
  headings present, unchanged in text, anchors unaffected by this phase's
  own new `## Documentation` section (which sits above both, and does not
  change either heading's own slug).
- Re-read the final `README.md`/`AGENTS.md` in full — confirmed every
  original `## `/`### ` heading (Locked Decision #2) is still present, in
  the same order, plus the one new `## Documentation` section each, no
  Markdown corruption (balanced code fences/backticks, headings intact).
- `search_in_dir "AGENTS.md"` (`src/`, spot-check) and `search_in_dir
  "README.md"` (`src/` + `tests/`, exhaustive — 12 hits confirmed, matching
  `PHASE0`'s own count) both re-classified against the real, final file
  contents; see the table above for the full per-comment breakdown.
- `search_in_dir` across `task_manager/`'s four named root-level `.md` files
  plus a targeted repo-wide regex for `](...README.md...)`/`](...AGENTS.md...)`
  — no genuinely broken link found outside this campaign's own strategy
  documents (which were confirmed to be illustrative template text, not live
  links).

## Step 5: Deliverables / files touched

- Created: `docs/README.md`,
  `task_manager/doc-refactor-1/PHASE4_COMPLETION_REPORT.md` (this file).
- Modified: `README.md` (248 -> 254 lines, gained a `## Documentation`
  section before `## Goal`), `AGENTS.md` (209 -> 215 lines, gained a
  `## Documentation` section before `## Coding Guidelines`).
- No other file was modified — the link-integrity audit found nothing to
  fix in `docs/architecture/*.md`, `docs/conventions/*.md`,
  `docs/CHANGELOG.md`, `BUILDING.md`, `TESTING.md`, or `TODO.md`.
- No `src/`, `tests/`, or `CMakeLists.txt` file was touched.
- Branch remains `fix/doc-refactor` throughout.

## Handoff note for Phase 5

- Phase 5 is the final phase: one full 3-configuration build + `ctest`
  regression pass (default / `-DGTE_ENABLE_EDITOR=OFF` /
  `-DGTE_ENABLE_PROJECT_PANEL=OFF`), covering BOTH the `network-impl-7`
  feature (Phase 1's still-pending full-regression obligation) AND this
  whole four-phase doc split, a manual live smoke test of `GET /activate_tab`
  / `GET /list_tabs`, and this campaign's own `CAMPAIGN_COMPLETION_REPORT.md`
  plus the required addendum to
  `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md`.
- The full, final `docs/` tree Phase 5 should expect: `docs/README.md`,
  `docs/CHANGELOG.md`, `docs/architecture/` (6 files), `docs/conventions/`
  (12 files) — 20 files total, all link-audited and confirmed clean by this
  phase.
- `README.md` is 254 lines / `AGENTS.md` is 215 lines as of the end of this
  phase — re-verify fresh with `search_in_dir "^## "` before trusting these
  numbers, per Workflow Rule #4/#5, same as every phase before this one.
