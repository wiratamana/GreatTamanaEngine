# PHASE4 — Documentation, Full Regression, and Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first).
**Previous phase:** `PHASE3_EDITOR_WIDE_RETROFIT_SWEEP.md` — read its own
`PHASE3_COMPLETION_REPORT.md` before starting (especially its "grep gate"
output — confirm it is genuinely clean before writing documentation that
claims it is).

---

## Step 1: The Goal Of This Phase

Make the fix permanent in THREE senses, not just one:

1. **Documented**, so a future engineer/AI agent discovers `ScopedUniqueId`
   and understands WHY it exists before ever reaching for a raw
   `ImGui::PushID(someDataString.c_str())` again.
2. **Mandated**, via a new `AGENTS.md` section — this is what makes the fix
   binding on code that does not exist yet, which is the whole point of
   "never happen again once and for all".
3. **Regression-proven**, via one full clean build and one full `ctest`
   pass — the FIRST full build/test run this entire campaign has done (every
   earlier phase deliberately used only incremental checks, per Locked
   Design Decision #8).

## Step 2: The Situation Going Into This Phase

By now: `ImGuiIdConflictTracker`/`ImGuiIdConflictGuard` (PHASE1),
`ScopedUniqueId` (PHASE2), and every existing Editor `PushID` call site
(PHASE2 + PHASE3) are all in place and individually incrementally-verified.
Nothing in `gte_core` was ever touched — this whole campaign lives entirely
inside `gte_editor` and `tests/`. This phase does not add any more retrofit
call sites — if PHASE3's own grep gate was clean, there is nothing left to
retrofit.

## Step 3: The Detailed Plan

### 3.1 — Write `docs/conventions/imgui-id-uniqueness.md`

Mirror the depth/structure of a neighboring file in `docs/conventions/` —
skim `docs/conventions/logging.md` or `docs/conventions/gpu-resource-memory-
tracking.md` first for the house style (problem statement, then the real
API, then "how to use it", then history/provenance). Required content,
at minimum:

- **The problem this exists to prevent, in one paragraph**, referencing the
  original bug report
  (`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\bugs\
  2026-09-25_RenderGraphPanel_Duplicate_Checkbox_ID_Highlight_Bug.txt`) and
  this campaign
  (`task_manager/editor-core-separation-10/PHASE0_MASTER_STRATEGY.md`) by
  path, so a future reader can find the full forensic history if they want
  it.
- **The two-layer system**, explained plainly:
  - Layer 1 (structural prevention): `gte::ScopedUniqueId`
    (`src/Editor/ImGuiUniqueId.h`) — derive ID uniqueness from loop position,
    never from data content.
  - Layer 2 (proactive detection, a safety net in case Layer 1 is ever
    bypassed by future code that doesn't use it): `gte::
    ImGuiIdConflictTracker`/`gte::ImGuiIdConflictGuard`
    (`src/Editor/ImGuiIdConflictTracker.h`, `ImGuiIdConflictGuard.h`) —
    logs via `GTE_LOG_ERROR("ImGuiIdConflict", ...)`, once per new
    incident, retrievable via `GET /get_logs?category=ImGuiIdConflict`.
  - Explicitly state that Dear ImGui's own built-in
    `io.ConfigDebugHighlightIdConflicts` (default `true`) is UNTOUCHED and
    remains a third, final backstop — this system adds to it, it does not
    replace or silence it.
- **The mandatory rule for all future code**: "Any ImGui widget built
  inside a loop over runtime data MUST enter its ID scope via
  `gte::ScopedUniqueId`, keyed by the loop's own iteration index. Do not
  assume any name/label/path/data string is unique enough on its own to
  build a widget ID out of, no matter how unlikely a real-world collision
  seems" — with a short "good" vs "bad" code snippet pair.
- **A permanent record of every call site this campaign touched** (the
  file manifest from `PHASE0_MASTER_STRATEGY.md` section 3.2, restated
  here as the "history" section), so a future reader auditing "did this
  ever get applied consistently?" has a checklist to compare against.

### 3.2 — Add a new `AGENTS.md` section

Open `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\AGENTS.md` and add a
new section. Placement: immediately after the existing "## Logging"
section (both concerns are about proactive, engine-owned diagnostics/
guardrails, and "ImGui Widget ID Uniqueness" itself depends on "Logging"'s
own `GTE_LOG_*` macros, so reading them in this order makes sense) and
before "## Render Target Format Matching". Use `read_file` on `AGENTS.md`
first to find the CURRENT exact line number of the boundary between those
two sections (do not trust any line number quoted elsewhere in this
campaign's own docs — `AGENTS.md` is a living file other unrelated work may
have touched between phases) and use `edit_line` to insert the new section
there without disturbing anything else in the file.

Suggested section text (adapt wording to match the file's existing voice,
but keep the substance):

```markdown
## ImGui Widget ID Uniqueness

`src/Editor/ImGuiUniqueId.h` (`gte::ScopedUniqueId`) is the ONE mandated way
to enter a per-iteration Dear ImGui ID scope for any widget built inside a
loop over runtime data - construct it with the loop's own distinct iteration
index (never a data-derived string alone) plus an optional human-readable
debug key, and let it manage `ImGui::PushID()`/`PopID()` for you. This
exists because a real, live bug shipped from doing exactly the unsafe thing
this class now prevents: `src/Editor/Panels/RenderGraphPanel.cpp`'s
"Offscreen Regime" table legitimately shows the same render-graph pass name
twice in one frame (Game View and Scene View both declare an Atmosphere LUT
pass under the same hard-coded name - a permanent, intentional design
choice, `task_manager/editor-core-separation-10` campaign,
`PHASE0_MASTER_STRATEGY.md` section 2.2), and that file's own checkbox ID
used to be built purely from that (sometimes-duplicate) pass name, so Dear
ImGui's own built-in `io.ConfigDebugHighlightIdConflicts` safety net (left
at its default `true`, still on today, still a final backstop) would flash
a "Programmer error: N visible items with conflicting ID!" red-highlight
the instant either row was hovered. A second, proactive layer,
`src/Editor/ImGuiIdConflictTracker.h`/`ImGuiIdConflictGuard.h` (the former
pure and Tier-1-tested, the latter the real ImGui/`Logger`-aware singleton,
reset once per frame from `ImGuiEditorLayer::NewFrame()`), catches any
future collision the moment it happens and logs it exactly once per new
incident via `GTE_LOG_ERROR("ImGuiIdConflict", ...)` - visible in the "Log"
panel and `GET /get_logs?category=ImGuiIdConflict` - rather than relying on
a human happening to hover the right widget and correctly recognizing what
Dear ImGui's own red highlight means. Every pre-existing `PushID()` call
site under `src/Editor/` was migrated onto `ScopedUniqueId` by this same
campaign, so this is a real, engine-wide, present-day guarantee, not just a
rule for new code.

Full convention: [docs/conventions/imgui-id-uniqueness.md](docs/conventions/imgui-id-uniqueness.md).
```

### 3.3 — Full clean build

This is the first and ONLY phase in this campaign allowed to do this. Use
this repo's own documented build flow (check `BUILDING.md` if unsure of the
exact generator/flags this machine uses) — the campaign's own reference
commands:

```
cmake --build build
```

(Working directory: the repo root.) If this repo's convention is a genuinely
fresh/clean configure+build (delete `build/` first, or use a fresh build
directory) rather than reusing the incremental one every earlier phase used,
do that here — the whole point of this step is to prove NOTHING was
silently broken across the whole campaign, not just the files this campaign
touched.

Fix any compile error found here immediately, in this phase — do not defer
it. If a fix is non-trivial, `delegate_task` a focused sub-task to diagnose
and fix it (per this campaign's own "Note 2"-style workflow: a heavy,
narrow fix delegated as its own step, but WITHOUT creating a new report
file for that sub-fix — fold the outcome into this phase's own
`PHASE4_COMPLETION_REPORT.md` instead).

### 3.4 — Full regression test

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Compare the pass count against the most recent baseline documented in
`AGENTS.md` (search it for the phrase "ctest regression pass" to find the
latest campaign's own reported total — this campaign added exactly ONE new
test file, `Editor/ImGuiIdConflictTrackerTests.cpp`, with at least 6 new
cases per `PHASE1_ID_CONFLICT_DETECTION_FOUNDATION.md` section 3.3, so the
new total should be the old baseline plus that many, with zero newly-failing
pre-existing tests). Any newly-failing pre-existing test is a real
regression — diagnose it (likely candidate: a missed call-site/signature
update from PHASE2/PHASE3 that compiled but behaves subtly differently) and
fix it via a focused `delegate_task`, same rule as 3.3 above (fold the
outcome into this phase's own report, no separate report file).

### 3.5 — Final live, HTTP-driven end-to-end proof

1. `run_app_background` the freshly-built Editor executable.
2. `gte_send_request` `GET /list_tabs` to confirm the exact panel name
   string for the Render Graph panel (reuse whatever PHASE2 already
   discovered and recorded in its own completion report, if available, to
   save a step).
3. `gte_send_request` `GET /activate_tab?...` to bring it to the front.
4. `gte_send_request` `GET /get_swapchain` (or `/get_game_view`) — visually
   confirm the "Offscreen Regime (Game View + Scene View)" table, WITH its
   still-intentional duplicate Atmosphere pass-name rows, renders
   identically to how it looked before this campaign (this is a "did we
   accidentally change any VISIBLE behavior" check, not just a compile
   check).
5. `gte_send_request` `GET /get_logs?category=ImGuiIdConflict` after the
   app has been running with that panel active for at least a few real
   seconds — confirm it is EMPTY.
6. `stop_app_background` the process.
7. Restate, honestly, in the completion report, the same disclosed
   limitation PHASE2 already noted: no HTTP endpoint exists to simulate a
   mouse hover, so this live check cannot directly re-enact "hover and
   confirm Dear ImGui's own red box no longer appears" end-to-end over the
   network — the authoritative proof for "the underlying mechanism is
   correct" is PHASE1's deterministic Tier-1 test
   (`ImGuiIdConflictTrackerTests.cpp`), and this live check's own real job
   is only to prove (a) nothing rendered differently and (b) no conflict
   was logged against real, live, genuinely-duplicate-named production
   data during an actual run.

### 3.6 — Write `CAMPAIGN_COMPLETION_REPORT.md`

Save it directly in `task_manager/editor-core-separation-10/` (not
`PHASE4_...`, the campaign-level report, mirroring every prior campaign's
own `CAMPAIGN_COMPLETION_REPORT.md`, e.g.
`task_manager/editor-core-separation-9/CAMPAIGN_COMPLETION_REPORT.md`).
Cover, across all four phases: what shipped, the final build/ctest numbers
(before/after), the full list of every retrofitted call site (the grep
gate's final clean output), the disclosed hover-simulation limitation from
3.5, and an explicit restatement that the Atmosphere pass-naming
duplication itself (the original bug's own data-level root cause) remains
100% untouched and permanent, by design (LDD7) — so a future reader is never
misled into thinking this campaign "fixed the duplicate pass names" when it
deliberately, permanently did not.

Then `git_add` + `git_commit` everything from this phase (including the
campaign completion report) in one commit.

## Step 4: Campaign Closeout

After this phase: the reported bug cannot recur from ANY existing Editor
code path (PHASE2 fixed the literal report; PHASE3 made it engine-wide), a
future regression from NEW code is caught and logged within one frame
instead of being silently visible-but-misdiagnosed (PHASE1's detection
layer), the rule is written down and discoverable by any future
engineer/AI (PHASE4's docs + `AGENTS.md`), and a full build + full
regression pass proves none of this broke anything else. This campaign is
complete.
