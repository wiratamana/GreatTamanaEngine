# `frame-debugger-4` — Campaign Completion Report: Frame Debugger Must Capture Atmosphere Scattering / Aerial Perspective

Parent: `PHASE0_MASTER_STRATEGY.md`
Branch: `feature/frame-debugger-impl`
Phases: 3 (all landed)

## Goal recap

The Editor's "Frame Debugger" window (built by the `frame-debugger-3` campaign) had a real, confirmed bug:
the retained preview it displayed — whether nothing was selected or any leaf was selected — was **always**
the PRE-atmosphere-composite image, never the real, final, atmosphere-composited output the "Game" panel/
`GET /get_game_view` had already been correctly showing since the `atmosphere-scattering-4` campaign. Two
independent, compounding root causes: (1) `ImGuiEditorLayer::BuildUI()` fed the Frame Debugger the wrong
texture (`m_gameView` instead of `m_gameViewComposited`), and (2) the real, separate
`"AtmosphereAerialPerspectiveCompositePass"` render-graph pass that actually produces the composited image
was entirely invisible anywhere in the event tree — an engineer inspecting the Frame Debugger had no way to
even discover that atmosphere compositing happened that frame. This campaign's goal: make the retained
preview genuinely, correctly composite-aware (defaulting to the final, fog-inclusive image, while still
letting the `"GameView"` leaf show the true pre-composite reconstruction), make the compositing step a real,
visible, selectable tree leaf, and prove — with a live, HTTP-driven, screenshot-verified smoke test, not just
code review — that the fix actually works.

## Phase-by-phase summary

### PHASE1 — Dual-Stage Retained Capture + Composite-Aware Preview Selection (flagged highest-risk phase)

