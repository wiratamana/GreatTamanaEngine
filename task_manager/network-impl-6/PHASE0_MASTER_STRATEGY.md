# PHASE0_MASTER_STRATEGY — "Texture3D over HTTP" (network-impl-6 campaign)

Orchestrator document. Every other `PHASE<n>_*.md` file in this same folder
is a child task of this one. Read this file FIRST, then read the current
`PHASE<n>` file the Task Status list below points you at. Always read the
previous phase's own `PHASE<n>_COMPLETION_REPORT.md` (if it exists yet)
before starting the next phase — it may contain corrections/clarifications
that supersede a stale assumption in this master file.

## Step 1 — The Goal (Where are we going?)

Add the ability to fetch a **PNG thumbnail of a live, GPU-resident 3D
(volume) texture** over the engine's existing embedded HTTP server — the
same class of capability Unity's Editor gives you when it draws a
raymarched "smoke cloud"-style preview thumbnail for a `Texture3D` asset in
the Inspector, except here the client is an **LLM/AI agent talking to
`GET /get_texture` over loopback HTTP**, not a human looking at an Editor
panel.

Concretely, once this campaign is done:

```
GET http://127.0.0.1:8080/get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView
```

must return a real PNG image — a single, fixed-angle, front-to-back
alpha-composited raymarch render of that live 3D LUT — using the *exact
same* endpoint, the *exact same* `?format=`/`Accept:` negotiation, and the
*exact same* `GET /list_textures` discovery endpoint that already work for
ordinary 2D render-graph textures today (`network-impl-4` campaign). No new
endpoint, no new query parameter, no new bridge type.

## Step 2 — The Situation / The Problem (Where are we now?)

- The engine already has a real 3D GPU resource type: `gte::VolumeTexture`
  (`src/Renderer/VolumeTexture.h/.cpp`) — a `VK_IMAGE_TYPE_3D` image,
  trilinear-filtered, clamp-to-edge. Its only real producer today is the
  Atmosphere Scattering campaign's **Aerial Perspective froxel volume**
  (`AtmosphereLutRenderer::AddAerialPerspectiveVolumePass()`,
  `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`), one instance per
  view (`"AtmosphereAerialPerspectiveVolume_GameView"` /
  `"..._SceneView"`, `128x128x32`, `VK_FORMAT_R16G16B16A16_SFLOAT`).
- The render graph (`gte::rg::RenderGraph`) already has full first-class
  vocabulary for this resource kind — `VolumeTextureHandle`,
  `RenderGraphBuilder::ImportVolumeTexture()`/`KeepVolumeTextureOutput()`,
  and a real per-frame resolution step producing a
  `RenderGraph::PhysicalVolumeTexture` (image/imageView/extent/format/
  `ResourceState`) for every declared volume texture, exactly parallel to
  its existing 2D `PhysicalTexture` machinery
  (`src/Renderer/RenderGraph/RenderGraph.h/.cpp`).
- **The one thing that does NOT exist yet**: any way to get pixels for a
  volume texture OUT of the engine. `RenderGraphDebugTextureRegistry`
  (`src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h/.cpp`), the
  data model behind `GET /get_texture`/`GET /list_textures`
  (`network-impl-4` campaign), only understands 2D textures — its
  `DebugTextureSnapshot` is built from `RenderTarget`, which has no 3D
  concept at all. `AGENTS.md` currently documents this as a **deliberate
  non-goal** ("do NOT retrofit `RenderGraphDebugTextureRegistry` itself to
  understand 3D resources generically"). **This campaign's whole purpose is
  to lift that restriction**, per an explicit, deliberate decision made for
  this campaign (see "Locked Design Decisions" below) — `AGENTS.md` must be
  updated to reflect the new reality once this campaign lands (Phase 6).
- There is also no way to *look at* a 3D texture's contents even if you
  could get raw bytes out — a `VolumeTexture`'s payload is not an image, it
  is a stack of voxels; a human/LLM needs an actual **rendered
  interpretation** (a raymarched thumbnail), not a raw byte dump, which is
  exactly the gap Unity's Texture3D "Volume" preview mode closes for
  humans. Nothing in this engine builds that today.
- Today's `GET /get_texture` code path (`Application::Run()`,
  `src/Application/Application.cpp`, the `FrameCaptureKind::NamedTexture`
  block) is a **pure pixel copy**: `Renderer::CaptureImagePixels()` reads
  the texture's *existing* rendered contents back to the CPU, unmodified.
  A volume texture has no equivalent "existing rendered 2D contents" to
  copy — pixels for its thumbnail must be **freshly rendered on demand**
  (the raymarch), not merely copied.

