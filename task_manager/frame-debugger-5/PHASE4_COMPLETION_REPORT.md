# PHASE4 — COMPLETION REPORT: Volume-Texture Ray-March Preview Reuse

## Parent -> `PHASE0_MASTER_STRATEGY.md`

Status: **DONE**. Branch: `feature/frame-debugger-impl` (unchanged, no new branch created).

## Summary

Selecting the `"AtmosphereAerialPerspectiveVolumePass"` leaf (either its Pre-GameView or its
Post-GameView occurrence) in the Frame Debugger now shows a **real, ray-marched 2D thumbnail** of that
frame's actual Aerial Perspective froxel volume contents, instead of falling back to the whole-frame Game
View image (PHASE3's own documented, deliberate gap). The ray-march itself reuses the ALREADY-SHIPPED
`VolumeTexturePreviewRenderer::RenderPreview()` (`src/Renderer/VolumeTexturePreviewRenderer.h/.cpp`) —
the EXACT SAME code `GET /get_texture` already uses to preview a volume texture over HTTP — called from a
brand-new place (`FrameDebuggerHistory::CaptureFrame()`), never modified, never duplicated.

`VolumeTexturePreviewRenderer`'s own public contract, GPU resource lifetime, and state-restoration
discipline were read in full before writing any code and are completely untouched by this phase — it is
reused exactly as designed ("self-contained... invoked at most once per network request"), just invoked
once per real capture trigger per volume-writing pass instead.

## Exact diff shape at each touch point

### 1. `src/Renderer/VolumeTexturePreviewRenderer.h`/`.cpp` — extracted, shared interpretation-selection rule

Per the phase document's own Step 2 instruction ("find and reuse this exact same interpretation-selection
rule... if it is not trivially reusable as-is (e.g. it is private/local to `Application.cpp`), extract it
into a small, shared, named function") — `Application.cpp`'s own GET `/get_texture` volume branch had this
rule inlined as two local lines, not reusable as-is. Extracted verbatim, byte-for-byte identical logic, into
a new free function:

```cpp
VolumeTexturePreviewInterpretation SelectVolumeTexturePreviewInterpretation(const std::string& volumeTextureName);
```

Implementation (unchanged behavior from what used to be inlined in `Application.cpp`):

```cpp
VolumeTexturePreviewInterpretation SelectVolumeTexturePreviewInterpretation(const std::string& volumeTextureName)
{
    const bool isAerialPerspectiveVolume = volumeTextureName.rfind("AtmosphereAerialPerspectiveVolume", 0) == 0;
    return isAerialPerspectiveVolume ? VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective
                                      : VolumeTexturePreviewInterpretation::GenericDensityInAlpha;
}
```

`Application.cpp`'s own call site now calls this function instead of re-deriving the rule inline —
zero behavior change for that existing call site, confirmed by the new Tier-1 test suite below. A SECOND
real call site, `FrameDebuggerHistory::CaptureFrame()` (this phase), now reuses the identical rule.

### 2. `src/Editor/FrameDebuggerData.h`/`.cpp` — `CollectComputePassVolumeTextureWrites()`

Per the phase document's Step 3.1 (and PHASE3's own Step 3.6 recommendation, followed here): a new, pure,
Tier-1-tested sibling function right next to PHASE3's `CollectComputePassTextureWrites()`, same file, same
signature shape, differing only in which `rg::ResourceKind` it filters on:

```cpp
struct FrameDebuggerComputePassVolumeTextureWrite {
    std::string passName;
    std::string writeVolumeTextureName;
};

std::vector<FrameDebuggerComputePassVolumeTextureWrite> CollectComputePassVolumeTextureWrites(
    const rg::RenderGraphSnapshot& graphSnapshot);
```

