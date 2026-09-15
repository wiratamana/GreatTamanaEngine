# PHASE2 — Extract `README.md` Into `docs/architecture/*.md` + `docs/CHANGELOG.md`

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first, especially "Locked
Decisions" #2, #3, #4, #5.
**Also read first:** `PHASE1_COMPLETION_REPORT.md` (this campaign's own, in
this same `task_manager/doc-refactor-1/` folder) — it records the EXACT final
line position of the new "Status" bullet Phase 1 added, which shifts every
line-number baseline quoted below.

## Step 1: The Goal

Shrink `README.md` from ~1972 lines down to a short, GitHub-style root
README, by moving its `## Architecture` subsections and its entire `##
Status` history out into new files under `docs/`, while leaving every
existing `##`/`###` heading (Locked Decision #2) in place as a short
summary-and-link.

## Step 2: The Situation / The Problem

`README.md`'s structure (baseline BEFORE Phase 1's edit; re-derive real
numbers with `search_in_dir "^## " README.md` and `search_in_dir "^### "
README.md` before touching anything):

```
0     # GreatTamanaEngine  (title + one-line tagline)
4     ## Goal                                  (short, ~4 lines - KEEP INLINE, unchanged)
9     ## Architecture                          (intro prose, ~19 lines)
29      ### Event handling                     (~24 lines)
54      ### Math                                (~8 lines)
63      ### Rendering                           (~86 lines)
150     ### Entity-Component-System (ECS)       (~80 lines)
231     ### Asset Pipeline                      (~205 lines)
437     ### Editor / Debug UI                   (~393 lines)
831   ## Building                               (already just links to BUILDING.md - KEEP UNCHANGED)
835   ## Testing                                (already just links to TESTING.md - KEEP UNCHANGED)
839   ## Status                                 (~1128 lines, reverse-chronological, one bullet
                                                  per past feature campaign, NOW includes Phase 1's
                                                  new activate_tab/list_tabs bullet at the very end)
1967  ## Roadmap                                (already just links to TODO.md - KEEP UNCHANGED)
```

`## Goal`, `## Building`, `## Testing`, and `## Roadmap` are ALREADY thin
(each is a handful of lines, several already pure links). Only `##
Architecture` (with its six subsections) and `## Status` are the actual bulk
of this file's 133 KB, and are this phase's real target.

## Step 3: The Plan

### 3.1 — Create `docs/architecture/*.md` (six new files)

For EACH of the six `### ` subsections under `## Architecture` listed above,
create the corresponding file below, containing that subsection's heading
(demoted from `###` to `#`, i.e. it becomes that new file's own top-level
title) plus its full original body text, moved verbatim (byte-for-byte prose,
only the heading level and any now-relative links need adjusting):

| Source subsection | New file |
|---|---|
| `### Event handling` | `docs/architecture/event-handling.md` |
| `### Math` | `docs/architecture/math.md` |
| `### Rendering` | `docs/architecture/rendering.md` |
| `### Entity-Component-System (ECS)` | `docs/architecture/ecs.md` |
| `### Asset Pipeline` | `docs/architecture/asset-pipeline.md` |
| `### Editor / Debug UI` | `docs/architecture/editor-debug-ui.md` |

Each new file should open with a one-line breadcrumb comment/link back, e.g.:
`_Part of [GreatTamanaEngine](../../README.md)'s architecture docs. See
[docs/README.md](../README.md) for the full documentation index._` — adjust
the exact relative path depth to whatever is actually correct once the file
is written (double-check by opening the file after creation and confirming
the link resolves).

`docs/architecture/ecs.md` MUST end with a cross-reference note pointing at
`docs/conventions/ecs.md` (Phase 3's future file — it is fine to reference a
file that doesn't exist until Phase 3 runs; Phase 4's link-integrity audit
will catch it if Phase 3 ever fails to create it), explaining the split:
"this file describes the ENGINE-side ECS architecture/layering; see
`../conventions/ecs.md` for the AGENTS.md-side coding CONVENTIONS for writing
new components/systems."

Any internal cross-reference already inside the moved prose (e.g. a mention
of "see the Editor / Debug UI section below" inside the ECS subsection, if
one exists — check the real text) must be rewritten into a real Markdown
link to the correct new `docs/architecture/*.md` file, not left as a
dangling "see below" that no longer has a "below" in the same file. **This is
NOT a rare edge case — see Step 3.1.1 immediately below, which is a verified
starting checklist, not a hypothetical.**

### 3.1.1 — Cross-reference audit (do this BEFORE writing any new file)

A `search_in_dir` pass for the literal substrings `above)` and `below)`
across the CURRENT `## Architecture` body turns up roughly two dozen hits.
Most are harmless same-subsection self-references (the citing text and the
cited text both live inside the SAME one of the six subsections — these move
together with their own file and need no change at all). A real handful,
however, cross a subsection boundary, and therefore need a genuine Markdown
link once the six subsections become six separate files. Verified against
the real file as of this writing (RE-VERIFY with your own fresh
`search_in_dir "above)"` / `search_in_dir "below)"` pass at execution time —
line numbers WILL have shifted, and this list is a verified starting point,
not a guarantee of completeness):

