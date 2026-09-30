# PHASE5 — Full Regression, Acceptance Audit, and Campaign Closeout

Parent: `PHASE0_MASTER_STRATEGY.md` — **MUST READ FIRST**. Also read
PHASE1-4's own `.md` files and ALL FOUR of their completion reports before
starting.

This is the ONLY phase in this campaign allowed to run a full clean build
and a full `ctest` regression pass (Locked Decision 6, `PHASE0`).

## Step 1: The Goal

Re-confirm, with FRESH evidence gathered in this phase (never copy-pasted
from an earlier phase's own report), every single acceptance-criteria
checkbox from the source design document's own Section 8 — adapted where
`PHASE0`'s own Locked Decision 1 (the two-interface split) changes the
literal wording — then write the campaign's final `CAMPAIGN_COMPLETION_REPORT.md`.

## Step 2: The Situation

By this point:
- `PassRecord` no longer stores `category`/`drawKind`/`tags` (PHASE3).
- `ViewScope` is `: std::uint8_t` (PHASE3).
- `IPassDebugMetadataSink`/`IPassDebugMetadataProvider` exist, Core-owned,
  opaque (PHASE1).
- `FrameDebuggerPassMetadataRecorder` implements both, Editor-owned
  (PHASE2).
- `RenderGraphBuilder::AddRenderPass()` writes to the sink;
  `BuildRenderGraphSnapshot()`/`BuildPassSnapshot()` read from the
  provider via a caller-supplied lookup (PHASE3).
- `RenderGraph` holds both pointers, wires `Execute()`/
  `ExecuteCompiledGraph()` accordingly, and `EditorHost` installs the one
  persistent recorder at startup (PHASE4).

This phase does not add new production code. It audits, tests, and closes
out.

## Step 3: The Plan

### 3.1 — Re-confirm every acceptance criterion (source doc, Section 8)

Go through EACH item below individually, gather fresh evidence for each
one (a direct code quote with file/line, a build/test output excerpt, or a
live capture), and record the evidence in this phase's own completion
report. Do not mark anything done without gathering the evidence yourself
in THIS phase, even if an earlier phase's own report already claims it.

1. `sizeof(PassRecord)` measurably shrinks — confirm by direct code
   inspection that `category`/`drawKind`/`tags` are genuinely absent from
   `RenderGraphTypes.h`'s `PassRecord` struct today, and that `kind`/
   `viewScope` are confirmed unchanged in type, position, and every call
   site that stamps them (a quick grep of `AddPass()`/`AddComputePass()`
   confirming they are byte-for-byte identical to before this campaign
   started is the concrete proof here).
2. `FrameDebuggerPassMetadataRecorder` (the REAL implementation of
   `IPassDebugMetadataSink`/`IPassDebugMetadataProvider`) has ZERO
   references anywhere outside `src/Editor/` — this codebase has no
   `GTE_ENABLE_EDITOR` preprocessor macro anymore (confirmed by grep —
   every remaining mention is a historical comment saying it "no longer
   exists anywhere in this codebase"; see AGENTS.md's "`gte_core` /
   `gte_editor` Library Separation" section), so do NOT go looking for a
   CMake option to toggle it — there isn't one. Instead, use the TWO
   real, already-existing, purpose-built mechanisms this exact repository
   maintains for proving `gte_core` never references `gte_editor`:
   `tools/ci/gte_core_standalone_probe/` (configures and builds ONLY the
   `gte_core` static library — zero `gte_editor`, zero SDL3, zero ImGui —
   see its own `README.md` for the exact `cmake -S
   tools/ci/gte_core_standalone_probe -B build-core-probe && cmake --build
   build-core-probe --target gte_core` invocation) and, for a strictly
   stronger LINK-time proof (an archive build alone cannot catch an
   unresolved-symbol reference), `tools/ci/gte_core_player_link_probe/`
   (links a real, standalone executable against `gte_core.a` alone). Run
   BOTH by hand as this phase's own fresh evidence — a genuine
   `gte_core -> gte_editor` symbol dependency introduced by this campaign
   would fail one of these two probes' own build/link step immediately,
   with a real compiler/linker error, not just a grep miss. Also do a
   clean `search_in_dir` audit for `FrameDebuggerPassMetadataRecorder`
   across `src/`, confirming every hit is under `src/Editor/` (i.e.
   compiled exclusively into the `gte_editor` static library target, never
   `gte_core`'s) — belt-and-suspenders alongside the two probes above, not
   a substitute for running them.
3. The Editor's "Render Graph" panel / Frame Debugger shows byte-for-byte
   identical output before and after this change, for the same captured
   frame — PHASE4 already gathered one live capture; gather a SECOND,
   independent one here (a different pass, or the same pass captured
   again after a full clean rebuild) as this phase's own fresh evidence.
4. Every Tier-1 test identified under TR3 that asserts on `category`/
   `drawKind`/`tags` is updated and passing (PHASE3's 4 rewritten tests);
   every Tier-1 test that asserts on `kind`/`viewScope` directly (via
   `AddPass()`/`AddComputePass()`, including the low-level,
   non-`AddRenderPass()` call sites) is confirmed, by direct inspection,
   to be completely unmodified and still passing — diff
   `RenderGraphBuilderTests.cpp`/`RenderGraphSnapshotTests.cpp` against
   this campaign's own starting point (e.g. `git log`/`git diff` against
   the commit immediately before PHASE1) to confirm no `kind`/`viewScope`
   assertion was touched anywhere.
5. `FrameDebuggerPassMetadataRecorder`'s own table is confirmed, by direct
   code inspection, to be a plain, contiguously index-addressed
   `std::vector` appended to via `push_back()` — never a container
   searched/found by key.
6. `IPassDebugMetadataSink` is confirmed, by direct code inspection, to
   expose exactly two pure-virtual methods (`OnPassDeclared()`/
   `BeginFrame()`); `IPassDebugMetadataProvider` is confirmed to expose
   exactly one (`QueryPassDebugMetadata()`) — this is `PHASE0`'s own
   Locked Decision 1 adaptation of the source document's original,
   single-interface wording. `RenderGraph::Execute()`'s own template body
   is confirmed to call `BeginFrame()` once, unconditionally, immediately
   after constructing that call's own fresh `RenderGraphBuilder` and
   before `build(builder)` ever runs; `OnPassDeclared()` is confirmed
   called indirectly, once per declared pass, via the builder it just
   installed the sink into.
7. A single `RenderGraph::SetDebugMetadataSink()`/`SetDebugMetadataProvider()`
   call, made once at Editor startup (`EditorHost`'s constructor, PHASE4),
   is sufficient for every later offscreen/present, synchronous/pipelined
   `Execute()` call that session to correctly populate the sink for its
   own graph — confirmed by PHASE3's own
   `SharedSinkAcrossTwoDeclarationCyclesNeverLeaksStaleEntries` test
   (re-run it here, in this phase, as fresh evidence) PLUS this phase's own
   live capture (3.1.3 above). **Known, honest wrinkle when gathering this
   live evidence**: today's real "present" (pipelined) regime declares
   exactly ONE pass, `"Present"` (`src/Application/RenderPasses.cpp`'s
   `AddPresentPass()`), via the plain `AddRenderPass(name, kind, viewScope,
   category, setup, execute)` call with every trailing `drawKind`/`tags`
   argument left at its own default — so `"Present"`'s own
   `category`/`draw_kind`/`tags` will legitimately read
   `"General"`/`"DrawMesh"`/`0` in the JSON EVEN WHEN this whole mechanism
   is wired correctly, which means the present regime ALONE cannot
   distinguish "correctly served from the recorder" from "silently
   defaulted because nothing is installed" — do not be misled by this into
   thinking the present regime is somehow unverified. The actual required
   evidence is: (a) the SAME captured frame's OFFSCREEN regime shows a
   real, NON-default value for at least one of its own passes (e.g.
   `"DrawSkyBackground"` → `draw_kind: "DrawQuad"`) — proving the recorder
   is genuinely populated and read back correctly for that regime's own
   `Execute()` call — and (b) the present regime's own `"Present"` entry is
   still PRESENT and structurally well-formed in the same JSON response
   (not missing, not null, not an error) with its own honestly-default
   values — proving the SAME shared recorder/lookup path served a SECOND,
   independent `Execute()` call in the same session without crashing or
   returning stale data from the first regime. Both regimes sharing one
   installed recorder object is what TR4/Locked Decision 1 actually
   requires — it does not require every regime to happen to contain a
   non-default-tagged pass, and none of this campaign's own scope adds one
   to the present regime.
8. `ViewScope`'s underlying type is confirmed, by direct inspection of
   `RenderGraphTypes.h`, to be an explicit `std::uint8_t` (TR6).
9. Full `ctest` regression pass, zero unexplained delta (Step 3.3 below).

### 3.2 — Full clean build

```
cmake --build build
```
(A genuinely full rebuild — if `build/` already has fresh incremental
artifacts from PHASE1-4, consider whether a `cmake --build build --clean-first`
or an equivalent full reconfigure is warranted here, per this phase's own
special "only phase allowed to do this" permission — use judgment, the
goal is a build that exercises EVERY translation unit this campaign
touched from a clean state at least once before declaring victory, not
performing pointless extra work for its own sake.)

### 3.3 — Full regression

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Confirm: 100% of executed tests pass, and the total test COUNT is greater
than or equal to what it was before this campaign started (this campaign
only ever ADDS tests — PHASE2's new recorder tests, PHASE3's rewritten +
new tests — never removes one). If ANY test fails:

1. Read the failure output carefully — is it a genuine regression this
   campaign's own changes caused, or an unrelated, pre-existing flake?
2. If it is a genuine regression, use `delegate_task` (mirroring Locked
   Decision 8's own `position: "next"` rule — this counts as "double-
   checking this phase's own just-finished work" even though the bug
   likely originates in an earlier phase's diff) to diagnose and fix it.
   Do NOT simply note the failure and move on — this phase's own
   Definition of Done requires a genuinely green full regression.
3. Re-run the full `ctest` pass after any fix, until it is clean.

### 3.4 — `CAMPAIGN_COMPLETION_REPORT.md`

Write this file into this same folder (`task_manager/editor-core-separation-25/`),
mirroring the depth/structure of prior campaigns' own
`CAMPAIGN_COMPLETION_REPORT.md` files in sibling
`task_manager/editor-core-separation-N/` folders (skim 1-2 of them for
tone/structure before writing this one — do not invent a new report shape
from scratch). At minimum, include:

- A one-paragraph summary of what shipped.
- The full Section 3.1 checklist above, each item with its own gathered
  evidence inline (quotes, output excerpts, capture summaries).
- The final full `ctest` output summary (pass count, zero failures).
- A short "what would the NEXT campaign in this series (BIG STEP 2 of 4 —
  Buffer Roots/Blit) need to know" note, if anything from this campaign's
  own implementation surfaced something worth flagging forward (there may
  be nothing — that is a valid, honest answer too, do not invent a
  concern that does not exist just to fill this section).

### 3.5 — Final commit

`git_add` + `git_commit` covering the completion report (and any fix
commits from Step 3.3 should already have been committed by whichever
`delegate_task` made them, per that sub-task's own Locked-Decision-10
obligation).

If you discover ANY ambiguity not already resolved by this file or
`PHASE0_MASTER_STRATEGY.md` — use `ask_questions` before proceeding. If you
delegate any part of this phase to a sub-task (e.g. the regression-fix
delegation in 3.3.2) — that sub-task MUST also be instructed to use
`ask_questions` for its own ambiguities, and MUST NOT create its own
separate report file (Locked Decision 8) unless it is itself fixing a
genuinely separate, pre-existing, unrelated flake that deserves its own
bug report via the `bug_report` tool instead (a different mechanism
entirely — only for a tool malfunction, never for this campaign's own
code).
