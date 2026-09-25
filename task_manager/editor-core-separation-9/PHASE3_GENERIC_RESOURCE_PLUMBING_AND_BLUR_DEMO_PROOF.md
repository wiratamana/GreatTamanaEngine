# editor-core-separation-9 — PHASE3: Generic Resource Plumbing & Blur Demo Proof

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it FIRST, in full. Read
`PHASE2_COMPLETION_REPORT.md` before starting — it records which
`plugins/demo_render_feature_v3/` folder name was actually used and which
blackboard stand-in choice was made.

**Use `ask_questions`** whenever a real design ambiguity comes up that
`PHASE0_MASTER_STRATEGY.md` or this file does not already resolve. If you
delegate any further work, that work must also be told to use
`ask_questions`.

## Step 1: The Goal

This is the phase that answers the user's ORIGINAL question for real,
end-to-end, with a live, mathematically-verified pixel proof: **"a compute
shader writes a texture, a later pass reads it"** — implemented as a real,
permanent, committed 2-pass GPU downsample-blur inside the `_v3` demo
plugin from PHASE2, using ONLY generic, already-shipped-by-PHASE1/2
primitives (`CreateTexture`/`ReadTexture`/`WriteTexture`/`Dispatch`/
`DrawFullscreenTriangle`) plus exactly ONE brand-new, tiny, additive
registry operation (`gte.builtin.blit_fullscreen`) — proving, a second time,
that a new operation lands with zero `IPluginRenderPassBuilder_v3` interface
change (Design Doc R13, this time for a GRAPHICS-kind operation, complementing
PHASE2's compute-kind proof).

## Step 2: The Situation

Read before starting: `PHASE2_COMPLETION_REPORT.md`,
`src/Core/Plugins/PluginRenderOperationRegistry.h/.cpp` (as it exists after
PHASE2's edits), `src/Core/Plugins/PluginRenderPassBuilderAdapter_v3.h/.cpp`
(same), and these two existing full-screen-triangle precedents to mirror
for the new graphics pipeline:

- `src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer` (or wherever
  `AtmosphereSkyBackground.vert`/`.frag` are consumed —
  `search_in_dir` for `AtmosphereSkyBackground.vert` to find the exact
  pipeline-construction call site) — a real, already-shipped, VERTEX-BUFFER-FREE
  3-vertex `vkCmdDraw(cmd, 3, 1, 0, 0)` full-screen triangle, tagged
  `RenderPassDrawKind::DrawQuad` (`AGENTS.md`'s "Render Pass System" section
  confirms this is the one real pass tagged `DrawQuad` today — mirror its
  exact vertex-shader-generates-its-own-clip-space-triangle trick, no vertex
  buffer/input layout needed at all).
- `src/Shaders/GBufferCopy.comp` (`mrt-1` campaign) — a real, already-shipped
  "read one texture, write another" compute copy shader — read it for
  binding-convention inspiration even though this phase's own
  `gte.builtin.blit_fullscreen` is a GRAPHICS (fragment), not compute,
  operation (a graphics-kind op is the one genuinely NEW structural shape
  this phase adds to the registry — `PluginRenderOpKind::DrawFullscreenTriangle`
  was declared in PHASE2 but never actually exercised until now).

## Step 3: The Plan

### 3.1 — `src/Shaders/PluginBlitFullscreen.vert`/`.frag` (NEW, tiny)

A minimal fullscreen-triangle pair: the `.vert` shader generates its own
3-vertex clip-space triangle from `gl_VertexIndex` (byte-for-byte mirror of
`AtmosphereSkyBackground.vert`'s own established trick — do not reinvent
this, copy the exact formula), the `.frag` shader has ONE binding
(`binding=0`, `sampler2D sourceTexture`, `CombinedImageSampler`) and does
nothing but `outColor = texture(sourceTexture, uv);` (a literal passthrough
— no color grading, no blending, this op's entire job is "make an
already-computed texture visible as a color-attachment write", the graphics
equivalent of `GBufferCopy.comp`'s own compute-side "just copy it" role).
Add both to `CMakeLists.txt` via `gte_add_shader(GreatTamanaEditor
src/Shaders/PluginBlitFullscreen.vert)` / `.frag` (mirror the exact call
shape every other `gte_add_shader()` line already uses).

### 3.2 — Register `gte.builtin.blit_fullscreen` in `PluginRenderOperationRegistry`

Add `RegisterBlitFullscreen()`: builds a graphics `Pipeline` from
`PluginBlitFullscreen.vert.spv`/`.frag.spv` (mirror whichever existing
single-`VkFormat` `Pipeline` constructor a fragment-only, no-vertex-input,
no-depth pipeline already uses — `RenderTarget`'s color format via
`Renderer::ColorFormat()`, per `AGENTS.md`'s "Render Target Format Matching"
section, NEVER a hardcoded `VkFormat` literal), a
`VkDescriptorSetLayout` with one `AddCombinedImageSampler(0)` binding, and
inserts `{ id="gte.builtin.blit_fullscreen", kind=DrawFullscreenTriangle,
slots={ {COMBINED_IMAGE_SAMPLER,false} }, maxParamBytes=0,
graphicsPipeline=&*m_blitPipeline, descriptorSetLayout=m_blitDescriptorSetLayout }`.
Call this from `EnsureBuiltinsRegistered()` alongside the existing 3
`RegisterUberOp()` calls and `RegisterBoxBlur()`.

Confirm `PluginRenderPassBuilderAdapter_v3::DrawFullscreenTriangle()`
(written in PHASE2, never yet exercised by a real registered op) actually
works against this REAL entry now — it must: resolve the ONE bound
`CombinedImageSampler` slot exactly like `Dispatch()` already does for its
own slots, then issue the real draw via whatever this engine's existing
"draw a full-screen triangle with a given `Pipeline`" call shape already is
(mirror `AtmosphereSkyBackgroundRenderer`'s own `m_renderer.Draw(...)` /
equivalent call — `search_in_dir` for the exact method name before writing
this) inside a `WriteColorAttachment`-declared graphics pass, never a
`vkCmdDispatch`.

### 3.3 — The real 2-pass demo, added to `plugins/demo_render_feature_v3/`

Inside that plugin's `AddRenderGraphPasses(IPluginRenderPassBuilder_v3&
builder)`:

```cpp
PluginTextureHandle sceneColor;
builder.TryGetNamedTexture("SceneColor", sceneColor); // R14 - always succeeds today

PluginTextureDesc halfResDesc{ width / 2, height / 2, PluginTextureDesc::Format::Rgba8Unorm };
PluginTextureHandle halfRes = builder.CreateTexture("DemoV3.HalfResBlur", halfResDesc);

// Pass 1: compute downsample-blur - READS sceneColor, WRITES halfRes
// (a DIFFERENT resolution - BoxBlur.comp samples by normalized UV, so this
// is a genuine, correct downsample, not merely a same-size blur).
builder.AddComputePass("DemoV3.Downsample",
    [](IPluginPassSetupContext& s, void* ud) {
        auto* ctx = static_cast<DemoV3PassData*>(ud);
        s.ReadTexture(ctx->sceneColor, PluginResourceAccess::ComputeShaderRead);
        s.WriteTexture(ctx->halfRes, PluginResourceAccess::ComputeShaderWrite);
    },
    [](IPluginCommandRecorder& r, void* ud) {
        auto* ctx = static_cast<DemoV3PassData*>(ud);
        r.BindTexture(0, ctx->sceneColor);
        r.BindTexture(1, ctx->halfRes);
        std::uint32_t dims[2] = { ctx->halfWidth, ctx->halfHeight };
        r.Dispatch("gte.builtin.box_blur", dims, sizeof(dims), groupsX, groupsY, 1);
    },
    &passData);

// Pass 2: graphics upsample-present - READS halfRes (the SAME handle Pass 1
// just wrote), WRITES this plugin's own private output target. The render
// graph's own real dependency-edge scan inserts the correct
// compute-write -> fragment-read barrier automatically - exactly like this
// engine's own internal Atmosphere LUT passes already do (Design Doc Part 1.3).
builder.AddGraphicsPass("DemoV3.UpsamplePresent",
    [](IPluginPassSetupContext& s, void* ud) {
        auto* ctx = static_cast<DemoV3PassData*>(ud);
        s.ReadTexture(ctx->halfRes, PluginResourceAccess::ShaderRead);
        s.WriteColorAttachment(ctx->privateOutput, /*hasClearColor=*/true, 0,0,0,0);
    },
    [](IPluginCommandRecorder& r, void* ud) {
        auto* ctx = static_cast<DemoV3PassData*>(ud);
        r.BindTexture(0, ctx->halfRes);
        r.DrawFullscreenTriangle("gte.builtin.blit_fullscreen", nullptr, 0);
    },
    &passData);
```

(`passData.privateOutput = builder.GetPrivateOutputTarget();` captured once
before declaring either pass.) This exact shape is the concrete, permanent,
committed, byte-for-byte realization of the Design Doc's own §4.7 worked
example — cite that section directly in this plugin's own header comment,
alongside a citation to this campaign's phase files, mirroring how
`plugins/demo_render_feature/RenderFeaturePlugin.cpp`'s own header comment
already cites its own originating campaign.

Give this plugin its own `GtePluginRenderFeatureDescriptor` (any free
stage/priority — `RenderFeatureStage::PostComposite`, a priority that does
not collide with any other loaded demo plugin, `RenderFeatureBlendMode::AlphaOver`
so the blurred result visibly composites over whatever ran before it).

### 3.4 — Live, mathematically-verified pixel proof (Design Doc R30)

Mirrors `editor-core-separation-6` PHASE6's own precedent exactly — a
manual, live-engine, HTTP-driven check, NOT a new `ctest`:

1. `run_app_background` `GreatTamanaEditor.exe` with a scene containing a
   known, high-contrast test pattern in view (e.g. a hard-edged checkerboard
   or a solid-color object against a contrasting background — reuse
   whatever existing test-scene primitive/spawn mechanism is simplest,
   `POST /instantiate_primitive` if nothing simpler exists).
2. `gte_send_request GET /get_game_view` BEFORE the `_v3` demo plugin's
   pass runs (temporarily disable it, or simply capture a build without it
   loaded) — the sharp, unblurred baseline.
3. `gte_send_request GET /get_game_view` WITH the `_v3` demo plugin active —
   confirm the hard edge is now visibly softened over a band of pixels
   consistent with `BoxBlur.comp`'s own fixed `kBlurRadius = 3` kernel
   PLUS the 2x resolution reduction (state the expected pixel-band width by
   hand, from the shader's own math, before comparing — do not just
   eyeball it and declare success).
4. Sample at least 3 specific pixel coordinates straddling the original hard
   edge (via `GET /get_texture` or `GET /get_game_view` pixel inspection —
   use whichever existing capture endpoint already returns raw/inspectable
   pixel data) and confirm each one's value is a plausible weighted average
   of the two original colors, not simply one of the two originals
   unchanged (the concrete, numeric proof a REAL blur happened, not merely
   "a pass ran without crashing").
5. `gte_send_request GET /get_logs?limit=200` — confirm zero unexpected
   `GTE_LOG_WARNING`/`GTE_LOG_ERROR` lines during this whole run.
6. `stop_app_background` when done. Record every screenshot/pixel value
   gathered directly in `PHASE3_COMPLETION_REPORT.md` — this is the
   evidence a future reader checks, not a "trust me" summary.

## Verification

- `cmake --build build` (incremental).
- The live pixel proof above (Step 3.4) — this phase's OWN required,
  substantive verification; do not substitute a lesser check.
- Confirm the existing `_v2` demo plugin(s) AND PHASE2's own parity demo
  still render correctly, unaffected (a quick `GET /get_game_view` glance
  is enough here — PHASE2 already did the deep parity proof).

## Non-Goals for this phase

- `IPluginBlackboard`'s real implementation — PHASE4.
- Any Render Graph panel UI change — PHASE4.
- A second, different demo plugin folder — this phase ADDS to
  `plugins/demo_render_feature_v3/` from PHASE2, never creates a new one.

## Completion

Write `PHASE3_COMPLETION_REPORT.md`: the exact new shader files added, the
exact registry entry shipped for `gte.builtin.blit_fullscreen`, the full
pixel-proof evidence (screenshots + sampled pixel values + the hand-computed
expected blur math they were checked against), and any `ask_questions`
round needed. `git_add` + `git_commit` ("editor-core-separation-9 PHASE3:
generic resource plumbing & GPU blur demo proof").