Fixed Root Cause A. `FrameDebuggerHistory` now retains TWO real images per captured frame instead of one:
the unchanged true pre-composite `"GameView"` copy (`FrameDebuggerHistoryEntry::preview`) plus a NEW true
post-composite `"GameViewComposited"` copy (`FrameDebuggerHistoryEntry::compositedPreview`, `std::nullopt`
only for a capture taken before the atmosphere-composite pass had ever produced anything yet this session —
a real, honest, non-one-way-ratchet state, not a bug). Both copies are made inside the SAME
`renderer.ImmediateSubmit()` call (Locked Design Decision #8 — one GPU submission, not two).
`ImGuiEditorLayer::BuildUI()`'s one `FrameDebuggerPanel::Build()` call site now also passes
`m_gameViewComposited` (the class's own existing, already-correctly-populated `RenderTexture*` member — the
exact same one the "Game" panel itself already preferred), closing Root Cause A directly.
`EnsurePreviewDescriptor()`'s new picking rule: the literal `"GameView"` leaf selected → the true pre-composite
image (preserving `frame-debugger-3`'s own original "as of the exact point this event finished" semantics for
that one leaf); anything else at all — including nothing selected — → the true, final, atmosphere-inclusive
image, falling back to the pre-composite one only when no composited copy exists yet. Verified: a full
manual/HTTP smoke check (the first-ever comparison of `GET /get_swapchain`'s default preview against `GET
/get_game_view` on both an empty scene and a real spawned cube, both matching pixel-for-pixel in composition)
plus a fast compile check of both `GreatTamanaEngine`/`GreatTamanaEngineTests`. No deviations from the phase
document.

### PHASE2 — Make the Real "Aerial Perspective Composite" Pass Visible in the Event Tree

Fixed Root Cause B. A pure, additive extension of `src/Editor/FrameDebuggerData.cpp`/`.h` only — no other
production file touched, confirming this phase's own "Risk level: LOW" framing. A new
`BuildAerialPerspectiveCompositeLeaf()` helper builds a real, selectable tree leaf for the already-real
`"AtmosphereAerialPerspectiveCompositePass"` render-graph pass (real `"Read Texture"`/`"Write Texture"` rows
straight from that pass's own `RenderGraphPassSnapshot::readNames`/`writeNames`, a real GPU-timing sample, and
`"n/a (compute pass)"` blend/Z/stencil rows, mirroring the existing `"GPU Skinning"` leaf's own conventions),
appended as `root`'s third child — after the optional `"GPU Skinning"` group and the `"GameView"` leaf — only
when that exact pass name is actually found in the current frame's `graphSnapshot` (the same "only add if
genuinely found" discipline `"GPU Skinning"` already established). The tree row's own displayed text is the
real, raw pass name (`"AtmosphereAerialPerspectiveCompositePass"`), while the shorter, friendlier
`"Aerial Perspective Composite"` label only ever appears in the Inspector's own "Pass" field once the leaf is
selected — a naming clarification added during this campaign's own full double-check pass, carried through
correctly into both PHASE2's own implementation and this closing phase's live-verification evidence. Thanks
to PHASE1's already-generalized picking rule, selecting this new leaf shows the true, final, composited
preview with ZERO further changes anywhere in `Panels/FrameDebuggerPanel.cpp`. Verified: 3 new Tier-1 test
cases (`AerialPerspectiveCompositePassProducesThirdLeafAfterGameView`,
`NoAerialPerspectiveCompositePassAddsNoThirdLeaf`, `AllThreeGroupsAppearTogetherInRealExecutionOrder`) — all
10 `FrameDebuggerSnapshotBuilderTest` cases (7 pre-existing + 3 new) pass. No deviations from the phase
document.

### PHASE3 — Tests Review, Documentation Sweep, Full Build/Regression, Live Verification (this phase)

Reviewed PHASE1's own `EnsurePreviewDescriptor()` picking rule and found it cleanly extractable as a genuine
pure function — extracted `ChooseFrameDebuggerPreviewSource()` (`src/Editor/FrameDebuggerData.h`/`.cpp`), a
new, dedicated, Tier-1-tested decision function covering all six meaningful input combinations the phase
document itself named, wired into `EnsurePreviewDescriptor()` in place of the previously-inline logic (no
behavior change — confirmed by the unchanged live smoke-test results below). Corrected every stale doc claim
this bug had made false: `AGENTS.md`'s "Frame Debugger" section (a new two-sentence clarification), the ENTIRE
`docs/conventions/frame-debugger.md` convention doc (the tree-shape description, the "Preview reconstruction"
bullet rewritten to describe the real dual-stage capture, a new "Aerial Perspective Composite" bullet, and a
brand-new "Known limitation, now fixed" section naming this campaign explicitly), `TODO.md`'s "Frame
Debugger" section (confirmed this bug was never previously listed there, added a `~~struck-through~~"now
fixed"` entry plus one new genuinely-scoped forward-looking item — extending this same dual-stage pattern to
a hypothetical future Scene-View Frame Debugger), and `README.md`'s "Status" section (a new top-of-list bug-fix
bullet). Then ran the full validation this phase's own Step 3.6/3.7 require: a full clean build of BOTH
`GTE_ENABLE_EDITOR=ON` (441/441 steps, zero errors) and `=OFF` (366/366 steps, zero errors, every
Frame-Debugger file correctly absent from the compile log) configurations, a full `ctest` regression pass
(100% of 1406 tests passed, one pre-existing machine-gated skip, unchanged from every prior campaign), and —
the closing achievement of the whole `frame-debugger-4` campaign — a genuine, fully-automated, HTTP-driven,
screenshot-verified live smoke test: spawning a real Sun light + a real, distance-scaled test cube purely
over HTTP, then directly comparing the Frame Debugger's own default preview against `GET /get_game_view`
(matching), explicitly selecting the `"GameView"` leaf (the true pre-composite reconstruction, real
Shader/Pass/Blend/Z-state data), and explicitly selecting the new `"AtmosphereAerialPerspectiveCompositePass"`
leaf (the true post-composite image again, plus real `Shader`/friendly `Pass` label data, confirming the
tree-row-vs-Inspector naming clarification live, on screen). See `PHASE3_COMPLETION_REPORT.md` for the full,
step-by-step evidence. No deviations from the phase document (one small, fully self-corrected, never-committed
`edit_line` mistake mid-implementation is flagged there for transparency).

## Final architecture (as landed)

