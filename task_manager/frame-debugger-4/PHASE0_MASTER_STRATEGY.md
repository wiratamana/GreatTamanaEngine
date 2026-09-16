# PHASE0 — MASTER STRATEGY: Frame Debugger Must Capture Atmosphere Scattering / Aerial Perspective

Campaign folder: `task_manager/frame-debugger-4/`
Branch: `feature/frame-debugger-impl`
Depends on: `task_manager/frame-debugger-3/` (the real-capture campaign — `src/Editor/FrameDebuggerCapture.h/.cpp`,
`FrameDebuggerData.h/.cpp`, `FrameDebuggerHistory.h/.cpp`, `Panels/FrameDebuggerPanel.h/.cpp`,
`FrameDebuggerPreviewProcessing.h/.cpp`) and `task_manager/atmosphere-scattering-4/` (the campaign that made
`"GameViewComposited"` the PERMANENT, always-on real output of the Game View, replacing the old
pre-atmosphere `"GameView"` texture as what the Editor's "Game" panel / `GET /get_game_view` actually show).

This is the **orchestrator** document. It contains no implementation instructions of its own — each child
phase (`PHASE1`..`PHASE3`) is a self-contained, independently compilable chunk. Read this file first, then
work the phases **in numeric order** (each assumes every previous one already landed). Every phase file
below MUST open with "## Parent -> PHASE0_MASTER_STRATEGY.md, read it first" and end with its own fast
compile-check command plus a `PHASEn_COMPLETION_REPORT.md` write-up, exactly like `frame-debugger-3`'s own
precedent.

## Phase list

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_DUAL_STAGE_RETAINED_CAPTURE_AND_PREVIEW_SELECTION.md` | The core bug fix: `FrameDebuggerHistory` retains TWO images per captured frame (the true pre-atmosphere-composite `"GameView"` copy, and the real post-composite `"GameViewComposited"` copy), wired end-to-end from `ImGuiEditorLayer::BuildUI()` down to `FrameDebuggerHistory::CaptureFrame()`, with the Inspector's preview box picking the right one based on which tree event is selected (defaulting to the final, atmosphere-inclusive image whenever nothing/anything-other-than-"GameView" is selected). |
| 2 | `PHASE2_AERIAL_PERSPECTIVE_COMPOSITE_EVENT_TREE_LEAF.md` | Makes the already-real `"AtmosphereAerialPerspectiveCompositePass"` render-graph pass VISIBLE in the Frame Debugger's own left-hand event tree, as a new leaf sibling of `"GameView"` (mirroring the existing `"GPU Skinning"` leaf pattern), with real read/write texture names + GPU timing — a pure, additive, self-contained data-model change requiring zero further changes to Phase 1's picking logic (it already generalizes correctly). |
| 3 | `PHASE3_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md` | Widens/adds Tier-1 tests for both prior phases, corrects every doc claim this bug made false (`AGENTS.md`, `docs/conventions/frame-debugger.md`, `README.md`, `TODO.md`), then a full clean build (both `GTE_ENABLE_EDITOR` configs) + full `ctest` regression + a live, HTTP-automation-driven, screenshot-verified smoke test proving the Frame Debugger's captured/previewed image now genuinely includes the atmosphere-scattering/aerial-perspective effect. |

---

## Step 1: The Goal (Where are we going?)

The Editor's "Frame Debugger" window (built by the `frame-debugger-3` campaign) must show the Game View
**exactly as the player/user actually sees it**, atmosphere scattering and aerial-perspective fog included —
today it silently does not. Concretely, after this campaign:

- The retained preview image the Frame Debugger displays by default (nothing selected, or any leaf other
  than the raw `"GameView"` pass itself) is the REAL, final, atmosphere-composited frame — pixel-for-pixel
  the same image the "Game" panel and `GET /get_game_view` show, fog/haze included.
- Selecting the `"GameView"` leaf explicitly still shows the TRUE pre-composite image (a real, honest "as of
  the exact point this specific event finished" reconstruction — preserving `frame-debugger-3`'s own Locked
  Design Decision #5 semantics for that one specific leaf).
- A brand-new `"Aerial Perspective Composite"` leaf appears in the event tree, sibling to `"GameView"`,
  showing the real post-composite image plus real read/write texture names and GPU timing for that actual
  compute pass — so an engineer can literally see, in the tree, that atmosphere compositing ran this frame,
  not just infer it from the final pixels looking foggy. **Naming clarification (added during this
  campaign's own full double-check pass):** exactly like the existing `"GPU Skinning"` group's own children
  (which show each real dispatch's raw pass name, e.g. `"SkinPass_A"`, never a prettified one), this new
  leaf's own TREE ROW TEXT is its real, raw render-graph pass name, `"AtmosphereAerialPerspectiveCompositePass"`
  — `"Aerial Perspective Composite"` is the shorter, friendlier grouping label shown in the Inspector's own
  "Pass" field once the leaf is selected (`FrameDebuggerEventDetails::passName`), not the tree label itself.
  Anyone verifying this leaf live (by eye, or via a screenshot) should expect to see the longer raw name in
  the tree pane specifically.
- Every existing Tier-1 test still passes, new Tier-1 tests cover the new tree leaf and (wherever testable
  without a live `VkDevice`) the new picking logic, and both `GTE_ENABLE_EDITOR=ON`/`=OFF` configurations
  still build cleanly.

## Step 2: The Situation (Where are we now?) — full root-cause investigation

Investigated directly in this repository, on `feature/frame-debugger-impl` (every file/line reference below
was correct at investigation time — render-graph/Editor code churns quickly in this repo, so re-read the
live source at implementation time rather than trusting a stale line number):

### 2.1 The render pipeline, as it actually runs today (confirmed in `src/Application/Application.cpp`)

For the Game View, every real frame does, in this exact order, inside ONE `RenderGraphBuilder` callback:

1. Zero-or-more real GPU-skinning compute passes (per skinned model this frame).
2. The real `"GameView"` graphics pass (`AddGameViewPass()`, `src/Application/RenderPasses.cpp`) — draws
   the scene geometry + the Sky Background, into a texture imported as `"GameView"`
   (`b.ImportTexture("GameView", gameTarget->Target(), ...)`).
3. **A second, separate, real compute pass, declared immediately after #2 in the SAME builder callback**:
   `AddAtmosphereCompositePass()` (`src/Application/AtmospherePassSequence.cpp`) →
   `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`
   (`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`), which calls
   `builder.AddComputePass("AtmosphereAerialPerspectiveCompositePass", ...)` — a REAL, NAMED render-graph
   pass, reading `"GameView"`'s own just-written color AND depth plus the aerial-perspective volume, and
   writing a brand-new, SEPARATE output texture imported as `"GameViewComposited"`
   (`VK_FORMAT_R8G8B8A8_UNORM`, storage-image-capable).
4. `Application::Run()`'s own finalize block (still `Application.cpp`) then does, unconditionally, whenever
   `gameTarget != nullptr`: finalizes `"GameViewComposited"` for external sampling, and calls
   `m_editorLayer->SetGameViewCompositedTexture(m_atmosphereLutRenderer.CompositedOutput("GameViewComposited"))`
   — handing `ImGuiEditorLayer` a stable pointer to the REAL, final, atmosphere-composited output. The SAME
   finalize block is also what `GET /get_game_view`'s own capture path reads from
   (`m_atmosphereLutRenderer.CompositedOutput("GameViewComposited")`, falling back to the pre-composite
   `gameTarget` only in the — should-be-unreachable — case the composited texture doesn't exist yet).

**Since the `atmosphere-scattering-4` campaign, `"GameViewComposited"` is what the Game View PERMANENTLY,
ALWAYS actually is, end-to-end** — the "Game" panel's own descriptor logic in
`ImGuiEditorLayer::BuildUI()` already prefers `m_gameViewComposited` over the raw `m_gameView` whenever a
composited texture is available (falling back to `m_gameView` only before the very first composite ever
ran). `"GameView"` itself is now, functionally, an internal INTERMEDIATE texture on the way to the real
final image, not the final image itself.

### 2.2 The bug, precisely (two independent, compounding root causes)

**Root cause A — the Frame Debugger's own retained preview is fed the WRONG texture.**
`ImGuiEditorLayer::BuildUI()` calls (confirmed live):

```cpp
m_frameDebuggerPanel.Build(m_ctx, renderer, renderGraph, m_gameView, gpuSkinningPassNames);
```

— passing `m_gameView` (the PRE-composite, atmosphere-FREE raw render target), never
`m_gameViewComposited` (the REAL final image the "Game" panel itself displays a few lines earlier in the
very same function, via its own separate `gameSource = (m_gameViewComposited != nullptr) ? m_gameViewComposited
: &m_gameView` local). `FrameDebuggerPanel::Build()` stashes this as `m_frameGameView`, and
`TriggerCapture()` hands it straight to `FrameDebuggerHistory::CaptureFrame()`, which makes its ONE retained
GPU-to-GPU copy from it. **The Frame Debugger has therefore never once retained an image containing the
atmosphere-scattering effect — not because compositing is broken, but because the Frame Debugger was wired
to the wrong end of the pipeline.**

**Root cause B — the compositing step is entirely invisible in the event tree.**
`FrameDebuggerData.cpp`'s `BuildRealFrameDebuggerSnapshot()` only ever looks up ONE pass by name,
`FindPassByName(graphSnapshot.passesInExecutionOrder, "GameView")`, and builds exactly two kinds of tree
node: an optional `"GPU Skinning"` group, and one final `"GameView"` leaf. The real, separate
`"AtmosphereAerialPerspectiveCompositePass"` — which genuinely exists in the very same
`gte::rg::RenderGraphSnapshot` this function already reads, right alongside `"GameView"` — is never looked
up, never shown, and never mentioned anywhere in the tree. An engineer inspecting the Frame Debugger has
**no way to even discover that atmosphere compositing happened this frame**, regardless of which image is
shown.

### 2.3 Why this is a genuinely safe, well-isolated fix (confirmed, not assumed)

- `FrameDebuggerHistory::CaptureFrame()`'s existing barrier/copy discipline
  (`rg::EmitImageBarrier`/`vkCmdCopyImage` inside one `renderer.ImmediateSubmit()` lambda) is completely
  format-agnostic: the retained copy is always created via `renderer.CreateRenderTexture(..., gameViewSource.Format(),
  ...)`, i.e. it always matches whichever REAL source it was given, byte for byte. Feeding it
  `"GameViewComposited"` (`VK_FORMAT_R8G8B8A8_UNORM`) instead of/alongside `"GameView"` (the swapchain's own
  negotiated, commonly-BGRA format) introduces no format-mismatch risk anywhere, because a `vkCmdCopyImage`
  is only ever attempted between a source and a destination created from THAT SAME source's own format.
- `FrameDebuggerPreviewRenderer::RenderPreview()` (the Channels/Levels compute shader dispatcher,
  `FrameDebuggerPreviewProcessing.h`) already takes `const RenderTexture& source` and only ever SAMPLES it
  (never requires the source itself to be storage-capable) — fully agnostic to which of the two retained
  textures it is pointed at.
- None of the existing Tier-1 tests
  (`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`, `tests/Editor/FrameDebuggerHistoryTests.cpp`)
  hand-fabricate a `gte::rg::RenderGraphPassSnapshot` named `"AtmosphereAerialPerspectiveCompositePass"` —
  so a new, additive "only add a leaf/behavior if that exact pass name is found" pattern (mirroring the
  existing `"GPU Skinning"` group's own "only add if at least one real pass actually matched" precedent)
  cannot silently break any of them. `FrameDebuggerHistory::CaptureFrame()` itself is explicitly documented
  as Tier-2 (no live `VkDevice` in its own test file) — its signature can change freely without touching a
  single existing unit test.
- `Panels/FrameDebuggerPanel.cpp`'s `RenderEventNode()` (the tree-drawing recursion) is already fully
  generic over `node.children` — it makes no assumption anywhere about exactly how many children a group
  has, so a tree that now has three real leaves instead of two needs zero rendering-code changes.

## Step 3: The Plan (detailed strategy)

### 3.1 Architecture at a glance (after this campaign)

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
   |         FrameDebuggerPanel::Build(..., gameView, compositedGameView, ...)     <- PHASE1 (new parameter)
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
   |            root.children = [ "GPU Skinning"? , "GameView" , "Aerial Perspective Composite"? ]
   |                                                                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^ NEW leaf
   |
   v
FrameDebuggerPanel::EnsurePreviewDescriptor()          <- PHASE1's new picking rule:
      selected leaf's passName == "GameView"  -> entry.preview            (true pre-composite)
      anything else (incl. nothing selected)  -> entry.compositedPreview, falling back to entry.preview
```

### 3.2 Locked Design Decisions (from the user's own answers during this campaign's own design review —
do not relitigate these during implementation; if a phase document's plan conflicts with one of these, the
PHASE document is wrong and must be fixed, not this list)

