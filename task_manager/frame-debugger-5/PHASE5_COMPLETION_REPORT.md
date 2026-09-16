# PHASE5 — COMPLETION REPORT: Tests Review, Documentation Sweep, Full Build/Regression, Live Verification

## Parent -> `PHASE0_MASTER_STRATEGY.md`

Status: **DONE**. Branch: `feature/frame-debugger-impl` (unchanged, no new branch created).

## Summary

This closing phase re-verified PHASE1-4's own tests were already updated correctly (they were — no stale
assertions found anywhere in `tests/`), swept every stale doc claim in `AGENTS.md`/`docs/conventions/
frame-debugger.md`/`TODO.md`/`README.md`, ran a full clean build of BOTH `GTE_ENABLE_EDITOR` configurations
(zero errors in either), ran the FULL `ctest` suite (100% pass, 1429/1430 — the same one pre-existing
machine-gated skip every prior campaign has also seen), and then ran the REQUIRED live, HTTP-automation-driven,
screenshot-verified smoke test against a real running `GreatTamanaEngine.exe`.

**The live pass found one real, genuine behavioral gap PHASE1-4 missed** (not merely a documentation
discrepancy): three of the newly-discoverable compute-dispatch leaves (`AtmosphereMultiScatteringLutPass`,
`AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumeDebugSlicePass`) write to this engine's one
genuinely HDR 2D-texture format (`VK_FORMAT_R16G16B16A16_SFLOAT`), whose real physical magnitudes are tiny
fractions of 1.0 — PHASE3's straight `vkCmdCopyImage` retained-copy path preserves the real bytes correctly,
but the Inspector's plain `ImGui::Image()` display of those raw values is **visually indistinguishable from
solid black**, failing this phase's own explicit success criterion ("not a blank/black rectangle"). This was
fixed as part of this closing phase (per this document's own explicit instruction to fix, not just document,
a real gap found during the live pass) — see "Bug found and fixed" below for the full root-cause and fix.

## Step 3.1 — Tier-1 test review

Re-read every test file PHASE1-4 touched end to end (`RenderGraphTypesTests.cpp`, `RenderGraphBuilderTests.cpp`,
`RenderGraphSnapshotTests.cpp`, `FrameDebuggerSnapshotBuilderTests.cpp`, `FrameDebuggerDataTests.cpp`,
`FrameDebuggerHistoryTests.cpp`), plus a whole-`tests/`-tree grep for `"GPU Skinning"`/
`"AtmosphereAerialPerspectiveCompositePass"` one more time:

- **No stale assertion found anywhere.** Every occurrence of `"GPU Skinning"`/the old hardcoded pass-name
  string in `tests/` is inside a REWRITTEN test's own explanatory comment describing the OLD scenario it
  replaced (e.g. `FrameDebuggerSnapshotBuilderTests.cpp`'s own header comment and several individual
  "REWRITTEN (was ...)" test comments) — never a live assertion still checking the old, now-replaced tree
  shape. `JobsPanelDataTests.cpp`'s one `"GPU skinning"` hit is an unrelated CPU/GPU skinning-MODE toggle
  display string, not this campaign's removed name-list mechanism.
- `FrameDebuggerSnapshotBuilderTests.cpp` (12 tests, was 10 before this campaign) fully covers: the split
  `"Compute Dispatches (Pre-GameView)"`/`"Compute Dispatches (Post-GameView)"` group shape, both-groups-absent/
  one-group-absent/both-present cases, culled-pass exclusion on both sides, per-`ResourceKind` read/write row
  labeling (all 3 kinds, both directions), the `eventIndex` monotonic-ordering invariant across all three tree
  regions, and (PHASE3/PHASE4) `CollectComputePassTextureWrites()`/`CollectComputePassVolumeTextureWrites()`'s
  own full behavior matrix (11 tests) including the "a pass with BOTH kinds of write appears in BOTH
  collections" edge case PHASE4's own document explicitly asked for.
- `FrameDebuggerDataTests.cpp`'s `ChooseFrameDebuggerPreviewSourceTest` covers every meaningful combination of
  the widened 5-boolean input space, matching `frame-debugger-4` PHASE3's own rigor bar exactly (confirmed by
  direct comparison against that file's own historical shape) — every pre-PHASE3 case is re-asserted with
  `hasSelectedComputePassPreview = false` proving zero behavior change, plus 3 new cases proving the new
  boolean's precedence rules.