```
Application::Run() offscreen Execute()
   |
   |-- AddGameViewPass()            writes "GameView"             (pre-composite, real scene geometry)
   |
   |-- AddAtmosphereCompositePass() writes "GameViewComposited"    (post-composite, REAL final image)
   |     (builder.AddComputePass("AtmosphereAerialPerspectiveCompositePass", ...))
   |
   v
ImGuiEditorLayer::BuildUI()
   |  m_gameView (pre-composite)         m_gameViewComposited (post-composite, nullable until first composite)
   |        |                                      |
   |        +------------------+   +---------------+
   |                           v   v
   |         FrameDebuggerPanel::Build(..., gameView, compositedGameView, ...)     <- PHASE1
   |                           |
   |                           v
   |         FrameDebuggerPanel::TriggerCapture()
   |                           |
   |                           v
   |         FrameDebuggerHistory::CaptureFrame(..., gameViewSource, compositedGameViewSource)  <- PHASE1
   |                           |
   |                           v
   |         FrameDebuggerHistoryEntry { snapshot, preview (pre-composite), compositedPreview (post-composite) }
   |
   |         BuildRealFrameDebuggerSnapshot(graphSnapshot, ...)   <- PHASE2 adds the new
   |            root.children = [ "GPU Skinning"? , "GameView" , "AtmosphereAerialPerspectiveCompositePass"? ]
   |                                                                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ NEW leaf
   |
   v
FrameDebuggerPanel::EnsurePreviewDescriptor()
      |
      v
ChooseFrameDebuggerPreviewSource(hasEntry, hasPreview,             <- PHASE3 extracted this exact decision
    hasCompositedPreview, isViewingGameViewLeaf)                      into a pure, Tier-1-tested function
      |
      v
   selected leaf's passName == "GameView"  -> entry.preview            (true pre-composite)
   anything else (incl. nothing selected)  -> entry.compositedPreview, falling back to entry.preview
```

`FrameDebuggerData.h`/`.cpp` remains the pure, ImGui-free data model at the center of it all — now including
both the tree-building logic (PHASE2) AND the preview-source-picking decision logic (PHASE3), both fully
Tier-1-tested with zero live `VkDevice`/ImGui context required.

## File-change inventory (final, as actually landed across all 3 phases)

No new production files this campaign — every change is a surgical extension of `frame-debugger-3`'s own
existing files.

Modified:
- `src/Editor/FrameDebuggerHistory.h`/`.cpp` (PHASE1 — `FrameDebuggerHistoryEntry::compositedPreview`,
  `CaptureFrame()`'s new nullable parameter + dual-copy `ImmediateSubmit()` body)
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (PHASE1 — `Build()`'s new parameter,
  `m_frameGameViewComposited` member, `TriggerCapture()`'s new argument, `EnsurePreviewDescriptor()`'s new
  picking rule; PHASE3 — that same function's picking rule delegated to the new pure
  `ChooseFrameDebuggerPreviewSource()`)
- `src/Editor/ImGuiEditorLayer.cpp` (PHASE1 — the one-line `Build()` call-site fix)
- `src/Editor/FrameDebuggerData.h`/`.cpp` (PHASE2 — new `BuildAerialPerspectiveCompositeLeaf()` +
  `BuildRealFrameDebuggerSnapshot()`'s new lookup/append; PHASE3 — new `FrameDebuggerPreviewSourceChoice`
  enum + `ChooseFrameDebuggerPreviewSource()`)
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (PHASE2 — 3 new test cases)
- `tests/Editor/FrameDebuggerDataTests.cpp` (PHASE3 — 1 new test, `ChooseFrameDebuggerPreviewSourceTest`,
  covering all six documented input combinations)
- `AGENTS.md`, `docs/conventions/frame-debugger.md`, `README.md`, `TODO.md` (PHASE3 — doc corrections)
- `task_manager/frame-debugger-4/PHASE1_COMPLETION_REPORT.md` .. `PHASE3_COMPLETION_REPORT.md`,
  `CAMPAIGN_COMPLETION_REPORT.md` (this file — written as each phase landed)

`tests/Editor/FrameDebuggerHistoryTests.cpp` was **never** touched by any phase of this campaign —
`FrameDebuggerHistory::CaptureFrame()`'s own signature change (PHASE1) does not affect that file at all (it
only exercises the pure `AdvanceFrameDebuggerHistoryWriteState()`/`ClampFrameDebuggerHistoryCursor()` free
functions), confirmed by both `PHASE1_DOUBLE_CHECK_NOTES.md`'s own review and this campaign's own live
compiles/test runs.

## Final verification evidence (PHASE3)

- **Full clean build, `GTE_ENABLE_EDITOR=ON`**: `cmake --build build --clean-first` — 441/441 steps, zero
  errors.
- **Full clean build, `GTE_ENABLE_EDITOR=OFF`**: fresh configure (`-DGTE_ENABLE_EDITOR=OFF`, zero network
  access needed) + `cmake --build build-editor-off --clean-first` — 366/366 steps, zero errors; confirmed
  every Editor-only Frame Debugger source file this campaign touched is entirely excluded.
