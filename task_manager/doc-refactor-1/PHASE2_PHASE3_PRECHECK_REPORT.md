# PHASE2 / PHASE3 Pre-Check Report

**Scope:** a review-only pass over `PHASE2_README_ARCHITECTURE_AND_CHANGELOG_
EXTRACTION.md` and `PHASE3_AGENTS_CONVENTIONS_EXTRACTION.md`, cross-checked
against the REAL current contents of `README.md` and `AGENTS.md`. No
implementation work was done — no `docs/` file was created, `README.md` and
`AGENTS.md` were not touched. `PHASE0`, `PHASE1`, `PHASE4`, `PHASE5` were read
for context only and were NOT modified, per the task's instructions.

## What was checked

1. Read `PHASE0_MASTER_STRATEGY.md` in full (orchestrator/Locked Decisions).
2. Read `PHASE2_README_ARCHITECTURE_AND_CHANGELOG_EXTRACTION.md` and
   `PHASE3_AGENTS_CONVENTIONS_EXTRACTION.md` in full.
3. Verified the REAL `README.md`/`AGENTS.md` heading structure with
   `search_in_dir "^## "` / `search_in_dir "^### "` against both files, and
   spot-read several hundred lines of both files' actual prose (`read_file`/
   `read_line`) to sanity-check the strategy documents' claims.
4. Confirmed `task_manager/network-impl-7/` contains all 11 files PHASE0
   describes (Phase 1-4 completion reports, no Phase 5/campaign report yet),
   consistent with PHASE0's claim that Phase 5's doc bullets are still
   outstanding.
5. Confirmed (via `search_in_dir` for `activate_tab`/etc. and by re-deriving
   every quoted line number) that `PHASE1_ACTIVATE_TAB_DOCUMENTATION_
   CLOSEOUT.md` of THIS campaign has evidently not run yet as of this
   pre-check: `README.md` is still exactly 1972 lines and `AGENTS.md` is
   still exactly 2109 lines, and every heading sits at EXACTLY the baseline
   line number both PHASE2 and PHASE3 quote (e.g. `## Networking` at line
   936, `## Status` at line 839). This is expected/fine for a pre-execution
   review pass — it just means the "re-verify at execution time, numbers
   will have shifted after Phase 1" caveats already present throughout
   PHASE0/PHASE2/PHASE3 are forward-looking and have not yet been exercised.

## Findings

### 1. Gaps (file-mapping completeness)
None found. Every one of README's six `## Architecture` subsections plus
`## Status` is accounted for in PHASE2's mapping table (Step 3.1 + 3.2).
Every one of AGENTS.md's 12 sections meant to move (including the nested
`### Named Texture Capture` subsection under `## Networking`) is accounted
for in PHASE3's mapping table (Step 2 baseline table + Step 3.1). Nothing is
silently left unmapped.

### 2. Incorrectness (line numbers / heading names)
- All quoted line numbers in both documents match the REAL current file
  content exactly (no drift yet, since Phase 1 of this campaign has not run
  — see above). The "re-verify before trusting any number" caveats already
  in both documents are clear and sufficient.
- One real, if minor, inaccuracy found and fixed: both documents quoted the
  nested heading as `### Named Texture Capture (GET /get_texture)`, but the
  REAL heading in `AGENTS.md` (line 1148) carries backticks around the
  endpoint name: `` ### Named Texture Capture (`GET /get_texture`) ``. A
  literal string search for the strategy's exact quoted text would not have
  matched the real file (a `^### ` regex anchor is unaffected, but the
  document's own text implied an exact heading). Fixed in PHASE3 (added an
  explicit note in Step 2, and corrected the two other verbatim quotes of
  this heading in Step 3.1).
