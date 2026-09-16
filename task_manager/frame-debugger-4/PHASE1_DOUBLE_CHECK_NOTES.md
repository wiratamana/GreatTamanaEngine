# PHASE1 DOUBLE-CHECK NOTES — isolated review pass, before the wider campaign double-check

Scope: this is the dedicated, isolated review of `PHASE1_DUAL_STAGE_RETAINED_CAPTURE_AND_PREVIEW_SELECTION.md`
called for by `PHASE0_MASTER_STRATEGY.md`'s Step 3.5 (PHASE1 flagged as this campaign's single
highest-risk phase). No production code was written or built this session; `PHASE0_MASTER_STRATEGY.md`,
`PHASE2_AERIAL_PERSPECTIVE_COMPOSITE_EVENT_TREE_LEAF.md`, and `PHASE3_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md`
were read for context only and were not modified (they are reviewed together in the next, separate,
full-campaign double-check step).

## What was checked

Every real source file PHASE1 cites by exact name was opened and its live, current state compared against
every claim/code sketch in the phase document:

- `src/Editor/FrameDebuggerHistory.h`/`.cpp` — confirmed `FrameDebuggerHistoryEntry` currently has exactly the
  one `preview` field the phase doc describes as the "before" state, `CaptureFrame()`'s current 3-argument
  signature and single-copy `ImmediateSubmit()` body match the doc's "Step 2" description verbatim (barrier
  sequence, debug-name buffer, `AdvanceFrameDebuggerHistoryWriteState()`/`ClampFrameDebuggerHistoryCursor()`
  calls at the end), and confirmed no other production or test file calls `CaptureFrame()` or reads
  `FrameDebuggerHistoryEntry::preview`/`compositedPreview` directly (only `Panels/FrameDebuggerPanel.cpp` does,
  exactly as the doc claims — "this campaign has exactly one real call site").
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` — confirmed `Build()`'s current signature, `TriggerCapture()`'s
  current single `m_history.CaptureFrame(*m_frameRenderer, snapshot, *m_frameGameView);` call, the current
  `EnsurePreviewDescriptor()` body (the exact `hasPreview`/neutral-check/dirty-check/`ImGui_ImplVulkan_AddTexture`
  structure the phase doc's replacement is meant to extend), `BuildInspectorPane()`'s existing
  `selectedEventIsGpuSkinning`/`showPreviewTexture` logic (confirmed untouched by the phase doc's plan, and
  confirmed that leaving it untouched is actually correct/sufficient), `m_selectedEventIndex`, and `m_history`
  all match the doc's description exactly. Confirmed `Build()`'s own "did the viewed history entry change
  underneath us" reset block correctly compares `currentEntry->preview->View()` (not `compositedPreview`), which
  the phase doc explicitly (and correctly) says must stay unchanged.
- `src/Editor/ImGuiEditorLayer.cpp` — confirmed the exact current call-site text
  `m_frameDebuggerPanel.Build(m_ctx, renderer, renderGraph, m_gameView, gpuSkinningPassNames);` (line 593 as of
  this session — matches the doc's quoted "before" text character-for-character), confirmed `m_gameView` is a
  plain `RenderTexture` member (not a pointer) and `m_gameViewComposited` is already declared exactly as
  `RenderTexture* m_gameViewComposited = nullptr;`, and confirmed the existing `gameSource = (m_gameViewComposited
  != nullptr) ? m_gameViewComposited : &m_gameView` local the doc references as the precedent to mirror really
  exists a few dozen lines above the Frame Debugger call site.
- `src/Editor/FrameDebuggerPreviewProcessing.h` — confirmed `FrameDebuggerPreviewRenderer::RenderPreview()`'s
  live signature is exactly `const Texture2D& RenderPreview(Renderer&, const RenderTexture& source,
  FrameDebuggerPreviewChannel, float, float)` — the phase doc's claim that it takes `const RenderTexture&` (and
  therefore needs no `const_cast` for the new `*selectedSource` picking logic) is still accurate.
- `src/Editor/FrameDebuggerData.h`/`.cpp` — confirmed `FindEventDetailsByIndex(const FrameDebuggerSnapshot&,
  int)` and `FrameDebuggerEventDetails::passName` exist with the shape the phase doc relies on.
- Supporting checks: `src/Renderer/RenderTexture.h` (`Extent()`/`Format()`/`Image()`/`View()`/`Sampler()`
  accessor shapes used throughout the doc's code sketches), `src/Renderer/Renderer.h`
  (`CreateRenderTexture()`'s by-value return, used with `std::optional::emplace(...)`), and
  `src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h` (`ResourceState`/`RequiredStateFor()`/
  `EmitImageBarrier()` signatures) — all match the doc's code sketches exactly, so every C++ snippet in the
  phase document (the `CaptureFrame()` body, the `EnsurePreviewDescriptor()` rewrite, the `Build()`/
  `TriggerCapture()`/call-site changes) compiles as written against the live codebase, with no
  const-correctness, ownership, or Vulkan-API-usage error found in any of them.
- `src/Editor/NullEditorLayer.cpp` — confirmed it still never calls `FrameDebuggerPanel::Build()` at all (the
  whole panel doesn't exist under `GTE_ENABLE_EDITOR=OFF`), matching the doc's Step 3.7 claim.
- `tests/Editor/FrameDebuggerHistoryTests.cpp` and `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` —
  confirmed the former only tests the pure ring-buffer arithmetic (never calls `CaptureFrame()` directly, so
  the signature change needs zero test-file changes), and confirmed no existing test fixture hand-fabricates a
  pass named `"AtmosphereAerialPerspectiveCompositePass"` (so PHASE2's later, additive leaf can't silently
  break anything here either).
- Searched the whole `src/` tree for any other consumer of the "old single-preview" shape (e.g. any
  `Renderer::CaptureImagePixels()`-style direct reader of a Frame-Debugger-retained texture outside
  `Panels/FrameDebuggerPanel.cpp`) — found none; `Renderer::CaptureImagePixels()` itself is a completely
  unrelated host-readback helper (used by `AtmosphereLutRenderer`/`VolumeTexturePreviewRenderer` for HTTP LUT
  inspection), not a consumer of `FrameDebuggerHistoryEntry` at all, so there is nothing this phase's own
  file-change inventory is missing on that front.
- Re-verified the edge cases explicitly called out in the task: first-capture-before-any-composite-exists
  (handled correctly — `compositedGameViewSource == nullptr` never dereferenced anywhere in the doc's own code,
  and `Application::Run()`'s finalize block genuinely does call `SetGameViewCompositedTexture()` with a real
  pointer from essentially the first rendered frame onward per `PHASE0_MASTER_STRATEGY.md`'s own Step 2.1,
  making this path real-but-rare rather than a routinely-hit one) and a resize landing between two captures
  (handled correctly — the doc's `CaptureFrame()` body independently re-reads `compositedGameViewSource->Extent()`
  both when creating `entry.compositedPreview` and again inside the `ImmediateSubmit()` lambda, rather than
  assuming it matches `gameViewSource`'s own extent).

## What was changed, and why

The phase document's own technical plan (every function signature, every code sketch, the picking-rule logic,
the barrier/copy sequence, the ownership/nullability rules) was found to be **fully correct and consistent
with the live codebase** — no C++ syntax error, no Vulkan API misuse, no const-correctness mistake, and no
ownership/lifetime bug was found anywhere in it; every cited file/function/field still exists with the shape
the doc assumes. Two small, targeted additions were made to close genuine (if minor) documentation-completeness
and verification gaps, given this phase's flagged highest-risk status — no functional/technical content was
changed:

1. **Section 3.1** now also instructs the implementer to update `FrameDebuggerHistoryEntry`'s existing
   top-of-struct summary comment (the paragraph in `FrameDebuggerHistory.h` immediately above the struct
   definition), which today describes only `preview`'s own "once written, always populated" nullability rule.
   Without this addition, a literal implementation of the phase doc's plan would have left that top-level
   comment technically-inaccurate/misleading once `compositedPreview` (which does NOT follow that same rule —
   it can legitimately go back to `std::nullopt` on a later capture into the same slot) was added, since a
   reader skimming just the class-level comment (rather than the new field-level comment) would wrongly assume
   every field in the struct behaves the same way. This was the one real gap found in an otherwise very
   thorough document.
2. **Section 3.8** (the recommended manual smoke check) now also suggests exercising two specific edge cases
   the phase's own code comments discuss but a plain "open → enable → compare against `/get_game_view`" pass
   does not otherwise touch: (a) resizing the Game View between two captures, to directly exercise the
   "extents may legitimately differ between two captures" case `CaptureFrame()`'s own new comments describe,
   and (b) explicitly toggling the `"GameView"` leaf selection on and off, to directly confirm the picking rule
   flips both ways (not just that the default/nothing-selected view happens to look right). It also clarifies
   that the very-first-capture-before-any-composite-exists path is real but practically unreachable in an
   ordinary live smoke test, and is meant to be verified by code review/reasoning about the nullable-pointer
   contract instead of a forced live repro, so a future implementer doesn't waste time trying to contrive it.

No other change was made. In particular, no file-change inventory item was found missing, no call site was
found unaccounted for, and no cross-file signature-change consequence was found unhandled.

## Conclusion

`PHASE1_DUAL_STAGE_RETAINED_CAPTURE_AND_PREVIEW_SELECTION.md` was already correct and sufficiently detailed
for a competent AI programmer to implement without further guesswork; the two additions above are small
clarity/safety-net improvements appropriate to this phase's flagged risk level, not corrections of a
functional mistake. No gap, incorrectness, insufficiency, or missing file was found beyond what is described
above.
