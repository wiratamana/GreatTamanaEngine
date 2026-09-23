# PHASE7 — Docs, Full Build, and Campaign Completion

## Parent

`PHASE0_MASTER_STRATEGY.md` (read it first). Read EVERY previous
`PHASEn_COMPLETION_REPORT.md` (1 through 6, including PHASE4/5/6's own
dedicated double-check notes/corrections) before starting — this phase is
the campaign's own final integration point and must know exactly what each
prior phase actually shipped, not just what it originally planned to.

## Step 1: The Goal (Where are we going?)

Close out the `render-pass-6` campaign:

1. Update `AGENTS.md`'s existing "Render Pass System" section with a short,
   honest note about this internal refactor (per `PHASE0_MASTER_STRATEGY.md`'s
   Locked Design Decision 6 — **no new heading**, no claim of new
   capability).
2. Run a full Tier-1 test sweep for everything this campaign touched.
3. Run a **full** `cmake --build build` (the only phase in this campaign
   allowed/expected to do so, per `PHASE0_MASTER_STRATEGY.md`'s Locked
   Design Decision 7).
4. Run a **full** `ctest` regression pass, confirm zero regressions against
   the baseline count recorded at the START of this phase.
5. Perform a live, HTTP-driven, screenshot/log-verified smoke test proving
   the whole campaign is genuinely behavior-preserving.
6. Write `CAMPAIGN_COMPLETION_REPORT.md`, the definitive record of what
   `render-pass-6` actually shipped, any deviation from the original plan,
   and what remains genuinely open (if anything).

## Step 2: The Situation (Where are we now?)

By the time this phase starts, PHASE1-6 have each landed independently
(with PHASE4/5/6 additionally having gone through their own dedicated
`delegate_task` double-checks), each with its own fast, targeted incremental
compile check only — **this phase is the FIRST point in the whole campaign
a full, whole-engine build and the full test suite are actually run.** This
means it is also the first point genuinely inter-phase integration issues
(if any slipped through six rounds of targeted incremental checks) would
surface — budget real time for this, and do not treat "it compiled
incrementally at every phase" as equivalent to "a full build/link/test pass
will definitely also succeed."

## Step 3: The Plan (How do we get there?)

### 3.1 — Record the baseline

Before making any change in this phase, run the full existing (pre-this-
campaign, i.e. whatever PHASE6 already left passing) `ctest` suite once and
record the exact pass count — this is the number every later run in this
phase must match or exceed, with zero regressions, per
`PHASE0_MASTER_STRATEGY.md`'s own Definition of Done. (If PHASE6's own
dedicated double-check already ran a full `ctest` pass as part of its
verification — check its own report first — reuse that count rather than
re-running redundantly, but re-confirm it is still accurate given anything
PHASE6's double-check may have additionally changed.)

### 3.2 — `AGENTS.md` documentation update

Locate the existing "Render Pass System" section (`AGENTS.md`, currently
around lines 133-267 — re-confirm the exact line range, since prior phases
may have shifted other parts of the file, though none of PHASE1-6 should
have touched `AGENTS.md` itself). Append a short paragraph at the END of
that section (after the existing `render-pass-4` writeup, before the "Full
history:" links line), in the same voice/style as the rest of the section,
e.g.:

> A further campaign, `render-pass-6` (seven phases,
> `task_manager/render-pass-6/PHASE0_MASTER_STRATEGY.md`,
> `CAMPAIGN_COMPLETION_REPORT.md`), reworked this system's own internals for
> scale and per-frame speed with **zero behavior change and zero public API
> change**: `RenderGraphCompiler::Compile()`'s dependency-graph construction
> moved from an `O(P^2)` adjacency matrix to `O(P+E)` adjacency lists (byte-
> identical `executionOrder` verified against the full pre-existing
> `RenderGraphCompilerTests.cpp` suite); the 9 parallel builder/
> `CompiledGraphInput` vectors (`textureDescs`/`textureNames`/
> `textureImportInfo`, etc.) collapsed into 3 per-kind `TextureSlot`/
> `BufferSlot`/`VolumeTextureSlot` vectors, eliminating a whole class of
> "forgot to push to array #3" silent-misalignment bug; every hand-duplicated
> `switch (usage.kind)` block scattered across `RenderGraph.cpp`/
> `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp` collapsed into one
> generic `DispatchByKind()` dispatcher (still a real, `default:`-less
> exhaustive switch internally — a 4th `ResourceKind` still fails to compile
> everywhere until updated); `PassContext`'s six `std::function` fields
> (freshly constructed, per pass, per `Execute()` call) became plain
> non-owning pointers + ordinary member functions, with zero change to any
> pass author's own call-site syntax; and `RenderGraphNameSlotTable`'s
> previously-silent timing-slot-budget overflow now produces a real,
> one-time log entry plus a `RenderGraphSnapshot::timingSlotBudgetExhausted`
> flag the Editor's "Render Graph" panel can surface. See
> `CAMPAIGN_COMPLETION_REPORT.md` for the full seven-phase writeup.

Adjust wording/exact details to match whatever PHASE1-6 actually ended up
shipping (per their completion reports) — do not just copy the paragraph
above verbatim if any detail drifted during implementation.

Also check `README.md`'s "Status" section (per this repository's own
general convention of a status bullet per shipped campaign) — since this is
explicitly an INTERNAL, non-feature-visible refactor (unlike every other
campaign that earns a Status bullet), use `ask_questions` to confirm with
the project owner whether a `README.md` bullet is warranted here at all, or
whether (matching the AGENTS.md decision already locked) this stays purely
an `AGENTS.md`-level note with no README-visible entry.

### 3.3 — Full Tier-1 test sweep