- No genuinely-extractable pure function was found left un-tested inside `FrameDebuggerHistory.cpp` — its own
  `CaptureFrame()` body stays correctly Tier-2/untested directly (needs a live `VkDevice`), with every pure
  piece of logic it depends on (`CollectComputePassTextureWrites()`/`CollectComputePassVolumeTextureWrites()`/
  `SelectVolumeTexturePreviewInterpretation()`) already fully extracted and Tier-1-tested by PHASE3/PHASE4.

**Conclusion: no test changes were needed for PHASE1-4's own work.** (This phase's own bug fix, described
below, is deliberately NOT given a new Tier-1 test — see "Why no new Tier-1 test was added for the HDR fix"
at the end of this report for the reasoning.)

## Step 3.2 — Documentation sweep

- **`AGENTS.md`** — "Frame Debugger" section rewritten to describe the generic `isComputePass`-driven
  discovery mechanism, the split `"Compute Dispatches (Pre-GameView)"`/`"(Post-GameView)"` groups, the
  per-pass retained-preview mechanism (2D copy or volume ray-march), and a forward pointer to the still-deferred
  "true per-pass stop/breakpoint" item in `TODO.md`.
- **`docs/conventions/frame-debugger.md`** — fully rewritten: "What is real today" now describes the split
  groups and generic discovery precisely (with a literal tree diagram), two "Known limitation, now fixed"
  sections now exist (`frame-debugger-4`'s composite-awareness fix, kept verbatim as history, and a NEW
  `frame-debugger-5` section for the compute-dispatch-coverage fix this campaign made), the data-model bullet
  list now names `CollectComputePassTextureWrites()`/`CollectComputePassVolumeTextureWrites()`/the widened
  `ChooseFrameDebuggerPreviewSource()`, and a new "Still-deferred future work" section names the true
  per-pass stop/breakpoint idea explicitly.
