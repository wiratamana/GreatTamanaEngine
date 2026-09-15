# PHASE3 — Extract `AGENTS.md` Into `docs/conventions/*.md`

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first, especially Locked
Decisions #2 and #3 (pointer headings; the exact 12-file list under
`docs/conventions/`).
**Also read first:** `PHASE1_COMPLETION_REPORT.md` (records the exact final
position of the new "Networking" bullet Phase 1 added — this shifts every
line number below for everything from `## Networking` onward) and
`PHASE2_COMPLETION_REPORT.md` (a worked example of the exact extraction
mechanics/order to reuse here — `AGENTS.md` is a separate file from
`README.md` so Phase 2's edits don't shift anything in this file, but the
WORKFLOW it used, including its Step 3.1.1 cross-reference audit, is the
template for this phase).

## Step 1: The Goal

Shrink `AGENTS.md` from ~2109 lines down to a short index, by moving 12 of
its 14 sections out into `docs/conventions/*.md`, while keeping every
existing `## `/`### ` heading in place as a short (2-4 line) summary-and-link
per Locked Decision #2 — this is the step that MUST NOT break the ~250
existing `// see AGENTS.md, "<Section Name>"` source comments across `src/`
and `tests/`, because the section they name still genuinely exists.

## Step 2: The Situation / The Problem

`AGENTS.md`'s structure (baseline BEFORE Phase 1's edit; re-derive real
numbers with `search_in_dir "^## " AGENTS.md` and `search_in_dir "^### "
AGENTS.md` before touching anything):

```
0     # AGENTS.md                                          (title, ~3 lines)
4     ## Coding Guidelines                                 (~19 lines - KEEP INLINE, unchanged, Locked Decision #3)
23    ## GPU Resource Memory Tracking                       (~63 lines)  -> docs/conventions/gpu-resource-memory-tracking.md
86    ## CPU Dependency Memory Tracking                     (~73 lines)  -> docs/conventions/cpu-dependency-memory-tracking.md
159   ## Profiling                                           (~284 lines) -> docs/conventions/profiling.md
443   ## Job System                                          (~493 lines) -> docs/conventions/job-system.md
936   ## Networking                                          (~385 lines, includes the nested
        ### Named Texture Capture (`GET /get_texture`)        subsection at ~1148, AND Phase 1's new
                                                               activate_tab/list_tabs bullet at its end)
                                                               -> docs/conventions/networking.md (whole
                                                               section INCLUDING the nested ### subsection)
1321  ## Render Target Format Matching                       (~57 lines)  -> docs/conventions/render-target-format-matching.md
1378  ## Skeletal Animation Pose Resolution                   (~49 lines)  -> docs/conventions/skeletal-animation-pose-resolution.md
1427  ## GPU Vertex Skinning                                  (~69 lines)  -> docs/conventions/gpu-vertex-skinning.md
1496  ## Atmosphere Scattering                                (~250 lines) -> docs/conventions/atmosphere-scattering.md
1746  ## Entity-Component-System (ECS)                        (~153 lines) -> docs/conventions/ecs.md
1899  ## Scene Serialization                                  (~41 lines)  -> docs/conventions/scene-serialization.md
1940  ## Editor Module Structure                              (~122 lines) -> docs/conventions/editor-module-structure.md
2062  ## Testability & Regression Safety                      (~47 lines) - KEEP INLINE, unchanged, Locked Decision #3
```

(Note: the real heading text carries backticks around the endpoint name —
``### Named Texture Capture (`GET /get_texture`)`` — match it exactly if you
ever search for it literally; a plain regex anchor on `^### ` does not care,
but a literal string search does.)

`## Coding Guidelines` and `## Testability & Regression Safety` are the two
sections that stay fully inline — they are short and are, by a wide margin,
the two most frequently cited sections in the whole codebase (the
"Testability & Regression Safety" heading alone is cited in dozens of test
files). The other 12 all move to `docs/conventions/`.

## Step 3: The Plan

### 3.1 — Create the 12 `docs/conventions/*.md` files

