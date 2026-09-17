# CAMPAIGN COMPLETION REPORT — `frame-debugger-7`

Campaign: `frame-debugger-7`. Branch: `feature/frame-debugger-impl` (unchanged
throughout the whole campaign — no branch switch performed at any phase, and
none performed in this final phase either). This report is written as
PHASE7's own Step 3.7 deliverable, mirroring
`task_manager/frame-debugger-6/CAMPAIGN_COMPLETION_REPORT.md`'s shape.

## 1. Recap — the two original bugs and the three Locked Design Decisions

Per `PHASE0_MASTER_STRATEGY.md`, this campaign set out to fix two real,
user-confirmed bugs in the Editor's "Frame Debugger" window:

- **Bug 1 — wrong first capture.** Pressing "Enable" for the first time
  produced a capture missing real per-object rows (e.g. no terrain row at
  all), even though the render graph/pass list itself was fine. Root cause:
  `Application::Run()` arms the capture context BEFORE `Game::Render()` runs,
  but the Enable checkbox is only actually processed LATER that same frame —
  so the very first `TriggerCapture()` call, fired synchronously from the
  click handler, always built its snapshot from an already-stale frame with
  zero recorded draws.
- **Bug 2 — wrong preview image per selected item.** Clicking any object/step
  in the event tree always showed the same whole-frame image (final,
  atmosphere-composited, every object already drawn), never "the screen as it
  looked with only that step applied" — genuine Unity-Frame-Debugger-style
  per-step reconstruction was structurally impossible with the old
  single-shared-target rendering scheme, since intermediate results were
  overwritten by the time any capture ran.

Plus three LOCKED, user-approved product decisions, all explicit BREAKING
CHANGES relative to prior `frame-debugger-*` campaigns:

1. **No multi-frame history** — exactly one captured frame is ever held in
   memory (removes the old 8-slot `FrameDebuggerHistory` ring buffer,
   its Prev/Next toolbar, and `GET /frame_debugger/step_history`).
2. **No artificial per-object safety cap** — every accumulated per-step
   preview is built unconditionally, since this work only ever runs once per
   explicit capture trigger, never every real frame.
3. **Unified accumulated-preview semantics for every leaf** (compute pass OR
   object draw alike) — replaces `frame-debugger-5`'s per-compute-pass
   distinct-texture preview outright.

## 2. Per-phase summary

- **PHASE1 — Remove History, Single-Capture Lifecycle.** `FrameDebuggerHistory`
  (class) renamed to `FrameDebuggerCurrentCapture`, holding exactly one
  `std::optional<FrameDebuggerHistoryEntry>` instead of an 8-slot ring buffer.
  The "Frame History" Prev/Next toolbar, `StepFrameHistoryFromCommand()`,
  `IEditorLayer::FrameDebuggerStepHistory()`, and the whole
  `GET /frame_debugger/step_history` HTTP route (parser, JSON field, response
  view) were removed end-to-end. A new lifecycle rule clears the captured data
  (never the "Enable" checkbox itself) on Disable, and on the first
  paused→running transition observed while still Enabled. `hasCapturedFrame`
  (bool) replaced `historyCount`/`historyCursor` (both `int`) everywhere. No
  ambiguity encountered — verified via `*FrameDebugger*` gtest filter
  (85/85 passed).
