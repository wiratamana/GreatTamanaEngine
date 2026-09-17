# PHASE7 — Full build, live end-to-end verification, campaign completion

_Parent: `PHASE0_MASTER_STRATEGY.md` — read it first. This is the FINAL
phase — builds on everything in Phases 1-6._

## Step 1: The Goal

Prove, live and end-to-end, over the real embedded HTTP server, with
screenshots, that BOTH original bugs are genuinely fixed — mirroring the
"live, HTTP-driven, screenshot-verified" closing-verification standard every
prior `frame-debugger-*` campaign already established (see e.g.
`task_manager/frame-debugger-6/PHASE5_LIVE_VERIFICATION_AND_DOCS.md` for the
exact style/rigor expected). This is the ONLY phase allowed to run a full
build and the full regression suite (per this campaign's own Note 4/5).

## Step 2: The Situation

Phases 1-6 were verified only with quick compile checks and, at most,
targeted incremental spot-checks (Phase 3). This phase closes the loop with
the real thing: a full rebuild, the full test suite, and a real running
instance driven purely over HTTP (matching this feature's own existing
"no manual-verification limitation" standard, `docs/conventions/frame-debugger.md`'s
own "Testing this feature" section).

## Step 3: The Plan

### 3.1 — Full build + full regression

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Fix anything that fails before continuing. If a fix is non-trivial or
touches a phase you didn't personally implement, use `delegate_task` to send
a precise, self-contained fix request back to the relevant phase's own
scope — do not silently patch around a real regression with a hack.

### 3.2 — Launch and prepare a real scene

- `run_app_background` the built engine executable.
- Load (or create, if none exists) a scene containing at least two visibly
  different objects drawn in a known order, ideally reproducing the user's
  own original repro: a terrain-like object plus a distinctly different
  "smoke cube"-style object, in that draw order — check for an existing
  sample scene under whatever the project's scenes folder is before
  building a new one from scratch (`search_in_dir`/`browse_dir` first).
- Confirm atmosphere/sky rendering is active (a Sun light entity present) so
  the composite-pass distinction is actually observable.

### 3.3 — Bug 1 proof (wrong first capture)

1. `GET /frame_debugger/open`.
2. `GET /frame_debugger/enable?value=true`.
3. Immediately `GET /frame_debugger/state` — confirm it honestly reports
   nothing captured yet (Phase 2's deferred-trigger design).
4. Wait one short real interval (a fraction of a second is enough — the
   engine keeps rendering every real frame even while paused, per
   `docs/conventions/time-and-playback-pause.md`), then `GET
   /frame_debugger/state` again — confirm `totalEventCount`/the event tree
   now DOES include every real object (both the terrain-like object AND the
   smoke-cube-like object) on this very first successful capture. Capture a
   `GET /get_swapchain` screenshot of the window at this point as evidence.

### 3.4 — Bug 2 proof (wrong preview per step)

1. `GET /frame_debugger/select_event?index=<the first-drawn object's own
   index>` — `GET /get_swapchain` — confirm the preview box shows ONLY that
   first object, no later object, no atmosphere/sky fog.
2. `GET /frame_debugger/select_event?index=<the second-drawn object's own
   index>` — `GET /get_swapchain` — confirm the preview box shows BOTH
   objects, still no atmosphere/sky fog.
3. Select a Pre-GameView compute leaf (e.g. an atmosphere LUT pass) — `GET
   /get_swapchain` — confirm the preview box shows the honest "nothing drawn
   yet" placeholder, not a stale/wrong image.
4. Select the LAST leaf in the tree (the post-composite leaf, or deselect
   entirely) — `GET /get_swapchain` — confirm the preview box now matches
   `GET /get_game_view`'s own live output pixel-for-pixel (same standard
   `frame-debugger-4`'s own closing verification already used).

### 3.5 — Lifecycle proof (Phase 1's clearing rule)

1. `GET /frame_debugger/enable?value=false` — `GET /frame_debugger/state` —
   confirm the captured data is gone (the tree is empty again).
2. Re-enable, confirm a fresh capture appears again. For the SECOND half of
   this proof (resuming playback while still Enabled must ALSO clear the
   capture) — **confirmed this revision, by grepping the real source: there
   is currently NO existing HTTP endpoint anywhere in this codebase that
   flips `EditorContext::playbackPaused` back to `false`** (searched
   `src/Network/`/`src/Application/` for "Resume"/"pause" — the ONLY way to
   resume playback today is a human physically clicking the "Resume" button
   in `PlaybackControls.cpp`'s own hand-drawn ImGui toolbar). The tools this
   phase actually has available (`gte_send_request`/`run_app_background`/
   `stop_app_background` — no mouse/keyboard input-injection tool of any
   kind is in this campaign's tool set) cannot click that button, so a
   genuine end-to-end HTTP-driven proof of THIS ONE specific transition is
   NOT achievable with what this phase has to work with — this is a real,
   confirmed tooling gap, not a step to silently skip or to force through by
   inventing a workaround. Do ONE of the following, in this preference
   order, and say plainly in the completion report which you did and why:
   - If, by the time this phase actually runs, a way to do this has become
     available (e.g. a future networking change added a resume-capable
     route, or a UI-automation tool was added to this session's tool set),
     use it and complete the live proof as originally written.
   - Otherwise, use `ask_questions` to ask a human how they want this one
     specific sub-check handled (e.g. accept a code-review-only
     confirmation instead, since Phase 1's own Definition of Done already
     explicitly accepted "verified at minimum by code review" for this
     exact same clearing rule) — do NOT silently downgrade the check
     yourself without asking, since the master strategy's own workflow
     rules require asking rather than guessing on a genuine ambiguity like
     this.
   - If the human says a code-review-only confirmation is acceptable:
     confirm, by reading the real, already-implemented Phase 1 code, that
     `FrameDebuggerPanel::Build()`'s own top-of-frame check (the
     `m_enabled && m_wasPlaybackPaused && !ctx.playbackPaused` branch) is
     wired correctly and really does call `m_currentCapture.Clear()`, and
     record that confirmation (with the exact file/line reviewed) in
     `CAMPAIGN_COMPLETION_REPORT.md` in place of a live HTTP trace for this
     one sub-step only — every OTHER live check in this phase (3.3/3.4, and
     3.5's own first point above) is fully HTTP-drivable and must still be
     proven live, with real evidence, exactly as written.

### 3.6 — Clean up

`stop_app_background` the running instance.

### 3.7 — `CAMPAIGN_COMPLETION_REPORT.md`

Write one, in this folder, mirroring
`task_manager/frame-debugger-6/CAMPAIGN_COMPLETION_REPORT.md`'s own
structure: summarize all 7 phases, list every Locked Design Decision from
`PHASE0` and confirm each was honored, and include the live verification
evidence from Steps 3.3-3.5 (screenshots/HTTP responses referenced or
embedded as the existing convention in this repo's prior campaign reports
already does).

### 3.8 — Final git commit

Commit the full campaign's final state (code + every completion report +
the campaign report) with a clear, descriptive message.

## Step 4: Definition of Done

- Full build green, full `ctest` regression green.
- Every one of Steps 3.3-3.5's live checks passes as described, EXCEPT that
  3.5's "resume while Enabled" sub-check may be satisfied by the
  code-review fallback described in 3.5 itself (only if a human explicitly
  approved that fallback via `ask_questions`) rather than a live HTTP trace,
  since no HTTP-drivable path to resume playback exists in this codebase
  today.
- `CAMPAIGN_COMPLETION_REPORT.md` written and committed.

If a genuine regression surfaces that needs a fix outside this phase's own
scope, use `delegate_task` with a precise, self-contained prompt describing
exactly what broke and where — and tell that delegated task to use
`ask_questions` for anything ambiguous, the same as every other phase in
this campaign.