- Softened one loosely-grounded example in PHASE3's Step 4 verification: it
  told the implementer to spot-check for the literal phrase "IMPORTANT
  ordering detail" inside AGENTS.md's Networking section, citing
  `network-impl-7`'s own Phase 4 completion report. That exact phrase does
  NOT currently exist anywhere in `AGENTS.md` (confirmed via `search_in_dir`)
  — it only exists in `task_manager/network-impl-7/`'s own strategy/
  completion-report files. It is expected to show up in `AGENTS.md`'s
  Networking section only once THIS campaign's own Phase 1 lands its new
  bullet, and Phase 1's own instructions do not guarantee that exact
  wording survives verbatim (Phase 1 says to write new prose "in the same
  voice", not to copy that literal phrase). Reworded the verification step
  so it no longer assumes one specific phrase's exact wording, and instead
  tells the implementer to re-derive whatever distinctive phrase Phase 1
  actually used at execution time.

### 3. Insufficiency (implementability for a zero-context implementer)
Both documents already covered the core mechanics well: exact new file
paths, the pointer-heading convention (with a worked example), and the
bottom-to-top editing order. The one substantial insufficiency found in
both was **under-scoping the internal cross-reference problem** (see
"Missing", next) — both documents acknowledged the problem exists ("any
internal cross-reference... must be rewritten...") but presented it as a
rare, maybe-nonexistent hypothetical ("if one exists — check the real
text"), when in reality it is pervasive throughout both files.

### 4. Missing (what a senior engineer would expect and flag)
This was the main substantive finding. Both `README.md` and `AGENTS.md` are
dense with `"(see X above)"` / `"(see X below)"` prose cross-references
between sections (~32 hits for `"below"` and ~32 more for the same pattern
in `README.md` alone; ~48 hits for `"above)"` plus ~32 for `"below)"` in
`AGENTS.md`). A representative, VERIFIED (by direct `read_line` inspection)
sample of real cross-SECTION references that will break or go stale once
the campaign splits sections into separate files:
- `README.md`: Math -> ECS; Rendering -> {Editor/Debug UI, ECS, Asset
  Pipeline}; ECS -> Status; Asset Pipeline -> {Editor/Debug UI (x2), Status,
  Rendering, ECS}. The ECS -> Status and Asset Pipeline -> Status cases are
  special: Status becomes `docs/CHANGELOG.md`, a DIFFERENT top-level file
  than the other five (which become `docs/architecture/*.md` siblings) —
  the original PHASE2 text only anticipated cross-links among
  `docs/architecture/*.md` siblings, not this cross-target case.
- `AGENTS.md`: CPU Dependency Memory Tracking <-> GPU Resource Memory
  Tracking; Profiling -> {CPU Dependency Memory Tracking, Editor Module
  Structure}; Networking -> Testability & Regression Safety; Networking's
  "Named Texture Capture" <-> Atmosphere Scattering; Scene Serialization ->
  Editor Module Structure; Editor Module Structure -> Testability &
  Regression Safety. The two references to "Testability & Regression
  Safety" are a special case the original PHASE3 text did not call out:
  that section STAYS INLINE in `AGENTS.md` (Locked Decision #3) rather than
  moving to `docs/conventions/`, so a reference to it from inside a SECTION
  THAT DOES MOVE needs to become a link back INTO `AGENTS.md` (via its
  GitHub heading anchor), not a same-folder sibling link.

Both documents' `docs/architecture/ecs.md` <-> `docs/conventions/ecs.md`
cross-link requirement was already present and correct in both documents
(including a correct relative-path depth), so no gap there.

### 5/6. Worth improving / Worth adding for QoL
Folded directly into the fixes below rather than left as separate
suggestions: a concrete, verified starting checklist (with approximate line
numbers, the citing section, and the exact fix) is far lower-risk for an
implementer to work from than a one-line "check if any exist" reminder, and
costs nothing extra to include since the audit was already done as part of
this review.

## What was changed

Both `PHASE2_README_ARCHITECTURE_AND_CHANGELOG_EXTRACTION.md` and
`PHASE3_AGENTS_CONVENTIONS_EXTRACTION.md` were overwritten in place (v2,
same file paths, nothing renamed) with the following additions:

- A new **"Cross-reference audit"** subsection (3.1.1) in each document,
  containing: (a) the real, verified count/sample of `"above)"`/`"below)"`
  hits found in the current file, (b) a concrete table of the actual
  cross-section references found (approximate line, source section, citation
  text, and the exact fix), and (c) a 2-3-case classification rule (self-
  reference / cross-sibling-file reference / reference into a file that
  DOESN'T move — `../CHANGELOG.md` for PHASE2, `AGENTS.md`'s own anchors for
  PHASE3) so the implementer can correctly classify anything their own fresh
  search additionally turns up.
- PHASE3 additionally documents the GitHub heading-anchor slugification rule
  (lowercase, spaces -> hyphens, drop non-alphanumeric/non-hyphen chars) with
  a worked example (`Testability & Regression Safety` ->
  `#testability--regression-safety`), plus a warning that a mistyped anchor
  fails silently.
- Both documents' Step 4 ("Verification for this phase") gained an explicit
  `search_in_dir "above)"` / `search_in_dir "below)"` re-check across every
  newly created/edited file, cross-referenced against the Step 3.1.1 audit,
  so a leftover stale cross-reference is caught as a bug in that same phase
  rather than silently deferred to Phase 4.
- PHASE3's Step 2 baseline table and Step 3.1 now quote the real
  `` ### Named Texture Capture (`GET /get_texture`) `` heading text
  (including its backticks) instead of the previously-inaccurate plain-text
  quote.
- PHASE3's Step 4 "IMPORTANT ordering detail" spot-check example was
  reworded to not assume one specific phrase's exact final wording, since
  that phrase does not exist in `AGENTS.md` yet (Phase 1 of this campaign
  has not run) and Phase 1's own instructions do not guarantee verbatim
  wording.
- Both documents' Step 6 handoff notes were updated to mention the new
  cross-reference audit work for Phase 4's benefit.

`PHASE0_MASTER_STRATEGY.md`, `PHASE1_ACTIVATE_TAB_DOCUMENTATION_CLOSEOUT.md`,
`PHASE4_CROSS_REFERENCE_LINK_INTEGRITY_AND_DOCS_INDEX.md`, and
`PHASE5_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md` were read for context only
and were NOT modified, per this task's explicit scope.

## Verification performed

- No build/compile/test actions were performed (out of scope for this pure
  documentation-review pass, per the task instructions).
- Both rewritten Markdown files were re-read in full after editing to
  confirm no Markdown code-span/backtick-nesting corruption was introduced
  (one such issue WAS introduced by a first-draft edit — a code span
  containing an inner, un-escaped backtick — and was caught and fixed in
  this same pass by re-reading the written file and switching to a
  double-backtick delimiter).
- `git status` will show `README.md`/`AGENTS.md` as unmodified, confirming
  no accidental implementation work happened during this review.

## Exact state left in

- `PHASE2_README_ARCHITECTURE_AND_CHANGELOG_EXTRACTION.md` and
  `PHASE3_AGENTS_CONVENTIONS_EXTRACTION.md` are both v2 (fixed in place, same
  file paths).
- `README.md`/`AGENTS.md` are untouched (still their pre-campaign 1972/2109
  lines).
- No `docs/` folder was created.
- Branch remains `fix/doc-refactor` throughout.
