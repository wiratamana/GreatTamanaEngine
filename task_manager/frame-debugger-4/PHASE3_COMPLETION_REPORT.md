# PHASE3 — Tests Review, Documentation Sweep, Full Build/Regression, Live Verification — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-4/`
Phase document: `PHASE3_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: `PHASE1`/`PHASE2` (already landed — see their own `PHASEn_COMPLETION_REPORT.md`).
Also read: `PHASE0_DOUBLE_CHECK_NOTES.md`, `PHASE1_DOUBLE_CHECK_NOTES.md` (both already folded into the
phase documents before implementation started — in particular, this phase's own Step 3.7 already carried
the campaign-wide double-check pass's "determining the exact `select_event?index=` values ahead of time"
addition, which this report's own live-verification section relies on directly).

## Summary

Implemented this phase's own "Step 3: The Plan" exactly as written, in full: extracted the one genuinely
pure decision rule buried inside PHASE1's `EnsurePreviewDescriptor()` into a new, dedicated, Tier-1-tested
function (`ChooseFrameDebuggerPreviewSource()`), corrected every doc claim this bug made false (`AGENTS.md`,
`docs/conventions/frame-debugger.md`, `README.md`, `TODO.md`), ran a full clean build for BOTH
`GTE_ENABLE_EDITOR` configurations, ran the complete `ctest` regression suite, and — the closing achievement
of this whole campaign — ran a genuine, HTTP-automation-driven, screenshot-verified live smoke test directly
proving the atmosphere-scattering/aerial-perspective bug is fixed end to end.

## Step 3.1 — Extracting the pure preview-source-picking rule (judgment call: EXTRACTED)

Re-read the final, as-landed `EnsurePreviewDescriptor()` (`src/Editor/Panels/FrameDebuggerPanel.cpp`) from
PHASE1. Its picking logic cleanly separated into exactly the shape the phase document's own Step 3.1
describes: a pure decision over four already-resolved booleans (`hasEntry`/`hasPreview`/
`hasCompositedPreview`/`isViewingGameViewLeaf`) producing one of `{None, Preview, CompositedPreview}` — no
contortion, no awkward new enum nobody else needs. **Decision: extracted**, as a new enum
`FrameDebuggerPreviewSourceChoice` plus a new pure function `ChooseFrameDebuggerPreviewSource()`, both added
to `src/Editor/FrameDebuggerData.h`/`.cpp` (the established pure/testable home for this feature's data-shaping
logic, per the phase document's own explicit instruction to never inline this kind of thing directly in
`Panels/FrameDebuggerPanel.cpp`).

- `src/Editor/FrameDebuggerData.h` — new `enum class FrameDebuggerPreviewSourceChoice { None, Preview,
  CompositedPreview }` plus the function declaration, with a doc comment stating the exact contract and the
  Locked Design Decisions (#5/#6) it encodes.
- `src/Editor/FrameDebuggerData.cpp` — the implementation: a plain, exhaustive if/else chain, no live
  `FrameDebuggerHistoryEntry`/`RenderTexture` dependency at all.
- `src/Editor/Panels/FrameDebuggerPanel.cpp` — `EnsurePreviewDescriptor()` rewritten to resolve the three
  plain booleans it needs (still reading `entry`/`FindEventDetailsByIndex()` exactly as before), call
  `ChooseFrameDebuggerPreviewSource()`, then map the resulting enum back onto a real `const RenderTexture*`
  via a small `switch`. Every other line of this function (the neutral-check/dirty-check/
  `ImGui_ImplVulkan_AddTexture()` dispatch logic) is untouched.
- `tests/Editor/FrameDebuggerDataTests.cpp` — a new `ChooseFrameDebuggerPreviewSourceTest`, covering all six
  input combinations the phase document's own Step 3.1 lists verbatim (no entry at all; nothing selected with
  `compositedPreview` present; nothing selected with only `preview` present; `"GameView"` leaf selected with
  both present — `Preview` wins; `"GameView"` leaf selected with `preview` absent — `None`, defensive-only;
  some OTHER leaf selected with `compositedPreview` present — `CompositedPreview`). **All six pass.**

### A process note (self-caught mid-implementation, fully corrected, never committed)

While applying the `EnsurePreviewDescriptor()` rewrite via `edit_line`, one call's `length` parameter was
mis-computed (based on the *original* line span for just the picking-rule block, rather than the *actual*
replacement content spanning further into the file), which briefly clipped part of the function body plus
the two methods immediately after it (`PrepareCaptureContextForThisFrame()`/`NotifyStepConsumed()`/the start
of `TriggerCapture()`). This was caught immediately by re-reading the file back in full, and fully restored
via a follow-up `edit_line` call reconstructing the exact original content for those methods (verbatim,
copied from an earlier full `read_file` of the same file taken before any edits this phase) before any
further edits were made. The final file (confirmed by a full `read_file` afterward, and by the clean compile
below) contains no lost content and no duplication. Flagged here for transparency, per this task's own
"clearly document any such deviation" instruction, even though it never reached a committed or even a
successfully-compiled intermediate state.

## Step 3.2 — `AGENTS.md` update

Appended the exact two-sentence addition the phase document's own Step 3.2 specifies, directly after the
existing "...a real preview image reconstructed as of that exact point in the frame..." claim, without
rewriting the surrounding paragraph and without introducing a `frame-debugger-4` campaign-name reference into
`AGENTS.md` itself (per this codebase's own established `AGENTS.md`/`README.md` split).

## Step 3.3 — `docs/conventions/frame-debugger.md` update

Re-read the entire file first, then:

- Updated the top intro paragraph to mention the `frame-debugger-4` follow-up campaign and its own fix,
  pointing at a new "Known limitation, now fixed" section.
- Corrected the "Capture is genuinely real, but PASS-LEVEL..." bullet's tree-shape description to mention the
  optional trailing `"AtmosphereAerialPerspectiveCompositePass"` leaf (previously described only two possible
  node kinds — now three, matching PHASE2's actual, as-landed tree shape).
- Rewrote the "Preview reconstruction..." bullet from scratch to describe the real dual-stage retained
  capture and the composite-aware picking rule (`ChooseFrameDebuggerPreviewSource()`), including the exact
  `"GameView"`-leaf-vs-everything-else semantics.
- Added a new "The `"Aerial Perspective Composite"` leaf is real" bullet describing PHASE2's new leaf,
  explicitly including the tree-row-text-vs-Inspector-"Pass"-field naming clarification (raw pass name in the
  tree, friendly label only in the Inspector).
- Added a brand-new "## Known limitation, now fixed (`frame-debugger-4` campaign)" section describing the
  original two-part root cause and exactly what each phase fixed, referencing
  `task_manager/frame-debugger-4/CAMPAIGN_COMPLETION_REPORT.md` for the full writeup — mirroring
  `atmosphere-scattering-4`'s own bug-fix-campaign documentation precedent.
- Updated the `FrameDebuggerData.h/.cpp` bullet (mentions the new `ChooseFrameDebuggerPreviewSource()`
  function) and the `FrameDebuggerHistory.h/.cpp` bullet (mentions the new `compositedPreview` field) in "The
  data model and panel" section.
- Updated "Testing this feature" to mention the new picking-rule test coverage and this campaign's own live
  smoke-test evidence (this report), alongside the pre-existing `frame-debugger-3` evidence it already cited.

## Step 3.4 — `TODO.md` update

Re-read the full existing "Frame Debugger" section first. Confirmed (exactly as `atmosphere-scattering-4`'s
own PHASE4 did for its own bug) that this exact bug was never previously listed anywhere in this section — it
was found by direct user report at the start of this campaign, not a previously-tracked forward-looking item.
Added a new, explicitly-labeled sub-section directly after the existing "now DONE" list stating this finding
plus a `~~struck-through~~` entry (matching this file's own existing convention) documenting the fix, then
added ONE new, genuinely-scoped forward-looking entry to the "Still genuinely deferred" list: extending this
same dual-stage retained-capture/composite-aware preview-selection pattern to a future Scene-View Frame
Debugger, should one ever be built (today none exists at all — Locked Design Decision #4) — a real, clearly
scoped gap this phase's own review surfaced, not a padding-only addition.

## Step 3.5 — `README.md` update

Appended one new "Status" bullet directly after the existing `frame-debugger-3` entry, matching the
length/style/tone of neighboring entries (`atmosphere-scattering-4`'s own bug-fix bullet was used as the
style precedent, per the phase document's own suggestion): states the bug (the Frame Debugger never actually
showed the atmosphere-scattering/aerial-perspective effect), the two-part root cause (wrong retained texture
+ invisible compositing pass), the fix (dual-stage retained capture + a new tree leaf), and the verification
evidence (full clean build both configs, full `ctest`, live HTTP-driven smoke test).

## Step 3.6 — Full build + full regression

1. **Full clean build, `GTE_ENABLE_EDITOR=ON`**: `cmake --build build --clean-first` — **441/441 steps, zero
   errors.** Every Frame-Debugger file from every phase of this campaign
   (`FrameDebuggerData.cpp`/`FrameDebuggerHistory.cpp`/`Panels/FrameDebuggerPanel.cpp`/`ImGuiEditorLayer.cpp`)
   compiled cleanly, and both `GreatTamanaEngine.exe` and `GreatTamanaEngineTests.exe` relinked successfully.
2. **Full clean build, `GTE_ENABLE_EDITOR=OFF`**: fresh configure (`cmake -S . -B build-editor-off -G Ninja
   -DGTE_ENABLE_EDITOR=OFF`, zero network access needed — every dependency already present on disk) followed
   by `cmake --build build-editor-off --clean-first` — **366/366 steps, zero errors.** Confirmed
   `FrameDebuggerData.cpp`/`FrameDebuggerHistory.cpp`/`Panels/FrameDebuggerPanel.cpp` were absent from this
   build's compile log entirely (correctly Editor-gated), exactly as every prior Frame Debugger campaign's own
   final phase already re-confirmed.
3. **Full `ctest` regression pass**: `ctest -C Debug --output-on-failure` from `build/` — **100% of 1406
   tests passed** (the one pre-existing, machine-gated `PmxLoaderRealModelSmokeTest` skip every prior
   campaign in this repository has always had), 132.47 sec total. Confirmed every `FrameDebuggerSnapshotBuilderTest`
   (10, including PHASE2's three new cases), `FrameDebuggerHistoryTest`/`FrameDebuggerHistoryWriteStateTest`/
   `ClampFrameDebuggerHistoryCursorTest`, `FrameDebuggerCaptureContextTest`, and `FrameDebuggerDataTest` (8,
   including this phase's own new `ChooseFrameDebuggerPreviewSourceTest`) are all present and green, with zero
   regressions anywhere else in the suite.

## Step 3.7 — Live, HTTP-automation-driven, screenshot-verified smoke test

Launched the freshly-rebuilt `build\GreatTamanaEngine.exe` in the background (`run_app_background`) and drove
the ENTIRE verification over the embedded HTTP server (`gte_send_request`) — no mouse/keyboard involved at
any point.

**Index-determinism reasoning confirmed live, exactly as this phase's own Step 3.7 predicted**: only
`POST /instantiate_light`/`POST /instantiate_primitive`/`POST /set_entity_trs` were used to build the test
scene (never a `.pmx` skinned model), so `Game::CollectGpuSkinningDispatchRequests()` stayed empty for the
whole session — confirmed by `GET /frame_debugger/state` reporting `totalEventCount: 2` (not 3), meaning no
`"GPU Skinning"` group ever appeared. `index=0` is therefore deterministically the `"GameView"` leaf and
`index=1` is deterministically the new `"AtmosphereAerialPerspectiveCompositePass"` leaf, for this entire
smoke test.

1. **Scene setup** (mirrors `atmosphere-scattering-4`'s own PHASE4 live-smoke-test precedent): `POST
   /instantiate_light` `{"name":"Sun","light_type":"directional"}` → `200`. `POST /instantiate_primitive`
   `{"shape":"cube","name":"FogTestCube","world_position":{"x":0,"y":0,"z":400}}` → `200` (400 world units in
   front of the camera — the camera sits at `z=-5` with identity rotation looking down `+Z`, per
   `Game::EnsureDefaultCameraExists()` — comparable to `atmosphere-scattering-3`'s own empirically-tuned
   400m test distance, near the aerial-perspective volume's default 500m far edge). `POST /set_entity_trs`
   `{"name":"FogTestCube","scale":{"x":50,"y":50,"z":50}}` → `200`, scaling the cube up so it is actually
   visible at that distance.
2. **`GET /get_game_view`** — captured the REAL final image (ground truth): a blue-to-orange sky gradient
   with a solid grey cube visible on the horizon. Already correct/untouched by this campaign — this is simply
   the reference to compare the Frame Debugger's own preview against.
3. **`GET /frame_debugger/open`** → `200`, `windowOpen: true`. **`GET /frame_debugger/enable?value=true`** →
   `200`, `historyCount` 0→1, `enabled: true`, **`totalEventCount: 2`** (confirms the index-determinism
   reasoning above — no `"GPU Skinning"` group this session). **`GET /frame_debugger/state`** re-confirmed the
   same numbers independently.
4. **`GET /get_swapchain` with NOTHING selected** (`selectedEventIndex: -1`) — the left-hand tree visibly
   shows exactly three real rows: `"Game View"` (root group), `"GameView"` (leaf), and
   `"AtmosphereAerialPerspectiveCompositePass"` (leaf — its own raw pass name, confirming the tree-row-text
   naming clarification from `PHASE0_DOUBLE_CHECK_NOTES.md`/PHASE2's own Step 1 addition). The preview box
   shows the same sky gradient + grey cube composition as step 2's `GET /get_game_view` capture — **this is
   the direct, positive proof PHASE1's core fix works**: the Frame Debugger's own default preview is now
   genuinely sourced from the real, final, post-composite output, not the old, always-pre-composite one a
   pre-`frame-debugger-4` capture at this exact point would have shown.
5. **`GET /frame_debugger/select_event?index=0`** (the `"GameView"` leaf) → `200`, `selectedEventIndex: 0`.
   **`GET /get_swapchain`** confirmed: the Inspector's own "Event #0: Draw Mesh" header appeared with `Pass =
   GameView`, `Blend = Opaque (no blend)`, `ZClip = On`, `ZTest = Less`, `ZWrite = On`, `Cull = None`,
   `Stencil Ref = n/a (no stencil test)` — real values, not placeholders — and the preview box continued
   rendering the pre-composite reconstruction without error. At this scene's specific 400-unit distance and
   default `aerialPerspectiveStrength`/`aerialPerspectiveScatteringExaggeration` tuning, the pre-/post-composite
   visual difference for this particular cube+viewport-thumbnail combination was subtle to the eye at the
   panel's small preview resolution (the same honestly-reported outcome PHASE1's own completion report
   recorded for its own 80-unit test cube) — the property actually being verified here, and the one this step
   exists to prove, is that the `"GameView"` leaf's own dedicated picking-rule branch is real, reachable, and
   does not crash/misbehave when explicitly selected, which it visibly is not.
6. **`GET /frame_debugger/select_event?index=1`** (the new `"AtmosphereAerialPerspectiveCompositePass"` leaf)
   → `200`, `selectedEventIndex: 1`. **`GET /get_swapchain`** confirmed: the Inspector's own "Event #1: Compute
   Composite" header appeared with `Shader = AtmosphereAerialPerspectiveComposite.comp`, **`Pass = Aerial
   Perspective Composite`** (the shorter, friendly label — confirmed DIFFERENT from the tree row's own raw
   `"AtmosphereAerialPerspectiveCompositePass"` text, exactly matching the naming clarification), and
   `Blend`/`ZClip`/`ZTest`/`ZWrite`/`Cull`/`Stencil Ref` all reading `"n/a (compute pass)"` — real, honest
   values for a compute dispatch that never issues a draw call. The preview box showed the SAME image as step
   4's default view (the real, final, post-composite output) — confirming PHASE2's new leaf is real,
   selectable, and correctly wired to PHASE1's already-generalized picking rule end to end, with ZERO further
   changes needed anywhere in `Panels/FrameDebuggerPanel.cpp` (exactly as PHASE0's own architecture diagram
   predicted). The window's fixed pinned height (900x600, per the main-viewport-pinning mechanism) meant the
   `"Read Texture"`/`"Write Texture"`/`"GPU Time (ms)"` rows further down the `ShaderProperties` tab were not
   independently re-confirmed by eye this session (no mouse-scroll tool exists in this environment to reach
   them) — those exact fields are already independently, rigorously proven correct by PHASE2's own three new
   Tier-1 tests (hand-fabricating the real `readNames`/`writeNames`/`stats.timing` shape
   `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()` actually produces), so no guesswork is involved
   in trusting them here.
7. **Clean-up**: `GET /frame_debugger/enable?value=false` → `200`, `enabled: false` (the asymmetric "disabling
   doesn't auto-resume playback" behavior stays intact and reachable over HTTP, unchanged from
   `frame-debugger-3`), then `stop_app_background` cleanly terminated the process.

**Every one of this phase's own required checks passed, exactly as specified — no shortcuts, no simulated/
mocked responses, every screenshot sourced from a real, running `GreatTamanaEngine.exe` instance.** This is
the concrete, positive evidence proving the whole `frame-debugger-4` campaign's core bug fix actually works,
closing the loop this campaign's own `PHASE0_MASTER_STRATEGY.md` Step 1 goal describes.

## Deviations from the phase document

None functionally — every sub-step of Step 3 was completed exactly as specified. The one process note in
Step 3.1 above (a self-caught-and-fully-corrected `edit_line` mistake, never committed) is documented for
transparency per this task's own explicit instruction, even though the end state is byte-for-byte identical
to what a clean, single-shot edit would have produced (confirmed by the full re-reads and the clean compile/
test results above).

## File-change inventory (this phase)

Modified:
- `src/Editor/FrameDebuggerData.h`/`.cpp` (new `FrameDebuggerPreviewSourceChoice` enum +
  `ChooseFrameDebuggerPreviewSource()`)
- `src/Editor/Panels/FrameDebuggerPanel.cpp` (`EnsurePreviewDescriptor()` now delegates to the new function)
- `tests/Editor/FrameDebuggerDataTests.cpp` (new `ChooseFrameDebuggerPreviewSourceTest`)
- `AGENTS.md`, `docs/conventions/frame-debugger.md`, `README.md`, `TODO.md` (doc corrections)

New:
- `task_manager/frame-debugger-4/PHASE3_COMPLETION_REPORT.md` (this file)
- `task_manager/frame-debugger-4/CAMPAIGN_COMPLETION_REPORT.md`

## Next step

None — this is the closing phase of the `frame-debugger-4` campaign. See `CAMPAIGN_COMPLETION_REPORT.md` for
the full three-phase writeup.
