# PHASE1 — Live Reproduction & Root Cause Diagnosis of the `AtmosphereAerialPerspectiveCompositePass` Lie

Parent: `PHASE0_MASTER_STRATEGY.md` (MUST READ FIRST — locked decisions in
its Step 3.1 apply here without repetition).

This is the FIRST phase of the campaign. There is no prior phase completion
report to read yet.

---

## Step 1: The Goal (Where are we going?)

Produce a **mechanically captured, log-backed, unambiguous** answer to: "why
does disabling `AtmosphereAerialPerspectiveCompositePass` via the Render
Graph panel/HTTP not stop it from executing and appearing in the Frame
Debugger?" No fix in this phase — only instrumentation (real code, committed)
and a written root-cause finding other phases can build on. Getting this
wrong wastes the whole rest of the campaign, so this phase must not guess: it
must PROVE its conclusion with captured `GET /get_logs` output quoted
verbatim in its own completion report.

## Step 2: The Situation (Where are we now?)

Re-read `PHASE0_MASTER_STRATEGY.md` Step 2.1 in full — it lists five concrete
hypotheses for the root cause. Every one of them is plausible from a static
read of the code; none has been confirmed or ruled out yet. The relevant
files are:

- `src/Renderer/RenderGraph/RenderPassToggleRegistry.h/.cpp` — the toggle
  registry itself (`NoteDeclaredAndCheckEnabled()`, `SetEnabled()`,
  `IsEnabled()`, `ListAll()`).
- `src/Core/Core.cpp` — `RegisterOffscreenRenderPipelineProviders()`'s
  `"AtmosphereComposite"` provider (search for that literal string), which
  calls `NoteDeclaredAndCheckEnabled("AtmosphereComposite")` BEFORE calling
  `AddAtmosphereCompositePass()`.