| Approx. baseline line | Found inside | Cites | Fix |
|---|---|---|---|
| ~58 | Math | "the hand-rolled ECS below" | link to `ecs.md` |
| ~73 | Rendering | "(see \"Editor / Debug UI\" below)" | link to `editor-debug-ui.md` |
| ~101 | Rendering | "\"Entity-Component-System\" below" | link to `ecs.md` |
| ~124, ~130 | Rendering | "\"Asset Pipeline\" below" | link to `asset-pipeline.md` |
| ~225 | Entity-Component-System (ECS) | "see \"Status\" below" | link to **`../CHANGELOG.md`** — Status is NOT another architecture file, it is this phase's OTHER deliverable, one directory level up from `docs/architecture/` |
| ~268, ~305 | Asset Pipeline | "\"Editor / Debug UI\" below" | link to `editor-debug-ui.md` |
| ~358 | Asset Pipeline | "\"Status\" below" | link to `../CHANGELOG.md` |
| ~365 | Asset Pipeline | "\"Rendering\" above" and "\"Editor / Debug UI\" below" | link to `rendering.md` / `editor-debug-ui.md` |
| ~372 | Asset Pipeline | "\"Entity-Component-System\" below" | link to `ecs.md` |
| ~900 | Editor / Debug UI | "\"Editor / Debug UI\" above" | self-reference (same new file) — no change needed |

For EACH hit your own search turns up, classify it as one of:

1. **Self-reference** — citing text and cited text both live inside the same
   one of the six subsections. Leave the prose exactly as-is.
2. **Cross-subsection reference** — cites one of the OTHER five `###`
   subsections. Rewrite as a real relative Markdown link to that
   subsection's new sibling file, e.g. `[Asset Pipeline](asset-pipeline.md)`
   (same folder as the citing file, so no `../` is needed).
3. **Reference to `## Status`** — rewrite as a link to `../CHANGELOG.md`
   (one level up from `docs/architecture/`, since `CHANGELOG.md` lives
   directly under `docs/`, NOT under `docs/architecture/`). Do not confuse
   this with case 2 — a naive `status.md` sibling link would be wrong, there
   is no such file.

Repeat this same audit, independently, when you reach Step 3.4's `README.md`
Status-body extraction, in case the Status body itself cites back into an
Architecture subsection. Because Locked Decision #2 keeps the `##
Architecture` heading (and its six `###` children) alive in the thinned
`README.md`, a plain-text mention like "see Architecture's Rendering section
above" still technically resolves for a human reader even without a link —
only convert it to a real link, or otherwise fix it, if it assumed FULL
inline content was sitting right there that has now moved out.

### 3.2 — Create `docs/CHANGELOG.md`

Move the ENTIRE `## Status` section's body (every bullet, in the exact same
reverse-chronological order, including Phase 1's newly-added final bullet)
into `docs/CHANGELOG.md`, with a new top-level title, e.g.:

```markdown
# Changelog

Reverse-chronological history of every feature campaign landed in this
engine. For the short, current-state summary, see the main
[README.md](../README.md)'s own "Status" section.

## <first moved bullet's own natural grouping, or just continue as one long list>
```

Keep the exact same bullet-list formatting/voice — this is a pure content
move, not a rewrite. Do not summarize, shorten, or reorder any existing
entry.

### 3.3 — Rewrite `README.md`'s `## Architecture` section (thin)

Replace the current ~822-line `## Architecture` section (the intro prose plus
all six `###` subsections) with:
- The intro's essential 3-4 lines only (the ASCII layering diagram
  `SDL -> Application -> Window and Renderer -> Game` and the one-paragraph
  explanation of what each layer is allowed to know about — this is short and
  valuable enough to keep inline verbatim).
