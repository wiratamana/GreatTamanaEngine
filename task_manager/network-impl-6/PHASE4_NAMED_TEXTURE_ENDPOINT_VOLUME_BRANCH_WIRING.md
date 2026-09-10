# PHASE4_NAMED_TEXTURE_ENDPOINT_VOLUME_BRANCH_WIRING

Parent: `PHASE0_MASTER_STRATEGY.md` — **READ THAT FILE FIRST.**
Previous: `PHASE3_VOLUME_RAYMARCH_PREVIEW_RENDERER.md` — read its own
`PHASE3_COMPLETION_REPORT.md` before starting.

## Step 1 — The Goal

Make `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
actually work end-to-end over real HTTP: `Application::Run()`'s existing
`FrameCaptureKind::NamedTexture` handling block gains a new branch — when
the requested name is NOT a registered 2D texture but IS a registered
volume texture, it now calls Phase 3's `VolumeTexturePreviewRenderer`
instead of the plain pixel-copy path, and rejoins the EXACT SAME
PNG-encode + `FulfillPendingRequest()` tail every existing 2D capture
already uses. By the end of this phase, a real `gte_send_request` call
against a running engine returns a real PNG raymarch thumbnail for a
volume name, and every EXISTING 2D-texture request still behaves exactly
as before.

## Step 2 — The Situation / The Problem

Re-read `Application.cpp`'s existing `FrameCaptureKind::NamedTexture` block
in full (roughly lines 836-931, already quoted in
`PHASE0_MASTER_STRATEGY.md`'s Step 2) — this phase's whole job is to insert
ONE new `else if` branch into it, in exactly one place: right where the
existing code currently does

```cpp
if (const std::optional<rg::DebugTextureSnapshot> snapshot = m_renderGraph.DebugTextureSnapshotFor(requestedName)) {
    ... // existing 2D path, UNCHANGED
}
// else: this name has never been registered yet this session - leave the request pending ...
```

This phase changes the shape to:

```cpp
if (const std::optional<rg::DebugTextureSnapshot> snapshot = m_renderGraph.DebugTextureSnapshotFor(requestedName)) {
    ... // existing 2D path, COMPLETELY UNCHANGED - not one line edited.
} else if (const std::optional<rg::DebugVolumeTextureSnapshot> volumeSnapshot =
               m_renderGraph.DebugVolumeTextureSnapshotFor(requestedName)) {
    ... // NEW volume path, this phase's whole job.
}
// else: this name has never been registered as EITHER kind yet this session - leave pending, unchanged comment/behavior.
```

Nothing about the OUTER `if (m_captureBridge.IsCaptureRequested(...))` guard,
the `requestedName`/`requestedChannel` reads, or anything AFTER this
if/else-if chain (the `GET /list_textures` block right below it) changes in
this phase — Phase 5 owns that.

## Step 3 — The Plan

### 3.1 — `Application.h`/`Application.cpp`: add the new renderer as a member

`Application` (the composition root) already owns
`m_atmosphereLutRenderer` (an `AtmosphereLutRenderer`) as a plain member,
constructed once, reused every frame. Add a new member the same way:

```cpp
VolumeTexturePreviewRenderer m_volumeTexturePreviewRenderer;
```

(`#include "Renderer/VolumeTexturePreviewRenderer.h"` in `Application.h`.)
This is a genuinely lazy/on-demand class (see Phase 3 — `EnsureInitialized()`
does nothing until the first real call), so adding it unconditionally as a
member costs nothing at startup, exactly like `AtmosphereLutRenderer`
itself costs nothing until its own first real pass runs.

### 3.2 — The new branch's exact logic

Inside the new `else if` branch (volume path found):

```cpp
} else if (const std::optional<rg::DebugVolumeTextureSnapshot> volumeSnapshot =
               m_renderGraph.DebugVolumeTextureSnapshotFor(requestedName)) {
    if (requestedChannel == DebugTextureChannel::Depth) {
        // A volume texture has no depth-companion concept at all (see
        // VolumeTarget.h) - exactly the same "positively known, permanent
        // failure" fast-fail the 2D path already uses for
        // wantsDepth && !snapshot->hasDepth (see PHASE0_MASTER_STRATEGY.md's
        // Locked Design Decision 6).
        m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
    } else {
        const std::uint64_t framesSinceUpdate =
            m_renderGraph.CurrentDebugTextureFrameCounter() - volumeSnapshot->lastUpdatedFrameCounter;

        m_renderer.WaitForGpuIdle(); // Already the case for the 2D path too - see that branch's own identical call, a few lines above this new one. Safe/idempotent to call twice per request in principle, but only ONE of the two branches ever actually runs per request (if/else-if), so this is called exactly once per request either way.

        const VolumeTexturePreviewRenderer::CapturedRawPixels raw =
            m_volumeTexturePreviewRenderer.RenderPreview(m_renderer, volumeSnapshot->target, volumeSnapshot->state);

        // raw.pixels is ALREADY tightly-packed RGBA8 (see Phase 3's own
        // RenderPreview() doc comment) - no BGRA swizzle, no HDR
        // conversion, no depth-to-grayscale conversion needed at all,
        // unlike the 2D path's own several format-dependent branches.
        std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
        m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
            CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
    }
}
```

