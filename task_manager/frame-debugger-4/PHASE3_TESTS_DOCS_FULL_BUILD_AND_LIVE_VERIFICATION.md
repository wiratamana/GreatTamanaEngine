# PHASE3 — Tests Review, Documentation Sweep, Full Build/Regression, Live Verification

## Parent -> PHASE0_MASTER_STRATEGY.md, read it first. Depends on PHASE1 and PHASE2 already landing.

Campaign folder: `task_manager/frame-debugger-4/`
Branch: `feature/frame-debugger-impl`
Risk level: LOW (docs + verification only, plus a small, optional, genuinely-scoped test addition) — but
this is the phase that PROVES the whole campaign actually fixed the bug, so its own verification steps are
not optional.

---

## Step 1: The Goal (Where are we going?)

Every doc claim this bug made false is corrected, one small additional regression test is added if (and
only if) PHASE1's own picking-rule logic turns out to contain a genuinely pure, extractable piece worth
Tier-1-testing, both `GTE_ENABLE_EDITOR` build configurations compile cleanly from a full clean build, the
full `ctest` regression suite passes at 100% (modulo the one pre-existing, machine-gated
`PmxLoaderRealModelSmokeTest` skip every prior campaign's own full run has always reported), and a live,
HTTP-automation-driven, screenshot-verified smoke test PROVES — not just asserts — that the Frame Debugger's
captured/previewed image now genuinely includes the atmosphere-scattering/aerial-perspective effect, and
that the new `"Aerial Perspective Composite"` tree leaf is real and selectable end-to-end.

## Step 2: The Situation (Where are we now?)

PHASE1 fixed the retained-texture root cause and wired a composite-aware picking rule; PHASE2 made the
compositing step visible in the tree. Neither phase touched documentation, and neither phase's own
compile-check was a FULL clean build or a FULL `ctest` run — both are still outstanding. Three existing docs
currently describe the OLD, buggy behavior as if it were correct and need updating:

- `AGENTS.md`'s "Frame Debugger" section says: "...selecting one shows real shader/blend/Z/stencil/texture/
  vector/matrix data plus a real preview image reconstructed as of that exact point in the frame..." — true
  in spirit, but was, in practice, ALWAYS the pre-composite image for the one and only leaf that ever showed
  an image at all (a real bug, now fixed) — worth a small, explicit clarifying addition, not a rewrite.
- `docs/conventions/frame-debugger.md` (the "full convention" doc `AGENTS.md` links to) almost certainly has
  a more detailed version of the same claim, plus (per `frame-debugger-3`'s own `PHASE8_COMPLETION_REPORT.md`)
  an explicit list of "what this feature does" — re-read it fully before editing; it may also still describe
  the event tree as having exactly two possible node kinds (`"GPU Skinning"` group + `"GameView"` leaf),
  which is now stale (three kinds, per PHASE2).
- `TODO.md`'s "Frame Debugger" section lists deferred/outstanding items — re-read it fully; this bug was
  never explicitly listed there (confirm this, the same way `atmosphere-scattering-4`'s own PHASE4 did for
  its own bug), so there is likely nothing to check off, but there IS a real, newly-relevant forward-looking
  item worth adding (see Step 3.3 below).
- `README.md`'s own "Status" section likely has a one-line entry for the `frame-debugger-3` campaign
  already — add a new, short entry for this bug-fix campaign directly after it, matching the length/style
  of neighboring entries (see `atmosphere-scattering-4`'s own `README.md` entry for the exact tone to match:
  short, factual, "what was broken -> what is fixed" phrasing, no marketing language).

## Step 3: The Plan

### 3.1 Review PHASE1 for a genuinely-extractable pure test candidate

Re-read the final, as-landed `EnsurePreviewDescriptor()` picking-rule block (PHASE1's Step 3.5). It contains
one small piece of genuinely PURE decision logic buried inside an otherwise Tier-2 (live ImGui/Vulkan
descriptor) function: "given `(hasPreview, hasCompositedPreview, isViewingGameViewLeaf)`, which of
`{None, Preview, CompositedPreview}` should be shown". If this is straightforward to extract as a small,
free, pure function (e.g. an enum-returning helper in `FrameDebuggerData.h`/`.cpp`, since that is already
the pure/testable home for this feature's data-shaping logic — never inline it directly inside
`Panels/FrameDebuggerPanel.cpp`, which stays Tier-2/ImGui-coupled by this whole campaign's own established
convention), extract it and add a small, dedicated Tier-1 test (`FrameDebuggerDataTests.cpp` or a new
`TEST(...)` group in `FrameDebuggerSnapshotBuilderTests.cpp` — pick whichever existing file this logic's own
subject matter fits best) covering all the meaningful input combinations:

- No entry at all (fresh history) → `None`.
- Entry exists, nothing selected, `compositedPreview` present → `CompositedPreview`.
- Entry exists, nothing selected, `compositedPreview` absent, `preview` present → `Preview`.
- Entry exists, `"GameView"` leaf selected, `preview` present → `Preview` (even if `compositedPreview` is
  ALSO present — explicit leaf selection always wins).
- Entry exists, `"GameView"` leaf selected, `preview` somehow absent (defensive-only, should not happen in
  practice) → `None`.
- Entry exists, some OTHER leaf selected (e.g. `"Aerial Perspective Composite"` or `"GPU Skinning"`),
  `compositedPreview` present → `CompositedPreview`.

**This extraction is a judgment call, not a hard requirement** — if `EnsurePreviewDescriptor()`'s own real
shape (once actually implemented in PHASE1) does not cleanly separate this way without contorting the
function or introducing an awkward new enum nobody else needs, it is equally acceptable to leave the logic
inline and rely on the live smoke test in Step 3.6 below as this specific rule's own verification, exactly
like `FrameDebuggerHistory::CaptureFrame()`/`EnsurePreviewDescriptor()`'s own pre-existing, already-accepted
Tier-2 status for everything else in this class. Document whichever choice was made, and why, in this
phase's own completion report — do not silently skip this step without a recorded rationale either way.

### 3.2 `AGENTS.md` — "Frame Debugger" section update

Append ONE new sentence (do not rewrite the existing paragraph) directly after the existing "...a real
preview image reconstructed as of that exact point in the frame..." claim, along these lines:

> Two of those reconstructed points are genuinely distinct: selecting the pass that actually draws the
> scene shows the frame before atmosphere scattering/aerial-perspective fog is applied, and a further,
> real `"Aerial Perspective Composite"` step shows the frame after it — the preview always defaults to the
> final, fog-inclusive image whenever nothing more specific is selected.

Match the existing paragraph's own tone/length exactly (see the file's own neighboring sections for style
precedent) — do not introduce campaign-name references (`frame-debugger-4`) into `AGENTS.md` itself; that
belongs in `README.md`'s "Status" section instead (per this codebase's own established split between the
two files).

### 3.3 `docs/conventions/frame-debugger.md` — full convention doc update

Re-read the ENTIRE file first (do not guess its structure from `AGENTS.md`'s short summary alone). At
minimum:

- Correct any claim implying the retained preview is always the pre-composite/raw Game View render — it is
  now genuinely, correctly composite-aware.
- Update the event-tree shape description (wherever it enumerates the tree's possible node kinds) to
  include the new `"Aerial Perspective Composite"` leaf as a third possible sibling, alongside `"GPU
  Skinning"` and `"GameView"`.
- Add a short, explicit "known limitation, now fixed" note referencing this campaign
  (`task_manager/frame-debugger-4/`) by name, mirroring how `atmosphere-scattering-4`'s own bug-fix campaign
  is referenced in ITS OWN convention doc (if such a precedent exists there — re-check before copying the
  exact phrasing) — future readers should be able to find the fix's own rationale without archaeology.

### 3.4 `TODO.md` — "Frame Debugger" section update

Re-read the full existing section first. Confirm (as `atmosphere-scattering-4`'s own PHASE4 did for its own
bug) whether this exact bug was ever listed there as a known gap — if it was, mark it done with a `~~struck
through~~` entry per this codebase's own existing convention (visible elsewhere in the same file); if it was
never listed (the more likely case, since this bug was only discovered by direct user report, not by a
previous campaign's own forward-looking TODO), add nothing retroactive, just confirm this in the completion
report. Add ONE new, genuinely-scoped forward-looking entry, e.g.: extending this same
"pre/post a real intermediate compositing step" preview-selection pattern to a FUTURE Scene-View Frame
Debugger, should one ever be built (today there is none at all — Locked Design Decision #4,
`PHASE0_MASTER_STRATEGY.md`) — a real, clearly-scoped gap this phase's own review surfaces, not a
padding-only entry.

### 3.5 `README.md` — "Status" section update

Append one new short entry (after the existing `frame-debugger-3` entry, and after any
`atmosphere-scattering-4` entry if both exist in the same list) describing: the bug (Frame Debugger silently
never showed atmosphere scattering), the two-part root cause (wrong retained texture + invisible compositing
pass), and the fix (dual-stage retained capture + a new tree leaf) — matching the length/style of
neighboring entries exactly (2-4 sentences, factual, no marketing language).

### 3.6 Full build + full regression

```
cmake --build build --clean-first
```
Must succeed with zero errors (`GTE_ENABLE_EDITOR=ON`, the default `build/` configuration).

```
cmake -S . -B build-editor-off -DGTE_ENABLE_EDITOR=OFF
cmake --build build-editor-off --clean-first
```
Must also succeed with zero errors — confirms every Editor-only file touched by this campaign
(`FrameDebuggerHistory.h/.cpp`, `FrameDebuggerData.h/.cpp`, `Panels/FrameDebuggerPanel.h/.cpp`,
`ImGuiEditorLayer.cpp`) is correctly excluded entirely from this configuration, exactly like every prior
Frame Debugger campaign's own final phase already re-confirmed.

```
cd build && ctest -C Debug --output-on-failure
```
Must report 100% passing (modulo the one pre-existing, machine-gated `PmxLoaderRealModelSmokeTest` skip) —
confirm every `FrameDebuggerSnapshotBuilderTest`-prefixed case (PHASE2's new ones included) and every other
pre-existing Frame-Debugger-family test (`FrameDebuggerCaptureTests.cpp`, `FrameDebuggerHistoryTests.cpp`,
`FrameDebuggerPreviewProcessingTests.cpp`, `FrameDebuggerDataTests.cpp`) is present and green, with zero
regressions anywhere else in the suite.

### 3.7 Live, HTTP-automation-driven, screenshot-verified smoke test

Using `run_app_background`/`gte_send_request`/`stop_app_background` (no mouse/keyboard needed at all, per
`frame-debugger-3` PHASE7/PHASE8's own precedent — the full `/frame_debugger/*` HTTP surface already
exists and needs no changes this campaign):

**Determining the exact `select_event?index=` values ahead of time (added during this campaign's own full
double-check pass — a genuine gap in the original plan):** `GET /frame_debugger/state` deliberately reports
only `totalEventCount` (a count), never the tree's own node names/shape — there is no HTTP route that lists
event names/indices (see `docs/conventions/frame-debugger.md`'s "HTTP automation" section; this campaign adds
none). Rather than guessing, rely on `BuildRealFrameDebuggerSnapshot()`'s own DETERMINISTIC construction order
(`FrameDebuggerData.cpp`: optional `"GPU Skinning"` group first, then the `"GameView"` leaf, then the optional
`"AtmosphereAerialPerspectiveCompositePass"` leaf last — see PHASE2's Step 3.2): as long as step 2 below spawns
ONLY primitives (`POST /instantiate_primitive`) and lights (`POST /instantiate_light`) — never a loaded `.pmx`
skinned model via a scene-file/asset path — `Game::CollectGpuSkinningDispatchRequests()` returns empty for the
whole session, so NO `"GPU Skinning"` group ever appears, meaning the `"GameView"` leaf is deterministically
`eventIndex == 0` and the new `"AtmosphereAerialPerspectiveCompositePass"` leaf is deterministically
`eventIndex == 1`, for every single capture this smoke test takes. Confirm this reasoning still holds (e.g. via
`GET /frame_debugger/state`'s own `totalEventCount == 2` right after step 4 below) before relying on it, and
note in the completion report that `index=0`/`index=1` map to these two specific leaves for exactly this
reason — do not leave a future reader to re-derive this from scratch. Also recall (PHASE0's own naming
clarification, Step 1) that the tree ROW TEXT for the new leaf reads `"AtmosphereAerialPerspectiveCompositePass"`
(its raw pass name), not the shorter `"Aerial Perspective Composite"` label — that shorter label only appears
in the Inspector's own "Pass" field once the leaf is selected; do not treat a screenshot showing the longer raw
name in the tree pane as a discrepancy.

1. Launch `GreatTamanaEngine.exe` from `build/` in the background.
2. Spawn something with visible geometry at a real distance so aerial-perspective fog is actually
   observable (mirrors `atmosphere-scattering-4`'s own PHASE4 live-smoke-test precedent exactly — e.g.
   `POST /instantiate_light` a "Sun", `POST /instantiate_primitive` a cube scaled up and positioned a few
   hundred world units away via `POST /set_entity_trs`, near the aerial-perspective volume's own far edge) -
   deliberately primitives/lights ONLY (never a skinned model), per the index-determinism note above.
3. `GET /get_game_view` — capture the REAL final image (ground truth: what the fog genuinely looks like
   today, already correct, untouched by this campaign).
4. `GET /frame_debugger/open`, `GET /frame_debugger/enable?value=true` (auto-captures the very first frame),
   `GET /frame_debugger/state` (confirm `historyCount == 1`, and `totalEventCount == 2` — the `"GameView"`
   leaf plus the new `"AtmosphereAerialPerspectiveCompositePass"` leaf, no `"GPU Skinning"` group present).
5. `GET /get_swapchain` with NOTHING selected yet (`selectedEventIndex == -1`) — the Frame Debugger's own
   preview box should visually match step 3's `/get_game_view` capture (same fog/haze) — **this is the
   direct, positive proof PHASE1's core fix works**, and was NOT true before this campaign (a prior
   `/get_swapchain` capture at this exact point would have shown a visibly LESS foggy image than
   `/get_game_view`, since the retained texture was, before this campaign, always the pre-composite one).
6. `GET /frame_debugger/select_event?index=0` (the `"GameView"` leaf — see the index-determinism note above)
   then `GET /get_swapchain` — the preview box should now visibly show LESS fog than step 5 (the true
   pre-composite reconstruction) — confirms the `"GameView"` leaf's own dedicated pre-composite semantics
   genuinely work, not just the default view.
7. `GET /frame_debugger/select_event?index=1` (the new `"AtmosphereAerialPerspectiveCompositePass"` leaf —
   see the index-determinism note above) then `GET /get_swapchain` — the preview box should show the SAME
   fog-inclusive image as step 5, AND the Inspector's own texture/vector rows should show real
   `"Read Texture: GameView"`/`"Write Texture: GameViewComposited"`/`"GPU Time (ms)"` entries — confirms
   PHASE2's new leaf is real, selectable, and correctly wired to PHASE1's picking rule end-to-end.
8. `GET /frame_debugger/enable?value=false` + `stop_app_background` — clean shutdown.

Record every step's real, observed result (not just "it worked") in this phase's own completion report,
exactly like `frame-debugger-3`'s own `PHASE8_COMPLETION_REPORT.md` and `atmosphere-scattering-4`'s own
`PHASE4_COMPLETION_REPORT.md` both already do for their own live smoke tests — screenshots (or a precise
textual description of what each one showed) for at least steps 3, 5, 6, and 7.

**If no distant, fog-producing geometry is convenient to spawn this session** (e.g. environment constraints),
the empty-sky case is still a valid, if weaker, substitute: confirm the Sky Background itself (which is
ALSO only correctly shown post-composite once the `atmosphere-scattering-4` fix landed) matches between
`/get_game_view` and the Frame Debugger's own default-view `/get_swapchain` capture — still a real,
observable difference versus this bug's own pre-fix behavior.

### 3.8 Completion report + campaign closeout

Write `task_manager/frame-debugger-4/PHASE3_COMPLETION_REPORT.md` covering every sub-step above (3.1's
extraction decision and rationale, the exact doc diffs for 3.2-3.5, both full-build results, the full
`ctest` pass/fail counts, and the complete live-smoke-test evidence from 3.7). Then write a final
`task_manager/frame-debugger-4/CAMPAIGN_COMPLETION_REPORT.md` (mirroring `frame-debugger-3`'s own
`CAMPAIGN_COMPLETION_REPORT.md` structure: goal recap, phase-by-phase summary, final architecture diagram,
file-change inventory, final verification evidence, conclusion) summarizing the whole 3-phase campaign.
Commit (`git_add` + `git_commit`) everything from this phase together in one commit, per this codebase's
own established workflow rule.