For EACH section in the table above (excluding "Coding Guidelines" and
"Testability & Regression Safety"), create the mapped file, containing that
section's heading (demoted from `##` to `#`, becoming the new file's own
top-level title) plus its FULL original body, moved verbatim, INCLUDING any
nested `### ` subheading it contains (only `## Networking` has one today —
``### Named Texture Capture (`GET /get_texture`)`` moves into `docs/conventions/
networking.md` as a `##`-demoted-from-`###`-to-`##` nested heading inside
that same new file, keeping its own internal hierarchy one level shallower
than it was, exactly mirroring how a `###` under a moved `##` naturally
becomes a `##` under a new file's own `#` title).

Each new file opens with the same breadcrumb-link convention Phase 2
established: `_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor
conventions. See [docs/README.md](../README.md) for the full documentation
index._` (verify the relative path actually resolves after writing).

`docs/conventions/ecs.md` MUST end with the same cross-reference Phase 2's
`docs/architecture/ecs.md` points back at (see Phase 2 Step 3.1's own note) —
this phase and Phase 2 reference each other's ECS file; make sure both links
are genuinely correct by opening both files after both phases are done (or,
if Phase 2 already ran, verify its half of the link right now).

This is the single largest phase in the whole campaign by line count moved
(~1826 lines across 12 files) — work through the table top-to-bottom,
committing to memory (or notes) which ranges you've already extracted, and
use `search_in_dir` before EVERY extraction to re-confirm the current exact
heading boundaries in `AGENTS.md` (each edit_line call on `AGENTS.md` itself
in Step 3.2 shifts everything below it, so extract-to-new-files FIRST for
ALL 12 sections while `AGENTS.md` is still untouched, exactly like Phase 2
did — read the whole file into new destination files before editing the
source file at all).

### 3.1.1 — Cross-reference audit (do this BEFORE, or while, writing each new file)

`AGENTS.md` is dense with `"(see X above)"` / `"(see X below)"` style
cross-references — a `search_in_dir "above)"` pass alone turns up ~48 hits,
and `search_in_dir "below)"` turns up ~32 more. Most are harmless
same-section self-references (both the citing and the cited text live
inside the SAME one of the 12 (or 14) sections — these move together with
their own file and need no change). A real subset crosses a SECTION
boundary, and because each of the 12 moved sections becomes a SEPARATE file,
those need to become real Markdown links — including a special case this
phase must not miss: several of them cite `Coding Guidelines` or
`Testability & Regression Safety`, i.e. one of the TWO sections that stay
inline in `AGENTS.md` itself rather than moving to `docs/conventions/` at
all. Verified against the real file as of this writing (RE-VERIFY with your
own `search_in_dir` pass at execution time — this is a starting checklist,
not a guarantee of completeness):

| Approx. baseline line | Found inside | Cites | Fix |
|---|---|---|---|
| ~72, ~88 | CPU Dependency Memory Tracking | "(see above)" -> GPU Resource Memory Tracking | link to `gpu-resource-memory-tracking.md` |
| ~232 | Profiling | "\"CPU Dependency Memory Tracking\" above" | link to `cpu-dependency-memory-tracking.md` |
| ~380 | Profiling | "\"Editor Module Structure\" below" | link to `editor-module-structure.md` |
| ~1129 | Networking | "\"Testability & Regression Safety\" below" | **link back into `AGENTS.md` itself**, e.g. `[Testability & Regression Safety](../../AGENTS.md#testability--regression-safety)` — this section is NOT one of the 12 moved files, it stays inline per Locked Decision #3 |
| ~1277, ~1289-1290, ~1311, ~1317 | Networking's "Named Texture Capture" subsection | "\"Atmosphere Scattering\" above/below" | link to `atmosphere-scattering.md` |
| ~1552 | Atmosphere Scattering | "\"Networking\" above, \"Named Texture Capture\"" | link to `networking.md` (optionally anchor straight to the nested subsection, e.g. `networking.md#named-texture-capture-get-get_texture`) |
| ~1876 | Scene Serialization | "\"Editor Module Structure\" below" | link to `editor-module-structure.md` |
| ~1977 | Editor Module Structure | "\"Testability & Regression Safety\" below" | **link back into `AGENTS.md` itself**, same as the ~1129 case above |

For EACH hit your own search turns up, classify it as one of:

1. **Self-reference** — citing text and cited text both live inside the same
   section. Leave the prose exactly as-is.
2. **Cross-section reference, both sides moving to `docs/conventions/`** —
   rewrite as a real relative Markdown link to the sibling file, e.g.
   `[Atmosphere Scattering](atmosphere-scattering.md)` (same folder, no
   `../` needed).
3. **Reference to `Coding Guidelines` or `Testability & Regression Safety`**
   (the two sections that stay inline in `AGENTS.md`) — rewrite as a link
   BACK into `AGENTS.md` itself using its GitHub-style heading anchor:
   lowercase the heading, replace spaces with hyphens, and drop characters
   that are neither letters/digits/hyphens/underscores (so `&` is simply
   removed, which is why "Testability & Regression Safety" collapses to the
   anchor `testability--regression-safety` — note the DOUBLE hyphen where
   the removed `&` used to be surrounded by two spaces). A mistyped anchor
   fails SILENTLY (the link resolves to the top of the file, no 404), so
   double-check it carefully rather than assuming your first guess is right.

Do this same audit again for the two nested-file cross-links Step 3.1
already calls out by name (the `docs/conventions/ecs.md` <-> `docs/
architecture/ecs.md` pair, and `docs/conventions/networking.md`'s own
internal link to its "Named Texture Capture" sub-heading) — those are
already covered explicitly elsewhere in this document, they are called out
again here only so you do the audit as ONE pass over the whole file instead
of missing pieces between two different instructions.

### 3.2 — Rewrite `AGENTS.md` itself (thin, pointer headings)

Only after all 12 new files exist and have been spot-checked for content
completeness, edit `AGENTS.md` itself, working from the BOTTOM of the file
upward (same reasoning as Phase 2 Step 3.5) so each edit doesn't invalidate
line numbers for content still above it that you haven't processed yet:

For each of the 12 sections, replace its full body with a 2-4 line summary
(written by reading that section's own opening sentence(s) — they are
usually already a serviceable summary) ending in a link, e.g.:

```markdown
## Job System

A general-purpose worker-thread pool (`gte::Jobs::JobSystem`) used to
parallelize per-frame CPU work (skinning, physics) across available cores,
with a strict main-thread-only rule for anything touching Vulkan/ImGui.

Full convention: [docs/conventions/job-system.md](docs/conventions/job-system.md).
```

`## Networking`'s shrunk summary must still mention, in its 2-4 lines, that
`GET /activate_tab`/`GET /list_tabs` exist (since that's the newest addition
per Phase 1) — do not let the summary go stale relative to the full file it
now links to.

`## Coding Guidelines` and `## Testability & Regression Safety` are left
completely untouched, in full, exactly where they already are (Locked
Decision #3) — do not shrink these two.

### 3.3 — Ordering sanity check

After the rewrite, `AGENTS.md`'s heading order must be UNCHANGED from before
this phase (Coding Guidelines, GPU Resource Memory Tracking, CPU Dependency
Memory Tracking, Profiling, Job System, Networking, Render Target Format
Matching, Skeletal Animation Pose Resolution, GPU Vertex Skinning, Atmosphere
Scattering, Entity-Component-System (ECS), Scene Serialization, Editor
Module Structure, Testability & Regression Safety) — only each section's
BODY shrank, never the sequence of headings. This matters because some
existing source comments say things like "see AGENTS.md's Job System Phase 4
audit table" expecting Job System to still exist as a real heading in a
specific relative position readers may have memorized; preserving order is a
cheap way to reduce surprise even though it's not strictly required for the
links themselves to work.

## Step 4: Verification for this phase

- `cmake --build build` — fast compile check (only `.md` files touched;
  confirm the no-op clean rebuild).
- Manually open (`read_file`) the new `AGENTS.md` and confirm all 14 original
  headings are still present in the same order, and spot-check 2-3 of the 12
  new `docs/conventions/*.md` files for content completeness (compare a
  memorable, still-actually-present sentence from partway through the
  original section against the new file — e.g., if Phase 1 landed its new
  "Networking" bullet with a distinctive phrase describing the
  `parsed.notFound`-before-`uiCommandBridge`-null-check ordering rule,
  confirm THAT phrase, whatever its exact final wording turned out to be,
  is present verbatim in `docs/conventions/networking.md` and NOT still
  sitting in `AGENTS.md`'s shrunk version — don't assume any one exact
  phrase quoted in a strategy document survived verbatim, re-find it in the
  real file first).
- Grep the newly-thinned `AGENTS.md` itself for any now-dangling internal
  cross-reference that used to point at a subsection that has now moved
  (e.g. if the old "Profiling" section said "see the Job System section
  below" and "Job System" is now several lines away but still a real heading
  in the same file, that link/mention is still fine per Locked Decision #2 —
  only flag it if the reference assumed FULL content was inline right below
  it that is no longer there).
- `search_in_dir "above)"` and `search_in_dir "below)"` across the new
  `AGENTS.md` and all 12 new `docs/conventions/*.md` files, cross-checked
  against your own Step 3.1.1 audit results — confirm nothing is left citing
  a now-relocated section by stale positional language ("above"/"below")
  instead of a real link, and specifically confirm every reference to
  `Coding Guidelines`/`Testability & Regression Safety` found inside one of
  the 12 new files is a real link back into `AGENTS.md`, not a dangling
  "below" that no longer has a "below" in that new file.

## Step 5: Deliverables / Files Touched

- Modified: `AGENTS.md` (dramatically shorter, same heading order, pointer
  summaries).
- Created: all 12 files under `docs/conventions/` listed in Step 3.1,
  `task_manager/doc-refactor-1/PHASE3_COMPLETION_REPORT.md`.
- `README.md` and `docs/architecture/*.md`/`docs/CHANGELOG.md` are NOT
  touched in this phase (that was Phase 2).

## Step 6: Handoff Note for Phase 4

Phase 4 still needs to: create `docs/README.md` (the landing index covering
BOTH Phase 2's `docs/architecture/`+`docs/CHANGELOG.md` AND this phase's
`docs/conventions/`), add a short "Documentation" pointer near the top of
both `README.md` and `AGENTS.md`, and run the full link-integrity audit
across every `.md` file this whole campaign touched or created (this phase's
12 new files' breadcrumb links, and every Step 3.1.1 cross-reference link —
including the ones pointing back into `AGENTS.md`'s own
`#coding-guidelines`/`#testability--regression-safety` anchors — included).