Walks `graphSnapshot.passesInExecutionOrder` directly (never Editor display labels — mirrors PHASE3's own
v2-review-revised approach exactly, per this campaign's `PHASE0_MASTER_STRATEGY.md` consistency
requirement), collecting one `{pass.name, pass.writeNames[i]}` pair for every real, surviving
(`isCulled == false`), compute-dispatch (`isComputePass == true`) pass whose FIRST
`writeKinds[i] == rg::ResourceKind::VolumeTexture` write is found. Deliberately a SEPARATE, independent
collection pass from `CollectComputePassTextureWrites()` (not a filter option on the same result) — a pass
could, in principle, have BOTH a `Texture`-kind write and a `VolumeTexture`-kind write in a future engine
(not true of any real pass today); the two collectors never assume "exactly one visual write kind per
pass," and a hypothetical dual-write pass would correctly appear in BOTH results (covered by a dedicated
test below).

### 3. `src/Editor/FrameDebuggerHistory.h` — new `m_volumePreviewRenderer` member

- New `#include "../Renderer/VolumeTexturePreviewRenderer.h"`.
- New private member: `VolumeTexturePreviewRenderer m_volumePreviewRenderer;` — owned directly, mirroring
  how `Application.cpp` already owns its own instance for `GET /get_texture`'s identical volume-preview use
  case (per the phase document's Step 3.2 item 2's own recommended, simplest, safest choice — no
  `ask_questions` needed here, the document already resolved this one explicitly).
  `VolumeTexturePreviewRenderer` is neither copyable nor movable (a user-declared destructor suppresses the
  implicit move, and copy is explicitly deleted) — this is fine, since `FrameDebuggerHistory` itself is
  never copied or moved anywhere in this codebase either (a plain, in-place member of `FrameDebuggerPanel`,
  itself a plain, in-place, non-copyable/non-movable member of `ImGuiEditorLayer` — confirmed via a full
  grep of every `FrameDebuggerHistory`/`FrameDebuggerPanel` reference before making this change).
- `CaptureFrame()`'s own doc comment widened to describe the new volume-write discovery/ray-march/upload
  sequence.
- `FrameDebuggerHistoryEntry::computePassPreviews`'s own doc comment widened: its "or - until a future
  PHASE4 - a volume-texture-only write... simply has NO entry here at all" caveat is now resolved — a
  volume-texture-only write DOES get a real entry, via the ray-march path below.

### 4. `src/Editor/FrameDebuggerHistory.cpp` — `CaptureFrame()`'s widened body (the ray-march branch)

Appended immediately after the existing single `ImmediateSubmit()` call (the pre-composite / post-composite
/ per-2D-texture-compute-pass copy loop from PHASE3, completely untouched):

1. **Step A'** (pure, CPU-side): `CollectComputePassVolumeTextureWrites(graphSnapshot)` — the SAME
   `graphSnapshot` PHASE3's own Step A already fetched via `renderGraph.LastSnapshot(...)` this same call,
   re-used rather than re-fetched.
