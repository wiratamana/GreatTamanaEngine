# PHASE3 Completion Report — AGENTS.md Conventions Extraction

**Parent:** `PHASE0_MASTER_STRATEGY.md`. Executed as specified in
`PHASE3_AGENTS_CONVENTIONS_EXTRACTION.md` (v2, already amended by
`PHASE2_PHASE3_PRECHECK_REPORT.md` with the Step 3.1.1 cross-reference audit
and the corrected `` ### Named Texture Capture (`GET /get_texture`) ``
heading text), having first read `PHASE0_MASTER_STRATEGY.md`,
`PHASE1_COMPLETION_REPORT.md`, `PHASE2_COMPLETION_REPORT.md`, and
`PHASE2_PHASE3_PRECHECK_REPORT.md` in this same `task_manager/doc-refactor-1/`
folder, plus `AGENTS.md` itself (re-deriving every line number fresh via
`search_in_dir`/`read_line` rather than trusting the strategy document's
baseline numbers, per Workflow Rule #4/#5).

## What was done

### Baseline re-verification

Confirmed the REAL, current (post-Phase-1, Phase-2-untouched) `AGENTS.md`
structure before touching anything, via `search_in_dir "^## "`/`"^### "`:
2146 lines total, all 14 `##` headings and the one nested
`` ### Named Texture Capture (`GET /get_texture`) `` sitting at EXACTLY the
line numbers `PHASE3_AGENTS_CONVENTIONS_EXTRACTION.md` (v2) already quoted
(`## Networking` at 936, the nested heading at 1185 — the v2 strategy
document's own baseline table already used these post-Phase-1 numbers, not
the pre-Phase-1 ones still shown in `PHASE0`'s own table). This matches
`PHASE2_COMPLETION_REPORT.md`'s own handoff note (`AGENTS.md` untouched by
Phase 2, still exactly what Phase 1 left it at).

### 3.1 — `docs/conventions/*.md` (twelve new files)

Created all twelve files exactly as mapped in the strategy document, each
opening with the same breadcrumb-link convention Phase 2 established
(`_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions.
See [docs/README.md](../README.md) for the full documentation index._` —
the `docs/README.md` link is a forward reference to Phase 4's own
not-yet-created file, per the strategy's explicit allowance), containing
that section's full original body verbatim (re-verified by direct
`read_line` against the real file for every one of the twelve sections
before writing, never trusting the earlier full-file dump from memory):
`gpu-resource-memory-tracking.md`, `cpu-dependency-memory-tracking.md`,
`profiling.md`, `job-system.md`, `networking.md` (including the nested
`` ### Named Texture Capture (`GET /get_texture`) `` subsection, demoted to
a `##` heading inside the new file, confirmed via `search_in_dir`),
`render-target-format-matching.md`, `skeletal-animation-pose-resolution.md`,
`gpu-vertex-skinning.md`, `atmosphere-scattering.md`, `ecs.md`,
`scene-serialization.md`, `editor-module-structure.md`.

`docs/conventions/ecs.md` ends with a "Relationship to the engine's ECS
architecture overview" section cross-linking to
`docs/architecture/ecs.md` — confirmed bidirectional: Phase 2's
`docs/architecture/ecs.md` already links forward to
`../conventions/ecs.md` (verified by direct read), and this phase's
`docs/conventions/ecs.md` links back via `../architecture/ecs.md`.

### Cross-reference audit (Step 3.1.1) — done as one pass over the whole file

Ran a fresh, real audit rather than trusting the strategy document's
pre-computed table verbatim (the table was a good starting checklist, but
several of its baseline line numbers/classifications needed re-deriving
against the real file): `search_in_dir "above)"` (20 hits) and
`search_in_dir "below)"` (23 hits) against the real, current `AGENTS.md`,
plus a second pass searching each of the 12 (14, including "Coding
Guidelines"/"Testability & Regression Safety") section heading names in
quotes to catch cross-references that don't use the literal
`"above)"`/`"below)"` pattern (e.g. `"Editor Module Structure")` with no
`above`/`below` at all, `` "GPU Vertex Skinning"\nabove.** `` split across a
line wrap, `"AGENTS.md`'s own \"Skeletal Animation Pose Resolution\"
section"`). Every genuine cross-SECTION reference found was classified and
fixed while writing the corresponding new file:

| Citing file | Cites | Fix applied |
|---|---|---|
| `cpu-dependency-memory-tracking.md` | GPU Resource Memory Tracking (above) | link to `gpu-resource-memory-tracking.md` |
| `profiling.md` | GPU Resource Memory Tracking (above) | link to `gpu-resource-memory-tracking.md` |
| `profiling.md` | CPU Dependency Memory Tracking (above) | link to `cpu-dependency-memory-tracking.md` |
| `profiling.md` | Editor Module Structure (below) | link to `editor-module-structure.md` |
| `profiling.md` | Coding Guidelines (stays inline) | link to `../../AGENTS.md#coding-guidelines` |
| `job-system.md` | Profiling (above) | link to `profiling.md` |
| `job-system.md` | AGENTS.md's own "Skeletal Animation Pose Resolution" section | link to `skeletal-animation-pose-resolution.md` |
| `networking.md` | Job System (above), x2 | link to `job-system.md` |
| `networking.md` | Job System + Profiling (above, combined) | links to both `job-system.md` and `profiling.md` |
| `networking.md` | Editor Module Structure (no above/below wording) | link to `editor-module-structure.md` |
| `networking.md` | Testability & Regression Safety (stays inline) | link to `../../AGENTS.md#testability--regression-safety` |
| `networking.md` (nested Named Texture Capture) | Atmosphere Scattering (below), x3 | link to `atmosphere-scattering.md` |
| `gpu-vertex-skinning.md` | Job System (above) | link to `job-system.md` |
| `gpu-vertex-skinning.md` | Profiling (above) | link to `profiling.md` |
| `atmosphere-scattering.md` | GPU Vertex Skinning (above, split across a line wrap) | link to `gpu-vertex-skinning.md` |
| `atmosphere-scattering.md` | Networking / Named Texture Capture (above) | link to `networking.md` |
| `ecs.md` | GPU Resource Memory Tracking (above) | link to `gpu-resource-memory-tracking.md` |
| `ecs.md` | Coding Guidelines (stays inline) | link to `../../AGENTS.md#coding-guidelines` |
| `ecs.md` | Testability & Regression Safety (below), x2 (one a "Tier-1-testability rule... below" paraphrase, not the literal heading quote) | link to `../../AGENTS.md#testability--regression-safety` |
| `ecs.md` | Editor Module Structure (below) | link to `editor-module-structure.md` |
| `ecs.md` | Render Target Format Matching (above) | link to `render-target-format-matching.md` |
| `editor-module-structure.md` | Coding Guidelines (stays inline) | link to `../../AGENTS.md#coding-guidelines` |
| `editor-module-structure.md` | Entity-Component-System (below) | link to `ecs.md` |
| `editor-module-structure.md` | Testability & Regression Safety (below) | link to `../../AGENTS.md#testability--regression-safety` |
| `editor-module-structure.md` | "`RenderSystem`'s rule below" — a pre-existing quirk in the original text: the referenced rule ("`Renderer` itself must never gain a dependency on ECS in either direction") actually lives EARLIER in the file, inside the ECS section, not "below" at all; this became a genuine cross-FILE reference once ECS moved to its own file | link to `ecs.md` |

Every OTHER `"above)"/"below)"` hit found (re-confirmed after the rewrite,
see Verification below) is a genuine same-file self-reference — the citing
and cited text both still live inside the same new `docs/conventions/*.md`
file after the split — and was left completely unchanged, matching the
strategy document's own classification rule.

One deviation from the pre-computed checklist worth calling out: the
strategy document's table said `~72` (CPU Dependency Memory Tracking citing
GPU Resource Memory Tracking) — re-verified against the real file, line 72
is actually a SELF-reference INSIDE the GPU Resource Memory Tracking section
itself (a debug-name bullet citing an earlier bullet in the same section),
not a cross-section reference at all; the real cross-section citation is
at line 88 (`Alongside \`GpuMemoryTracker\` (above)`), which was fixed. This
is exactly the kind of drift Workflow Rule #4/#5 warns about — the
checklist is a starting point, not ground truth.

### 3.2 — Rewrote `AGENTS.md` itself (thin, pointer headings)

Worked from the BOTTOM of the file upward, exactly as the strategy
document specifies (Editor Module Structure -> Scene Serialization -> ECS
-> Atmosphere Scattering -> GPU Vertex Skinning -> Skeletal Animation Pose
Resolution -> Render Target Format Matching -> Networking -> Job System ->
Profiling -> CPU Dependency Memory Tracking -> GPU Resource Memory
Tracking), so no `edit_line` call ever invalidated a line number for
content still above it that hadn't been processed yet. Each of the 12
sections was replaced with its `##` heading unchanged, a fresh 4-6 line
summary written from that section's own real opening sentences (not
invented from scratch), and a `Full convention: [docs/conventions/x.md]
(docs/conventions/x.md).` closing line. `## Networking`'s summary
explicitly mentions `GET /activate_tab`/`GET /list_tabs` (Phase 1's newest
addition) alongside the pre-existing capture/ECS-mutating endpoint
families, per the strategy's own explicit requirement. `## Coding
Guidelines` and `## Testability & Regression Safety` were left completely
untouched, in full, exactly where they already were.

One small follow-up fix made after the initial rewrite: the first draft of
`## Networking`'s summary used the word "below" to refer to "Named Texture
Capture" support, which no longer made sense once the full "Named Texture
Capture" subsection lived only in the linked file, not literally below in
this same shrunk paragraph — reworded to drop "below" entirely (a
self-contained mention with no dangling positional reference), verified via
a second `read_line` pass and a `search_in_dir "below)"` re-check.

### 3.3 — Ordering sanity check

`search_in_dir "^## "` against the final `AGENTS.md` confirms all 14
original headings are present, in the exact same order as before this
phase: Coding Guidelines, GPU Resource Memory Tracking, CPU Dependency
Memory Tracking, Profiling, Job System, Networking, Render Target Format
Matching, Skeletal Animation Pose Resolution, GPU Vertex Skinning,
Atmosphere Scattering, Entity-Component-System (ECS), Scene Serialization,
Editor Module Structure, Testability & Regression Safety.

## Deviations from the strategy document

- The pre-computed cross-reference checklist in the strategy document (Step
  3.1.1) was a good starting point but not fully accurate against the real
  file — see the "~72" note above. A fresh audit was performed instead of
  trusting the table verbatim, per the document's own instruction to
  "re-verify with your own `search_in_dir` pass at execution time."
- Several genuine cross-section references were found that the strategy
  document's own checklist did NOT enumerate (they use neither the literal
  `"above)"` nor `"below)"` pattern): `networking.md`'s "Editor Module
  Structure" reference (no positional word at all), `job-system.md`'s
  "AGENTS.md's own \"Skeletal Animation Pose Resolution\" section"
  reference, `atmosphere-scattering.md`'s "GPU Vertex Skinning" reference
  (split across a line wrap, `"above.**"` not `"above)"`), and
  `editor-module-structure.md`'s "`RenderSystem`'s rule below" reference
  (which the audit determined is actually a pre-existing quirk: the real
  referenced rule sits EARLIER in the original file, inside the ECS
  section, despite being called "below"). All four were found by a second
  audit pass (searching each section's own heading name in quotes, plus a
  plain-text search for the referenced concept) and fixed the same way as
  the checklist's own entries, since they are the exact same class of
  problem the checklist's entries already address.
