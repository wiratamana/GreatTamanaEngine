# PHASE5 — Documentation, Full Test Sweep, and Final Live Validation

Parent: `PHASE0_MASTER_STRATEGY.md` — **read that file first**, and read
`PHASE1`-`PHASE4`'s own completion reports before starting (this phase is the
whole campaign's final integration checkpoint — every prior decision/snag
recorded in those reports matters here).

**Use `ask_questions` whenever you hit a genuine ambiguity or a design choice
this document (or `PHASE0_MASTER_STRATEGY.md`) doesn't already pin down.** If
you delegate any further sub-task, that delegation prompt must repeat this
same instruction.

## Step 1: The Goal (Where are we going?)

Close out the `mrt-1` campaign: document the new MRT capability where this
project's own conventions expect it, run the full test suite and full build
(the ONE phase in this campaign allowed to do so, per `PHASE0`'s Locked
Design Decision 8), and produce a final, live, running-engine visual
confirmation that ties every earlier phase's claims together into one
end-to-end proof.

## Step 2: The Situation (Where are we now?)

- `AGENTS.md` has an established, per-feature documentation convention (see
  its existing sections, e.g. "Render Target Format Matching", "Profiling",
  "GPU Resource Memory Tracking") — this campaign needs a new section (or a
  clearly-marked addition to an existing relevant one, e.g. wherever the
  Render Graph's own pass-declaration rules are documented) describing:
  how `WriteColorAttachment()` now supports multiple calls per pass,
  attachment-index-equals-shader-location, the 8-attachment cap, and that
  `Pipeline`/`CreatePipeline()` now accept multiple color formats.
- `README.md`'s "Status" section has one bullet per shipped feature,
  following a consistent format — this campaign needs one new bullet.
- `docs/conventions/` (check what already exists — e.g. `networking.md`
  referenced by `logger-1`'s own campaign) may already have a relevant
  rendering-conventions file to extend, or this may be the first
  MRT-specific mention anywhere in `docs/` — confirm which via
  `browse_dir`/`search_in_dir` before deciding where the detailed convention
  write-up belongs; do not invent a brand-new top-level doc file
  speculatively if an existing one is clearly the right home.
- Every prior phase in this campaign explicitly deferred its OWN full build/
  full `ctest` run to this phase (Locked Design Decision 8) — this is the
  first point in the whole campaign those are run for real.

## Step 3: The Plan (How do we get there?)

### 3.1 — Documentation

- Update `AGENTS.md` with the new MRT section/addition described above.
- Update `README.md`'s "Status" section with a new bullet, matching the
  existing bullets' exact tone/format (e.g. compare against whatever bullet
  the compute-shader or Atmosphere Scattering campaigns added for their own
  "Status" entries).
- If a rendering/render-graph-specific conventions doc already exists under
  `docs/conventions/`, extend it; otherwise note in this phase's completion
  report that none exists yet and this campaign's own `AGENTS.md` addition is
  the sole documentation surface (do not create a new conventions file
  speculatively unless this project's own existing pattern clearly calls
  for one for every campaign of this size — check `task_manager/render_graphs/`'s
  own completion reports for precedent on whether the original Render Graph
  campaign created one).
- Cross-reference `task_manager/RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`
  and `Top3-Priority-Features.txt`'s own MRT section — if either explicitly
  lists MRT as "not yet implemented"/a future gap, update it to reflect that
  it is now DONE (with a pointer to `task_manager/mrt-1/`), so the project's
  own gap-tracking documents don't go stale and contradict reality. Confirm
  this is appropriate (rather than leaving historical documents as a
  point-in-time snapshot) via `ask_questions` if genuinely unsure of this
  project's own convention here — check whether OTHER shipped campaigns
  (e.g. compute-shader, once shipped) updated their own "not yet
  implemented"-style predecessor documents, and mirror whatever precedent is
  found.

### 3.2 — Final Tier-1 test sweep

- Re-run every test added in PHASE1 (`RenderGraphBuilderTests.cpp`/
  `RenderGraphCompilerTests.cpp`/`RenderGraphSnapshotTests.cpp` additions)
  and confirm they still pass against the FINAL state of the code (PHASE2/3/4
  may have shifted exact field names/shapes slightly from PHASE1's original
  sketch — reconcile any drift here rather than leaving stale test
  expectations).
- Add any test this campaign is still missing to close out how-to Stage 6
  item 18 in full: at minimum, one more `RenderGraphCompilerTests.cpp` case
  confirming a pass with a `ColorAttachmentDesc` list AND a separate
  non-attachment `WriteTexture()`/compute write on a DIFFERENT pass in the
  same graph still culls/orders correctly (a light regression guard against
  the "writes order doesn't necessarily match colorAttachments order" risk
  `PHASE1`'s own `ColorAttachmentDesc` doc comment calls out).
- Confirm `Pipeline`'s own (if any exist) unit/integration tests still pass
  — check `tests/Renderer/` for any `PipelineTests.cpp`-equivalent; if none
  exists (Pipeline needs a live `VkDevice`, so it may have none today), note
  this explicitly rather than assuming coverage that doesn't exist.

### 3.3 — Full build + full regression

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Both must succeed. If `ctest` reveals a failure anywhere in the suite
(not just this campaign's own new tests — a full regression can surface an
unrelated pre-existing flake or a genuine regression this campaign
introduced elsewhere), diagnose it: if it is clearly caused by this
campaign's own changes, fix it directly, in the phase file/module where the
root cause actually lives (not a band-aid in this phase); if it is
genuinely unrelated to anything this campaign touched, document it in this
phase's completion report and use `ask_questions` before deciding whether to
fix it here or leave it for a separate, dedicated follow-up task — per this
project's own established working agreement (mirrors `logger-1`'s identical
phrasing), diagnosing a real failure found by the one full-build/full-test
phase is expected work for that phase, not something to silently skip.

### 3.4 — Final live, running-engine visual proof

Using `run_app_background` to launch the built `GreatTamanaEngine.exe` and
`gte_send_request`/`load_image` to inspect it without blocking:

1. Launch the engine, confirm it boots (a screenshot via
   `/get_game_view`/`/get_swapchain` showing the normal, unmodified Game View
   — proving Locked Design Decision 4/the campaign's whole "no default
   behavior change" promise one final time, end-to-end, in the fully
   integrated build).
2. Toggle the Scene panel's new "Show GBuffer Validation (debug)" checkbox on
   (via whatever this project's existing HTTP command-bridge mechanism is
   for driving Editor UI state remotely — check
   `EditorUiCommandBridge`/`GET/POST` routes already used by other
   campaigns' own smoke tests, e.g. `logger-1`'s `PHASE5` or
   `frame-debugger-*`'s own end-to-end checks, for the exact existing
   mechanism to reuse here, rather than inventing a new one).
3. Fetch and visually confirm (via `load_image`) that the albedo and normal
   G-buffer outputs are real, independently distinct images — the same
   proof `PHASE4`'s own Definition of Done already required, re-confirmed
   here against the FINAL, fully-integrated, fully-built binary.
4. `stop_app_background` to clean up.

### Definition of Done for the whole campaign (final checkpoint)

- Every item in `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done for the
  whole campaign" section is independently re-confirmed true here, against
  the final build.
- `cmake --build build` and `ctest -C Debug --output-on-failure` both pass in
  full.
- `AGENTS.md`/`README.md` updated; any stale "not yet implemented" tracking
  document updated per Step 3.1.
- `CAMPAIGN_COMPLETION_REPORT.md` written at
  `task_manager/mrt-1/CAMPAIGN_COMPLETION_REPORT.md`, summarizing every
  phase's outcome, any deviation from this document set's original plan (and
  why), and the final visual-proof evidence from Step 3.4.
- Git: stage and commit everything (source changes + every `.md` report +
  this final report) with a clear, descriptive commit message.
