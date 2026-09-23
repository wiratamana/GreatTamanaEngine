# PHASE5 — Full Verification, Live Smoke Test, and Campaign Completion

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). **Depends on:** `PHASE1`-`PHASE4`,
all landed and each individually compiling/passing their own targeted tests. This is the ONLY
phase in this campaign allowed to run a full clean build and the full regression suite — per
this campaign's own process rules (do not run a full build/full `ctest` in any earlier phase).

---

## Step 1 — The Goal

Prove, end-to-end, with the real engine running, that Core Campaign 1 ("De-hardcode
`RenderPassCategory`") is complete and safe to merge: the full automated test suite passes
with no new failures, the running engine's Frame Debugger tree is visually and structurally
unchanged for both real production consumers (Atmosphere LUT passes, GPU Skinning
dispatches), and the codebase's own living documentation (`AGENTS.md`) accurately reflects
what actually shipped — including any deliberate deviation any earlier phase in this
campaign had to make from its own original plan (mirrors this codebase's own established
"Deviations from the original design doc" convention, e.g. `render-pass-3`'s
`CAMPAIGN_COMPLETION_REPORT.md`).

---

## Step 2 — The Situation

By this point: `RenderPassCategory` has 2 enumerators; `RenderPassTagMask` is a real,
end-to-end-plumbed piece of pass metadata; two new Layer-2 tag headers exist
(`AtmosphereRenderPassTags.h`, `GpuSkinningRenderPassTags.h`); a new Core registry
(`RenderPassGroupRegistry.h/.cpp`) exists with its own Tier-1 tests; `FrameDebuggerData.cpp`'s
tree-grouping logic is fully generic and covered by a new genericity-proof test. Every prior
phase ran only an incremental build and a narrow, targeted test subset — this phase is the
first point anyone has run the WHOLE test suite since this campaign started, so it is also
the first point a subtle cross-file interaction (if any exists) would surface.

---

## Step 3 — The Plan

### 3.1 Full clean-enough build

Run `cmake --build build` for the main configuration. If any earlier phase's incremental
build masked a stale-object-file issue (rare, but possible after several phases of header
relocation — `RenderPassTag`/`RenderPassTagMask` physically moved files in PHASE1), and you
see a suspicious error that looks like a stale build artifact rather than a real code error,
it is acceptable to selectively clean just the affected translation units' object files
rather than doing a full wipe-and-rebuild, to save time — but do NOT skip verifying that the
final state is a genuinely clean, warning-free build.

Also do a quick incremental build of `build-editor-off` (this repo's own second, Editor-
disabled build configuration) — `RenderPassGroupRegistry.h/.cpp`'s own PHASE2 acceptance bar
already asked for a check here; PHASE5 is where you confirm it one more time against the
FULL, final, integrated code (not just the registry file in isolation), since
`AtmosphereLutRenderer`'s now-real constructor (PHASE3) is a new, real call site of this
facility that must ALSO compile with the Editor module off.

### 3.2 Full regression test run

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Compare the total test count and pass rate against the most recent prior campaign's own
documented baseline (`AGENTS.md`'s "Render Pass System" section currently cites
`render-pass-6`'s own "1736 tests, 100% passing, one pre-existing environment-gated skip" —
re-confirm this exact baseline number by checking `AGENTS.md`/the most recent completion
report for whichever campaign actually ran last on this branch, since other work may have
landed since `render-pass-6`). Expect the total count to have grown by exactly the number of
new tests added across PHASE1 (tag threading), PHASE2 (registry), PHASE3 (call-site fixture
updates — these mostly change EXISTING tests, not add new ones), and PHASE4 (the genericity
test). If ANY test fails that this campaign did not deliberately intend to change, treat it
as a real regression: diagnose it yourself first (read the failure output, form a hypothesis
about which phase's change caused it), then use `delegate_task` to fix it — per this
project's own top-level instructions, do not attempt to patch around a failing test's
expectation without first understanding WHY it failed.

### 3.3 Live, HTTP-driven Frame Debugger smoke test

Launch the engine (`run_app_background`) and, over its embedded network server
(`gte_send_request`), reproduce the exact user-visible check the source strategy document's
own acceptance bar calls for (Section 3, Core Campaign 1's own "Acceptance bar" bullet):

1. `GET /frame_debugger/open` — bring the Frame Debugger panel to the front.
2. `GET /frame_debugger/enable` — arm capture.
3. Let at least one real frame render (Atmosphere is always compiled in per `AGENTS.md`, so
   its LUT passes run every frame unconditionally — no special scene setup should be needed
   to exercise the "Compute LUT" heading), then `GET /frame_debugger/capture` (or the
   documented equivalent trigger — confirm the exact current endpoint name/shape by reading
   `docs/conventions/frame-debugger.md`/`NetworkRoutes.cpp` live, since this campaign never
   changes the networking layer itself but must use whatever endpoints genuinely exist today).
4. `GET /frame_debugger/state` — retrieve the captured tree and CONFIRM: a `"Compute LUT"`
   group node exists and contains the expected Atmosphere LUT pass leaves (Transmittance,
   Multi-Scattering, Sky-View, Aerial Perspective Volume, and — if a debug slice pass happens
   to be active this session — its debug-slice pass too); a `"Compute Dispatches
   (Pre-GameView)"` group node exists containing GPU Skinning dispatches IF ANY model in the
   currently-loaded scene actually uses GPU skinning this frame (if the loaded scene has no
   skinned model, this bucket may legitimately be absent — that is correct, expected, pre-
   existing behavior, not a regression; if you need to force a positive case, use the
   engine's own `POST /instantiate_primitive`/model-loading endpoints, or ask the user via
   `ask_questions` how to get a GPU-skinned model into the current scene if none is already
   present).
5. If a screenshot/visual comparison is useful, `gte_send_request` against `/get_game_view`
   to confirm the actual rendered image is visually unaffected (this campaign changes zero
   rendering behavior, so the Game View pixels themselves must be pixel-identical to before —
   this is a much cheaper/faster sanity check than a full pixel diff, just eyeball it via
   `load_image`/the image content block `gte_send_request` itself returns).
6. Retrieve engine logs (`GET /get_logs`) and confirm no unexpected `WARNING`/`ERROR` entries
   appeared — in particular, confirm the PHASE2 "tag registered twice under a different
   heading" soft warning never fires during normal operation (it firing would indicate a real
   bug — e.g. two features accidentally choosing the same tag bit).
7. `stop_app_background` the engine once done.

### 3.4 Update `AGENTS.md`

Add a new paragraph to the existing "Render Pass System" section (immediately after the
`render-pass-6` paragraph, following that section's own established prose style/citation
convention exactly — read the last 2-3 paragraphs of that section again immediately before
writing, to match voice/format precisely) summarizing this campaign in the same density as
its predecessors: what `RenderPassCategory` used to hardcode, what it hardcodes now, the new
`RenderPassTagMask`/`RenderPassGroupRegistry` mechanism, the two new per-feature tag headers,
and the dead-field bug fix (PHASE1, Step 2.4) discovered and fixed along the way, citing
`task_manager/render-pass-7/PHASE0_MASTER_STRATEGY.md` and
`CAMPAIGN_COMPLETION_REPORT.md` exactly like every prior campaign's own citation.

### 3.5 Write `CAMPAIGN_COMPLETION_REPORT.md`

In `task_manager/render-pass-7/`, mirroring `render-pass-6/CAMPAIGN_COMPLETION_REPORT.md`'s
own shape: one paragraph per phase (1-5) summarizing what shipped, the final `ctest` count/
pass-rate, the live smoke-test result, and an explicit "Deviations from the original plan"
section listing anything any phase's own completion report already flagged as a deviation
(e.g. the exact tag bit values actually chosen in PHASE3 if they differed from this
document's suggested `1ull << 0`/`1ull << 1`).

### 3.6 Final commit

`git_add`/`git_commit` covering `AGENTS.md`, the new `CAMPAIGN_COMPLETION_REPORT.md`, and
this phase's own `PHASE5_COMPLETION_REPORT.md` (a short report is still expected here too,
per this campaign's own established per-phase pattern, even though most of its content is
duplicated into the campaign-level report).

### Acceptance bar for this campaign as a whole

- Full clean build succeeds in both `build` and `build-editor-off`.
- Full `ctest` run: 100% pass (matching or exceeding the pre-campaign baseline count, zero
  new failures, the same single pre-existing environment-gated skip this repo has carried
  across every recent campaign — confirm it is still exactly that one skip, not a new one).
- Live HTTP smoke test confirms the Frame Debugger tree's "Compute LUT"/"Compute Dispatches
  (Pre-GameView)" grouping is visually and structurally IDENTICAL to pre-campaign behavior.
- `grep`/`search_in_dir` for `RenderPassCategory::AtmosphereLut`/`RenderPassCategory::GpuSkinning`
  across all of `src/`/`tests/` returns zero hits.
- `AGENTS.md` and `CAMPAIGN_COMPLETION_REPORT.md` both accurately, honestly describe exactly
  what shipped, including any deviation from this master strategy's original plan.