## Step 3 — The Plan (How do we get there?)

Six phases, strictly ordered (each one depends on the previous one's new
code existing and compiling):

| Phase | One-line goal | Primary new/changed files |
|---|---|---|
| **1** | New, pure, Tier-1-testable data model: a volume-texture counterpart of `RenderGraphDebugTextureRegistry`. | `src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h/.cpp` (new), `tests/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistryTests.cpp` (new) |
| **2** | Wire that registry into `RenderGraph::ExecuteCompiledGraph()` so every volume texture the graph resolves this frame is automatically upserted into it — zero opt-in, mirroring the 2D texture auto-registration exactly. | `src/Renderer/RenderGraph/RenderGraph.h/.cpp` (modified) |
| **3** | The actual raymarch thumbnail renderer: a new, self-contained, on-demand GPU compute pass (`Shaders/VolumeTexturePreview.comp`) plus its C++ driver class (`VolumeTexturePreviewRenderer`) and a pure CPU "oracle" math header for the fixed camera/ray-box setup. | `Shaders/VolumeTexturePreview.comp` (new), `src/Renderer/VolumeTexturePreviewMath.h/.cpp` (new), `src/Renderer/VolumeTexturePreviewRenderer.h/.cpp` (new), matching new Tier-1 test file |
| **4** | Wire it all into `Application::Run()`'s existing `GET /get_texture` handling: a requested name that resolves to a *volume* (not a 2D texture) now runs Phase 3's renderer instead of a plain pixel copy, then rejoins the existing PNG-encode/`FulfillPendingRequest()` path unchanged. | `src/Application/Application.cpp` (modified) |
| **5** | `GET /list_textures` also lists every registered volume (so an LLM caller can *discover* `texture_name`s worth requesting), tagged with a `kind` field distinguishing `"texture2d"`/`"texture3d"`. | `src/Application/FrameCaptureBridge.h/.cpp`, `src/Network/NetworkRoutes.h/.cpp`, `src/Network/NetworkServer.cpp`, `src/Application/Application.cpp` (all modified) |
| **6** | Documentation (`AGENTS.md`/`README.md`/`TODO.md`) + a full clean build/regression pass, closing out the campaign. | `AGENTS.md`, `README.md` (modified), a completion report |

### Locked Design Decisions (answers already confirmed with the project owner — do not re-litigate these)

1. **Data source is 100% programmatic/engine-internal.** No new `*.gta`
   asset type, no 3D-texture importer, no asset pipeline work of any kind.
   The only volume textures this campaign ever needs to preview are ones
   the engine itself already creates at runtime (today: the two Atmosphere
   aerial-perspective volumes) — and this feature must work generically for
   *any* future `VolumeTextureHandle` a pass declares, with zero extra
   opt-in, exactly like `RenderGraphDebugTextureRegistry` already does for
   2D textures.
2. **Only "Volume" preview mode** (Unity's default: front-to-back
   alpha-composite raymarch). No "Slice" mode, no "Maximum Intensity
   Projection" mode, no Editor UI/inspector panel for this at all — this
   campaign is a **pure networking/debugging capability for an LLM agent**,
   not an Editor feature. (A future Editor panel could reuse the same
   renderer later, but building one is explicitly out of scope here.)