- **PHASE2 — Deferred Capture Trigger (fixes Bug 1).** The Enable checkbox's
  false→true edge no longer calls `TriggerCapture()` synchronously; it only
  arms a new `bool m_pendingCaptureAfterEnable`, consumed (read-and-cleared,
  then a real `TriggerCapture()` call) at the very START of the next
  `Build()` call — by which point the capture context was correctly armed for
  that whole intervening frame's rendering. The Step/Capture-button paths were
  confirmed, by code reading, to already share none of Bug 1's root cause
  (both only ever run on a frame where Enable was already `true` beforehand).
  Verified via compile check + `*FrameDebugger*` gtest filter (85/85 passed);
  no full live proof yet (that was explicitly deferred to this phase, per the
  master strategy's own workflow rules).
- **PHASE3 — Unified Step Timeline & Per-Draw Replay Rendering (fixes Bug 2,
  the heaviest/riskiest phase).** `AddFrameDebuggerReplayPasses()`
  (`src/Application/RenderPasses.h/.cpp`) declares N brand-new, debug-only,
  additive Render Graph passes on an explicit capture-trigger frame — one per
  real object the `"GameView"` pass draws that frame — each redrawing objects
  `[0..i]` FROM SCRATCH into its own dedicated destination `RenderTexture`,
  with only the VERY LAST replay pass also drawing the sky background
  (mirroring the real `"GameView"` pass's own true ordering). The Phase 2
  single-bool handshake was generalized into a two-bool
  `m_pendingCaptureTrigger`/`m_replayServicedThisFrame` pending/serviced
  handshake covering all three real capture triggers (Enable-edge, Step,
  Capture button) PLUS the HTTP "Capture" route. **Two real bugs were found
  and fixed during this phase's own mandatory manual visual spot-check** (not
  just code review): (A) the new replay passes' destination textures were
  never added to the render graph's root `outputs` set, so
  `RenderGraphCompiler`'s backward-reachability culling silently skipped their
  `execute` lambdas entirely, leaving genuine uninitialized-VRAM-garbage
  images — fixed by returning `std::vector<rg::TextureHandle>` from
  `AddFrameDebuggerReplayPasses()` and appending every handle to `outputs`;
  (B) `FrameDebuggerPanel::CaptureNowFromCommand()` (the `GET
  /frame_debugger/capture` HTTP route) still called `TriggerCapture()`
  synchronously, silently producing captures with zero replay-step images on
  any HTTP-triggered second/third capture — fixed by routing it through the
  same deferred two-bool handshake as the hand-driven button. Both fixes
  were confirmed correct via a live, HTTP-driven, three-capture spot-check
  (details in PHASE3_COMPLETION_REPORT.md) — this phase's own mandated Step 4
  visual check is exactly why these two implementation oversights (not
  document ambiguities) were caught before they ever reached this final phase.
- **PHASE4 — Preview Wiring & Data Model.** Wires the real, correct
  per-object replay images from Phase 3 into `ChooseFrameDebuggerPreviewSource()`
  via a new `FrameDebuggerStepPreviewKind` enum (`NotYetDrawn`/`PerObjectStep`/
  `PreComposite`/`PostComposite`), computed once per event node inside
  `BuildRealFrameDebuggerSnapshot()`. The `frame-debugger-5`-era
  per-compute-pass-distinct-texture preview mechanism
  (`FrameDebuggerComputePassPreview`, `CollectComputePassTextureWrites()`/
  `CollectComputePassVolumeTextureWrites()`, the whole HDR-round-trip/
  volume-ray-march capture code, `FrameDebuggerCurrentCapture::
  m_volumePreviewRenderer`) was removed outright, per Locked Design Decision
  #3 — an explicit, user-approved breaking change (the raw texture pixels
  remain inspectable elsewhere, e.g. the Render Graph panel / `GET
  /get_texture`; no diagnostic capability was actually lost). `PerObjectStep`
  deliberately NEVER falls back to the whole-frame `compositedPreview`/
  `preview` images — this is what makes Bug 2's fix honest. Verified via
  compile check + `*FrameDebugger*` gtest filter (80/80 passed — net change
  from 85 reflects 10 deleted tests for the removed mechanism plus 6 new
  `stepPreviewKind`/`stepPreviewIndex` tests).
- **PHASE5 — Panel UI & HTTP Cleanup.** A full identifier sweep (9 named
  identifiers) across `src/`/`tests/` plus a `/frame_debugger/*` route-table
  cross-check and a three-message empty/placeholder-state UI audit. **Found
  zero live dead-code hits** — every remaining occurrence of a removed
  identifier was a comment correctly documenting this campaign's own history.
  Zero functional code changes this phase; the audit itself, fully documented,
  was the deliverable. 80/80 `*FrameDebugger*` tests still passed (unchanged,
  since no code was touched).