Confirm every Tier-1 test file this campaign touched or added
(`RenderGraphNameSlotTableTests.cpp`, `RenderGraphSnapshotTests.cpp`,
`RenderGraphCompilerTests.cpp`, `RenderGraphBuilderTests.cpp`,
`RenderGraphTypesTests.cpp`, and any new file like
`RenderGraphPassContextTests.cpp` from PHASE3 if it was added) is present,
compiles, and is included in the test target's build — a quick
`search_in_dir`/`browse_dir` sweep of `tests/Renderer/RenderGraph/` plus a
check of `tests/CMakeLists.txt` (confirm every new/renamed test file is
actually registered there — a new test FILE that isn't added to
`CMakeLists.txt` silently never runs, which would be a false "all green"
if this step is skipped).

### 3.4 — Full build

```
cmake --build build
```
(Working directory: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`.)
Confirm zero errors, and scan warnings output for anything new this
campaign's own changes introduced (in particular, confirm no new
"unreachable code"/"not all control paths return a value" warnings from
PHASE6's `DispatchByKind()` unreachable-fallback line, and no new
sign-comparison/narrowing warnings from PHASE4's adjacency-list index
types).

### 3.5 — Full regression test

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
Confirm the pass count is >= the baseline recorded in 3.1, with genuinely
ZERO regressions (every previously-passing test still passes) — any
newly-failing test is treated as a real regression to diagnose and fix
per `AGENTS.md`'s own rule, never worked around by loosening an assertion.
If a regression is found and its root cause is NOT obviously a small,
local fix, use `delegate_task` to spin up a dedicated fix task rather than
attempting a rushed patch under this phase's own time pressure — per the
top-level task's own Note 4/instructions: diagnose it, then `delegate_task`
a new task to fix whatever is broken, rather than trying to silently patch
around it here.

### 3.6 — Live, HTTP-driven smoke test

Using `run_app_background` to launch the engine (find its built executable
under `build/`, confirm the exact path/name from a `browse_dir` of the build
output directory) and `gte_send_request` to drive it:

1. `GET /get_game_view` and `GET /get_swapchain` — confirm both return real
   images, visually consistent with what any PRE-campaign screenshot would
   show for the same default scene (no visual regression from this whole
   campaign's changes).
2. Fetch `GET /get_logs` — confirm no new unexpected warnings/errors appear
   during normal startup/a few seconds of running (in particular, confirm
   PHASE1's new slot-budget-overflow warning does NOT fire spuriously under
   normal operation — it should only ever fire if a real overflow occurs,
   which should not happen at this engine's current real pass counts).
3. If the Editor's "Render Graph" panel is reachable via the existing
   `GET /activate_tab`/`GET /list_tabs` endpints, activate it and confirm
   (via a screenshot, `GET /get_swapchain` or equivalent) that pass names/
   reads/writes/stats still display correctly — proving PHASE1's new
   `timingSlotBudgetExhausted` flag didn't break the panel's existing
   rendering, and that PHASE2/3/5/6's internal changes didn't disturb what
   the panel displays.
4. Use `stop_app_background` to cleanly terminate the engine process once
   done — do not leave it running.

### 3.7 — `CAMPAIGN_COMPLETION_REPORT.md`

Write the definitive campaign record, mirroring
`task_manager/render-pass-5/CAMPAIGN_COMPLETION_REPORT.md`'s own shape:
what each of the 7 phases actually shipped (pulling from each
`PHASEn_COMPLETION_REPORT.md`, including any dedicated-double-check
corrections folded into PHASE4/5/6's own files), the final `ctest` pass
count (before/after comparison against 3.1's baseline), confirmation of the
byte-identical `executionOrder` proof (PHASE4), confirmation of the
zero-`default:`-case exhaustiveness proof (PHASE6), any deviation from
`PHASE0_MASTER_STRATEGY.md`'s original plan and why, and an honest "what
remains genuinely open" section (e.g. P2's `PassDesc` value type and P3's
three opportunistic items remain explicitly out of scope and unshipped by
this campaign, per its own Non-Goals).

### 3.8 — What NOT to do in this phase

- Do NOT implement any P2/P3 item (`PassDesc`, resource-pool indexed lookup,
  `PassRecord::DebugMetadata` grouping, barrier-branch de-duplication) —
  those remain explicitly out of scope for `render-pass-6` regardless of how
  tempting they look once P0/P1 has landed cleanly.
- Do NOT silently loosen any test assertion to force a green `ctest` run —
  diagnose and fix, or delegate a fix task, per 3.5.
- Do NOT add a new `AGENTS.md` heading — Locked Design Decision 6 is a short
  note appended to the EXISTING "Render Pass System" section only.

## Definition of Done for this phase (and the whole campaign)

- `AGENTS.md`'s "Render Pass System" section carries the new short note;
  `README.md`'s "Status" section is updated only if `ask_questions`
  confirms it should be.
- Every Tier-1 test file this campaign touched/added is registered in
  `tests/CMakeLists.txt` and passes.
- `cmake --build build` succeeds cleanly, default configuration,
  `GTE_ENABLE_EDITOR` ON.
- `ctest` passes with zero regressions against the 3.1 baseline.
- The live HTTP-driven smoke test (3.6) confirms visually-unchanged
  rendering and a clean log stream.
- `CAMPAIGN_COMPLETION_REPORT.md` is written and is the accurate,
  definitive record of the whole `render-pass-6` campaign.
- Changes are committed via `git_add`/`git_commit`.

**Remember**: use `ask_questions` for the `README.md` "Status" bullet
question (3.2) and for anything else genuinely ambiguous this document
doesn't already resolve.