3. **A single, fixed default camera** — no yaw/pitch/distance/zoom query
   parameters on the endpoint at all. Deterministic, icon-like output,
   mirroring Unity's own Project-browser thumbnail idea. The one endpoint
   call always produces the same framing for the same volume dimensions.
4. **Compile-time gating**: follow `GET /get_texture`'s own existing
   gating exactly. That endpoint's underlying mechanism
   (`RenderGraphDebugTextureRegistry`/`RenderGraph`) is core,
   always-compiled Renderer-tier code with **no** `GTE_ENABLE_EDITOR`
   dependency at all; only the HTTP route itself is gated behind
   `GTE_ENABLE_NETWORK` (default ON). Every new file this campaign adds
   (Phases 1-3) must be **always compiled**, Editor-independent, exactly
   like `RenderGraphDebugTextureRegistry`/`AtmosphereLutRenderer` already
   are — never placed under `src/Editor/`, never behind `#if
   GTE_ENABLE_EDITOR`.
5. **`GET /get_texture` is extended, not duplicated.** No new endpoint, no
   new query parameter, no new `FrameCaptureKind` enumerator, no new
   cross-thread bridge type. This is a deliberate, explicit exception to
   `FrameCaptureBridge.h`'s own general "a future endpoint needing
   different engine data must build its own narrow bridge" precedent
   (confirmed with the project owner specifically for this campaign) — the
   existing `FrameCaptureKind::NamedTexture` slot, `RequestedTextureName()`/
   `RequestedTextureChannel()`, and `?format=`/`Accept:` negotiation are all
   reused completely unchanged. The only NEW branching is "does this name
   resolve to a 2D texture or a volume texture" inside
   `Application::Run()`'s existing handler.
6. **`channel=depth` on a volume name is a 409**, exactly like requesting
   depth on a 2D texture that was registered with `hasDepth == false` — a
   `VolumeTarget`/`VolumeTexture` has no depth-companion concept at all (see
   `VolumeTarget.h`'s own doc comment), so this is not a new failure mode to
   invent, just the existing one applied to a case that's always true for
   every volume.
7. **The renderer is generic over volume dimensions/aspect ratio.** A
   volume's `(width, height, depth)` are not assumed to be equal (the
   Atmosphere volume itself is `128x128x32`, not a cube) — the raymarch box
   proxy's local-space half-extents are scaled proportionally to
   `(width, height, depth) / max(width, height, depth)`, so a non-cubic
   volume renders as a non-cubic box, never silently stretched/squashed
   into a unit cube. There is no real-world/physical-size metadata for a
   volume texture anywhere in this engine, so this ratio (texel-count-based
   aspect) is the correct, only-available default — matching the how-to
   reference material's own stated fallback for "generic texture data with
   no world-size metadata".
8. **The output thumbnail is a fixed, small, persistent 2D RGBA8 image**
   (256x256 — see Phase 3), created ONCE and reused/overwritten across every
   request, mirroring `ComputeBlurValidation`'s/the Atmosphere Aerial
   Perspective volume's own established "one persistent resource, created
   once, never recreated per request" precedent. It is rendered fresh
   on-demand, at request time, NOT once per engine frame — this is
   deliberately rare, human/LLM-triggered debug traffic, the same "a real
   extra GPU submission/wait, acceptable ONLY because it's at most once per
   network request" tier `Renderer::CaptureImagePixels()`/`WaitForGpuIdle()`
   already established for `GET /get_texture` itself.
9. **The raymarch shader treats the volume's alpha channel as density**
   (`density = sample.a`, `color = sample.rgb`, exactly the how-to
   reference material's own recommended default) with a documented,
   tunable `densityScale` constant baked into the shader/push constants —
   this campaign does NOT need the output to be a physically "correct"
   interpretation of Atmosphere-specific data (the aerial-perspective
   volume's own RGBA16F channels are transmittance/in-scattering, not a
   literal density+color volume) — it only needs to be a genuinely useful,
   generic **spatial debug visualization** that reveals whether the
   volume's data looks structurally sane (gradients, holes, all-zero
   regions, etc.), the same spirit as Unity's own generic Volume preview
   mode being useful for arbitrary Texture3D content regardless of its
   real-world meaning.