- `docs/conventions/ecs.md`'s closing cross-reference section is written as
  a short, explicit "Relationship to the engine's ECS architecture
  overview" paragraph rather than a bare one-line pointer, so both this
  file and `docs/architecture/ecs.md` clearly explain to a reader landing
  on EITHER one why a second, similarly-named file exists and what the
  difference is — consistent with, but slightly more detailed than, the
  strategy document's own minimum requirement ("MUST end with the same
  cross-reference Phase 2's `docs/architecture/ecs.md` points back at").
- No other deviations. Every file-mapping and structural requirement in the
  (already-precheck-corrected) strategy document was followed as written.

## Verification performed

- `cmake --build build` — `ninja: no work to do.` (clean no-op rebuild, as
  expected for a `.md`-only change; confirms nothing under `src/`/`tests/`/
  `CMakeLists.txt` was touched this phase).
- No test filter run performed this phase, per Workflow Rule #1 (no
  `.cpp`/`.h`/`CMakeLists.txt` file touched).
- Re-read the final `AGENTS.md` in full (`read_file`) — confirmed all 14
  original headings still present, in the same order, no Markdown
  corruption (balanced code fences/backticks, headings intact), and the
  file shrank from 2146 lines / ~155 KB (pre-campaign) down to **209 lines
  / 11.2 KB**.
