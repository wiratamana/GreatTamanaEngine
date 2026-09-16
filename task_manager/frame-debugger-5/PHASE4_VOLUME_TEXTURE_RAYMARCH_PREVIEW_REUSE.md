# PHASE4 — Volume-Texture Ray-March Preview Reuse

## Parent -> `PHASE0_MASTER_STRATEGY.md`, read it first. Depends on PHASE1, PHASE2, and PHASE3 already landed.

Branch: `feature/frame-debugger-impl`
Risk level: MEDIUM — self-contained (only the one volume-writing pass, `AtmosphereAerialPerspectiveVolumePass`,
is affected today), but reuses an existing, already-shipped GPU utility class in a NEW way it was not
originally written for (Editor-side retained storage instead of one-shot HTTP-response CPU pixels) — read
`src/Renderer/VolumeTexturePreviewRenderer.h/.cpp` in full before starting.

## Step 1: The Goal (Where are we going?)

Close the ONE gap PHASE3 deliberately left open: a compute pass whose only visual write is a 3D volume
texture (today: `AtmosphereAerialPerspectiveVolumePass`, writing the 128x128x32 Aerial Perspective froxel
volume) must ALSO show a real preview when its own leaf is selected in the Frame Debugger — specifically, a
real ray-marched 2D thumbnail, per the user's own explicit answer during this campaign's design review
("there should be a preview 3d volume texture using ray marching. please reuse that code"). Concretely,
after this phase:

- `FrameDebuggerHistory::CaptureFrame()` also eagerly produces a real ray-marched thumbnail for every
  compute pass whose write list contains a `ResourceKind::VolumeTexture` entry, reusing
  `VolumeTexturePreviewRenderer::RenderPreview()` (`src/Renderer/VolumeTexturePreviewRenderer.h/.cpp`) —
  the EXACT SAME code `GET /get_texture` already uses to preview a volume texture over HTTP — never a new,
  separately-written raymarcher.
- This thumbnail is retained into the SAME `computePassPreviews` mechanism PHASE3 built, keyed by the same
  pass name, so `ChooseFrameDebuggerPreviewSource()`/`EnsurePreviewDescriptor()` need ZERO further changes
  to display it — from the picking-logic's point of view, a volume-derived preview and a plain 2D-texture
  preview are indistinguishable, both are just "this pass's own retained preview texture."
- Selecting the Aerial Perspective Volume pass's own leaf now shows a real, recognizable ray-marched
  rendering of that volume's current contents, not a placeholder and not the whole-frame Game View image.

## Step 2: The Situation (Where are we now?)

`VolumeTexturePreviewRenderer` (`src/Renderer/VolumeTexturePreviewRenderer.h`) is a small, self-contained,
already-shipped, on-demand GPU utility:

```cpp
class VolumeTexturePreviewRenderer {
public:
    struct CapturedRawPixels {
        std::vector<std::uint8_t> pixels; // width*height*4 RGBA8, tightly packed.
        int width = 0;
        int height = 0;
    };

    CapturedRawPixels RenderPreview(Renderer& renderer, const VolumeTarget& volume, const rg::ResourceState& previousState,
        VolumeTexturePreviewInterpretation interpretation = VolumeTexturePreviewInterpretation::GenericDensityInAlpha);
    // ...
};
```