### Non-goals (explicitly out of scope for this whole campaign)

- No 3D texture asset import/`*.gta` pipeline of any kind.
- No "Slice" or "Maximum Intensity Projection" preview modes.
- No Editor UI panel/inspector preview for volume textures.
- No camera/orbit configurability via query parameters.
- No changes to `GET /get_swapchain`/`GET /get_game_view`, and no changes to
  the EXISTING 2D-texture behavior of `GET /get_texture`/`GET
  /list_textures` (every existing 2D capture request must behave
  byte-for-byte identically after this campaign as before it).

### Cross-phase file map (every file this campaign touches, for quick reference)

**New files:**
- `src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h`
- `src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.cpp`
- `tests/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistryTests.cpp`
- `src/Renderer/VolumeTexturePreviewMath.h`
- `src/Renderer/VolumeTexturePreviewMath.cpp`
- `tests/Renderer/VolumeTexturePreviewMathTests.cpp`
- `src/Renderer/VolumeTexturePreviewRenderer.h`
- `src/Renderer/VolumeTexturePreviewRenderer.cpp`
- `src/Shaders/VolumeTexturePreview.comp` (NOT a top-level `Shaders/` directory —
  this engine's real GLSL source folder is `src/Shaders/`; see Phase 3's own
  v2 Revision Notes for the exact correction. The RUNTIME staged SPIR-V path
  referenced from C++ is the separate, lowercase `"shaders/VolumeTexturePreview.comp.spv"`.)

**Modified files:**
- `src/Renderer/RenderGraph/RenderGraph.h`
- `src/Renderer/RenderGraph/RenderGraph.cpp`
- `src/Application/Application.h` (new `VolumeTexturePreviewRenderer` member — see Phase 4's own Step 3.1)
- `src/Application/Application.cpp`
- `src/Application/FrameCaptureBridge.h`
- `src/Application/FrameCaptureBridge.cpp`
- `src/Network/NetworkRoutes.h`
- `src/Network/NetworkRoutes.cpp`
- `src/Network/NetworkServer.cpp`
- `CMakeLists.txt` (new source files + new shader registration)
- `tests/CMakeLists.txt` (new test files)
- `AGENTS.md`, `README.md` (Phase 6 documentation)

### Workflow rules every phase must follow

1. **No full build/full regression test until Phase 6.** Phases 1-5 only
   need a fast, targeted compile check (see each phase's own "Verification"
   section for exactly which target(s) to build) — this mirrors this
   engine's other multi-phase campaigns (`network-impl-4`, `atmosphere-
   scattering-1`) exactly.
2. **Every phase writes its own `PHASE<n>_COMPLETION_REPORT.md`** in this
   same folder once its own compile check passes, then commits (code +
   report) to git — never bundle two phases into one commit.
3. **Every Tier-1-testable piece of new logic gets a real test in the same
   phase that introduces it** — never "add tests later". Phases 1 and 3
   each introduce genuinely pure, Tier-1-testable modules (the registry
   itself, and the camera/ray-box math) specifically so this is possible;
   Phase 2/4/5's own changes are integration wiring inside GPU/network-
   adjacent code that this codebase's own conventions already classify as
   "Tier 2, no automated coverage yet" (see `TESTING.md`) — don't invent a
   fake VkDevice-requiring test for those, follow the existing precedent
   instead (a manual runtime smoke check via `gte_send_request`/
   `run_app_background` at Phase 6 is the acceptance test for the
   integration wiring).
4. **Never regress the existing, shipped 2D-texture behavior of `GET
   /get_texture`/`GET /list_textures`.** Every new branch this campaign adds
   must be strictly ADDITIVE — an existing 2D texture name must resolve
   through the exact same code path, with the exact same behavior, byte for
   byte, as it did before this campaign started.