- `src/Application/AtmospherePassSequence.cpp` — `AddAtmosphereCompositePass()`,
  a thin forwarder into `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`.
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` — the actual
  `AddAerialPerspectiveCompositePass()` method (~line 778), which calls
  `NoteDeclaredAndCheckEnabled("AtmosphereAerialPerspectiveCompositePass")` a
  SECOND time, under a DIFFERENT name than the provider-level check above.
- `src/Editor/Panels/RenderGraphPanel.cpp` — `BuildDisabledBuiltInPassesSection()`
  (the checkbox the user clicked) and `BuildPassRow()` (the per-pass row in
  the two enabled regime tables above it).
- `src/Editor/EditorHost.cpp` (~line 693-706) — inside the main-loop's
  render-graph-control bridge pump, the
  `case RenderGraphControlCommandKind::SetBuiltInPassEnabled:` block (there is
  NO separate function literally named `SetBuiltInRenderPassEnabled()` anywhere
  in the codebase — that exact name only exists inside a doc comment in
  `RenderPassToggleRegistry.h`; treat that comment as slightly stale and use
  this real inline switch-case as the actual call site) that calls
  `m_core.GetRenderPassToggleRegistryMutable().SetEnabled(rgcRequest->setPassEnabled.name,
  rgcRequest->setPassEnabled.enabled)` and stores the boolean result into
  `rgcResult.success` (a deny-listed name — e.g. `"Present"`/`"ClearViewTarget"`
  — makes `SetEnabled()` return `false` here; confirm the HTTP response/panel
  actually surfaces that rejection instead of silently claiming success).
- `src/Network/NetworkRoutes.cpp`/`NetworkServer.cpp` — `GET
  /render_graph/set_pass_enabled` route, and `RenderGraphControlCommandBridge`
  (search for it) that actually applies the mutation on the main thread.
- `src/Editor/FrameDebuggerData.cpp` — the tree-builder that produces the
  "Compute Dispatches (Post-GameView)" section (search for that literal
  string, ~line 780) and the `RenderGraphSnapshot`/`isCulled` semantics it
  reads.

## Step 3: The Plan

### 3.1 — Reproduce, live, with zero ambiguity, BEFORE touching any code

1. Build incrementally first (`cmake --build build`, from the project root)
   to get a known-good baseline binary — do not skip this even though no
   code has changed yet; confirms the environment is sane before you start.
2. Launch it: `run_app_background` on `build\GreatTamanaEditor.exe`.
3. Drive the WHOLE repro purely over HTTP (never rely on a mouse click you
   cannot script/replay):
   - `gte_send_request` `GET /frame_debugger/open`
   - `gte_send_request` `GET /frame_debugger/enable?value=true`
   - `gte_send_request` `GET /render_graph/passes` — record the CURRENT
     `enabled` state of `AtmosphereAerialPerspectiveCompositePass` (expect
     `true`, the default).
   - `gte_send_request` `GET /frame_debugger/capture` — force one fresh
     capture with the pass still enabled.
   - `gte_send_request` `GET /frame_debugger/state` — confirm the event tree
     shows `AtmosphereAerialPerspectiveCompositePass` present (baseline:
     confirmed present while enabled — expected, not the bug).
   - `gte_send_request` `GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=false`
     — the exact mutation the user's checkbox performs.
   - `gte_send_request` `GET /render_graph/passes` again — confirm the JSON
     now reports `enabled: false` for this pass (if it does NOT, that is
     ALREADY the root cause — the mutation itself silently fails or targets
     the wrong entry; stop here and skip straight to 3.3's instrumentation
     around `SetEnabled()`/the network route/bridge instead of the
     declare-time guard).
   - Wait at least 2-3 real frames (a short `run_shell timeout` sleep is fine,
     or just issue the next capture request — the important thing is at
     least one full frame has elapsed with the registry already reporting
     `enabled: false`).
   - `gte_send_request` `GET /frame_debugger/capture` — force a FRESH capture
     AFTER the toggle, explicitly ruling out the "stale capture" hypothesis
     from PHASE0 Step 2.1.
   - `gte_send_request` `GET /frame_debugger/state` — inspect the event tree.
     **This is the decisive check**: does `AtmosphereAerialPerspectiveCompositePass`
     still appear as a real, populated leaf?
   - Also pull `gte_send_request` `GET /get_game_view` and visually confirm
     whether aerial perspective compositing is still visibly applied to the
     rendered image (a second, independent, pixel-level confirmation beyond
     the JSON tree).
4. Record every one of the above request/response pairs verbatim in this
   phase's own completion report's "Reproduction Log" section — this is the
   evidence trail every later phase and the final campaign report will cite.

### 3.2 — If the mutation itself is confirmed broken (registry never flips to `enabled: false`)

Add temporary `GTE_LOG_DEBUG("RenderPassHonestyDiag", ...)` calls (category
string exactly `"RenderPassHonestyDiag"` — temporary, PHASE2 removes this
category once root cause is fixed, unless PHASE5 finds permanent value in
keeping a trimmed version) at:
- `RenderPassToggleRegistry::SetEnabled()` — log `name`, requested `enabled`,
  whether `IsDenyListed()` rejected it, and the entry's state before/after.
- The network route handler for `/render_graph/set_pass_enabled`
  (`NetworkRoutes.cpp`) — log the raw parsed query name/enabled value before
  it reaches the bridge, so a URL-encoding/parsing bug (e.g. the pass name
  containing no special characters, but confirm anyway) is ruled out.
- Whatever main-thread pump actually calls `SetEnabled()` for this route
  (`RenderGraphControlCommandBridge` or equivalent — locate it via
  `search_in_dir` for `set_pass_enabled` inside `src/`) — confirm it runs on
  the SAME `Core`/registry instance the render loop reads (not a copy, not a
  second `Core` instance in some test/headless path).

Rebuild incrementally, repeat the 3.1 repro script, and capture the resulting
`GET /get_logs?category=RenderPassHonestyDiag` output.

### 3.3 — If the mutation IS confirmed correct (registry does flip to `enabled: false`) but the pass still executes

This means the DECLARE-TIME guard is the problem. Add temporary
`GTE_LOG_DEBUG("RenderPassHonestyDiag", ...)` calls at:
- `Core.cpp`'s `"AtmosphereComposite"` provider lambda — log the return value
  of `NoteDeclaredAndCheckEnabled("AtmosphereComposite")` EVERY frame (or at
  least whenever it changes), and whether the lambda actually proceeds past
  that guard into `AddAtmosphereCompositePass()`.
- `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()` — log the
  value of `passEnabledThisFrame`, `aerialPerspectiveVolumeHandle.IsValid()`,
  and the final `ShouldDeclareAtmospherePassThisFrame(...)` result, EVERY
  call, unconditionally (this function already returns early on `false`, so
  logging right before that `if` is enough to see both branches over time).
- `RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled()` itself — log
  `name` and the returned `enabled` value on every call (this will be noisy —
  fine, temporary) to see definitively whether this exact literal string,
  `"AtmosphereAerialPerspectiveCompositePass"`, is being looked up with the
  registry correctly reporting `false`, or whether a DIFFERENT code path
  (a second, undiscovered declare site — `search_in_dir` for
  `"AtmosphereAerialPerspectiveCompositePass"` and for
  `AddAerialPerspectiveCompositePass` across all of `src/` to make sure there
  is truly only the one call site already identified in Step 2 above) is
  responsible for what the Frame Debugger actually shows.
- If a second call site is found (e.g. something added by a later campaign
  that duplicates this pass, or a Frame-Debugger-only replay/preview pass
  that happens to reuse the exact same debug name — check
  `src/Editor/FrameDebuggerReplayPasses.cpp` explicitly for this, since
  `AGENTS.md` documents that file's passes are DELIBERATELY never migrated
  onto the toggle-registry-aware system) — that IS very possibly the root
  cause, and must be logged/confirmed explicitly, quoting the exact debug
  name string it declares and comparing it byte-for-byte against what
  `FrameDebuggerData.cpp` groups under "Compute Dispatches (Post-GameView)".

Rebuild incrementally, repeat the 3.1 repro script with instrumentation live,
and capture the resulting `GET /get_logs?category=RenderPassHonestyDiag`
output covering at least 3 consecutive frames spanning the toggle-off moment.

### 3.4 — If `FrameDebuggerData.cpp`'s tree-builder itself is the culprit

If the two checks above both prove the pass is genuinely NOT declared into
the graph this frame (i.e. `RenderGraphSnapshot::passesInExecutionOrder`
truly has no entry for it), yet the Frame Debugger's own displayed tree still
shows it, the bug is inside `FrameDebuggerData.cpp`'s own tree-construction
logic (search for `"Compute Dispatches (Post-GameView)"`, ~line 780, and the
surrounding grouping loop) — possibly a caching/staleness bug where a
PREVIOUS frame's leaf list is reused/merged with the current one instead of
being rebuilt fresh every capture. Add `GTE_LOG_DEBUG("RenderPassHonestyDiag", ...)`
logging the exact size and pass-name contents of
`graphSnapshot.passesInExecutionOrder` at the moment `FrameDebuggerData`
builds its tree, immediately before and after the toggle-off moment, and
compare that list directly against what `GET /frame_debugger/state` reports
afterward.

### 3.5 — Ambiguity checkpoint

If, after following 3.2-3.4, the evidence points to something structurally
different from every hypothesis in PHASE0 Step 2.1 (e.g. a genuine Vulkan
synchronization/timing artifact, or a caching layer nobody anticipated),
STOP and use `ask_questions` before deciding how to proceed into PHASE2 —
this is exactly the kind of "I found something unexpected, a design choice is
needed" moment the user asked to be consulted on.

### 3.6 — Wrap-up

1. Remove any instrumentation that turned out to be a dead end (keep only
   what actually contributed to the final root-cause finding, still under the
   `"RenderPassHonestyDiag"` category, still temporary — PHASE2 will remove
   or permanently fold in whatever remains once the real fix lands).
2. Incremental compile check (`cmake --build build`) must succeed.
3. Write `PHASE1_COMPLETION_REPORT.md` in this same folder with: the full
   reproduction log (Step 3.1), the exact confirmed root cause, the exact
   file/line/function responsible, and a direct pointer to which hypothesis
   from `PHASE0_MASTER_STRATEGY.md` Step 2.1 turned out to be correct (or a
   new one, if none matched).
4. `git_add` + `git_commit` (message should name the confirmed root cause,
   e.g. "editor-core-separation-21 PHASE1: root cause diagnosis of
   AtmosphereAerialPerspectiveCompositePass toggle lie").
5. `stop_app_background` the running engine instance before finishing.
6. Do NOT call `delegate_task` from inside this phase (see PHASE0 Step 3.1,
   rule 6). If something needs delegating, say so in the completion report
   and let the orchestrator handle it.