It raymarches an arbitrary live `VolumeTarget` into a fixed 256x256 RGBA8 thumbnail, blocking (uses
`Renderer::ImmediateSubmit()` internally), restoring the source volume's own image layout to
`previousState` before returning — already exactly the "at most once per request, never per-frame" cost
tier this campaign's own eager-capture design (Locked Design Decision #4) already accepts for
`CaptureFrame()` as a whole. `Application.cpp` already owns one instance
(`m_volumeTexturePreviewRenderer`) and already picks between
`VolumeTexturePreviewInterpretation::GenericDensityInAlpha`/`AtmosphereAerialPerspective` based on the
requested texture name (see `Application.cpp`'s own call site, ~line 1127) — **find and reuse this exact
same interpretation-selection rule** rather than re-deriving it independently; if it is not trivially
reusable as-is (e.g. it is private/local to `Application.cpp`), extract it into a small, shared, named
function both call sites can use, rather than duplicating the logic by hand.

`RenderGraph::DebugVolumeTextureSnapshotFor(name)` (`src/Renderer/RenderGraph/RenderGraph.h`) is the volume
counterpart of PHASE3's `DebugTextureSnapshotFor()` — already returns a real
`DebugVolumeTextureSnapshot{ name, regime, target (VolumeTarget), state }` for any volume-texture name ever
seen this session, already correctly kept up to date, already reachable via the same
`m_frameRenderGraph`/`renderGraph` parameter PHASE3 already threaded into `CaptureFrame()`.

`VolumeTexturePreviewRenderer::RenderPreview()` returns CPU-side pixels (it was built for an HTTP JSON
response, `GET /get_texture`) — NOT a live GPU texture/`VkImageView` ready for `ImGui::Image()`. This phase
needs those CPU pixels re-uploaded into a small, retained GPU texture the Frame Debugger CAN display. Find
this engine's own existing "raw RGBA8 CPU pixel buffer -> a displayable GPU texture" precedent (e.g. how
`Renderer::CreateTexture2D()` already accepts raw pixel data + a debug name, used throughout
`AtmosphereLutRenderer.cpp`/elsewhere to seed a texture's initial contents via a staging buffer) — reuse
that exact mechanism rather than hand-rolling a new upload path.

## Step 3: The Plan

### 3.1 Where to detect a volume-only write

Extend PHASE3's revised "Step A" collection pass (v2 review finding — PHASE3's own Step 3.3 was revised
during this campaign's second-iteration review to walk the raw `rg::RenderGraphSnapshot` directly, via
`renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback)`, rather than string-matching
against Editor display labels — read PHASE3's own Step 3.3 in full before touching this): for every real,
surviving (`isCulled == false`), `isComputePass == true` pass, ALSO look for the FIRST index `i` where
`pass.writeKinds[i] == rg::ResourceKind::VolumeTexture`, and if found, collect `{pass.name,
pass.writeNames[i]}`. Collect these into a SEPARATE small list (pass name + volume write name), distinct from
PHASE3's 2D-texture list — a pass could, in principle, have both kinds of write in a future engine (not true
of any pass today), so keep the two collection passes independent rather than assuming "exactly one visual
write kind per pass." If you followed PHASE3's own Step 3.6 recommendation and extracted its 2D-texture
collector as a standalone, named, Tier-1-tested pure function taking an `rg::RenderGraphSnapshot`, prefer
adding this volume-write collector as a SIBLING pure function right next to it (same file, same signature
shape, just checking `VolumeTexture` instead of `Texture`) rather than duplicating the traversal loop by
hand — the two are otherwise identical except for which `ResourceKind` they filter on.

### 3.2 `CaptureFrame()`'s widened body — the ray-march branch

For each collected `(passName, volumeWriteName)` pair:

1. `const auto volumeSnapshot = renderGraph.DebugVolumeTextureSnapshotFor(volumeWriteName);` — skip silently
   (no crash) if `std::nullopt` (defensive only, mirrors PHASE3's own 2D case).
2. Own a `VolumeTexturePreviewRenderer` instance somewhere reachable from `CaptureFrame()` — the simplest,
   safest choice is a new private member directly on `FrameDebuggerHistory` (mirrors how `Application` itself
   just owns one directly; this class is explicitly designed to be cheap to own one-per-consumer, see its
   own header comment: "self-contained... this is invoked at most once per network request"). If you find a
   good reason this ownership should live elsewhere instead (e.g. `FrameDebuggerPanel`), that is fine, but
   if genuinely torn between two reasonable homes for it, `ask_questions`.
3. Call `m_volumePreviewRenderer.RenderPreview(renderer, volumeSnapshot->target, volumeSnapshot->state,
   <interpretation, resolved per Step 2's reuse note above>)` — this is its OWN internal
   `ImmediateSubmit()` call, genuinely SEPARATE from the main copy-loop's `ImmediateSubmit()` (this is
   unavoidable: `RenderPreview()`'s own implementation is not something this phase should try to inline/merge
   into a shared command buffer — respect its existing self-contained contract exactly as `Application.cpp`
   already does). This means one `CaptureFrame()` call, once volume passes exist, now issues 2 (or more)
   separate GPU submissions total — document this explicitly in the completion report as a deliberate,
   accepted, still-genuinely-on-demand cost (capture only happens on Enable-edge/Step/explicit-Capture-click,
   never per real frame), not a regression of the "one submission" rule from PHASE0's Locked Design Decision
   #8 (that rule is about the MAIN whole-frame + per-2D-pass copy loop specifically, not about a
   deliberately-separate, already-self-contained utility class this phase merely calls into).