1. **Full per-stage fidelity, not a single merged image.** `FrameDebuggerHistory` retains TWO images per
   captured frame — the true pre-composite `"GameView"` copy and the true post-composite
   `"GameViewComposited"` copy — so that selecting the `"GameView"` tree leaf still shows the state
   "as of that exact point" (preserving `frame-debugger-3`'s own Locked Design Decision #5 literally), while
   a NEW `"Aerial Perspective Composite"` leaf shows the true after-state. This was an explicit, deliberate
   user choice over the cheaper "just always show the final composited image everywhere" alternative.
2. **The new tree leaf is REQUIRED, not optional polish.** `"AtmosphereAerialPerspectiveCompositePass"`
   must become a real, visible, selectable event in the tree (mirroring the existing `"GPU Skinning"` leaf
   pattern: real read/write texture names + real GPU timing, `"n/a (compute pass)"` blend/Z/stencil rows) —
   an explicit user requirement, independent of the dual-image decision above.
3. **No new permanent Editor regression-diagnostic tool this campaign** (unlike
   `atmosphere-scattering-4`'s own "Validate Aerial Perspective Sky Purity" button) — Tier-1 unit tests on
   the pure builder/picking functions are the user's own explicitly chosen, sufficient regression safety
   net for this bug class. Do not add a new Editor panel button/diagnostic pass this campaign.
4. **Scope stays strictly Game-View-only**, exactly matching `frame-debugger-3`'s own pre-existing Locked
   Design Decision #7 — this campaign does not add Scene-View capture, and does not touch
   `"SceneViewComposited"` anywhere. (Scene View has no Frame Debugger capture at all today, and this
   campaign does not add one.)
5. **The pre-composite `"GameView"` leaf's own picking rule stays keyed on `details->passName == "GameView"`
   specifically** (not `eventIndex == 0`, not "the first leaf", not "whichever leaf is NOT the new one") —
   robust to future tree-shape changes (e.g. a future GPU-skinning group with a different number of
   children) and self-documenting at the call site.
6. **The "else" branch of the picking rule (anything other than the literal `"GameView"` leaf, INCLUDING
   nothing selected) always prefers `compositedPreview`, falling back to `preview` only when
   `compositedPreview` itself is `std::nullopt`.** This one rule, written ONCE in PHASE1, is what makes
   PHASE2's brand-new `"Aerial Perspective Composite"` leaf correctly show the post-composite image with
   ZERO further changes to the picking logic — PHASE2 only ever touches the DATA/TREE model, never
   `Panels/FrameDebuggerPanel.cpp`'s preview-selection code.
7. **`FrameDebuggerHistory::CaptureFrame()`'s new `compositedGameViewSource` parameter is a plain, nullable
   `RenderTexture*`** (never a reference, never a `std::optional<std::reference_wrapper<...>>`) — the
   simplest possible way to represent "not available yet, but that's a real and honest state, not an
   error", mirroring `IEditorLayer::AddBlurValidationPass()`'s own `std::optional<rg::TextureHandle>`-style
   "genuinely absent this frame" precedent in spirit (a raw nullable pointer is the more direct, idiomatic
   choice here since `RenderTexture` is neither trivially copyable nor already wrapped in `std::optional` at
   the call site — `ImGuiEditorLayer::m_gameViewComposited` is ALREADY exactly this same `RenderTexture*`
   shape).
8. **Both retained copies are made inside the SAME `renderer.ImmediateSubmit()` call, not two separate
   ones** — a single GPU submission + fence wait for the whole `CaptureFrame()` call, exactly as cheap as
   the current single-copy version plus one more `vkCmdCopyImage` (still a genuinely on-demand, "at most
   once per real capture trigger" cost, never per-frame).
9. **Three phases** (`PHASE1`..`PHASE3`), chosen so the core bug fix (PHASE1), the tree-visibility
   enhancement (PHASE2), and the tests/docs/full-verification closeout (PHASE3) are each independently
   compilable, low-risk, reviewable chunks — PHASE1 is flagged as this campaign's single highest-risk phase
   (see Step 3.5 below).

### 3.3 Non-Goals (explicitly out of scope for this campaign)

- Scene-View Frame Debugger capture of any kind — does not exist today, and this campaign does not add it
  (Locked Design Decision #4 above).
- A new permanent Editor diagnostic/validation button for this specific regression class (Locked Design
  Decision #3 above) — Tier-1 tests are the chosen safety net.
- Any change to `AtmosphereSettings`' own tunables, to the actual aerial-perspective blend math
  (`AtmosphereAerialPerspectiveComposite.comp`/`AtmosphereAerialPerspectiveCompositeMath.h`), or to the
  `atmosphere-scattering-4` campaign's own already-shipped, already-correct pass-through-for-sky-pixels fix
  — this campaign only touches how the FRAME DEBUGGER observes/displays already-correct pipeline output, not
  the pipeline's own rendering correctness.
- Any change to `GET /get_game_view`, `GET /get_texture`, `GET /list_textures`, or
  `RenderGraphDebugTextureRegistry` — those already correctly serve `"GameViewComposited"` today; this bug
  is isolated entirely to the Frame Debugger's own separate, independent capture/retention path.
- Per-individual-draw-call event granularity inside the new `"Aerial Perspective Composite"` leaf — it is
  pass-level, exactly like every other leaf in this tree (`frame-debugger-3`'s own permanent Locked Design
  Decision #1, unchanged).
- Publishing either retained preview texture into `RenderGraphDebugTextureRegistry` — not needed; the
  Frame Debugger's own in-panel `ImGui::Image()` display remains the primary, sufficient way to see it.

### 3.4 File-change inventory (full campaign, across all phases — see each phase file for its own exact
per-phase slice)

Modified files (no new files this campaign — every change is a surgical extension of `frame-debugger-3`'s
own existing files):

- `src/Editor/FrameDebuggerHistory.h`/`.cpp` (PHASE1 — `FrameDebuggerHistoryEntry::compositedPreview`,
  `CaptureFrame()`'s new nullable parameter + dual-copy `ImmediateSubmit()` body)
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (PHASE1 — `Build()`'s new parameter,
  `m_frameGameViewComposited` member, `TriggerCapture()`'s new argument, `EnsurePreviewDescriptor()`'s new
  picking rule)
- `src/Editor/ImGuiEditorLayer.cpp` (PHASE1 — the one-line `Build()` call-site fix)
- `src/Editor/FrameDebuggerData.h`/`.cpp` (PHASE2 — new `BuildAerialPerspectiveCompositeLeaf()` +
  `BuildRealFrameDebuggerSnapshot()`'s new lookup/append)
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (PHASE2/PHASE3 — new test cases)
- `tests/Editor/FrameDebuggerHistoryTests.cpp` (PHASE3 — widened only if a genuinely new PURE helper is
  extracted; otherwise untouched, since `CaptureFrame()` itself stays Tier-2/untested directly)
- `AGENTS.md`, `docs/conventions/frame-debugger.md`, `README.md`, `TODO.md` (PHASE3 — doc corrections)
- `task_manager/frame-debugger-4/PHASE1_COMPLETION_REPORT.md` .. `PHASE3_COMPLETION_REPORT.md`,
  `CAMPAIGN_COMPLETION_REPORT.md` (written as each phase lands)

### 3.5 Note on review depth (for the double-check / 2nd iteration)

**PHASE1 is this campaign's single highest-risk phase** — it is the only one that touches live Vulkan
barrier/copy code (`FrameDebuggerHistory::CaptureFrame()`'s `ImmediateSubmit()` lambda), changes a
cross-file function signature threaded through three files
(`ImGuiEditorLayer.cpp` → `FrameDebuggerPanel.h`/`.cpp` → `FrameDebuggerHistory.h`/`.cpp`), and introduces a
brand-new nullable-pointer lifetime/ownership rule that must be gotten right (nothing may ever dereference
`compositedGameViewSource` when it is `nullptr`). PHASE2 and PHASE3 are comparatively low-risk, additive,
pure-data-model/docs work. A reviewing pass should double-check PHASE1 in isolation before reviewing the
campaign as a whole — see this campaign's own 2nd-iteration workflow for exactly how that extra check is
scheduled.

### 3.6 Order of work

Work phases 1 -> 3 strictly in order; each does a fast compile check before moving on. PHASE3 is the only
one that does a full clean build (both `GTE_ENABLE_EDITOR=ON` and `=OFF`) + full `ctest` regression + a
live, HTTP-automation-driven runtime smoke test. See each phase file for its own exact compile-check command
and file-change inventory.
