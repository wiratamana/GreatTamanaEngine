# PHASE4_COMPLETION_REPORT — Named Texture Endpoint Volume Branch Wiring (network-impl-6)

Parent: `PHASE0_MASTER_STRATEGY.md`. Task file:
`PHASE4_NAMED_TEXTURE_ENDPOINT_VOLUME_BRANCH_WIRING.md`. Previous phase:
`PHASE3_VOLUME_RAYMARCH_PREVIEW_RENDERER.md` (`PHASE3_COMPLETION_REPORT.md`
read in full before starting this one — nothing in it superseded this
phase's own assumptions; `VolumeTexturePreviewRenderer::RenderPreview()`'s
real signature, `CapturedRawPixels` shape, and the "not wired to the network
endpoint yet" starting state all matched exactly what this phase's own Step 3
plan assumed).

## Summary

Wired `Application::Run()`'s existing `FrameCaptureKind::NamedTexture`
handling block to branch on volume textures: a requested `texture_name` that
does NOT resolve to a registered 2D texture but DOES resolve to a registered
volume texture (via `RenderGraph::DebugVolumeTextureSnapshotFor()`) now
renders a fresh on-demand raymarch thumbnail via Phase 3's
`VolumeTexturePreviewRenderer` and rejoins the exact same PNG-encode +
`FulfillPendingRequest()` tail every existing 2D capture already uses. `GET
/get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView` now
works end-to-end over real HTTP, verified with a live running engine. The
existing 2D-texture code path was not touched at all (not one line inside the
existing `if (const std::optional<rg::DebugTextureSnapshot> snapshot = ...)`
block was edited).

## What was done

Followed Step 3 of `PHASE4_NAMED_TEXTURE_ENDPOINT_VOLUME_BRANCH_WIRING.md`
exactly:

1. **`src/Application/Application.h`** (Step 3.1)
   - Added `#include "../Renderer/VolumeTexturePreviewRenderer.h"`.
   - Added a new plain member, `VolumeTexturePreviewRenderer
     m_volumeTexturePreviewRenderer;`, declared right after
     `m_atmosphereLutRenderer` — the same "constructed once, reused every
     call, genuinely lazy/on-demand, costs nothing until its first real call"
     ownership shape `m_atmosphereLutRenderer` itself already has.
2. **`src/Application/Application.cpp`** (Steps 3.2/3.3/3.4)
   - Added the new `else if (const std::optional<rg::DebugVolumeTextureSnapshot>
     volumeSnapshot = m_renderGraph.DebugVolumeTextureSnapshotFor(requestedName))`
     branch, in exactly the one place the strategy document specified — right
     after the existing 2D `if` block's closing brace, inside the same outer
     `if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::NamedTexture))`
     guard. The existing 2D branch's own code (including its own
     `m_renderer.WaitForGpuIdle()` call and every one of its format-dependent
     branches) is byte-for-byte unchanged.
   - The new branch fast-fails with `FrameCaptureFailureReason::TargetNotAvailable`
     (HTTP 409) when `requestedChannel == DebugTextureChannel::Depth` — a
     volume texture has no depth-companion concept at all.
   - Otherwise: computes `framesSinceUpdate` from
     `m_renderGraph.CurrentDebugTextureFrameCounter() -
     volumeSnapshot->lastUpdatedFrameCounter` (the SAME shared frame counter
     the 2D path already uses — no second counter), calls
     `m_renderer.WaitForGpuIdle()`, then
     `m_volumeTexturePreviewRenderer.RenderPreview(m_renderer,
     volumeSnapshot->target, volumeSnapshot->state)`, PNG-encodes the
     already-tightly-packed RGBA8 result directly (no BGRA swizzle, no HDR
     conversion, no depth-to-grayscale conversion — none of the 2D path's
     format-detection branching applies here), and calls
     `m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
     CapturedPngImage{...})`.
   - `#include` hygiene: `rg::DebugVolumeTextureSnapshot` was already visible
     transitively through `Application.h`'s existing
     `#include "../Renderer/RenderGraph/RenderGraph.h"` (which itself
     `#include`s `RenderGraphDebugVolumeTextureRegistry.h` — confirmed by a
     direct grep before adding anything), so no redundant new include was
     added for it, per the strategy document's own "check before adding a
     redundant include" instruction.
   - The trailing `// else: ...` comment after the whole if/else-if chain was
     updated from "this name has never been registered yet this session" to
     "this name has never been registered as EITHER kind yet this session",
     matching the strategy document's own exact wording — nothing else below
     the if/else-if chain (the `GET /list_textures` block) was touched.

## Deviations from the strategy document