- **`TODO.md`** — "Frame Debugger" section gained a new `~~struck-through~~"now fixed"` entry for this exact
  bug (mirroring the existing `frame-debugger-4` entry's own shape) and a new, explicitly-scoped "True per-pass
  stop/breakpoint execution control" entry under "Still genuinely deferred", citing `PHASE0_MASTER_STRATEGY.md`'s
  own Non-Goals verbatim.
- **`README.md`** — a new top-of-list "Status" bullet for this campaign's fix, mirroring `frame-debugger-4`'s
  own precedent bullet exactly in shape/level of detail.

No `ask_questions` call was needed for the documentation sweep — every fact described was already
independently re-confirmed against the live source/live smoke test before being written down.

## Step 3.3 — Full clean build, both `GTE_ENABLE_EDITOR` configurations

```
cmake --build build --clean-first
```
→ **442/442 build steps succeeded, zero errors, zero warnings introduced.**

```
cmake -S . -B build-editor-off -DGTE_ENABLE_EDITOR=OFF
cmake --build build-editor-off --clean-first
```
→ **367/367 build steps succeeded, zero errors.** Confirmed by direct inspection of the build log: every
`FrameDebuggerData.cpp`/`FrameDebuggerCapture.cpp`/`FrameDebuggerHistory.cpp`/`FrameDebuggerPreviewProcessing.cpp`/
`Panels/FrameDebuggerPanel.cpp` compile step is absent from the `=OFF` log (as expected — `ImGuiEditorLayer.cpp`
is likewise absent, replaced by `NullEditorLayer.cpp`), while `RenderGraphTypes.cpp`/`RenderGraphBuilder.cpp`/
`RenderGraphSnapshot.cpp` (PHASE1's own core, non-Editor-gated changes) compile in BOTH configurations,
confirming the "core RenderGraph code is never Editor-gated" rule holds.

(This full clean build/regression sequence was run **twice** during this phase — once before the HDR bug fix
below was found and applied, and once again afterward, to produce final, authoritative numbers reflecting the
actually-shipped state of the code.)

## Step 3.4 — Full `ctest` regression pass

```
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
→ **100% tests passed, 1429/1430** (1 pre-existing, machine-gated skip —
`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`, exactly the same single skip
`frame-debugger-4` and every other recent campaign has also seen on this same development machine; not a new
skip introduced by this campaign). Zero new failures, zero assertions needed loosening.

## Step 3.5 — Live, HTTP-automation-driven, screenshot-verified smoke test (full transcript)

Mirrors `frame-debugger-4/PHASE3_COMPLETION_REPORT.md`'s own exact working method: `run_app_background` the
real engine, drive it entirely over `gte_send_request`, screenshot via `GET /get_swapchain`, `stop_app_background`
when done — zero mouse/keyboard involved anywhere.

### 1. Launch + sanity check

`run_app_background(GreatTamanaEngine.exe)` → PID 29256. `GET /get_swapchain` confirmed the Editor's default
layout renders correctly (Hierarchy/Scene/Game/Inspector/Memory/Profiler/Render Graph/Atmosphere/Jobs/Project),
with a real blue-to-gold sky gradient already visible in both "Scene" and "Game" — confirming the atmosphere
LUT passes (including their real `DirectionalLightResolver` placeholder-sun fallback, per
`atmosphere-scattering-4`'s own precedent) run unconditionally, with no scene setup required.

### 2. GPU Skinning leaf — explicitly, deliberately SKIPPED, reason documented

Per this phase's own Step 3.5 item 2's explicit allowance ("if none exists cheaply... acceptable to... note in
the completion report was skipped for a documented, specific reason"): grepped
`src/Network/NetworkServer.cpp` for every `POST`/`GET` route — confirmed only `/instantiate_primitive` and
`/instantiate_light` exist for spawning ECS content over HTTP; neither can spawn a GPU-skinned rigged model
(that requires a real `.pmx`+`.vmd` asset pair and `Game::PlayAnimationOnEntity()`/GPU-skinning-mode toggle,
none of which has an HTTP entry point). `GET /list_textures` and a `GET /get_swapchain` screenshot of
"Hierarchy" both confirmed the default scene has no skinned model loaded either (`Entity 0 (Camera)` only).
GPU-Skinning-leaf presence was therefore **not independently re-verified live this phase** — its own generic
discovery mechanism (via `isComputePass`, identical code path to every other compute pass) was already
exhaustively Tier-1-tested by PHASE2's own
`TwoPreGameViewComputePassesProduceOnePreGameViewGroup`/`PreAndPostGameViewGroupsAppearTogetherAsSiblingsInRealExecutionOrder`
tests (which explicitly include a `Buffer`-kind-write compute pass modeling GPU Skinning's own shape), which is
judged sufficient given the identical, non-special-cased discovery code path every other verified leaf below
also goes through.

### 3. Open + Enable (auto-capture) + `GET /frame_debugger/state`

`GET /frame_debugger/open` → `windowOpen: true`. `GET /frame_debugger/enable?value=true` →
`{"enabled":true,"historyCount":1,"totalEventCount":10,...}`. `GET /get_swapchain` confirmed the EXACT
predicted split tree shape (Locked Design Decision #8):

- `"Compute Dispatches (Pre-GameView)"` (5 children): `AtmosphereTransmittanceLutPass`,
  `AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumePass`,
  `AtmosphereAerialPerspectiveVolumeDebugSlicePass`.
- `"GameView"` leaf (sibling, in between).
- `"Compute Dispatches (Post-GameView)"` (4 children): `AtmosphereAerialPerspectiveCompositePass`,
  `AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumePass`, `AtmosphereAerialPerspectiveCompositePass`
  (the Sky-View LUT and Aerial Perspective machinery genuinely re-run a second time later in this same real
  frame — a true fact about this engine's current render graph, matching PHASE3's own prior live finding
  byte-for-byte).

Both groups present, `totalEventCount == 10` — matches the phase document's own expected numbers exactly.

### 4. Per-leaf image verification (index → leaf → screenshot result)

| Index | Leaf | Result |
|---|---|---|
| 0 | `AtmosphereTransmittanceLutPass` | Real, distinct black → reddish → white gradient LUT image. |
| 1 | `AtmosphereMultiScatteringLutPass` | **Initially: solid, indistinguishable-from-black rectangle — the real bug this phase found and fixed (see below).** After the fix: a real, distinct blue-to-black gradient, pixel-plausible-matching `GET /get_texture?texture_name=AtmosphereMultiScatteringLut`'s own already-shipped debug-exposure visualization. |
| 2 | `AtmosphereSkyViewLutPass` | Real, distinct sky-panorama-shaped LUT image (light blue-white dome band over black) — clearly different from both the Transmittance and Multi-Scattering LUTs. |
| 3 | `AtmosphereAerialPerspectiveVolumePass` | Real, ray-marched, glowing light-blue tapering-frustum thumbnail (PHASE4) — narrow near-edge, widening far-edge, exactly the atmosphere-aware interpretation `atmosphere-scattering-2`/`-3` built. |
| 4 | `AtmosphereAerialPerspectiveVolumeDebugSlicePass` | Real, distinct solid-blue rectangle — its own real captured content (this pass renders one flat Z-slice of the froxel volume), correctly different from every other leaf. |
| 5 | `GameView` | The true pre-atmosphere-composite reconstruction (unchanged `frame-debugger-4` behavior, re-confirmed). |
| 6 | `AtmosphereAerialPerspectiveCompositePass` | Pixel-matches `GET /get_game_view`'s own final, fog-inclusive output exactly (re-confirmed, unchanged from `frame-debugger-4`'s own already-proven behavior). |
| 7-9 | (the post-GameView re-run occurrences of Sky-View LUT/Aerial Perspective Volume/Composite) | Not individually re-screenshotted this phase (PHASE3/PHASE4 already confirmed these show byte-for-byte identical content to their pre-GameView twins, since both write the exact same underlying named texture) — spot-confirmed via `/frame_debugger/state` reporting the correct tree position/count only. |

Every leaf's own Inspector "Pass"/read-write rows were also visually confirmed correct and non-fabricated at
each step (`Blend`/`ZClip`/`ZTest`/`ZWrite`/`Cull`/`Stencil Ref` all read `n/a (compute pass)` for every
compute leaf; `"GameView"` shows real `Opaque (no blend)`/`Less`/`On`/`None` pipeline state).

### 5. Bug found and fixed: HDR compute-pass previews were visually indistinguishable from black

**Root cause.** `AtmosphereMultiScatteringLutPass`/`AtmosphereSkyViewLutPass`/
`AtmosphereAerialPerspectiveVolumeDebugSlicePass` all write to `VK_FORMAT_R16G16B16A16_SFLOAT` — this engine's
one genuinely HDR 2D-texture color format (`atmosphere-scattering-1` campaign's own Phase 4). A
multi-scattering "response"/LUT value is, by design, a tiny fraction of 1.0 in absolute terms (see
`src/Encoding/HdrColorVisualization.cpp`'s own pre-existing "typically small in absolute terms" doc comment).
PHASE3's straight `vkCmdCopyImage` retained-copy path (`ComputePassCopySource`) preserves those raw HDR bytes
correctly — the bytes really are copied, and `Renderer::CaptureImagePixels()`-based cross-checks against `GET
/get_texture` (which applies a debug-only 400x exposure + Reinhard tonemap for this exact format) confirmed the
underlying data was never actually corrupted or missing — but the Inspector's plain `ImGui::Image()` display of
those raw values, with no exposure/tonemap applied anywhere in the Frame Debugger's own preview path, renders as
solid black to the human/AI-agent eye. This directly violated this phase's own explicit success criterion
("not a blank/black rectangle") for 3 of the 9 real compute-dispatch leaves.

**Fix** (`src/Editor/FrameDebuggerHistory.cpp`, `src/Editor/FrameDebuggerHistory.h` doc-comment updates only):
`FrameDebuggerHistory::CaptureFrame()`'s existing 2D-texture-write discovery loop now checks each discovered
write's real, current format. A `VK_FORMAT_R16G16B16A16_SFLOAT` write is routed to a NEW, separate loop
(mirroring the phase document's own precedent of reusing already-shipped code rather than inventing new logic):

1. `Renderer::CaptureImagePixels(source.image, ..., VK_FORMAT_R16G16B16A16_SFLOAT, ..., source.colorState,
   /*bytesPerPixel=*/8)` — a CPU readback of the source's real, currently-tracked GPU state (never assuming
   `ShaderRead`), restoring it afterward, exactly like every existing capture path already does.
2. `Encoding::ConvertHdrRgba16fToRgba8()` — the EXACT SAME, already-shipped, already-tested function
   `GET /get_texture` has used for this one format since `atmosphere-scattering-1` — never a second,
   independently-maintained copy of its exposure/tonemap math.
3. Upload the converted RGBA8 pixels into a fresh `VK_FORMAT_R8G8B8A8_UNORM` `RenderTexture` via a staging
   buffer, mirroring this SAME file's own already-existing volume-texture-preview upload sequence (PHASE4)
   byte-for-byte in shape.

Every existing, already-verified LDR (`R8G8B8A8_UNORM`/`BGRA8_UNORM`) compute-pass preview — Transmittance
LUT, the Aerial Perspective Composite pass's own `"GameViewComposited"` write, GPU Skinning's own (buffer, so
already excluded) write, any future non-HDR compute pass — keeps using the exact same zero-risk, single-
`ImmediateSubmit()` GPU-to-GPU copy it already used before this fix, byte-for-byte unchanged; confirmed by
re-screenshotting the Transmittance LUT leaf (index 0) after the fix landed and seeing byte-for-byte the same
gradient image as before.

**Verified after the fix** (re-launched the engine, repeated steps 3-4 above): `AtmosphereMultiScatteringLutPass`
now shows a real, distinct blue-to-black gradient; `AtmosphereSkyViewLutPass` (also `VK_FORMAT_R16G16B16A16_SFLOAT`,
confirmed by the same `GET /list_textures` inspection that found `AtmosphereMultiScatteringLut`'s format) now
correctly shows its own distinct sky-panorama gradient through the same fix; `AtmosphereAerialPerspectiveVolumeDebugSlicePass`
shows a real, distinct solid-blue rectangle. Re-ran the full clean build (both `GTE_ENABLE_EDITOR` configs) and
the full `ctest` regression suite AFTER this fix landed — both are the authoritative final numbers reported in
Steps 3.3/3.4 above.

### 6. Shutdown

`stop_app_background(pid)` — clean shutdown, no hang, matching every other campaign's own precedent.

## Why no new Tier-1 test was added for the HDR fix

The fix itself is a pure "which already-tested, pure function do I call, and what GPU format do I compare
against" branch inside `FrameDebuggerHistory::CaptureFrame()` — a Tier-2, `VkDevice`-requiring method with no
automated coverage today (exactly like the rest of that method, per PHASE3/PHASE4's own already-established,
unchanged rationale: "a real ray-march/GPU copy needs a live `VkDevice`"). The one new PURE decision this fix
introduces — "is this format the one HDR format, yes/no" — is a single `==` comparison against a named Vulkan
constant, not a function complex enough to warrant its own extracted Tier-1 test; the actual EXPOSURE/TONEMAP
MATH this fix relies on (`Encoding::ConvertHdrRgba16fToRgba8()`) already has its own, complete, pre-existing
Tier-1 test coverage (confirmed still passing, unchanged, in the ctest run above) from the `atmosphere-scattering-1`
campaign that introduced it — this fix is a NEW CALLER of that already-tested function, not new untested logic.
The fix's own correctness was instead verified the same way every other Tier-2 GPU capture path in this
campaign already was: a live, HTTP-driven, screenshot-verified smoke test (above), matching this whole
campaign's own established verification discipline for GPU-resident code.

## Deviations from the phase document

None in shape — the phase document's own Step 3.5 explicitly anticipated and pre-authorized exactly this
scenario ("If anything in this live pass reveals a real behavioral gap PHASE1-4 missed... fix it as part of
this closing phase"). No `ask_questions` call was needed: the fix is a narrow, well-precedented reuse of
already-shipped, already-reviewed code (the same "reuse, don't reinvent" discipline PHASE4 itself already
established for the volume-texture ray-march), not a genuinely large or ambiguous design decision.

## Next

`CAMPAIGN_COMPLETION_REPORT.md` (this same folder) is the campaign-level summary — see that file for the full
phase-by-phase recap, final architecture diagram, complete file-change inventory (including this phase's own
`FrameDebuggerHistory.cpp`/`.h` HDR fix), and every outstanding/deferred item.