- Confirmed via `search_in_dir "^## "`/`"^### "` against the final
  `AGENTS.md` that every original heading is still present at its new
  (shifted) line number, and that the one nested `### ` heading is gone
  from `AGENTS.md` (moved into `networking.md` as a `##`).
- `search_in_dir "above)"`/`search_in_dir "below)"` re-run across the final
  `AGENTS.md` and every one of the 12 new `docs/conventions/*.md` files:
  every remaining hit was manually re-confirmed to be either (a) a genuine
  same-file self-reference, or (b) a real Markdown link followed by the
  literal word `above)`/`below)` in the surrounding prose (e.g. `[Job
  System](job-system.md) above)` — the link itself is correct; the
  trailing word is just the original sentence's own wording) — zero stale,
  un-linked cross-file references remain.
- Spot-checked content completeness for 3 of the 12 new files by
  re-finding a distinctive phrase from the real `AGENTS.md` (rather than
  assuming any one exact quoted phrase from the strategy document survived)
  and confirming it exists verbatim in the new file and NOT in the thinned
  `AGENTS.md`: `EditorUiCommandBridge` (present 3x in `networking.md`,
  present only once — in the shrunk summary's own brief mention — in
  `AGENTS.md`, consistent with "the summary still names the bridge briefly,
  the full detail lives in the linked file"), the Phase-4 thread-safety
  classification table (present in full, all 14 rows, in `job-system.md`;
  absent from `AGENTS.md`), and the `hoffstadt/pl-sky` reference-
  implementation citation (present in `atmosphere-scattering.md`; absent
  from `AGENTS.md`).
- Confirmed `docs/conventions/ecs.md` <-> `docs/architecture/ecs.md`
  resolve to each other by direct `read_file` inspection of both link
  targets (`../architecture/ecs.md` and `../conventions/ecs.md`
  respectively).
- `browse_dir details:true`: `AGENTS.md` dropped from ~155 KB (2146 lines,
  end-of-Phase-1 state) to **11.2 KB (209 lines)**. The 12 new
  `docs/conventions/*.md` files total roughly 155 KB combined (`job-
  system.md` 46.8 KB being the largest, matching it being the single
  largest section moved by line count), confirming this was a pure
  relocation, not a lossy summarization.
- `git_status`: only `AGENTS.md` shows as modified, and `docs/conventions/`
  shows as a new untracked directory — no other file in the repository was
  touched this phase.

## Exact state left in

- Modified: `AGENTS.md` (2146 -> 209 lines, ~155 KB -> 11.2 KB).
- Created: `docs/conventions/gpu-resource-memory-tracking.md`,
  `docs/conventions/cpu-dependency-memory-tracking.md`,
  `docs/conventions/profiling.md`, `docs/conventions/job-system.md`,
  `docs/conventions/networking.md`,
  `docs/conventions/render-target-format-matching.md`,
  `docs/conventions/skeletal-animation-pose-resolution.md`,
  `docs/conventions/gpu-vertex-skinning.md`,
  `docs/conventions/atmosphere-scattering.md`, `docs/conventions/ecs.md`,
  `docs/conventions/scene-serialization.md`,
  `docs/conventions/editor-module-structure.md`,
  `task_manager/doc-refactor-1/PHASE3_COMPLETION_REPORT.md` (this file).
- `README.md` and everything under `docs/architecture/`/`docs/CHANGELOG.md`
  are untouched (that was Phase 2's job).
- No `src/`, `tests/`, or `CMakeLists.txt` file was touched.
- Branch remains `fix/doc-refactor` throughout.

## Handoff note for Phase 4

- Phase 4 still needs to: create `docs/README.md` (the landing index
  covering BOTH Phase 2's `docs/architecture/`+`docs/CHANGELOG.md` AND this
  phase's `docs/conventions/`), add a short "Documentation" pointer near
  the top of both `README.md` and `AGENTS.md`, and run the full
  link-integrity audit across every `.md` file this whole campaign touched
  or created.
- Every breadcrumb link in this phase's 12 new files points at
  `../README.md` (i.e. `docs/README.md`) — this file does not exist yet
  and is Phase 4's own deliverable; Phase 4's link audit must confirm all
  12 of these resolve once it lands.
- Every `../../AGENTS.md#coding-guidelines` /
  `../../AGENTS.md#testability--regression-safety` anchor link added this
  phase (5 total: `profiling.md` x1, `ecs.md` x2, `editor-module-
  structure.md` x2, `networking.md` x1 — six, not five; recount confirmed
  via `search_in_dir "AGENTS.md#"` against `docs/conventions/`) should be
  spot-checked by Phase 4's audit against the real, final anchor GitHub
  would generate for those two headings — this campaign generated them by
  hand following the documented slugification rule (lowercase, spaces ->
  hyphens, drop non-alphanumeric/non-hyphen characters), not by rendering
  the Markdown and checking a live anchor, so an independent check is
  worthwhile.
