# PHASE4 — Docs, Full Build, Full Regression, Live Verification (Campaign Closeout)

_Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on Phases 1, 2, and 3 all being complete
and individually compile-checked already._

## Step 1: The Goal

Close out the `frame-debugger-9` campaign: confirm all three features work together,
correctly, in a real full build, pass the full existing regression suite with zero
newly-broken tests, get a genuine live/visual proof (not just "it compiles"), and leave
the documentation in a state that matches every other closed campaign in this codebase
(`docs/conventions/frame-debugger.md`'s existing "What's new (`frame-debugger-N`
campaign)" section pattern).

## Step 2: The Situation

- Every prior phase only ran a narrow, incremental compile check — this is the FIRST
  point in the campaign a full clean build and the full `ctest` suite are allowed to
  run, per `PHASE0_MASTER_STRATEGY.md`'s workflow rule #1.
- `docs/conventions/frame-debugger.md` has one clearly-established section pattern per
  past campaign: `## What's new (\`frame-debugger-N\` campaign)` — this phase adds the
  `frame-debugger-9` one, following that exact heading/tone/level of detail (see the
  existing `frame-debugger-6`/`frame-debugger-7`/`frame-debugger-8` sections in that
  same file for the expected shape: what was broken/missing, what changed, which files,
  explicitly-scoped-out items).
- `AGENTS.md`'s "Frame Debugger" paragraph is a dense, evergreen SUMMARY, not a
  per-campaign changelog — it should only be touched if this campaign changed something
  the summary's own top-level claims would now misstate (e.g. it currently says nothing
  about texture previews being unavailable, so it likely needs NO edit at all — verify
  this by re-reading it fresh at the start of this phase, and only edit it if a
  concrete inaccuracy is found).
- Every prior campaign's closing phase wrote both a final `PHASEn_COMPLETION_REPORT.md`
  AND a `CAMPAIGN_COMPLETION_REPORT.md` in the same folder (see
  `task_manager/frame-debugger-8/` for the exact precedent) — this phase does the same.

## Step 3: The Plan

### 3.1 — Tests (should already be in place from Phases 1 & 3 — this phase only RUNS them)

Confirm, by reading the diffs from Phases 1 and 3, that:
- `ComputeAspectFitImageRect()` has full test coverage in
  `tests/Editor/FrameDebuggerDataTests.cpp` (Phase 1, Step 3.4).
- `FrameDebuggerTextureProperty::kind`/`isRenderGraphResource` are asserted correctly in
  `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` for every construction site
  (Phase 3, Step 3.8).

If either is missing or incomplete, add it now before proceeding — do not treat "the
implementer meant to add it in an earlier phase" as good enough; this phase is the last
chance to catch a gap before the full suite runs.

### 3.2 — Full build