4. Take the returned `CapturedRawPixels{pixels, width, height}` and upload it into a freshly (re)created,
   retained texture object (reuse whatever this engine's own established "raw pixels -> displayable GPU
   texture" helper turns out to be, per Step 2's reuse note — if it is `Texture2D` rather than
   `RenderTexture`, and `FrameDebuggerComputePassPreview::preview` (PHASE3) is typed as `RenderTexture`, you
   have TWO reasonable resolutions: (a) widen `FrameDebuggerComputePassPreview` to hold "whichever of
   `RenderTexture`/`Texture2D` is present" (e.g. via a small tagged union / two optional fields), or (b)
   create a `RenderTexture` here too and upload into IT via the same staging-buffer approach
   `Renderer::CreateTexture2D()`'s own implementation already uses internally, keeping
   `FrameDebuggerComputePassPreview` a single, uniform type. **This is exactly the kind of implementation-detail
   fork this campaign expects you to resolve via `ask_questions` if it is not obvious once you have actually
   read `Texture2D.h`/`RenderTexture.h`'s real interfaces** — do not silently pick one and move on if it
   feels like a coin flip.
5. Append the resulting entry to `entry.computePassPreviews`, keyed by `passName`, exactly like PHASE3's 2D
   entries — from here on, PHASE3's own picking/display logic needs no further changes at all.

### 3.3 Tests

`CaptureFrame()` stays Tier-2 for this new branch too (a real ray-march needs a live `VkDevice`) — if you
extracted a pure "collect volume-write pass/name pairs from a snapshot" helper (mirroring PHASE3's own Step
3.6 suggestion), add real Tier-1 tests for THAT pure function in
`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (or wherever PHASE3 put its own equivalent 2D-texture
version) covering: a pass with a volume write is correctly found; a pass with only a 2D-texture write is
correctly excluded from this list; a pass with both (hypothetically) appears in both lists.

### 3.4 What this phase deliberately does NOT do

- Does not change `VolumeTexturePreviewRenderer`'s own public contract at all — it is reused exactly as-is,
  called from a NEW place, never modified.
- Does not add a way to pick a specific Z-slice/orbit angle for the Frame Debugger's own thumbnail — reuses
  `RenderPreview()`'s own existing FIXED camera framing verbatim, exactly like `GET /get_texture` already
  does. A future, richer, interactive volume-preview control is out of scope here.
- Does not attempt to make this ray-march path cheaper/deferred/lazy — Locked Design Decision #4 (eager
  capture) still applies; a volume pass's own thumbnail is captured on every real capture trigger, exactly
  like every 2D one.

### 3.5 Fast compile check

```
cmake --build build --target GreatTamanaEngineTests
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure -R FrameDebugger
```

A genuine, real, running-engine sanity check is strongly recommended before considering this phase done
(the ray-march path cannot be meaningfully exercised by a Tier-1/Tier-2 unit test alone) — use
`run_app_background` to launch `GreatTamanaEngine.exe`, trigger a Frame Debugger capture over HTTP (see
`docs/conventions/frame-debugger.md`'s existing HTTP automation entry points, `frame-debugger-3` PHASE7),
select the Aerial Perspective Volume leaf, and use `gte_send_request`/`load_image` to visually confirm a
real, non-blank, recognizably ray-marched thumbnail appears — then `stop_app_background` when done. Do not
skip this just because the compile check passed.

### 3.6 Report

Write `task_manager/frame-debugger-5/PHASE4_COMPLETION_REPORT.md`, including a real screenshot/description
of the ray-marched thumbnail you actually saw. Commit via `git_add`/`git_commit`.