Notes:
- `framesSinceUpdate` uses the SAME shared
  `m_renderGraph.CurrentDebugTextureFrameCounter()` the 2D path already
  calls (Phase 2 deliberately did not add a second counter — see that
  phase's own Step 2) — computed BEFORE `WaitForGpuIdle()`/the render,
  exactly mirroring the 2D path's own existing comment about why (a
  capture that takes a few extra milliseconds to render must not report
  itself as fresher than it actually was at the moment the request was
  serviced).
- `WaitForGpuIdle()` is still correct/necessary here even though this
  branch is about to do its OWN extra `ImmediateSubmit()` calls
  internally (Phase 3's `RenderPreview()`) — it ensures every OTHER
  in-flight frame submission (the real Game/Scene/Present regimes) has
  fully finished before this branch starts mutating the volume image's
  layout via a manual barrier, exactly the same reason the 2D path already
  needs it before its own `CaptureImagePixels()` call.
- No `bytesPerPixel`/`isHdrColor`/depth-conversion branching is needed
  here at all — `VolumeTexturePreviewRenderer::RenderPreview()` always
  produces plain RGBA8 (see Phase 3), unlike the 2D path which has to
  handle several source pixel formats. Keep this branch simple; do not
  copy the 2D path's format-detection code into it.

### 3.3 — Double-check ordering against the existing 2D branch

The NEW `else if` must only ever be reached when `DebugTextureSnapshotFor()`
returned `std::nullopt` — i.e. a name can never be BOTH a registered 2D
texture AND a registered volume texture at the same time in practice
(nothing in this engine registers the same string under both kinds), but
if it somehow ever were, the 2D branch wins (matches `GET /get_texture`'s
pre-existing behavior of resolving 2D names first, and is a reasonable,
harmless tie-break either way — do not add special-case logic to detect or
warn about this; it is not expected to ever happen given how names are
chosen throughout this codebase, e.g. `"AtmosphereAerialPerspectiveVolume_GameView"`
vs. `"AtmosphereAerialPerspectiveVolumeDebugSlice"` are already textually
distinct).

### 3.4 — `#include` hygiene

`Application.cpp` already includes whatever is needed for
`rg::DebugTextureSnapshot`/`FrameCaptureKind`/`Encoding::EncodeRgba8ToPng()`
— add `#include "Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h"`
(for `rg::DebugVolumeTextureSnapshot`, though it likely already comes in
transitively via `RenderGraph.h` — check before adding a redundant include)
and confirm `Renderer/VolumeTexturePreviewRenderer.h` is included (from 3.1).

### Verification

- Fast compile check: build `gte_core` and the full `GreatTamanaEngine` app
  target (this phase's whole point is an end-to-end runtime behavior, so a
  real app build — not just `gte_core` — is warranted here, though this is
  still short of the FULL clean-build+ctest regression reserved for Phase
  6).
- **Real runtime smoke test** (this phase's actual acceptance criterion):
  1. `run_app_background` the built `GreatTamanaEngine.exe`.
  2. Wait a moment for a few frames to render (the Atmosphere feature has
     no on/off switch — its two volumes are registered from the very
     first frame that renders anything, see `AGENTS.md`, "Atmosphere
     Scattering").
  3. `gte_send_request` against
     `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
     — confirm a real image comes back (the tool renders it directly for
     you to inspect) and that it visually looks like a plausible
     raymarched box silhouette against a dark-gray background (not solid
     black, not solid gray, not garbage/noise).
  4. Re-run the EXACT SAME request a second time and confirm it still
     works (proves the renderer's persistent output/descriptor-set-rewrite
     state is safe to call repeatedly, not just once).
  5. `gte_send_request` against a KNOWN-GOOD, pre-existing 2D texture name
     (e.g. `"Swapchain"`, or `"AtmosphereAerialPerspectiveVolumeDebugSlice"`)
     and confirm it STILL returns exactly what it did before this
     campaign — this is the regression check for Locked Design Decision
     "never regress existing 2D behavior".
  6. `gte_send_request` against
     `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView&channel=depth`
     and confirm it returns HTTP 409 (not a crash, not a hang, not a 200).
  7. `stop_app_background` the process when done.
- Write `PHASE4_COMPLETION_REPORT.md`, commit.