**One incidental, self-inflicted mistake made and fully corrected during this
phase's own editing process, not a deviation from the plan's design:** an
early `edit_line` call meant to insert the two new `#include` lines into
`Application.h` used a `length` that also removed several existing lines
(the file's opening class doc-comment paragraph, the `namespace gte {`
opener, and the pre-existing `#include "../Window/Window.h"` line) —
caught immediately by the very next fast compile check, which failed with
`field 'm_window' has incomplete type 'gte::Window'`. Both accidentally
-removed pieces (the doc comment/namespace opener, and the `Window.h`
include) were restored in full via two follow-up `edit_line` calls, and the
file was re-read afterward to confirm it now matches its original shape plus
only the two intended additions (`VolumeTexturePreviewRenderer.h`'s include
and the new member). This is flagged here for transparency, not because it
represents a design deviation from the strategy document — the final,
committed `Application.h` matches the plan exactly (new include + new member,
nothing else changed) and the "field has incomplete type" compiler error
caught the mistake before it could reach a commit.

No other deviations. Every other concrete detail in the strategy document
(the exact `else if` shape, the depth fast-fail, the shared frame counter,
the "no format branching needed" note, the ordering-tie-break reasoning in
Step 3.3) matched the real source tree exactly and needed no further
correction.

## Verification

Per this phase's own "Verification" section:

1. **Fast compile check**:
   - `cmake --build build --target gte_core` — succeeded cleanly (after the
     one incidental mistake above was caught and fixed by this same step,
     then re-run clean).
   - `cmake --build build --target GreatTamanaEngineTests` — succeeded.
   - `cmake --build build --target GreatTamanaEngine` (the full app target,
     as this phase's own Verification section explicitly calls for, since
     its whole point is an end-to-end runtime behavior) — succeeded, shader
     staging log confirms `VolumeTexturePreview.comp.spv` (built in Phase 3)
     is staged next to the built `.exe` alongside every other shader.
2. **Real runtime smoke test** (this phase's actual acceptance criterion),
   against the freshly rebuilt `GreatTamanaEngine.exe`:
   1. Launched via `run_app_background` (PID 16740).
   2. Waited briefly for the Atmosphere feature's two aerial-perspective
      volumes to start resolving (always active from the first rendered
      frame — no on/off switch).
   3. `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
      returned a real HTTP 200 PNG (9805 bytes) — visually inspected via
      `load_image`: a clearly non-cubic, thin box silhouette rendered from a
      plausible isometric-ish angle against a dark-gray background — neither
      solid black, nor solid background, nor garbage/noise.
   4. Re-ran the exact same request a second time — identical HTTP 200,
      identical byte count (9805 bytes), identical visual result — confirms
      the renderer's persistent output/descriptor-set-rewrite state is safe
      to call repeatedly, not just once.
   5. `GET /get_texture?texture_name=Swapchain` (a known-good, pre-existing
      2D texture name) — returned a real HTTP 200 PNG (139206 bytes) showing
      the live Editor UI (Hierarchy/Inspector/Scene/Game/dock panels), exactly
      the same as before this campaign. `GET
      /get_texture?texture_name=AtmosphereAerialPerspectiveVolumeDebugSlice`
      (another pre-existing 2D texture, itself a slice of the SAME volume
      this phase's new branch also serves, but registered and served through
      the completely separate, unmodified 2D code path) also returned a
      normal HTTP 200 PNG showing a plausible transmittance-gradient slice —
      both confirm zero regression to the existing, shipped 2D-texture
      capture path.
   6. `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView&channel=depth`
      returned HTTP 409 with a JSON error body
      (`"requested channel is not available for texture_name ..."`) — not a
      crash, not a hang, not a 200 — exactly the expected fast-fail.
   7. Stopped the background process (`stop_app_background`, PID 16740) once
      verification completed.
3. Per this phase's own workflow rule (Phase 0's "No full build/full
   regression test until Phase 6"), no `ctest` run was performed this phase.

## Result

Phase 4 is complete: `GET /get_texture` now genuinely serves BOTH 2D textures
and volume textures through the exact same endpoint, query-parameter
negotiation, and cross-thread bridge, with zero new endpoint/parameter/bridge
type — confirmed against a real, live running engine over real HTTP. Every
pre-existing 2D-texture behavior is confirmed byte-for-byte unchanged. Next:
Phase 5 (`PHASE5_LIST_TEXTURES_VOLUME_SURFACING.md`), which makes `GET
/list_textures` also surface every registered volume texture (today,
`ListDebugVolumeTextures()` still has no caller outside `RenderGraph` itself).
