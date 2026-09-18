# PHASE1 — Aspect-Ratio-Correct Preview (Feature 1) — COMPLETION REPORT

_Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE1_ASPECT_RATIO_CORRECT_PREVIEW.md` exactly as specified — no deviations
from the phase document were needed._

## What was done

Implemented exactly the plan in `PHASE1_ASPECT_RATIO_CORRECT_PREVIEW.md`,
Step 3, with no material deviation:

1. **`src/Editor/FrameDebuggerData.h`** — added, right after
   `FrameDebuggerRenderTargetInfo`:
   - `struct FrameDebuggerAspectFitRect { offsetX, offsetY, width, height; }` —
     a plain, ImGui-free rectangle.
   - `FrameDebuggerAspectFitRect ComputeAspectFitImageRect(float availableWidth,
     float availableHeight, float sourceWidth, float sourceHeight);` — the one
     pure aspect-fit helper (Locked Design Decision #7 in
     `PHASE0_MASTER_STRATEGY.md`), with full doc comments matching this
     codebase's existing density/tone.

2. **`src/Editor/FrameDebuggerData.cpp`** — added `#include <algorithm>` (for
   `std::max`) and the implementation of `ComputeAspectFitImageRect()`,
   copied verbatim from the phase document's own Step 3.1 code sample
   (letterbox when the source is relatively wider than the box,
   pillarbox when relatively taller, centered on whichever axis has slack,
   with the documented degenerate-input "fill the box at (0,0)" fallback).

3. **`src/Editor/Panels/FrameDebuggerPanel.cpp`**, `BuildInspectorPane()` —
   replaced the texture-preview child window body exactly as the phase
   document's Step 3.2 specifies:
   - Pushed `ImGuiCol_ChildBg` to opaque black (`IM_COL32(0, 0, 0, 255)`)
     around the whole `"FrameDebuggerTexturePreview"` child window (both the
     image-shown branch and the placeholder-text branch), popped it right
     after `ImGui::EndChild()`.
   - When an image is shown, the previously-unconditional
     `ImGui::Image(descriptor, avail)` (which stretched the texture to fill
     the box) is now computed via `ComputeAspectFitImageRect(avail.x,
     avail.y, snapshot.renderTarget.width, snapshot.renderTarget.height)`,
     with the cursor offset by `fit.offsetX`/`fit.offsetY` before drawing the
     image at `ImVec2(fit.width, fit.height)`.
   - The placeholder-text branch (`"No Texture"` / `"Nothing drawn yet at
     this point in the frame."`) is completely untouched except that it now
     sits on the same solid-black background.

4. **`tests/Editor/FrameDebuggerDataTests.cpp`** — added a new test group for
   `ComputeAspectFitImageRect()`, covering every case Step 3.4 of the phase
   document calls for:
   - `ComputeAspectFitImageRectExactAspectMatchFillsWithNoOffset` — matching
     aspect ratios fill exactly, zero offset on both axes.
   - `ComputeAspectFitImageRectWiderSourceLetterboxesTopAndBottom` — a wider
     source in a square box: width fills exactly, height is smaller, bars
     top/bottom (`offsetY > 0`, `offsetX == 0`).
   - `ComputeAspectFitImageRectTallerSourcePillarboxesLeftAndRight` — a
     taller source in a square box: height fills exactly, width is smaller,
     bars left/right (`offsetX > 0`, `offsetY == 0`).
   - `ComputeAspectFitImageRectDegenerateInputFillsAtOrigin` — zero/negative
     `availableWidth`/`sourceWidth` inputs both produce the safe fallback,
     never NaN/negative.
   - `ComputeAspectFitImageRectRealWorldCase417x333In800x400Box` — the exact
     concrete case named in the phase document (this campaign's own
     reference screenshot's reported 417×333 resolution inside an 800×400
     box), hand-computed expected fitted width/offset asserted with
     `EXPECT_NEAR`.

No other file was touched. No deviation from the phase document was needed —
the existing code matched the phase document's own "Situation" description
exactly (verified by reading `BuildInspectorPane()` before making any change),
so the Step 3.2 replacement code could be applied essentially verbatim.

## Verification evidence

1. **Targeted incremental compile check** (no full/clean rebuild):
   - `cmake --build build --target GreatTamanaEngineTests` — succeeded,
     recompiling `FrameDebuggerData.cpp`, `Panels/FrameDebuggerPanel.cpp`,
     and the test binary (9 build steps, 0 errors/warnings related to this
     change).
   - `cmake --build build --target GreatTamanaEngine` — succeeded (the real
     Editor executable, used for the live/visual check below).

2. **Narrow test run** (`tests\GreatTamanaEngineTests.exe
   --gtest_filter=FrameDebuggerDataTest.*`) — **19/19 tests passed**,
   including the 5 new `ComputeAspectFitImageRect*` cases and every
   pre-existing `FrameDebuggerDataTest` case (proving nothing else in this
   file regressed).

3. **Live, HTTP-driven, screenshot-verified smoke test** — launched
   `build\GreatTamanaEngine.exe` via `run_app_background`, then drove it
   over the embedded HTTP server:
   - `GET /frame_debugger/open` → window opened.
   - `GET /frame_debugger/enable?value=true` → enabled.
   - `GET /frame_debugger/capture` → `hasCapturedFrame: true`,
     `totalEventCount: 8`.
   - `GET /get_swapchain` (nothing selected, default `PostComposite`
     "GameView" preview) — **screenshot confirms the real 417×333 Game View
     texture is now shown pillarboxed with solid black bars left/right
     inside the wider preview box**, exactly the reference scenario named in
     the phase document (previously this would have been stretched
     horizontally to fill the box).
   - `GET /frame_debugger/select_event?index=1` (a Pre-GameView compute
     leaf, `AtmosphereMultiScatteringLutPass`) → `GET /get_swapchain`
     confirms the `"Nothing drawn yet at this point in the frame."`
     placeholder now also sits on the same solid-black child background
     (previously the same color, so no visual regression there, but
     confirms the `ImGuiCol_ChildBg` push/pop wraps both branches
     uniformly, per Locked Design Decision #4).
   - `GET /frame_debugger/enable?value=false` to leave the engine in a clean
     state, then `stop_app_background` to close it.

## Deviations from the strategy document

None. Every deliverable in Step 3.6 of
`PHASE1_ASPECT_RATIO_CORRECT_PREVIEW.md` was produced exactly as specified.

## Files changed

- `src/Editor/FrameDebuggerData.h`
- `src/Editor/FrameDebuggerData.cpp`
- `src/Editor/Panels/FrameDebuggerPanel.cpp`
- `tests/Editor/FrameDebuggerDataTests.cpp`
- `task_manager/frame-debugger-9/PHASE1_COMPLETION_REPORT.md` (this file)
