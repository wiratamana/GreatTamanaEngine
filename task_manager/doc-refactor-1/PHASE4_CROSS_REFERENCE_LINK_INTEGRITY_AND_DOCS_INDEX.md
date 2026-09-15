# PHASE4 — `docs/README.md` Index + Cross-Reference Link-Integrity Audit

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first.
**Also read first:** `PHASE2_COMPLETION_REPORT.md` and
`PHASE3_COMPLETION_REPORT.md` (this campaign's own, in
`task_manager/doc-refactor-1/`) — they list the exact final file names/paths
created, which this phase indexes and audits.

## Step 1: The Goal

Make the new `docs/` tree actually navigable (a proper landing index, the way
a well-organized open-source project's `docs/` folder reads), point both
`README.md` and `AGENTS.md` at it near their very top, and prove — by
actually checking every link, not just assuming Locked Decision #2 made
everything automatically safe — that nothing in this repository's own `.md`
files (excluding `third_party/`) got left dangling by Phases 1-3.

## Step 2: The Situation / The Problem

- After Phases 2-3, `docs/` contains: `CHANGELOG.md`, `architecture/` (6
  files), `conventions/` (12 files) — 19 files total, but no single page
  lists them all together or explains the two-way split
  (`architecture/` = README's old content, `conventions/` = AGENTS.md's old
  content) to a newcomer.
- Locked Decision #2 (keep pointer headings) means most existing source-code
  comments (`// see AGENTS.md, "Networking"`) remain textually valid, but
  this was never mechanically verified against the ACTUAL new file contents
  — only asserted as a design intent. This phase is where that assertion
  gets checked for real.
- Markdown link syntax (`[text](path)`) is easy to get subtly wrong when
  moving content between files at different folder depths (`docs/
  architecture/rendering.md` linking back to the root `README.md` needs
  `../../README.md`, not `../README.md` — an easy off-by-one relative-path
  mistake). Every link Phases 2-3 wrote needs a real check, not a visual
  skim.

## Step 3: The Plan

### 3.1 — Write `docs/README.md` (the landing index)

Create `docs/README.md` with:
- A one-paragraph intro: what this folder is, and the two-way split
  (`architecture/` mirrors the root `README.md`'s old "Architecture"
  section; `conventions/` mirrors the root `AGENTS.md`'s subsystem sections).
- A `## Architecture` list with one bullet per file in `docs/architecture/`,
  each bullet = the file's title + one-sentence description + relative link
  (e.g. `- **[Rendering](architecture/rendering.md)** — the Vulkan pipeline
  behind the \`Renderer\` abstraction.`).
- A `## Conventions` list with one bullet per file in `docs/conventions/`,
  same shape.
- A `## Changelog` line linking to `CHANGELOG.md`.
- A `## Other Project Documentation` list linking to the three top-level files
  this campaign deliberately leaves untouched (Locked Decision #5) but which
  are just as much a part of "the project's documentation" as anything under
  `docs/` — **[BUILDING.md](../BUILDING.md)** (prerequisites/build
  instructions), **[TESTING.md](../TESTING.md)** (how to build/run the test
  suite), and **[TODO.md](../TODO.md)** (known limitations/roadmap). This
  makes `docs/README.md` a genuinely complete one-stop index (per this
  campaign's own Definition of Done) rather than one that only covers the
  two folders THIS campaign happened to create.
- A closing line linking back to both root files: `See the root
  [README.md](../README.md) and [AGENTS.md](../AGENTS.md) for the short
  overview each of these was extracted from.`

### 3.2 — Add a "Documentation" pointer near the top of `README.md`

Immediately after the title/tagline (before `## Goal`), add a short new
section:

```markdown
## Documentation

This README is intentionally thin. Full architecture detail, the complete
project changelog, and contributor conventions all live under
[`docs/`](docs/README.md) — start at **[docs/README.md](docs/README.md)**.
```

### 3.3 — Add a "Documentation" pointer near the top of `AGENTS.md`

Same idea, immediately after the title (before `## Coding Guidelines`):

```markdown
## Documentation

This file covers universal coding guidelines and testability rules, plus a
short summary + link for every subsystem-specific convention. Full detail
for each subsystem lives under [`docs/conventions/`](docs/README.md).
```

### 3.4 — Link-integrity audit (the actual verification, not just a skim)

For every `.md` file created or modified across Phases 1-4 (`README.md`,
`AGENTS.md`, `docs/README.md`, `docs/CHANGELOG.md`, all 6
`docs/architecture/*.md` files, all 12 `docs/conventions/*.md` files):

1. `search_in_dir` (regex `\]\([^)]+\)` or similar) across `README.md`,
   `AGENTS.md`, and the whole `docs/` folder to enumerate every Markdown link
   target in one pass.
2. For each relative link found, resolve it manually against the actual
   folder structure (`docs/architecture/rendering.md`'s `../../README.md`
   really does mean "go up two levels from `docs/architecture/` to the
   project root, then `README.md`" — confirm the target file genuinely
   exists at that resolved path via `browse_dir`/`read_file`).
3. Fix any link found broken (wrong relative depth, typo'd filename, wrong
   case) directly in the file where it lives.
4. Cross-check the two intentional cross-references between Phase 2's
   `docs/architecture/ecs.md` and Phase 3's `docs/conventions/ecs.md` (each
   phase document asked the other's file to link to it) — confirm BOTH
   directions actually resolve, not just one.

### 3.5 — Sweep for stale content assumptions (not just broken syntax)

Search `BUILDING.md`, `TESTING.md`, and `TODO.md` (the three top-level files
this campaign deliberately leaves untouched, per `PHASE0`'s Locked Decision
#5) for any link that points INTO `README.md` or `AGENTS.md` with an anchor
that assumed a specific heading's FULL content would be found there (e.g. an
anchor link like `README.md#status` expecting the entire chronological
history right there — the heading still exists per Locked Decision #2, so
the anchor itself still resolves, but if any of these three files' own prose
explicitly promises "the full history is below this link", that sentence is
now slightly misleading and should be updated to mention `docs/CHANGELOG.md`
instead). Fix any such found; if none exist, note that explicitly in this
phase's completion report rather than silently skipping the check.

Also search the whole `task_manager/` folder's own root-level `.md` files
(`COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`,
`GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`,
`HAIR_SKIRT_PHYSICS_DRIVEN_ANIMATION_REQUIREMENTS.md`,
`RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`) and every other
campaign subfolder for any hardcoded link INTO `README.md`/`AGENTS.md` by
line anchor or exact heading text that might now read oddly (links by
heading name still resolve per Locked Decision #2; only fix genuinely broken
ones, do not rewrite files outside this campaign's scope for cosmetic
reasons alone).

### 3.6 — Do NOT touch `src/`/`tests/` comments — but DO audit all ~12 `README.md`-citing ones individually (per `PHASE0`'s own nuance note)

Per `PHASE0`'s Locked Decision #2, the ~250 existing `// see AGENTS.md, "..."`
/`// see README.md: "..."` source comments are explicitly OUT of scope for
this whole campaign and must not be edited in this phase either — the
pointer-heading design is exactly what keeps them valid without touching
them. Confirm this remains true for the ~250 `AGENTS.md`-citing comments by
spot-checking 3-5 of them against the real, now-thinned `AGENTS.md` (e.g.
confirm `AGENTS.md`'s `## Networking` heading — cited by
`Application/Application.h` — still literally exists at a `## Networking`
heading, even though its body is now short).

The ~12 `README.md`-citing comments need MORE than a 3-5 spot-check, per
`PHASE0_MASTER_STRATEGY.md`'s own "Important nuance for Phase 4's own audit"
note (Step 2) — check EVERY ONE of them (re-`search_in_dir "README.md"`
across `src/`/`tests/` to re-confirm the current list, then classify each
hit). As verified during this campaign's own review pass, the ~12 break down
into three classes — confirm each still holds after Phases 1-3 actually ran,
and record the final classification of every one in this phase's own
completion report rather than only reporting a pass/fail count:

| File | Cites | Class | Why it's fine |
|---|---|---|---|
| `src/Application/Application.cpp` (~line 617) | generic "AGENTS.md/README.md/TODO.md" | Generic | No specific heading/phrase assumed — always resolves. |
| `src/Assets/AssetDatabase.h` (~line 81) | "future notes" | Pre-existing stale | This exact phrase does not appear verbatim in `README.md` even BEFORE this campaign — unaffected either way, not this campaign's doing. |
| `src/Assets/AssetImporter.h` (~line 160) | "text file can stay still for now" | Pre-existing stale | Same as above. |
| `src/Assets/AssetTypes.h` (~lines 17, 23) | "future notes" / "text file can stay still for now" | Pre-existing stale | Same as above (two hits, same file). |
| `src/Assets/GtaFile.h` (~line 22) | "README.md's original spec for the exact byte layout" | Heading-protected | Refers to the 64-byte `*.gta` header description in the (still-headed) "Asset Pipeline" section — moves to `docs/architecture/asset-pipeline.md`, one hop away via the kept heading/link, same protection as every `AGENTS.md` citation. |
| `src/Assets/GtaFile.h` (~line 77) | "text file can stay still for now" | Pre-existing stale | Same as `AssetImporter.h` above. |
| `src/Editor/EditorCamera.h` (~line 15) | generic historical mention, no exact phrase | Generic/historical | No specific searchable text assumed. |
| `src/Editor/Panels/ProjectPanel.h` (~line 117) | "text file can stay still for now" | Pre-existing stale | Same as above. |
| `src/Game/Animation/AnimationSystem.cpp` (~line 497) | **"A spawned MMD model can now actually be ANIMATED"** | **Status-prose, one-hop** | **This IS real, current `README.md` prose (in `## Status`) that this campaign relocates.** Unless it happens to still be among the last 3-5 kept bullets, it is fully preserved, verbatim, in `docs/CHANGELOG.md` — reachable via the (still-present) `## Status` heading's own "see `docs/CHANGELOG.md`" link, one hop further than before, never deleted. |
| `src/Game/Animation/AnimationSystem.cpp` (~line 510) | generic "README.md/TODO.md" | Generic | No specific phrase assumed. |
| `src/Scene/SceneTextFormat.h` (~line 9) | "text file can stay still for now" | Pre-existing stale | Same as above. |

For every "Pre-existing stale" row, no action is needed — these already didn't
resolve to a literal substring in `README.md` before this campaign touched
anything, so this campaign neither breaks nor fixes them; just note this in
the completion report so a future reader doesn't rediscover the same
non-issue. For the one "Status-prose, one-hop" row, confirm the reasoning
column above still holds against the REAL, post-Phase-2 `README.md`/
`docs/CHANGELOG.md` (i.e. actually open both files and confirm the quoted
phrase is genuinely still present, verbatim, in one of them) — this is the
one entry in this table that is a genuine, campaign-caused content move, and
it must be a documented, deliberate acceptance, not a silent gap.

## Step 4: Verification for this phase

- `cmake --build build` — fast compile check (only `.md` files touched;
  confirm no-op clean rebuild).
- Every link enumerated in Step 3.4 resolves; list, in this phase's
  completion report, exactly how many links were checked and how many (if
  any) were found broken and fixed.
- The 3-5 spot-checked `AGENTS.md`-citing comments, AND all ~12 individually
  re-classified `README.md`-citing comments (Step 3.6's table), are confirmed
  still valid/accounted-for against the real, post-Phase-2/3 file contents —
  include the final per-comment classification table in this phase's own
  completion report, not just a pass/fail summary.

## Step 5: Deliverables / Files Touched

- Created: `docs/README.md`,
  `task_manager/doc-refactor-1/PHASE4_COMPLETION_REPORT.md`.
- Modified: `README.md` and `AGENTS.md` (each gains a short "Documentation"
  section near the top), plus any file found to have a genuinely broken link
  during the Step 3.4/3.5 audit (list every such fix explicitly in the
  completion report, by file name and what changed).

## Step 6: Handoff Note for Phase 5

Phase 5 is the final phase: the one full 3-configuration build + regression
pass (covering both this whole doc split AND the still-pending
`network-impl-7` full-regression obligation from Phase 1), the manual live
`GET /activate_tab`/`GET /list_tabs` smoke test, and this campaign's own
`CAMPAIGN_COMPLETION_REPORT.md`.
