# PHASE6 — Real Channels (All/R/G/B/A) + Levels preview processing

## Parent -> `PHASE0_MASTER_STRATEGY.md` (READ THIS FIRST — Locked Design Decision #8).
## Depends on: `PHASE1`..`PHASE5` (already landed).

## Step 1: The Goal

Make the existing Channels row (five cosmetic `SmallButton`s: All/R/G/B/A) and Levels slider
(currently disabled) functionally real: selecting a channel actually isolates it in the preview
image, and dragging the Levels slider actually remaps black/white points — via a brand-new, small,
dedicated preview-compositing mechanism, explicitly NOT by reusing `/get_texture`'s existing,
semantically-unrelated `channel=color|depth` query parameter.

## Step 2: The Situation

- `FrameDebuggerPanel::BuildInspectorPane()`'s Channels row/Levels slider already exist as
  disabled controls (`frame-debugger-2`, PHASE5) — this phase makes them real, reading/writing new
  persisted state on `FrameDebuggerPanel` itself (e.g. `m_channel` — an enum `All`/`R`/`G`/`B`/`A`
  — and `m_levelsBlack`/`m_levelsWhite` floats).
- **`VolumeTexturePreviewRenderer`/`Shaders/VolumeTexturePreview.comp`**
  (`src/Renderer/VolumeTexturePreviewRenderer.h/.cpp`) is this codebase's own existing precedent
  for "a small, dedicated, on-demand compute shader that reads one source texture and writes a
  fresh, small preview output texture" — copy this SHAPE (not its raymarch math, which is
  unrelated) for this phase's own `FrameDebuggerPreviewProcessing` mechanism: a tiny compute
  shader (`Shaders/FrameDebuggerPreview.comp`) that reads PHASE3's retained per-slot preview
  texture, applies channel-isolate + levels remap per pixel, and writes into a SEPARATE, small,
  dedicated scratch `RenderTexture` the panel actually displays (never mutates the retained
  historical copy in place — a user must be able to flip Channels/Levels back and forth without
  losing the original captured pixels).
- **Do not reuse `/get_texture?channel=color|depth`** — that parameter means "give me the color
  half or the depth half of a named texture," an entirely different axis from "isolate the R/G/B/A
  channel of an already-color image for VISUAL inspection." Any new query parameter this phase (or
  PHASE7) introduces for this purpose must use ITS OWN, differently-named parameter (e.g.
  `channel=all|r|g|b|a` on a brand-new `/frame_debugger/set_channel` route, never anywhere near
  `/get_texture`).
- Recompute the scratch preview ONLY when `m_channel`/`m_levelsBlack`/`m_levelsWhite`/the current
  history cursor actually changed since the last recompute (a small "is this still dirty" check) —
  never every single ImGui frame; this mirrors `RenderGraphPanel`/`ProfilerPanel`'s own existing
  "only recompute a frozen snapshot when its own Pause-state actually changes" discipline in
  spirit, applied here to avoid needless per-frame compute dispatches for a UI element that only
  changes on an explicit user click/drag.

## Step 3: The Plan

### 3.1 New module: `FrameDebuggerPreviewProcessing`

Home: `src/Editor/FrameDebuggerPreviewProcessing.h`/`.cpp` (`GTE_ENABLE_EDITOR`-only) + a new
`Shaders/FrameDebuggerPreview.comp`. Two clean layers, mirroring this whole codebase's
"pure CPU math oracle first, GPU shader mirrors it" discipline (`AGENTS.md`'s "Atmosphere
Scattering"/"GPU Vertex Skinning" sections both establish this exact precedent for a reason — copy
it here too, at a much smaller scale):

1. **A pure, Tier-1-tested CPU function** describing the exact per-pixel transform, e.g.:
   ```cpp
   enum class FrameDebuggerPreviewChannel { All, R, G, B, A };
   std::array<float, 4> ApplyFrameDebuggerPreviewTransform(
       std::array<float, 4> srcRgba, FrameDebuggerPreviewChannel channel,
       float levelsBlack, float levelsWhite) noexcept;
   ```
   Isolating a channel (e.g. `R`) replicates that one channel's value across R/G/B (alpha forced
   to 1.0), matching Unity's own "view a single channel as a grayscale image" convention (already
   referenced by this campaign's own reference screenshot's "Channels" row) — `All` passes RGBA
   through unchanged except for the levels remap. Levels remap: `clamp((x - levelsBlack) /
   max(levelsWhite - levelsBlack, 1e-5), 0, 1)`, applied per-channel BEFORE any channel-isolation
   masking (confirm/adjust ordering here if implementation reveals a more visually sensible order
   — document whichever is chosen).