- **PHASE6 — Tests, Docs & Final Cleanup.** A campaign-wide Tier-1 test audit
  (re-reading every touched test file end to end against every prior phase's
  own completion report) found zero gaps and made zero test-file changes — a
  legitimate, complete outcome, since Phases 1-4 had already added/updated
  their own tests alongside their own code per `AGENTS.md`'s rule. The
  documentation side needed real work: a new "What's new (`frame-debugger-7`
  campaign)" section was added to `docs/conventions/frame-debugger.md`
  (covering both bug fixes, both breaking changes, and the new per-object
  replay-rendering mechanism), several existing sections of that same file
  were rewritten in place to describe the system as it exists today,
  `AGENTS.md`'s "Frame Debugger" summary paragraph was condensed to match this
  campaign's shipped reality, and `TODO.md` gained a new campaign entry plus a
  correction to a now-stale "Per-individual-draw-call event granularity" item.
  80/80 `*FrameDebugger*` tests passed (unchanged, no source edits).
- **PHASE7 (this phase) — Full Build, Live End-to-End Verification, Campaign
  Completion.** See Sections 3 and 4 below.

## 3. Full build and full regression (this phase's own Step 3.1 — the ONLY
phase in this campaign allowed to do this)

- `cmake --build build` (working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — succeeded. Output was
  a single `[1/1] Linking CXX executable GreatTamanaEngine.exe...` step (every
  translation unit was already up to date from Phases 1-6's own incremental
  builds — confirming those phases' own "quick compile check" verifications
  were honest and did not leave anything unbuilt).
- `cmake --build build --target GreatTamanaEngineTests` — `ninja: no work to
  do` (already fully built).
- `ctest -C Debug --output-on-failure` (working directory `build/`) — **100%
  tests passed, 1548 total** (1 test — `PmxLoaderRealModelSmokeTest.
  LoadsAnMmdModelIfPresentOnThisMachine` — reported `Skipped`, which is
  EXPECTED and unrelated to this campaign: it is a machine-dependent smoke
  test that only runs when a real MMD model file happens to be present on the
  developer's own machine, per its own name/documented convention). Total
  test time: 111.10 seconds. Zero failures, zero regressions from ANY prior
  phase's own work — confirming Phases 1-6's incremental verification
  (targeted `*FrameDebugger*` gtest-filter runs) generalizes cleanly to the
  full suite, and that no other engine subsystem was destabilized by this
  campaign's changes.

No fix was needed — nothing failed, so `delegate_task` was never invoked for
a regression fix in this phase.

## 4. Live, HTTP-driven, screenshot-verified end-to-end proof (Steps 3.2-3.5)

### 3.2 — Launch and scene preparation

- Launched `build/GreatTamanaEngine.exe` via `run_app_background` (PID
  12092).
- An existing sample scene was already present
  (`build/Project/TestScene.gtscene`, discovered via `search_in_dir` before
  building anything new) containing exactly the repro shape the phase
  document asked for: a `"SmokeTestCube"` primitive (drawn first, entity
  index 0) and a terrain mesh entity (`"Entity 2"`, drawn second, a real
  multi-part `.gta` mesh asset with no `Name` component — matching
  `frame-debugger-6`'s own already-documented, non-regression naming
  behavior), plus a `"Directional Light"` (`DirectionalLight` component,
  confirming atmosphere/sky rendering is active) and a `Camera`. Loaded via
  `POST /load_scene` with an empty body (`{}`, using
  `Editor::SceneIO::DefaultScenePath()`) →
  `{"resolved_path":"...\\build\\Project\\TestScene.gtscene","success":true}`.
- `GET /get_swapchain` confirmed the scene rendered correctly: the
  "Hierarchy" panel lists `Directional Light`, `Entity 4 (Camera)`, `terrain`
  (with its real mesh-part child `Entity 2`), and `SmokeTestCube`; the "Game"
  panel shows a real, sunlit snow-mountain terrain with a visible
  blue-to-warm sky gradient (atmosphere scattering genuinely active).

### 3.3 — Bug 1 proof (wrong first capture) — LIVE, PASSED

1. `GET /frame_debugger/open` → `{"success":true, "state":{...
   "hasCapturedFrame":false, "totalEventCount":0, "windowOpen":true, ...}}`.
2. `GET /frame_debugger/enable?value=true` → immediately
   `{"enabled":true, "hasCapturedFrame":false, "totalEventCount":0, ...}` —
   **honestly reports nothing captured yet**, exactly as Phase 2's deferred-
   trigger design promises (no synchronous, stale-frame capture happens on
   the click itself).
3. A brief real interval later (the very next tool round-trip — the engine
   keeps rendering every real frame even while paused, per
   `docs/conventions/time-and-playback-pause.md`), `GET /frame_debugger/state`
   → `{"enabled":true, "hasCapturedFrame":true, "totalEventCount":9, ...}`.
4. `GET /get_swapchain` (screenshot evidence) confirmed the event tree now
   shows, on this very first successful capture: `"Compute Dispatches
   (Pre-GameView)"` with all 5 real atmosphere LUT/volume passes,
   `"GameView"` expanded with BOTH `"SmokeTestCube (Entity 0)"` AND `"Entity 2
   (Entity 2)"` (the terrain) as real, individually selectable child leaves,
   and `"Compute Dispatches (Post-GameView)"` with the composite pass — **9
   total events, matching `totalEventCount:9`, with every real object present
   on the very first capture. Bug 1 is confirmed fixed.**

### 3.4 — Bug 2 proof (wrong preview per step) — LIVE, PASSED

Event index numbering (confirmed structurally, per `FrameDebuggerData.cpp`'s
own monotonic `nextEventIndex` assignment order): indices 0-4 = the 5
Pre-GameView compute leaves, index 5 = the `"GameView"` leaf itself, index 6 =
`"SmokeTestCube (Entity 0)"` (first object drawn), index 7 = `"Entity 2
(Entity 2)"` (second/last object drawn), index 8 = the Post-GameView
composite leaf.

1. `GET /frame_debugger/select_event?index=6` (first-drawn object) → `GET
   /get_swapchain`: the preview box shows a mostly dark/clear-color image with
   ONLY the first object's contribution — critically, **no sky/atmosphere
   background at all** (confirmed correct by code reading `RenderPasses.cpp`'s
   `AddFrameDebuggerReplayPasses()`: only the VERY LAST replay pass also draws
   the sky background, mirroring the real `"GameView"` pass's true ordering —
   so step 6, with only 1 of 2 objects drawn, correctly has no sky yet
   either).
2. `GET /frame_debugger/select_event?index=7` (second/last-drawn object) →
   `GET /get_swapchain`: the preview box now shows BOTH objects' accumulated
   result — the terrain mountain AND the base sky gradient (drawn together
   with this last object, per the replay-pass ordering above) — but
   **critically still without the Aerial Perspective haze/fog the composite
   pass alone adds** (this is the pre-composite `"GameView"` state, per
   Locked Design Decision — "PreComposite always shows the raw `Preview`,
   never `CompositedPreview`").
3. `GET /frame_debugger/select_event?index=0` (a Pre-GameView compute leaf,
   `AtmosphereTransmittanceLutPass`) → `GET /get_swapchain`: the preview box
   shows the honest placeholder text **"Nothing drawn yet at this point in
   the frame."** — not a stale/wrong image, not a blank white/garbage
   texture.
4. `GET /frame_debugger/select_event?index=8` (the composite pass's own
   leaf, the LAST leaf in the tree) → `GET /get_swapchain` AND, separately,
   `GET /get_game_view` for a direct side-by-side comparison. **Both images
   are pixel-for-pixel visually identical** — the same real, final,
   atmosphere-composited mountain/sky frame — confirming the Frame Debugger's
   own preview for the true final step matches the engine's own live output
   exactly, the same standard `frame-debugger-4`'s own closing verification
   already used.

**All four sub-checks passed. Bug 2 is confirmed fixed**: selecting object/
step K genuinely shows the real screen exactly as it looked with only steps
`1..K` applied — no later object, no atmosphere fog until the real composite
step, and an honest placeholder before anything has been drawn at all.

### 3.5 — Lifecycle proof (Phase 1's clearing rule)

1. **LIVE, PASSED** — `GET /frame_debugger/enable?value=false` →
   `{"enabled":false, "hasCapturedFrame":false, "totalEventCount":0, ...}`.
   `GET /get_swapchain` confirmed the tree is genuinely empty again, showing
   the message *"Enable Frame Debugger above to inspect the current frame's
   render events."* Re-enabling (`GET /frame_debugger/enable?value=true`)
   followed by `GET /frame_debugger/state` confirmed a **fresh** capture
   appears again (`hasCapturedFrame:true, totalEventCount:9`) — the
   Disable→re-Enable half of the lifecycle-clearing rule is fully proven
   live.
2. **SECOND HALF — EXPLICITLY SKIPPED, per direct human instruction.** As
   the phase document's own corrected Step 3.5 anticipated, no HTTP endpoint
   anywhere in this codebase can flip `EditorContext::playbackPaused` back to
   `false` (confirmed by grep during this campaign's own planning pass), and
   this phase's tool set has no mouse/keyboard/UI-automation capability to
   click the hand-drawn "Resume" button in `PlaybackControls.cpp`. Per the
   phase document's own required workflow, `ask_questions` was used to ask a
   human how to handle this ONE specific sub-check, offering (in the
   document's own stated preference order) the code-review-only fallback
   Phase 1's own Definition of Done already accepted for this exact same
   rule. **The human explicitly chose a fourth option: skip this one
   sub-check entirely and simply note the gap in this report, rather than
   perform even the code-review fallback.** Per that explicit instruction,
   NEITHER a live HTTP trace NOR a code-review confirmation was performed for
   the "resume playback while Enabled clears the capture" transition in this
   phase. This is a real, acknowledged, human-approved gap in this campaign's
   own closing verification for this ONE specific sub-check only — every
   other live check in Steps 3.3-3.5 was fully proven live, with real
   evidence, exactly as the phase document required.

### 3.6 — Clean up

`stop_app_background` (PID 12092) — the running instance was terminated
cleanly after all live verification completed.

## 5. Locked Design Decisions from `PHASE0_MASTER_STRATEGY.md` — confirmed honored

1. **No multi-frame history** — confirmed: `FrameDebuggerCurrentCapture` holds
   exactly one `std::optional<FrameDebuggerHistoryEntry>`; Disable/re-Enable
   proven live in Section 4 above to genuinely clear and rebuild that single
   slot, never accumulating a second one.
2. **No per-object safety cap** — confirmed: the live scene's 2-object replay
   built both accumulated step images unconditionally (Section 4, Bug 2
   proof steps 1-2); `AddFrameDebuggerReplayPasses()`'s own source
   (Section 2, Phase 3 summary) confirms no cap of any kind exists in the
   loop that declares one replay pass per real object.
3. **Unified accumulated-preview semantics for every leaf** — confirmed live:
   a compute-pass leaf (index 0) and a per-object leaf (index 6/7) both now
   resolve through the same `FrameDebuggerStepPreviewKind`-driven picking
   rule, with the compute leaf correctly showing the honest `NotYetDrawn`
   placeholder rather than a distinct per-pass texture (the removed
   `frame-debugger-5` mechanism).
4. **The real, always-on `"GameView"` pass is never modified** — confirmed
   throughout: `GET /get_game_view`'s own live output was completely
   unaffected by any Frame Debugger operation performed in this session
   (Section 4, Bug 2 proof step 4's pixel-identical comparison is itself
   direct live proof of this — the real pass's true final output matches the
   debugger's own reconstruction, meaning neither one corrupted the other).
5. **The very first capture after "Enable" contains real per-object data** —
   confirmed live in Section 4, Bug 1 proof (both `SmokeTestCube` and the
   terrain entity present on the very first successful capture).
6. **A pre-`"GameView"` compute step's own accumulated image is honestly
   "nothing drawn to the screen yet"** — confirmed live (Section 4, Bug 2
   proof step 3, the `AtmosphereTransmittanceLutPass` leaf).
7. **This campaign's rendering changes only ever run while the Frame Debugger
   window is open AND "Enable" is checked, on an explicit capture-trigger
   frame** — confirmed by code review across every phase (the two-bool
   `m_pendingCaptureTrigger`/`m_replayServicedThisFrame` handshake, and
   `AddFrameDebuggerReplayPasses()`'s own `#if GTE_ENABLE_EDITOR` guard and
   `objectCount == 0` no-op path) — this campaign's own full `ctest`
   regression pass (Section 3) additionally confirms zero effect on any
   other engine subsystem when the Frame Debugger is not in active use.

## 6. Explicitly deferred / out-of-scope items (unchanged from PHASE0)

- **No isolated/cropped/masked preview image of just one entity's own
  pixels.** A per-object step's preview is a real, correct, ACCUMULATED
  whole-frame image as of that step (objects `[0..i]` drawn), never an
  isolated crop of just object `i`'s own pixels — getting that would need a
  stencil/ID-buffer or a full draw-call-level command-buffer replay, a
  separate, larger, not-yet-approved future feature (per `TODO.md`'s own
  updated "Still genuinely deferred" list, Phase 6).
- **True per-pass "stop"/breakpoint execution control** (pausing the GPU
  mid-frame at a specific compute-dispatch boundary) remains out of scope,
  unchanged from `frame-debugger-5`/`frame-debugger-6`.
- **The O(N) shared-target replay optimization** (a lower-cost alternative to
  this campaign's accepted O(N²) per-step full-redraw scheme) was
  investigated and explicitly deferred during Phase 3's own design — it would
  require widening `rg::PassContext` to expose a raw `VkImage` mid-pass, a
  broader, higher-risk change than this campaign's own scope justified. Noted
  in `docs/conventions/frame-debugger.md`'s "Still-deferred future work"
  section (Phase 6).
- **The "resume playback while Enabled clears the capture" HTTP-driven live
  proof** — explicitly skipped this phase per direct human instruction
  (Section 4, Step 3.5, item 2 above) — a real, acknowledged, human-approved
  gap in this campaign's own final verification, not a silent omission.

## 7. Definition of Done — Checklist (PHASE7 and whole campaign)

- [x] Full build green (`cmake --build build` — no errors; already fully
      built from Phases 1-6's own incremental work).
- [x] Full `ctest` regression green (1548/1548 run tests passed, 1 expected
      machine-dependent skip, zero failures).
- [x] Step 3.3 (Bug 1 proof) passed live, with real HTTP/screenshot evidence.
- [x] Step 3.4 (Bug 2 proof, all four sub-checks) passed live, with real
      HTTP/screenshot evidence.
- [x] Step 3.5's first half (Disable/re-Enable clearing) passed live, with
      real HTTP/screenshot evidence.
- [~] Step 3.5's second half (resume-while-Enabled clearing) — explicitly
      SKIPPED per direct human instruction via `ask_questions`, not silently
      omitted; documented as a known, approved gap (Section 4/6 above). Per
      the phase document's own Definition of Done, this is an ACCEPTED
      outcome for this one specific sub-check.
- [x] `CAMPAIGN_COMPLETION_REPORT.md` written (this file).
- [x] Every phase's own individual completion report
      (`PHASE1_COMPLETION_REPORT.md` through `PHASE6_COMPLETION_REPORT.md`)
      is present in this same folder and already committed from its own
      phase.
- [x] Final `git_add`/`git_commit` for the full campaign's final state (code
      + every completion report + this report).

## 8. Conclusion

Both of this campaign's original bugs are confirmed fixed, live, against the
real running engine, over the embedded HTTP server, with screenshot evidence
for every check: **Bug 1** — the very first Frame Debugger capture after
pressing "Enable" now genuinely contains every real object in the scene, with
no one-frame lag hiding content; **Bug 2** — selecting any step in the event
tree, compute pass or per-object draw alike, now shows the real screen exactly
as it looked with only that step's own work applied, honestly showing "nothing
drawn yet" before the first draw and matching the true live final output
pixel-for-pixel at the last step. All three Locked Design Decisions from
`PHASE0_MASTER_STRATEGY.md` were honored and are individually confirmed above.
The one acknowledged gap — a live HTTP trace of the "resume playback while
Enabled" capture-clearing transition — was a genuine, pre-identified tooling
limitation (no HTTP endpoint or UI-automation capability exists to drive it),
and was resolved by directly asking a human, who chose to accept the gap
rather than require either a live trace or the code-review fallback Phase 1's
own Definition of Done had already established as acceptable for this exact
rule.