```
cmake --build build
```
(Working directory: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`, per the
Environment section every delegated task for this campaign already receives.) Fix any
compile error found here before proceeding — this is the first time all three phases'
changes are compiled together in one pass, so a cross-phase integration issue (e.g. a
missed include, a signature mismatch between Phase 1's `ComputeAspectFitImageRect()`
and Phase 3's second call site) is most likely to surface here first.

### 3.3 — Full regression test

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Every test must pass. A newly-failing test is a real regression — diagnose and fix it
(most likely candidates: an existing `FrameDebuggerData`/`FrameDebuggerSnapshotBuilder`
test whose expectations didn't account for the two new `FrameDebuggerTextureProperty`
fields defaulting differently than a hand-built test fixture assumed). Do not loosen a
test's expectation without first understanding why it failed (`AGENTS.md`'s own
"Testability & Regression Safety" rule).

### 3.4 — Live, HTTP-driven, screenshot-verified proof (all three features together)

Use `run_app_background` to launch the built Editor executable, then drive it via the
existing embedded HTTP server and `gte_send_request` (mirrors every past campaign's own
closing verification methodology, e.g. `task_manager/frame-debugger-8/
PHASE3_COMPLETION_REPORT.md`'s screenshot-verified proof):

1. `GET /frame_debugger/open` → confirm the window opens (screenshot via
   `gte_send_request` against `/get_swapchain`).
2. `GET /frame_debugger/enable?value=true` → confirm a real capture lands (tree is
   populated, not "No frame captured yet.").
3. **Feature 1 check**: `GET /frame_debugger/select_event?index=<a non-square LUT
   compute leaf, e.g. Sky-View LUT>` → screenshot → confirm the preview box now shows
   black letterbox/pillarbox bars around a correctly-proportioned image, not a
   stretched one.
4. **Feature 3 check**: since HTTP automation cannot literally drag a mouse, confirm via
   `GET /frame_debugger/select_event?index=N` for several different `N` in sequence
   that selection/preview tracking is still fully correct end-to-end through the new
   `SetSelectedEventIndex()` chokepoint (Phase 2) — if this environment permits a true
   interactive mouse-drag/keyboard test of the slider itself, perform that too and note
   the result in the completion report; if not, explicitly document that the
   HTTP-reachable half of Feature 3 was verified and the pure-mouse-drag half rests on
   Phase 2's own code-review-level correctness (ImGui's own well-established
   `SliderInt()`/`IsKeyPressed()` semantics), same as this codebase already accepts for
   other ImGui-only interactions elsewhere.
5. **Feature 2 check**: select a compute-dispatch leaf known to have real texture rows
   (e.g. `AtmosphereSkyViewLutPass`, which reads `AtmosphereTransmittanceLut`/
   `AtmosphereMultiScatteringLut` and writes `AtmosphereSkyViewLut_GameView`, per the
   campaign's own reference screenshot). If this environment can drive an actual mouse
   click on the new "View" button, do so for at least one 2D texture (confirm a real,
   non-garbage LUT image renders) and, if a volume-texture-producing pass is active this
   session (e.g. an Aerial Perspective Volume pass), one volume texture too (confirm a
   real ray-marched thumbnail renders, not a blank/garbage image) — then click "Back to
   Step Preview" and confirm the ordinary step preview returns correctly. Document
   whatever subset of this was actually exercisable from this environment, honestly —
   do not claim a click-through was performed if it wasn't.
6. `stop_app_background` the Editor process when done.

### 3.5 — Documentation updates

**`docs/conventions/frame-debugger.md`**: add a new section, following the exact
established pattern of the `frame-debugger-6`/`-7`/`-8` sections already in that file:

```
## What's new (`frame-debugger-9` campaign)

`frame-debugger-9` (`task_manager/frame-debugger-9/PHASE0_MASTER_STRATEGY.md`, four
phases) is a pure Quality-of-Life follow-up, adding three user-requested improvements
with no other behavior change:

- **The step-preview image now respects its own real aspect ratio** (PHASE1) - a new,
  Tier-1-tested pure helper, `ComputeAspectFitImageRect()`
  (`src/Editor/FrameDebuggerData.h/.cpp`), computes a centered, uniformly-scaled
  letterbox/pillarbox rect (solid black bars on the short axis) instead of the old
  `ImGui::Image(descriptor, avail)` full-stretch call.
- **The frame-step slider is a real, interactive control** (PHASE2) - mouse-draggable
  and Left/Right-arrow-key-nudgeable while focused, in addition to the pre-existing
  tree-row click and `GET /frame_debugger/select_event` HTTP route - all four paths now
  funnel through one new chokepoint, `FrameDebuggerPanel::SetSelectedEventIndex()`.
- **Any render-graph-registered texture a compute-dispatch leaf's own ShaderProperties
  tab lists (Read/Write Texture, Read/Write Volume Texture rows) can now actually be
  viewed** (PHASE3) via a small "View" button next to its row - a strictly ON-DEMAND,
  ONE-SHOT preview (never a continuously live view): a 2D texture is read back via
  `Renderer::CaptureImagePixels()` (the exact same primitive `GET /get_texture` already
  uses, including its BGRA/HDR conversion rules) and re-uploaded into a freshly-owned
  `Texture2D`; a volume texture is ray-marched via the pre-existing
  `VolumeTexturePreviewRenderer` (unchanged) and uploaded the same way. Displayed until
  the user dismisses it ("Back to Step Preview"), selects a different event, takes a new
  Capture, or Disables/Resumes - never cached or kept continuously up to date.
  Deliberately out of scope: "Material Texture" rows (per-entity/mesh asset textures -
  a different, non-render-graph system) - a clean, documented future item, not silently
  forgotten.

See `task_manager/frame-debugger-9/PHASE0_MASTER_STRATEGY.md` and each
`PHASEn_COMPLETION_REPORT.md`/`CAMPAIGN_COMPLETION_REPORT.md` in that same folder for the
full four-phase writeup and live verification evidence.
```
(Adjust wording/detail level to match this campaign's own actual final implementation
if anything shifted slightly during Phases 1–3 — this text is a strong starting draft,
not a rigid, must-match-verbatim template.)

**`AGENTS.md`**: re-read the existing "Frame Debugger" paragraph fresh. Only edit it if
a concrete inaccuracy is found (e.g. if it ever claims the preview box "always shows the
image stretched to fill" — it currently does not make any such claim, so this is
unlikely to need a change at all). If no edit is warranted, explicitly say so in the
completion report rather than silently skipping it without comment.

### 3.6 — Reports

Write `task_manager/frame-debugger-9/PHASE4_COMPLETION_REPORT.md` covering: the full
build result, the full `ctest` result (pass count, zero failures), the live verification
evidence from Step 3.4 (screenshots/response bodies, or a clear description of what was
and wasn't exercisable from this environment), and the documentation diff summary.

Then write `task_manager/frame-debugger-9/CAMPAIGN_COMPLETION_REPORT.md` — a short,
top-level summary of the whole four-phase campaign (mirrors
`task_manager/frame-debugger-8/CAMPAIGN_COMPLETION_REPORT.md`'s own shape): the original
three user requests, what was actually built for each, which files changed overall, the
final full-build/full-regression/live-verification result, and any explicitly-scoped-out
future items (Material Texture preview, per-per-draw-call raw preview, true GPU
breakpoint stepping — the last two already pre-existing, documented deferrals from
earlier campaigns, unrelated to this one's own new deferral).

### 3.7 — Commit

`git_add` everything (code + docs + both reports) and `git_commit` on
`feature/frame-debugger-impl` with a message summarizing the full campaign close-out.

### 3.8 — Deliverables

- `docs/conventions/frame-debugger.md` — new `frame-debugger-9` section.
- `AGENTS.md` — edited only if a concrete inaccuracy was found (explicitly noted either
  way in the completion report).
- `task_manager/frame-debugger-9/PHASE4_COMPLETION_REPORT.md`.
- `task_manager/frame-debugger-9/CAMPAIGN_COMPLETION_REPORT.md`.
- A clean full build + fully green `ctest` run + documented live verification evidence.