- **Full `ctest` regression pass**: `ctest -C Debug --output-on-failure` (from `build/`) — **100% of 1406
  tests passed** (1 pre-existing machine-gated skip, `PmxLoaderRealModelSmokeTest`), 132.47 sec total,
  covering every `FrameDebuggerSnapshotBuilderTest`/`FrameDebuggerHistoryTest`/`FrameDebuggerCaptureContextTest`/
  `FrameDebuggerDataTest` case added across this whole 3-phase campaign, plus the entire rest of the suite
  with zero regressions.
- **Live, HTTP-automation-driven, screenshot-verified smoke test**: spawned a real Sun light + a real,
  distance/scale-tuned test cube purely over HTTP (`POST /instantiate_light`/`/instantiate_primitive`/
  `/set_entity_trs`), then — with `GET /frame_debugger/state` confirming the predicted `totalEventCount: 2`
  (no `"GPU Skinning"` group, so `index=0`/`index=1` deterministically map to `"GameView"`/
  `"AtmosphereAerialPerspectiveCompositePass"`) — confirmed via real screenshots at every step: the DEFAULT
  preview (nothing selected) visually matches `GET /get_game_view`'s own real, final, composited output
  (**the direct, positive proof this campaign's core bug is fixed**); explicitly selecting the `"GameView"`
  leaf shows the true pre-composite reconstruction with real Shader/Pass/Blend/Z-state Inspector data;
  explicitly selecting the new `"AtmosphereAerialPerspectiveCompositePass"` leaf (its own raw pass name
  visible in the tree row, confirmed distinct from the Inspector's own friendlier `"Pass = Aerial
  Perspective Composite"` field) shows the same final, composited image again, plus its own real
  `Shader`/`Blend`/`Z`/`Stencil` compute-pass facts — every step confirmed with a real screenshot from a real
  running `GreatTamanaEngine.exe` instance, with zero mouse/keyboard involved at any point. See
  `PHASE3_COMPLETION_REPORT.md` for the full, exact step-by-step transcript.

## Bug fix: CLOSED

The Frame Debugger's retained preview now genuinely, correctly includes the atmosphere-scattering/
aerial-perspective effect by default, exactly as it always should have — the `frame-debugger-3` campaign's
own real per-frame capture machinery was never broken, it was simply wired to the wrong end of the render
pipeline (Root Cause A) with no way to even discover the missing compositing step existed (Root Cause B).
Both root causes are now fixed, with real, dedicated Tier-1 test coverage for the new picking-rule logic and
the new tree leaf, and a genuine, live, HTTP-driven, screenshot-verified proof that the fix works end to end —
not just a code-review assertion that it should.

## Outstanding / deferred (see `TODO.md` for the full, current list)

- **Extending this same dual-stage retained-capture/composite-aware preview-selection pattern to a future
  Scene-View Frame Debugger** — a real, clearly-scoped gap this campaign's own PHASE3 review surfaced (no
  Scene-View Frame Debugger capture exists at all today — Locked Design Decision #4,
  `PHASE0_MASTER_STRATEGY.md` — unchanged, not attempted here), not a padding-only entry.
- Every item already listed as "Still genuinely deferred" under `TODO.md`'s "Frame Debugger" section from the
  `frame-debugger-3` campaign (per-individual-draw-call granularity, Scene View/Present capture in general, a
  full generic shader-reflection system, a redundant Pause/frozen-snapshot control, and publishing the
  retained preview into `RenderGraphDebugTextureRegistry`) remains unchanged and out of scope for this
  campaign too.

## Conclusion

All three phases of `frame-debugger-4` landed exactly per `PHASE0_MASTER_STRATEGY.md`'s plan, with the one
self-caught, fully-corrected, never-committed editing mistake documented in `PHASE3_COMPLETION_REPORT.md` for
transparency. The Editor's "Frame Debugger" window now genuinely shows the Game View exactly as the player/
user actually sees it — atmosphere scattering and aerial-perspective fog included — by default, with the true
pre-composite reconstruction still available via the `"GameView"` leaf and the compositing step itself now a
real, visible, selectable `"AtmosphereAerialPerspectiveCompositePass"` tree leaf. Verified by a full clean
build (both Editor configurations, 441/441 and 366/366 steps respectively, zero errors), a full `ctest`
regression pass (100% of 1406 tests), and a genuine, fully-automated, HTTP-driven, screenshot-verified live
smoke test directly proving the fix — the same rigor `frame-debugger-3`'s own closing phase established as
this feature's permanent verification bar.