- Six short `### ` subheadings, ONE PER original subsection (Locked Decision
  #2 — do not delete these headings), each shrunk to exactly 1-2 sentences
  summarizing what that subsystem is, followed by a link, e.g.:
  ```markdown
  ### Rendering

  A real Vulkan pipeline (instance/device/swapchain, dynamic rendering, one
  or more `RenderTexture` targets for Editor panels) sits behind the
  `Renderer` abstraction `Game` talks to.

  Full detail: [docs/architecture/rendering.md](docs/architecture/rendering.md).
  ```
  Write each of these six summaries by actually reading that subsection's own
  opening 1-2 sentences (they are usually already a good summary of the whole
  subsection) rather than inventing new wording from scratch.

### 3.4 — Rewrite `README.md`'s `## Status` section (thin, Locked Decision #4)

Replace the ~1128-line body with:
- The exact last 3-5 bullets, verbatim, in their original order (use
  `read_line`/`search_in_dir "^- \*\*" README.md` to find the last 5 top-level
  bullet start lines before your edit, then confirm with a human-style
  sanity read that they are genuinely the 5 most recent, not accidentally
  including a nested sub-bullet).
- Immediately after, one line: `See **[docs/CHANGELOG.md](docs/CHANGELOG.md)**
  for the full project history.`
- Re-run the Step 3.1.1 cross-reference audit against the bullets you are
  about to move: if any of the last 3-5 bullets you're KEEPING inline cites
  an Architecture subsection ("...see 'Rendering' above..."), that mention
  now needs to point at `docs/architecture/rendering.md` instead, since the
  full "Rendering" body no longer lives in this same file.

### 3.5 — Mechanical execution order (to avoid line-number drift mistakes)

Do the extraction in this exact order, re-reading line numbers with
`search_in_dir`/`read_line` immediately before EACH step (never reuse a line
number computed before a prior step's edit):

1. Read and copy out the full `## Status` body first (bottom of the file) —
   write it to `docs/CHANGELOG.md` as a brand new file (no existing file, no
   line-shifting risk).
2. Read and copy out each of the six `### ` Architecture subsections — write
   each to its own new `docs/architecture/*.md` file (again, brand new files,
   no risk). Apply the Step 3.1.1 cross-reference fixes AS you write each new
   file, not as a separate pass afterward — it is much easier to fix a link
   while you're already looking at the exact sentence than to re-find it
   later.
3. ONLY NOW start editing `README.md` itself, working from the BOTTOM of the
   file upward (replace `## Status`'s body first, since editing lines near
   the end of the file does not shift line numbers for content above it),
   THEN replace `## Architecture`'s body (which is further up the file).
   Editing bottom-to-top avoids having to recompute every subsequent line
   number after each `edit_line` call. `## Roadmap` (after `## Status`) is
   completely unaffected by anything above it and can be left alone.
4. Use `write_file` (not `edit_line`) for the final full-file rewrite of
   `README.md` if that ends up simpler than several chained `edit_line`
   calls — either approach is fine; use whichever leaves the smaller chance
   of an off-by-one line error given the exact tool call sequence you choose.

## Step 4: Verification for this phase

- `cmake --build build` — fast compile check (this phase touches only `.md`
  files, so this must remain a clean no-op rebuild; run it anyway as a
  regression-safety sanity check).
- No test filter run is needed this phase (no `.cpp`/`.h`/`CMakeLists.txt`
  file is touched).
- Manually open (via `read_file`) the new `README.md`, each new
  `docs/architecture/*.md` file, and `docs/CHANGELOG.md`, and confirm:
  - Every one of the six original `### ` Architecture subheadings still
    exists somewhere (either in the thinned `README.md` or in its own new
    file) — nothing was silently dropped.
  - `README.md`'s file size dropped dramatically (spot-check via
    `browse_dir details:true` on the project root, comparing the new
    `README.md` size against its pre-Phase-2 133 KB).
  - No Markdown syntax got corrupted (headings still start with `#`/`##`/
    `###` followed by a space, code fences still balanced, etc.).
  - `search_in_dir "above)"` and `search_in_dir "below)"` across the new
    `README.md` and all six new `docs/architecture/*.md` files turn up
    NOTHING that is still a stale, unconverted cross-subsection reference
    (compare against your own Step 3.1.1 audit results) — a leftover
    "see X below" whose "X" is no longer anywhere in the same file is a bug
    in this phase's own work, not something to leave for Phase 4.

## Step 5: Deliverables / Files Touched

- Modified: `README.md` (dramatically shorter).
- Created: `docs/CHANGELOG.md`, `docs/architecture/event-handling.md`,
  `docs/architecture/math.md`, `docs/architecture/rendering.md`,
  `docs/architecture/ecs.md`, `docs/architecture/asset-pipeline.md`,
  `docs/architecture/editor-debug-ui.md`,
  `task_manager/doc-refactor-1/PHASE2_COMPLETION_REPORT.md`.
- `AGENTS.md` is NOT touched in this phase (that is Phase 3).

## Step 6: Handoff Note for Phase 3 and Phase 4

- Phase 3 does the exact same kind of extraction against `AGENTS.md` — it can
  reuse this phase's own completion report as a worked example of the
  bottom-to-top editing order, the breadcrumb-link convention, and the
  Step 3.1.1-style cross-reference audit (Phase 3's own document has its own
  version of that audit, tailored to `AGENTS.md`'s 12 sections, including the
  extra wrinkle that some of THOSE cross-references point at `AGENTS.md`
  sections that stay inline rather than moving to a sibling file).
- Phase 4's link-integrity audit must specifically check: every link this
  phase wrote (`README.md` -> `docs/architecture/*.md`, `README.md` ->
  `docs/CHANGELOG.md`, each `docs/architecture/*.md`'s own breadcrumb link
  back to `README.md`/`docs/README.md`, and every cross-subsection link
  fixed per Step 3.1.1) actually resolves once `docs/README.md` exists (it
  does not exist yet as of this phase — that is expected, Phase 4 creates
  it).