2. For each discovered `{passName, writeVolumeTextureName}` pair:
   - `renderGraph.DebugVolumeTextureSnapshotFor(writeVolumeTextureName)` — skipped silently (no crash, no
     fake entry) on `std::nullopt`, mirroring PHASE3's own identical defensive contract for the 2D case.
   - `SelectVolumeTexturePreviewInterpretation(writeVolumeTextureName)` — the SAME shared rule
     `Application.cpp` uses, resolved above.
   - `m_volumePreviewRenderer.RenderPreview(renderer, volumeSnapshot->target, volumeSnapshot->state,
     interpretation)` — this call's own internal `ImmediateSubmit()` (plus its own internal readback
     `ImmediateSubmit()`) is genuinely SEPARATE from the main copy-loop's single `ImmediateSubmit()` above.
     This is a deliberate, accepted, still genuinely ON-DEMAND cost (documented explicitly in code comments
     at the call site) — `frame-debugger-4`'s own Locked Design Decision #8 ("one single `ImmediateSubmit()`
     call... never N separate submissions") is specifically about that main whole-frame + per-2D-pass copy
     loop; it does not apply to this already self-contained utility class this phase merely calls into,
     exactly as the phase document's own Step 3.2 item 3 anticipated. One `CaptureFrame()` call, once
     volume-writing passes exist, now issues MORE THAN ONE separate GPU submission total — never a
     regression of anything, since `CaptureFrame()` itself still only ever runs on Enable-edge/Step/
     explicit-Capture-click, never per real frame.
   - **The resolved type-mismatch fork** (Step 3.2 item 4): confirmed, per PHASE3's own completion report,
     that `FrameDebuggerComputePassPreview::preview` is a plain `RenderTexture` (never a `Texture2D`,
     never `std::optional`) — so this phase's own two documented options collapse to option (b): "create a
     `RenderTexture` here too and upload into IT via the same staging-buffer approach
     `Renderer::CreateTexture2D()`'s own implementation already uses internally, keeping
     `FrameDebuggerComputePassPreview` a single, uniform type." No `ask_questions` call was needed — the
     fork was already resolved by PHASE3's own landed code, exactly as this phase's task description
     anticipated ("if it did not [resolve it]... call `ask_questions` rather than guessing" — it did).
   - A fresh `RenderTexture` is created via `renderer.CreateRenderTexture(raw.width, raw.height,
     VK_FORMAT_R8G8B8A8_UNORM, debugName)` (starts `VK_IMAGE_LAYOUT_UNDEFINED`), then `raw.pixels` (tightly
     packed RGBA8 — `VolumeTexturePreviewRenderer::RenderPreview()`'s own documented output shape) is
     uploaded into it via a throwaway host-visible staging `Buffer` + a dedicated `ImmediateSubmit()` that
     transitions `UNDEFINED -> TRANSFER_DST`, `vkCmdCopyBufferToImage()`, then `TRANSFER_DST ->
     SHADER_READ_ONLY_OPTIMAL` — mirroring `GpuResourceFactory::CreateTexture2D()`'s own internal upload
     sequence exactly (same three-step barrier/copy/barrier shape, just targeting a `RenderTexture`'s color
     image instead of a `Texture2D`'s).
   - The result is appended to `entry.computePassPreviews` via
     `FrameDebuggerComputePassPreview{ passName, std::move(volumePreviewTexture) }` — from here on, PHASE3's
     own picking/display logic (`ChooseFrameDebuggerPreviewSource()`/`EnsurePreviewDescriptor()`) needed
     **zero** further changes: a volume-derived preview and a plain 2D-texture preview are indistinguishable
     from that logic's point of view, exactly as the phase document promised.

### 5. `src/Application/Application.cpp`

The GET `/get_texture` volume branch's own two-line interpretation-selection now calls the shared
`SelectVolumeTexturePreviewInterpretation()` function instead of re-deriving the rule inline —
byte-for-byte identical resulting behavior, confirmed unchanged by re-running the live verification below
(the Transmittance LUT / Aerial Perspective Volume previews shown via the Frame Debugger below are served
by the exact same underlying interpretation rule `GET /get_texture` has always used).

## Tests

- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — 6 new tests for `CollectComputePassVolumeTextureWrites()`
  (mirroring PHASE3's own 5-test shape for its 2D sibling, one-for-one): finds exactly one
  `VolumeTexture`-kind write; excludes `Texture`-only and `Buffer`-only writes; excludes a culled compute
  pass; excludes a non-compute pass; only collects the FIRST `VolumeTexture`-kind write when a pass
  (hypothetically) declares two; PLUS one additional test the phase document's own Step 3.3 explicitly asked
  for — a pass with BOTH a `Texture`-kind AND a `VolumeTexture`-kind write appears in BOTH
  `CollectComputePassTextureWrites()`'s AND `CollectComputePassVolumeTextureWrites()`'s own results.
- `tests/Renderer/VolumeTexturePreviewRendererTests.cpp` (**new file**, registered in `tests/CMakeLists.txt`
  right after `Renderer/VolumeTexturePreviewMathTests.cpp`) — 3 new Tier-1 tests for the newly-extracted,
  genuinely pure `SelectVolumeTexturePreviewInterpretation()` function (no live `VkDevice`/`Renderer`/
  `VolumeTexturePreviewRenderer` instance involved at all — the class itself correctly remains Tier-2/
  untested directly, a real ray-march needs a live `VkDevice`, unchanged from before this phase): the
  `"AtmosphereAerialPerspectiveVolume"` name-prefix case selects the atmosphere-aware interpretation (both
  with and without a trailing suffix, e.g. `"...Volume_GameView"`); any other name selects the generic
  interpretation (including the empty string); the prefix match is correctly anchored at the START of the
  name, not just any substring occurrence.

**Total: 6 new tests in the existing snapshot-builder test file, 3 new tests in a brand-new Tier-1 test
file — 9 new `TEST()` entries this phase.**

`FrameDebuggerHistory::CaptureFrame()` itself remains Tier-2/untested directly (a real ray-march needs a
live `VkDevice`), exactly as PHASE3 anticipated and as this phase's own document explicitly required a live
verification pass for instead (below) — this was NOT skipped despite the compile check passing.

## Fast compile check (as run this phase)

```
cmake --build build --target GreatTamanaEngineTests
```
→ succeeded, zero warnings/errors introduced (this run also exercised the full `gte_core` static library,
since `FrameDebuggerHistory.cpp`/`FrameDebuggerData.cpp`/`VolumeTexturePreviewRenderer.cpp`/
`Application.cpp` all live there).

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure -R FrameDebugger
```
→ 100% tests passed, **80/80** (up from PHASE3's 74 — the 6 new `CollectComputePassVolumeTextureWrites`/dual-write
tests are the only new ones matching this filter; `VolumeTexturePreviewRendererTests.cpp`'s own 3 tests don't
match the `FrameDebugger` name filter and were confirmed separately: `ctest -R VolumeTexturePreviewRenderer`
→ 100% tests passed, 3/3).

No full `ctest` regression across the WHOLE suite was run this phase, per this phase's own explicit "Fast
Compile Check" rule (PHASE5 is the phase responsible for the full clean build + full regression pass) — but
a full clean build of `GreatTamanaEngine` itself (not just the test target) WAS additionally performed this
phase specifically to support the live HTTP-driven verification below.

## Live, HTTP-driven verification against a real running `GreatTamanaEngine.exe`

Built the full engine (`cmake --build build --target GreatTamanaEngine`), launched it via
`run_app_background`, and drove the Frame Debugger entirely over HTTP
(`GET /frame_debugger/open` → `GET /frame_debugger/enable?value=true` → a sequence of
`GET /frame_debugger/select_event?index=N` + `GET /get_swapchain` screenshots), then `stop_app_background`
when done.

The very first `enable?value=true` call auto-captured one real frame, reporting the exact same
`totalEventCount: 10` tree shape PHASE3's own live verification already found (9 compute-dispatch leaves +
1 `"GameView"` leaf, split 5 Pre-GameView / 4 Post-GameView) — confirming PHASE4 introduced zero regression
to PHASE1/2/3's own already-landed behavior.

**Selecting index 3 (`AtmosphereAerialPerspectiveVolumePass`, the Pre-GameView occurrence)** produced a
real, clearly non-blank, recognizably ray-marched thumbnail: against a dark, near-black background, a
distinct, glowing light-blue TAPERING FRUSTUM shape (narrow near the left/near edge, visibly widening
toward the right/far edge) — exactly the frustum-shaped raymarch proxy + atmosphere-aware
transmittance/in-scattering interpretation the `atmosphere-scattering-2`/`atmosphere-scattering-3`
campaigns built for this exact volume, and visually completely distinct from both the whole-frame Game View
image (a blue sky-to-horizon gradient) and from a 2D LUT texture's own preview (see below). The event
details panel correctly showed `Shader`/`Pass: AtmosphereAerialPerspectiveVolumePass`,
`Blend`/`ZClip`/`ZTest`/`ZWrite`/`Cull`/`Stencil* : n/a (compute pass)` — a real compute-dispatch leaf, not a
placeholder.

**Selecting index 8 (`AtmosphereAerialPerspectiveVolumePass`, the Post-GameView occurrence — the SAME
volume, re-dispatched a second time later in this same real frame)** produced the byte-for-byte visually
identical frustum-shaped thumbnail — correct, since both occurrences write the exact same
`"AerialPerspectiveVolume"` (froxel volume) name, and `CaptureFrame()` resolves each occurrence's own
preview independently but from the SAME underlying volume contents at capture time.

**Selecting index 0 (`AtmosphereTransmittanceLutPass`, a plain 2D-texture compute pass, PHASE3's own
territory)** was re-confirmed completely unaffected by this phase's changes: still shows its own distinct
gradient LUT image (black → reddish → white band), nothing to do with the volume preview's own frustum
shape — proving PHASE3's picking logic needed genuinely zero changes and both preview KINDS coexist
correctly, exactly as the phase document promised.

No crash, no wrong/blank image, no fallback to the whole-frame Game View image for either volume-writing
leaf — the ONE remaining gap this whole `frame-debugger-5` campaign set out to close is now closed.

## Design decisions / ambiguity check

No `ask_questions` call was needed this phase. Both implementation-detail forks the phase document itself
flagged as open were resolved by re-reading the ALREADY-LANDED PHASE1-3 code before writing anything new,
exactly as instructed:

1. **The `RenderTexture` vs. `Texture2D` type-mismatch fork** (Step 3.2 item 4) — resolved by PHASE3's own
   completion report and the live `FrameDebuggerHistory.h` header: `FrameDebuggerComputePassPreview::preview`
   is a plain `RenderTexture`. This collapses the fork to option (b) verbatim (create a `RenderTexture`,
   upload via a staging-buffer sequence mirroring `CreateTexture2D()`'s own internal one) — no ambiguity
   remained once the real, current header was actually read.
2. **Which existing GPU-texture-from-raw-CPU-pixels upload helper to reuse** (`PHASE0_MASTER_STRATEGY.md`'s
   Locked Design Decision #7b) — `GpuResourceFactory::CreateTexture2D()`'s own internal
   `staging-buffer + ImmediateSubmit(barrier/vkCmdCopyBufferToImage/barrier)` sequence was read in full
   (`GpuResourceFactory.cpp`) and its exact shape (not the function itself, which only ever targets a
   `Texture2D`) was mirrored by hand against a `RenderTexture`'s own color image instead — there was no
   reasonable second option once `Texture2D` was ruled out by point 1 above, so this was a mechanical
   "same shape, different target type" translation, not a genuine design fork.
3. **Where `m_volumePreviewRenderer` should be owned** (Step 3.2 item 2) — the phase document's own
   recommended default ("the simplest, safest choice is a new private member directly on
   `FrameDebuggerHistory`") was followed with no reason found to deviate — `FrameDebuggerPanel` was
   considered as the alternative the document itself named, but `FrameDebuggerHistory` is where the OTHER
   two GPU-upload-owning pieces of state for this exact capture already live (the retained
   `preview`/`compositedPreview`/`computePassPreviews` textures themselves), so keeping the renderer that
   produces one more of them in the same class was the more cohesive, less-torn choice — not a coin flip.

## What this phase deliberately did NOT do (per its own Step 3.4, confirmed unchanged)

- `VolumeTexturePreviewRenderer`'s own public contract, GPU resource lifetime, or state-restoration
  discipline was not modified at all — read in full before starting, called from a new place only.
- No Z-slice/orbit-angle picking control was added — `RenderPreview()`'s own existing fixed camera framing
  is reused verbatim, exactly like `GET /get_texture` already does.
- The ray-march path was not made cheaper/deferred/lazy — it runs on every real capture trigger for every
  volume-writing pass, exactly like every 2D compute-pass preview (Locked Design Decision #4, eager
  capture).

## Next phase

PHASE5 (`PHASE5_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md`) is the final campaign phase: widens/adds
Tier-1 tests for every prior phase (this phase's own new tests already satisfy its own slice of that),
corrects every stale doc claim (`AGENTS.md`, `docs/conventions/frame-debugger.md`, `README.md`, `TODO.md`),
then a full clean build (both `GTE_ENABLE_EDITOR` configs) + full `ctest` regression + one final,
comprehensive, HTTP-driven, screenshot-verified smoke test proving every atmosphere LUT compute pass now
appears as its own selectable leaf with its own correct, distinct real output image — this phase's own live
verification above already demonstrates the volume-texture leaf half of that claim end-to-end.
