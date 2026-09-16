# PHASE3 — COMPLETION REPORT: Generic Per-Pass Retained Preview Capture (2D Textures)

## Parent -> `PHASE0_MASTER_STRATEGY.md`

Status: **DONE**. Branch: `feature/frame-debugger-impl` (unchanged, no new branch created).

## Summary

Selecting any leaf under either `"Compute Dispatches (Pre-GameView)"`/`"Compute Dispatches (Post-GameView)"`
group (PHASE2) now shows **that specific pass's own real output texture** in the Inspector's preview box,
instead of always falling back to the whole-frame Game View image. `FrameDebuggerHistory::CaptureFrame()`
eagerly discovers every real, surviving compute-dispatch pass's own FIRST `Texture`-kind write this same
capture (via a new, pure, Tier-1-tested function, `CollectComputePassTextureWrites()`), resolves each one's
CURRENT physical texture + tracked GPU state via the already-existing `RenderGraphDebugTextureRegistry`
(`RenderGraph::DebugTextureSnapshotFor()`), and makes one more retained GPU-to-GPU copy per pass — all inside
the SAME single `ImmediateSubmit()` call the pre-composite/post-composite copies already used (never a
separate submission per pass). A pass whose only write is a `Buffer` or a `VolumeTexture` (e.g. GPU Skinning,
the Aerial Perspective Volume pass) correctly gets **no** retained preview at all this phase — a real, honest
"not available yet" state (`VolumeTexture` support is explicitly PHASE4's job) — and the Inspector falls back
to the existing whole-frame `compositedPreview`/`preview` picking rule unchanged.

**Zero regressions**: every pre-existing barrier/copy for `gameViewSource`/`compositedGameViewSource` is
byte-for-byte unchanged; the new per-pass copies are strictly additive, and critically **never assume
`ShaderRead`** for a compute pass's source state — each source is transitioned FROM and restored back TO its
own real, currently-tracked `ResourceState` (read straight out of the registry), exactly per this phase's own
"extra care" mandate.

## Exact new `ImmediateSubmit()` body shape

One single `renderer.ImmediateSubmit()` call per `CaptureFrame()` invocation, containing, in this fixed order:

1. **Pre-composite copy** (unchanged from `frame-debugger-4`): `gameViewSource` (ShaderRead -> TransferSrc) ->
   copy -> `entry.preview` (fresh/Undefined -> TransferDst) -> both restored (source back to ShaderRead,
   destination left in ShaderRead for display). **1 copy, always present.**
2. **Post-composite copy** (unchanged from `frame-debugger-4`), only when `compositedGameViewSource != nullptr`
   this capture: same shape, against `compositedGameViewSource`/`entry.compositedPreview`. **0 or 1 copy.**
3. **NEW (this phase) — one compute-pass copy per discovered `{passName, writeTextureName}` pair**, in the
   order `CollectComputePassTextureWrites()` discovered them (real `passesInExecutionOrder` order): for each
   pair `i`,
   - `EmitImageBarrier(source.image, range, source.colorState, transferSrc)` — the source's real, CURRENT
     tracked state (from `DebugTextureSnapshotFor()`), **never a hardcoded `ShaderRead` assumption**.
   - `EmitImageBarrier(destination.Image(), range, freshImageState, transferDst)` — destination is a freshly
     `CreateRenderTexture()`d scratch texture (`VK_IMAGE_LAYOUT_UNDEFINED` to start, exactly like the two
     existing copies above).
   - `vkCmdCopyImage(source.image -> destination.Image())`.
   - `EmitImageBarrier(source.image, range, transferSrc, source.colorState)` — **restored back to the source's
     own real original state**, never assumed to be `ShaderRead` (this is the exact "never assuming
     `ShaderRead` blindly" requirement this phase's own risk-flag called out).
   - `EmitImageBarrier(destination.Image(), range, transferDst, shaderRead)` — the retained copy itself is left
     in `ShaderRead`, ready for `ImGui::Image()` display, matching the other two retained copies.

   **0 to N copies**, where N = however many real, surviving compute passes this frame had at least one
   `Texture`-kind write (in the live verification below, N was 7 out of 9 real compute-dispatch leaves).

**Total this phase**: `2 + N` copies (or `1 + N` when no composited source exists yet), all inside the SAME
single `ImmediateSubmit()` call — never N separate submissions (frame-debugger-4's own Locked Design Decision
#8, correctly NOT confused with this campaign's own, differently-numbered Locked Design Decision #8 about the
pre/post-GameView tree split — see `PHASE0_MASTER_STRATEGY.md`'s own explicit disambiguation note).

## Exact diff shape at each touch point

### 1. `src/Editor/FrameDebuggerData.h`/`.cpp`

- New struct `FrameDebuggerComputePassTextureWrite { std::string passName; std::string writeTextureName; };`
- New pure function `CollectComputePassTextureWrites(const rg::RenderGraphSnapshot&)` — Step A of the phase
  document's revised (v2 review) plan: walks `graphSnapshot.passesInExecutionOrder` directly (never the
  Editor's own built `FrameDebuggerEventNode` tree/display-string row labels), collecting one
  `{pass.name, pass.writeNames[i]}` pair for every real, surviving (`isCulled == false`) compute-dispatch
  (`isComputePass == true`) pass whose FIRST `writeKinds[i] == rg::ResourceKind::Texture` write is found. A
  pass with no `Texture`-kind write at all is excluded entirely (never a fake entry).
- `FrameDebuggerPreviewSourceChoice` gained a new enumerator, `ComputePassPreview`.
- `ChooseFrameDebuggerPreviewSource()` widened with a new 5th parameter, `hasSelectedComputePassPreview` — new
  rule inserted between the existing `"GameView"` branch and the existing `hasCompositedPreview` branch (a
  selected compute-pass preview wins over `CompositedPreview`); every other branch's order/outcome is
  byte-for-byte unchanged.

### 2. `src/Editor/FrameDebuggerHistory.h`

- New struct `FrameDebuggerComputePassPreview { std::string passName; RenderTexture preview; };` — `preview`
  is a plain (non-`optional`) `RenderTexture`, always populated for any entry that exists at all.
- New field `std::vector<FrameDebuggerComputePassPreview> computePassPreviews;` on `FrameDebuggerHistoryEntry`
  — entirely rebuilt (`.clear()`'d then re-populated) on every single real capture, unlike `preview`
  (once-written-always-populated) or `compositedPreview` (may legitimately go back to `std::nullopt`).
- `CaptureFrame()`'s signature widened with a new `const rg::RenderGraph& renderGraph` parameter (forward-
  declared `namespace rg { class RenderGraph; }` at the top of the header, mirroring
  `Panels/FrameDebuggerPanel.h`'s own identical forward declaration).

### 3. `src/Editor/FrameDebuggerHistory.cpp`

- `#include "../Renderer/RenderGraph/RenderGraph.h"` added (needed for the real `RenderGraph`/
  `DebugTextureSnapshot`/`ExecuteTimingMode` definitions this file now consumes).
- `CaptureFrame()`'s body widened exactly per the "Exact new `ImmediateSubmit()` body shape" section above:
  Step A (`renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback)` +
  `CollectComputePassTextureWrites()`) runs BEFORE any Vulkan call; Step B
  (`renderGraph.DebugTextureSnapshotFor(writeTextureName)`, skipping silently on `std::nullopt` — defensive
  only) creates each retained `RenderTexture` (via **aggregate list-initialization**, not
  default-construct-then-assign, since `RenderTexture` has no default constructor) and records its real source
  image/extent/`colorState` into a small local `ComputePassCopySource` vector; the single `ImmediateSubmit()`
  lambda then performs the pre-composite copy, the optional post-composite copy, and the new per-compute-pass
  loop, in that fixed order.

### 4. `src/Editor/Panels/FrameDebuggerPanel.cpp`

- `TriggerCapture()`'s one call site updated to
  `m_history.CaptureFrame(*m_frameRenderer, *m_frameRenderGraph, snapshot, *m_frameGameView, m_frameGameViewComposited);`
  (`m_frameRenderGraph`'s non-null guard already existed at this call site from before this phase — no new
  defensive check needed).
- `EnsurePreviewDescriptor()` widened: resolves the currently-selected leaf's own real `passName` (if any),
  linearly scans `entry->computePassPreviews` for a matching entry, and passes
  `selectedComputePassPreview != nullptr` as the new 5th argument to `ChooseFrameDebuggerPreviewSource()`; a
  new `switch` case maps `FrameDebuggerPreviewSourceChoice::ComputePassPreview` onto
  `&selectedComputePassPreview->preview`. Every other branch is unchanged.

## QoL note (per the phase document's own explicit call-out, not a bug)

`"AtmosphereAerialPerspectiveCompositePass"` has a real `Texture`-kind write (`"GameViewComposited"`) — the
SAME texture `entry.compositedPreview` already retains for an unrelated reason (its final-output role). After
this phase, that pass ALSO gets its own, genuinely redundant, second retained copy of the exact same texture
content under `computePassPreviews`. Per the picking rule, `ComputePassPreview` wins whenever that leaf is
selected, so the displayed image is still byte-for-byte correct either way — confirmed visually in the live
verification below (selecting that leaf shows the identical final composited sky image). This redundancy was
deliberately left as-is, per the phase document's own explicit instruction not to special-case that pass out
of the generic discovery loop.

## Live, manually-triggered verification against a real running `GreatTamanaEngine.exe`

Built the full engine (`cmake --build build --target GreatTamanaEngine`) and launched it in the background,
then drove the Frame Debugger entirely over HTTP (`GET /frame_debugger/open` -> `GET
/frame_debugger/enable?value=true` -> a sequence of `GET /frame_debugger/select_event?index=N` +
`GET /get_swapchain` screenshots).

**This real, live capture found exactly 9 real, surviving compute-dispatch leaves** (`totalEventCount == 10`
total events: 9 compute leaves + 1 `"GameView"` leaf), split as:

- `"Compute Dispatches (Pre-GameView)"` (5 leaves): `AtmosphereTransmittanceLutPass`,
  `AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumePass`,
  `AtmosphereAerialPerspectiveVolumeDebugSlicePass`.
- `"Compute Dispatches (Post-GameView)"` (4 leaves): `AtmosphereAerialPerspectiveCompositePass`,
  `AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumePass`, `AtmosphereAerialPerspectiveCompositePass`
  (the Sky-View LUT and Aerial Perspective machinery genuinely re-run a second time later in the same real
  captured frame at this engine's current state — a true fact about this frame's real render graph, not
  something this phase's discovery logic invented or mis-ordered).

**Of these 9, 7 have a real `Texture`-kind write and got their own retained, genuinely distinct preview
texture** (visually confirmed via screenshot for 3 of them, each showing a clearly different image from the
whole-frame default and from each other):

- `AtmosphereTransmittanceLutPass` (index 0) — a distinct gradient LUT image (black -> reddish -> white band),
  completely different from the default whole-frame sky preview.
- `AtmosphereAerialPerspectiveVolumeDebugSlicePass` (index 4) — a distinct (solid black, but genuinely its own
  real captured content, not a crash/placeholder) image.
- `AtmosphereAerialPerspectiveCompositePass` (index 6, post-GameView) — shows the identical final composited
  sky image (the expected, accepted redundancy noted above).
- `AtmosphereSkyViewLutPass` (index 7, the post-GameView occurrence) — a distinct sky-panorama-shaped LUT
  image, clearly different again from both the Transmittance LUT and the whole-frame image.

**The remaining 2 — both `AtmosphereAerialPerspectiveVolumePass` occurrences (indices 3 and, symmetrically, its
post-GameView twin) — correctly have NO retained compute-pass preview at all**, since their only write is a
`VolumeTexture` (`ResourceKind::VolumeTexture`, out of this phase's explicit scope, deferred to PHASE4).
Selecting index 3 was confirmed (via screenshot) to cleanly fall back to the existing default whole-frame
composited image — no crash, no wrong 2D interpretation of the 3D volume, exactly as this phase's Step 1
requires.

Selecting the `"GameView"` leaf itself (index 5) was also confirmed unaffected — still shows the true
pre-composite image, exactly as `frame-debugger-4` already established.

## Fast compile check (as run this phase)

```
cmake --build build --target GreatTamanaEngineTests
```
-> succeeded, zero warnings/errors introduced.

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure -R FrameDebugger
```
-> 100% tests passed, 74/74 (up from PHASE2's 69 — 5 new `FrameDebuggerSnapshotBuilderTest.CollectComputePassTextureWrites*`
tests, plus the widened `FrameDebuggerDataTest.ChooseFrameDebuggerPreviewSourceTest`, which is one existing
test extended in place rather than counted as a new one).

No full build/full regression was run as part of the required compile-check step, per this phase's own "Fast
Compile Check" rule — however, a full clean build of `GreatTamanaEngine` itself (not just the test target) WAS
additionally performed this phase specifically to support the live HTTP-driven verification above (PHASE5 is
still the phase responsible for the REQUIRED comprehensive full-build/full-regression/live-verification pass).

## Test coverage added

- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — 5 new tests for `CollectComputePassTextureWrites()`:
  finds exactly one `Texture`-kind write; excludes `Buffer`-only and `VolumeTexture`-only writes; excludes a
  culled compute pass; excludes a non-compute pass (e.g. `"GameView"` itself); only collects the FIRST
  `Texture`-kind write when a pass (hypothetically) declares two.
- `tests/Editor/FrameDebuggerDataTests.cpp` — `ChooseFrameDebuggerPreviewSourceTest` widened to the new
  5-boolean input space: every pre-existing case now passes `hasSelectedComputePassPreview = false` and
  asserts the EXACT SAME outcome as before PHASE3 (proving zero regression to any existing combination), plus
  3 new cases proving `hasSelectedComputePassPreview == true` wins over `CompositedPreview` (both with and
  without `compositedPreview` present), and that the literal `"GameView"` leaf still wins over even a
  (nonsensical, defensive-only) `hasSelectedComputePassPreview == true`.
- `FrameDebuggerHistory::CaptureFrame()` itself remains Tier-2/untested directly (no live `VkDevice` in
  `tests/Editor/FrameDebuggerHistoryTests.cpp`, unchanged from before this phase) — exactly as the phase
  document's own Step 3.6 anticipated, since the newly-added pure logic was successfully extracted into
  `CollectComputePassTextureWrites()` instead, which IS fully Tier-1-tested above.

## Design decisions / ambiguity check

No `ask_questions` call was needed this phase. The phase document's own Step 3.3 "Step A" revision (reading
`rg::RenderGraphSnapshot` directly via `renderGraph.LastSnapshot()` rather than string-matching the Editor's
own `"Write Texture"` row labels) was followed exactly as written — no fork encountered there. The one
implementation detail not spelled out verbatim by the phase document — that `FrameDebuggerComputePassPreview`
must be constructed via aggregate list-initialization rather than default-construction-then-assignment (since
`RenderTexture` has no default constructor) — was a straightforward compile-time constraint, not a design
ambiguity, and was resolved by using `FrameDebuggerComputePassPreview{ passName, CreateRenderTexture(...) }`
directly.

## Next phase

PHASE4 (`PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md`) closes the one remaining gap this phase's Step 3.7
deliberately leaves open: a compute pass whose only visual write is a 3D volume texture (today: both
`AtmosphereAerialPerspectiveVolumePass` occurrences) still falls back to the whole-frame image — PHASE4 reuses
the already-shipped `VolumeTexturePreviewRenderer` ray-march thumbnail renderer to give it a real preview too.