2. **A tiny compute shader mirroring that exact same formula in GLSL** (`FrameDebuggerPreview.comp`),
   taking the source image as a sampled input and levels/channel as push constants, writing the
   composited RGBA8 result into the scratch `RenderTexture`.

### 3.2 Wiring inside `FrameDebuggerPanel`

- Channels row: each `SmallButton` sets `m_channel` directly (no longer wrapped in
  `ImGui::BeginDisabled()`), and visually highlights whichever is currently active (compare against
  every other toggle-button precedent already in this codebase, e.g. `ProfilerPanel`'s own
  view-mode buttons, for the exact "highlighted when active" ImGui idiom to copy).
- Levels slider: a real `ImGui::SliderFloat`/two-handle range control writing
  `m_levelsBlack`/`m_levelsWhite` (clamp `levelsBlack < levelsWhite` always).
- On any change (or a history-cursor change from PHASE4), dispatch the compute shader once via
  `renderer.Dispatch(...)` (the existing generic compute-dispatch primitive, `Renderer.h`) against
  the currently-viewed history entry's retained texture, writing into one shared scratch
  `RenderTexture` the panel itself owns (lazily created, sized to match the largest historical
  entry seen so far, or simply re-created at the source's exact size each time it's needed —
  implementer's call based on real measured cost).
- The preview box (PHASE4) now displays THIS scratch texture instead of the raw retained one,
  whenever `m_channel != All` or the levels sliders are non-default; when both are at their
  neutral defaults (`All`, `levelsBlack = 0`, `levelsWhite = 1`), it is equally correct to just
  display the raw retained texture directly and skip the dispatch entirely (a valid, cheap
  optimization — document if chosen).

### 3.3 Tier-1 testing

`tests/Editor/FrameDebuggerPreviewProcessingTests.cpp` — table-driven cases for
`ApplyFrameDebuggerPreviewTransform()`: `All` + neutral levels is a no-op; isolating each of
`R`/`G`/`B`/`A` produces the expected replicated-grayscale/alpha-forced-1 result for a hand-picked
non-trivial input color; a levels remap with `black=0.25, white=0.75` correctly clamps/rescales a
few hand-picked sample values (including one below `black` -> 0, one above `white` -> 1, one
exactly halfway -> 0.5).

### 3.4 What this phase explicitly does NOT do

- Does not add a new HTTP endpoint (PHASE7's job, though PHASE7 DOES add `set_channel`/
  `set_levels` routes that ultimately write these SAME `m_channel`/`m_levelsBlack`/`m_levelsWhite`
  fields — this phase only needs to make those fields exist and be real, so PHASE7 has something
  concrete to set).
- Does not touch `/get_texture`'s own existing `channel` parameter in any way.
- Does not apply any processing to the EVENT TREE or event-details text — only to the preview
  IMAGE.

### 3.5 Compile check

Fast compile check (`GreatTamanaEngine` + `GreatTamanaEngineTests`, run
`--gtest_filter=*FrameDebuggerPreview*`). A live smoke test toggling each channel button and
dragging the Levels slider, screenshotted via `/get_swapchain` once the window happens to be
reachable, is worthwhile here if practical, but not blocking (PHASE8 covers this exhaustively via
full HTTP automation).

### 3.6 File-change inventory

New: `src/Editor/FrameDebuggerPreviewProcessing.h`, `src/Editor/FrameDebuggerPreviewProcessing.cpp`,
`Shaders/FrameDebuggerPreview.comp`, `tests/Editor/FrameDebuggerPreviewProcessingTests.cpp`.
Modified: `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (real Channels/Levels state + wiring),
`CMakeLists.txt` (new source + shader compile rule, mirroring `cmake/CompileShaders.cmake`'s
existing per-shader entries), `tests/CMakeLists.txt`.

Write `PHASE6_COMPLETION_REPORT.md` once done.
