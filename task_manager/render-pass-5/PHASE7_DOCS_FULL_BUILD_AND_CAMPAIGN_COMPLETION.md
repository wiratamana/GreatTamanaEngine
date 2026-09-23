# PHASE7 — Docs, Full Build/Test, Campaign Completion

## Parent
`PHASE0_MASTER_STRATEGY.md` — read it first. Also re-read `PHASE1`–`PHASE6`'s
own completion reports in full before starting — this phase's own
documentation must accurately reflect every real decision/deviation those
reports recorded, not this planning document's own original assumptions
where the two disagree.

## Step 1: The Goal (Where are we going?)

Close the campaign: update this repository's own living documentation to
describe the new capability (matching every prior campaign's own precedent —
`AGENTS.md`/`README.md` both get a new section/bullet), run the ONE full
build + full regression pass this whole campaign has been deferring (per
Locked Design Decision 9, PHASE0), perform a final live smoke test, and write
the campaign-level completion report.

## Step 2: The Situation (Where are we now?)

- `AGENTS.md` has a dedicated section per major subsystem/campaign (see its
  "Render Pass System", "GPU Vertex Skinning", "Multi-Render-Target (MRT) /
  G-Buffer Support" sections for the exact structure/tone to mirror) — this
  campaign needs its own, e.g. "GPU-Driven Rendering (Frustum Culling +
  Indirect Draw)", summarizing: the batching eligibility rule (Locked Design
  Decision 7), the new `ResourceAccess::VertexShaderStorageRead` enumerator,
  the new `VertexLayout::PositionNormalInstanced`, and a pointer to
  `task_manager/render-pass-5/PHASE0_MASTER_STRATEGY.md` for full detail —
  mirroring every existing section's own "full convention:
  `docs/conventions/...md`" closing-line pattern if this campaign's own scope
  warrants a new `docs/conventions/` file (decide based on how much of this
  belongs in `AGENTS.md` directly vs. a linked convention doc, matching how
  smaller campaigns like `render-pass-2` stayed inline while bigger ones like
  "GPU Vertex Skinning" got a dedicated `docs/conventions/
  gpu-vertex-skinning.md`).
- `README.md`'s "Status" section is a reverse-chronological list of shipped
  features, one bullet (often a long, detailed paragraph) per campaign — see
  the existing "mrt-1"/"logger-1"/"render-pass-3" entries for the exact
  level of detail/tone expected. Add a new bullet at the TOP of that list
  (most recent first).

## Step 3: The Plan

### 3.1 — Documentation updates

- `AGENTS.md`: new section (placement: alongside "Render Pass System"/"GPU
  Vertex Skinning", in whichever position keeps related sections logically
  grouped — decide during implementation).
- `README.md`: new "Status" bullet, written in the same voice/detail level as
  the existing entries — accurately describing what actually shipped
  (cross-check against every `PHASEn_COMPLETION_REPORT.md`, not this
  document's own original plan, wherever they disagree — an honest,
  as-built description, exactly like `render-pass-3`'s own README entry
  explicitly calls out its "LOUD, DELIBERATE DEVIATION" from its own original
  design brief).
- Update `task_manager/GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`'s
  own "Step 0: Status Update" section with a final note that Phase C onward
  is now closed by this campaign (`render-pass-5`), mirroring how that same
  document's Phase A/B were marked "ALREADY SHIPPED" once the compute-shader
  campaign closed them — do the same courtesy for the next reader of that
  historical document.
- Update `task_manager/RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`'s
  Section B.2/C.2/C.3/D — mark the buffer-side cross-pass read, indirect
  draw support, and GPU culling items as ✅ DONE, mirroring the exact
  "~~struck-through~~... CLOSED, see..." editing convention already used
  throughout that same file for the texture-side equivalents.
- Update `task_manager/COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`'s
  Section C.1 (item 1) the same way.

### 3.2 — Full build + full regression

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Record the exact `ctest` pass count before/after this campaign (mirroring
every prior campaign's own "N tests, 100% passing, up from the prior
campaign's own M baseline" reporting convention in `README.md`). Any
newly-failing test is a real regression to diagnose and fix — never loosen a
test's expectation without understanding why it failed first (`AGENTS.md`'s
own testability rule).

If the full build or full `ctest` run uncovers a genuine defect whose fix
belongs in an earlier phase's own file(s) (not a pure documentation/process
fix), **do not silently patch it inline as part of "phase 7 docs work"** —
delegate a small, clearly-scoped, dedicated `delegate_task` to fix it,
referencing exactly which earlier phase's own file(s) are affected, per this
whole campaign's Note 4 working agreement ("diagnose it and delegate a new
task using `delegate_task` to fix something that broke"). That delegated
fix task must also use `ask_questions` for any genuine ambiguity, and must
repeat this same instruction to anything it further delegates.

### 3.3 — Final live smoke test

- Launch the built engine (`run_app_background`).
- Re-run PHASE6's own 3.3 checklist one final time, end-to-end, against the
  FULLY built, FULLY tested binary (not the incrementally-compiled one used
  during PHASE1–6's own individual checks).
- Confirm via `gte_send_request`/`GET /get_game_view`/`GET /get_swapchain`
  that the batch renders correctly and the "instances culled this frame"
  readout responds to camera movement.
- `stop_app_background` once confirmed.

### 3.4 — `CAMPAIGN_COMPLETION_REPORT.md`

Mirror `task_manager/mrt-1/CAMPAIGN_COMPLETION_REPORT.md`'s own shape
exactly: a phase-by-phase summary of what actually shipped (cross-referencing
every `PHASEn_COMPLETION_REPORT.md`), an explicit "what remains genuinely
open" section (occlusion culling, hierarchical culling, LOD selection,
textured/bindless batching, primitive-shape batching, async compute — every
Non-Goal from `PHASE0` restated here as an honest, permanent record, not
silently dropped), and the final `ctest`/build/smoke-test confirmation.

### What We Will NOT Do (this phase)

- No new production code beyond what 3.2's regression run genuinely requires
  to fix (and even then, delegated to a dedicated follow-up task, never
  bundled silently into this phase's own commit).
- No scope additions of any kind — this phase closes the campaign as
  planned, it does not extend it.

### Full build + full test

**This is the one phase that runs the full build and full `ctest` pass** —
see PHASE0's Locked Design Decision 9. Commit the documentation updates, the
completion report, and (if any) a dedicated regression-fix commit separately
and clearly.
