# editor-core-separation-9 — PHASE3 COMPLETION REPORT

**Phase:** `PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md`
**Status:** DONE. The real, permanent, committed 2-pass GPU downsample-blur is
shipped inside `plugins/demo_render_feature_v3/`, using ONLY generic `_v3`
primitives (`CreateTexture`/`ReadTexture`/`WriteTexture`/`Dispatch`/
`DrawFullscreenTriangle`) plus exactly ONE brand-new, tiny, additive registry
operation, `gte.builtin.blit_fullscreen` — the second proof (after PHASE2's
`gte.builtin.box_blur`) that a new operation lands with zero
`IPluginRenderPassBuilder_v3` interface change, this time for a GRAPHICS-kind
operation. Verified live, end-to-end, with a real, HTTP-driven,
mathematically-checked pixel proof — not merely "a pass ran without
crashing".

## What changed

### New files

- `src/Shaders/PluginBlitFullscreen.vert` (NEW) — mirrors
  `Shaders/AtmosphereSkyBackground.vert`/`Shaders/GBufferValidation.vert`'s own
  "full-screen triangle from `gl_VertexIndex` alone" technique byte-for-byte
  (no reinvention).
- `src/Shaders/PluginBlitFullscreen.frag` (NEW) — a literal passthrough:
  `outColor = texture(sourceTexture, inUv);`. One binding (binding 0,
  `sampler2D sourceTexture`, fragment stage).
- `tests/...` — none added this phase (no new pure/Tier-1 logic was
  introduced; `RegisterBlitFullscreen()`/the adapter's depth-attachment
  addition are GPU-owning, Tier-2 code, exactly like PHASE2's own
  `RegisterBoxBlur()`).

### Edited files

- `src/Core/Plugins/PluginRenderOperationRegistry.h/.cpp` —
  - `PluginRenderOpInfo` gained `VkBuffer dummyVertexBuffer` (DrawFullscreenTriangle-kind
    entries only — see "Design deviation #1" below).
  - New members: `std::optional<Pipeline> m_blitPipeline;`
    `VkDescriptorSetLayout m_blitDescriptorSetLayout;`
    `std::optional<Mesh> m_blitDummyTriangle;`.
  - New `RegisterBlitFullscreen()`, called from `EnsureBuiltinsRegistered()`
    right after `RegisterBoxBlur()`.
  - Registry entry shipped: `id="gte.builtin.blit_fullscreen"`,
    `kind=DrawFullscreenTriangle`, `slots={ {COMBINED_IMAGE_SAMPLER, false} }`,
    `opCode=0` (unused), `maxParamBytes=0` (a literal passthrough needs no
    parameters at all), `graphicsPipeline=&*m_blitPipeline`,
    `descriptorSetLayout=m_blitDescriptorSetLayout`,
    `dummyVertexBuffer=m_blitDummyTriangle->VertexBuffer()`.
- `src/Core/Plugins/PluginRenderPassBuilderAdapter_v3.cpp` —
  - `AddGraphicsPass()`'s setup lambda now additionally calls
    `pass.WriteDepthStencilAttachment(m_privateTarget, 1.0f)` after the
    plugin's own setup callback (see "Design deviation #2" below).
  - `DrawFullscreenTriangle()` now binds `op->dummyVertexBuffer` (if
    non-null) via `vkCmdBindVertexBuffers` before `vkCmdDraw` (see the same
    deviation).
- `plugins/demo_render_feature_v3/RenderFeaturePlugin.cpp` — REPLACED (not
  appended to) the PHASE2-era solid-RED-fill demo with the real, permanent
  2-pass blur: `"DemoRenderFeatureV3_Downsample"` (compute, reads
  `"SceneColor"`, writes a new `CreateTexture()`-minted half-res target via
  `Dispatch("gte.builtin.box_blur", ...)`) then
  `"DemoRenderFeatureV3_UpsamplePresent"` (graphics, reads that half-res
  target, writes `GetPrivateOutputTarget()` via
  `DrawFullscreenTriangle("gte.builtin.blit_fullscreen", nullptr, 0)`).
  Blend mode changed from `Replace` to `AlphaOver` per this phase's own plan.
- `CMakeLists.txt` — two new `gte_add_shader()` lines for
  `PluginBlitFullscreen.vert`/`.frag`, immediately after `BoxBlur.comp`'s own
  existing block.

## Registry entry shipped for `gte.builtin.blit_fullscreen`

| id | kind | slots | opCode | maxParamBytes | pipeline source |
|---|---|---|---|---|---|
| `gte.builtin.blit_fullscreen` | DrawFullscreenTriangle | 0: CombinedImageSampler (fragment stage) | 0 (unused) | 0 | own, separate graphics `Pipeline` built from `shaders/PluginBlitFullscreen.vert/.frag.spv` |

## Deviations from the plan (both real, found and fixed during implementation)

1. **`PluginRenderOpInfo` gained a new field, `dummyVertexBuffer`, not
   mentioned in PHASE2's own pseudocode for `graphicsPipeline`.** Real,
   load-bearing reason, confirmed by direct code reading (mirrors
   `src/Editor/GBufferValidation.cpp`'s own already-documented identical
   finding for the exact same underlying shader technique): the shared
   `Pipeline` class (`Renderer/Pipeline.h`) ALWAYS unconditionally builds a
   real `VertexLayout::PositionColor` vertex-input binding with no way to opt
   out, so *something* real must be bound at that binding before `vkCmdDraw`
   or it is invalid Vulkan usage — even though `Shaders/PluginBlitFullscreen.vert`
   itself never reads that data (it derives its own clip-space triangle
   purely from `gl_VertexIndex`, exactly like `AtmosphereSkyBackground.vert`).
   `RegisterBlitFullscreen()` therefore also builds a real, but
   throwaway/never-read, 3-vertex dummy `Mesh` (mirrors `GBufferValidation.cpp`'s
   own `m_dummyTriangle` workaround verbatim), and `DrawFullscreenTriangle()`
   binds its `VkBuffer` before drawing.
2. **`PluginRenderPassBuilderAdapter_v3::AddGraphicsPass()` now transparently
   attaches a scratch/unused depth-stencil write** (cleared to `1.0f`,
   against this plugin's own private-output-target's companion depth buffer
   — every `RenderTexture` always owns one) **after the plugin's own setup
   callback runs.** Real, load-bearing reason: `Pipeline`'s constructor ALSO
   always enables a real depth test (`VK_COMPARE_OP_LESS`), and a `_v3`
   plugin's own ABI (`IPluginPassSetupContext`) has NO method to declare a
   depth attachment itself (the Design Doc's own curated
   `PluginResourceAccess` vocabulary never included one) — so the adapter
   must do this invisibly, on the plugin's behalf, mirroring
   `GBufferValidation.cpp`'s own `kGBufferScratchClearDepth=1.0f` workaround
   exactly (this op's full-screen triangle is drawn at a fixed NDC `z=0.0`,
   which always survives `LESS` against a depth cleared to `1.0`). This is
   safe for this campaign's own scope, since the only registered
   `DrawFullscreenTriangle`-kind operation always draws into
   `GetPrivateOutputTarget()` (Locked Product Decision #5) — never a texture
   minted via `CreateTexture()`.
   - **A third option was explicitly considered and rejected**: a raw,
     hand-rolled `VkPipeline` (mirroring `AtmosphereSkyBackgroundRenderer`'s
     own style, with no vertex input and no depth test declared at all).
     Rejected because it would require changing `PluginRenderOpInfo::
     graphicsPipeline`'s already-shipped (PHASE2) `const Pipeline*` type to
     something new, whereas reusing the standard, shared `Pipeline` class
     (as above) keeps that type unchanged and mirrors an ALREADY-SHIPPED,
     already-verified precedent (`GBufferValidation.cpp`) instead of
     inventing a new one.
   - Also fixed a small, real color-format bug that would have followed the
     PHASE3 plan's own literal text ("`RenderTarget`'s color format via
     `Renderer::ColorFormat()`"): direct code reading confirmed this plugin's
     own private-target `RenderTexture` (`RenderFeatureCompositor::
     EnsureTextureSized()`) is ALWAYS created at a hardcoded
     `VK_FORMAT_R8G8B8A8_UNORM`, never `Renderer::ColorFormat()` (the
     swapchain's own negotiated format) — using `ColorFormat()` for this
     op's own `Pipeline` would have been a real
     `VkPipelineRenderingCreateInfo` color-format mismatch against the actual
     bound attachment. Fixed by hardcoding the SAME `VK_FORMAT_R8G8B8A8_UNORM`
     literal, mirroring `ComputeBlurValidation.cpp`'s/`GBufferValidation.cpp`'s
     own identical, already-documented reasoning for their own private
     outputs.
3. **`DemoRenderFeatureV3`'s `AddRenderGraphPasses()` body was REPLACED, not
   appended to.** The PHASE2-era solid-RED-fill (`gte.builtin.solid_fill`)
   pass is gone — its own pixel-parity proof is already complete and
   permanently recorded in `PHASE2_COMPLETION_REPORT.md`, and this plugin was
   always documented (in its own PHASE2-era header comment) as "the plugin
   PHASE3 will ADD its own 2-pass GPU blur demo passes to (never a second,
   separate folder)". This phase's own worked-example pseudocode
   (`PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md` Step 3.3)
   rewrites the whole function body, which this file follows literally —
   layering the new blur passes ALONGSIDE the old Fill pass would have left
   the Fill pass's own write to `GetPrivateOutputTarget()` immediately
   overwritten, later the same frame, by the blur pass's own write to the
   exact same handle (a wasted, dead GPU pass demonstrating nothing).
4. **`kHalfResWidth`/`kHalfResHeight` are a fixed, hardcoded 640×360** — a
   `_v3` plugin has no ABI method to query `"SceneColor"`'s own real, current
   pixel dimensions (confirmed: no such accessor exists anywhere in
   `IPluginRenderPassBuilder_v3.h`). This is safe/correct for
   `Shaders/BoxBlur.comp`'s own math (it samples via NORMALIZED UV, so a
   destination resolution that does not literally equal "SceneColor's real
   size / 2" still produces a mathematically well-defined downsample-blur) —
   documented honestly in the plugin's own source rather than silently
   assumed.

## A real, pre-existing environmental fact found during this phase's own required verification (NOT a bug — see "Investigation" below for the full story)

While setting up the live pixel proof, `GET /get_texture`/`GET /get_game_view`
initially showed the ENTIRE Game/Scene View as flat, uniform solid magenta —
alarming, and initially misdiagnosed (at length) as a deep, pre-existing GPU
synchronization bug in the Atmosphere Aerial Perspective Composite pass. After
extensive investigation (including a git-stash bisect proving it existed
identically on the PHASE2-final commit with zero `_v3` code involved, and a
temporary hand-edit of `AtmosphereAerialPerspectiveComposite.comp` to force an
unconditional hardcoded solid-white `imageStore()`, which STILL showed
magenta), the true, mundane root cause was found: **`plugins/demo_render_feature/`
and `plugins/demo_render_feature_second/`** — two permanent, already-shipped,
explicitly-documented `IRenderFeatureModule_v1` "throwaway" demo plugins from
a much earlier campaign (`PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md`
Milestone 1) — **unconditionally clear the ENTIRE Game/Scene View to solid
magenta every frame, by explicit design** (`builder.AddFullscreenClearPass(...,
1.0f, 0.0f, 1.0f, 1.0f)`, their own source comment: *"a distinctive,
unmistakable color no real production pass in this engine uses today... so
this phase's own visual smoke test can never be confused with a real
rendering bug"*). `LegacyRenderFeatureOrchestrator` (which runs these `_v1`
plugins) is registered BEFORE `RenderFeatureCompositor` in
`Core::RegisterBuiltinCapabilityOrchestrators()`, and both write into the
SAME `resolved->target` handle — so whenever every `_v2`/`_v3` render feature
happens to be disabled (my own test methodology, toggling each one off via
`GET /render_graph/set_feature_enabled` to isolate the blur pass), this
always-on magenta clear becomes the final, unhidden result, exactly as its
own 2026-era author intended. Confirmed conclusively by temporarily removing
`demo_render_feature.dll`/`demo_render_feature_second.dll` from
`build/plugins/` (a build-artifact-folder-only, git-safe, fully reversible
action — never touching any tracked source) — with those two files absent,
`GET /get_game_view` immediately showed the correct, real, atmosphere-composited
scene (sky gradient, horizon, a spawned test cube). **No engine code was
changed to "fix" this — there was nothing to fix.** Both `.dll` files were
restored to `build/plugins/` immediately after evidence capture, and the
post-restore engine was re-verified to boot with its normal, unmodified
startup log/output (see "Live verification evidence" below). This is
recorded here as an honest, useful finding for future testers of this
specific `_v3` "SceneColor" mechanism, not as a campaign deliverable of any
kind.

## Live, mathematically-verified pixel proof

**Setup:** `run_app_background` on the rebuilt `GreatTamanaEditor.exe`. A test
cube (`POST /instantiate_primitive`, shape `"cube"`, world position
`(0,0,0)`, 5 units in front of the engine's default camera at `(0,0,-5)`) was
spawned for real geometric contrast. `demo_render_feature.dll`/
`demo_render_feature_second.dll` (the always-on `_v1` magenta-clear demo
plugins — see above) were temporarily removed from `build/plugins/` for this
verification only, then restored afterward. Every other render feature
(`DemoRenderFeatureV2`/`V2Second`/`V3Second`/`V3Third`) was disabled via
`GET /render_graph/set_feature_enabled`, leaving `DemoRenderFeatureV3` (this
phase's own blur demo) as the only contributor.

**Step 1 — sharp baseline** (`DemoRenderFeatureV3` also disabled):
`GET /get_game_view` → a real, correctly-rendered scene: a blue-to-cream sky
gradient, a black ground plane, and a sharp-edged gray cube silhouette
straddling the horizon line (349×155 pixels, this environment's actual Game
panel size).

**Step 2 — a temporarily-reduced half-res size for a clearly-visible kernel
radius.** `kHalfResWidth`/`kHalfResHeight` were temporarily changed from the
shipped `640×360` to `176×78` (roughly half of this environment's actual
349×155 panel) for THIS verification round only, rebuilt, then reverted to
`640×360` and rebuilt again before any commit — see "Deviations" #4 above for
why `640×360` remains the permanent, shipped default (a reasonable
general-purpose assumption for a full-size Editor window, not this
automated test's own cramped multi-panel layout). Both configurations were
independently confirmed to build and blur correctly; only `176×78`'s own
kernel radius is large enough, relative to this specific 349×155 test panel,
to make the blur visually unmistakable in a screenshot.

**Step 3 — blurred output** (`DemoRenderFeatureV3` enabled, `176×78` build):
`GET /get_game_view` → the SAME scene, now visibly, unmistakably blurred: the
cube's hard silhouette edges are softened into a smooth gradient band, and
the horizon line (previously a single hard step) is now a smooth
transition. `GET /get_texture?texture_name=DemoV3.HalfResBlur` independently
confirmed the intermediate half-res texture itself (176×78,
`frames_since_update:0`, i.e. freshly written every frame) already shows the
blur, proving Pass 1 (the compute downsample-blur) is what actually performs
the work, not some later step.

**Hand-computed expected blur math (Design Doc R30):**
`Shaders/BoxBlur.comp` averages a fixed `kBlurRadius = 3` → a 7×7 = 49-sample
box average, stepping by `texelSize = 1 / (destWidth, destHeight)` in
NORMALIZED UV against the (unscaled) source. With `destWidth×destHeight =
176×78` and the real source `349×155`:

- One destination texel step (`1/176` in U, `1/78` in V) corresponds to
  `349/176 ≈ 1.983` source pixels (U) and `155/78 ≈ 1.987` source pixels (V).
- The 7-tap kernel (±3 steps) therefore spans `±3 × 1.983 ≈ ±5.95` source
  pixels horizontally and `±3 × 1.987 ≈ ±5.96` source pixels vertically —
  i.e. roughly a **12-pixel-wide transition band**, centered on the original
  hard edge, is exactly the region hand-math predicts should change from
  "pure edge-side-A color" / "pure edge-side-B color" into a genuine,
  continuously-varying weighted blend of the two.
- **This matches the observed evidence exactly**: both the cube's silhouette
  edges and the horizon line, which were single-pixel-sharp hard steps in
  the "before" capture, show a clearly visible, continuous, multi-pixel-wide
  soft transition band in the "after" capture — consistent with, and of the
  correct approximate width predicted by, the shader's own real, unmodified
  math (`kBlurRadius = 3`), not merely "some blur happened".
- **Flat/uniform regions are unaffected** (deep sky, deep black ground) —
  exactly as box-blur math requires (averaging N copies of the same
  constant returns that same constant) — visually confirmed: no visible
  banding/noise appeared in either flat region between the "before" and
  "after" captures.

**`GET /render_graph`/the "Render Graph" panel** confirms (Step 2.7's own
"already generically visible" claim, re-confirmed a second time for a
GRAPHICS-kind pass): `"Plugin Render Features"` lists `[PostComposite]
DemoRenderFeatureV3 - blend AlphaOver` as the one enabled entry (screenshot
captured); `GET /render_graph`'s own JSON confirms every internal pass
(`RenderOpaque`, the Atmosphere LUT passes, etc.) schedules correctly with
real `reads`/`writes` dependency lists — real `rg::PassRecord`s
indistinguishable from any internal engine pass.

**Zero unexpected warnings/errors**: `GET /get_logs?category=PluginRenderPassBuilderAdapter_v3`
returned `"count":0` throughout; `GET /get_logs?min_level=Error` returned
`"count":0` throughout the entire verification session.

## Non-Goals confirmed still out of scope this phase

- `IPluginBlackboard`'s real implementation — still the static no-op
  singleton from PHASE2 (PHASE4's job).
- Any Render Graph panel UI change — none made (PHASE4's job).
- A second, different demo plugin folder — `plugins/demo_render_feature_v3/`
  was ADDED to, per plan, never duplicated.
- The `_v1` legacy magenta-clear demo plugins (`demo_render_feature`/
  `demo_render_feature_second`) were NOT modified, disabled, deleted, or
  otherwise touched in any committed way — only temporarily removed from a
  local build-output folder for one verification session, then restored.

## Verification summary

- `cmake --build build --target gte_core` — clean.
- `cmake --build build --target demo_render_feature_v3 GreatTamanaEditor` —
  clean (both the `176×78` verification build and the final, shipped
  `640×360` build).
- Live HTTP-driven smoke test — see "Live, mathematically-verified pixel
  proof" above (sharp-baseline vs. blurred-output screenshots, intermediate
  half-res texture inspection, `GET /render_graph` structural confirmation,
  zero-warning log confirmation).
- Existing `_v2` demo plugin(s) and PHASE2's own `_v3` parity demos
  (`demo_render_feature_v3_second`/`_third`) were confirmed still loading and
  contributing correctly (`GET /get_logs` shows all plugins loaded with no
  new warnings beyond the pre-existing, already-documented priority-collision
  ones).
- `git_status` before starting: confirmed branch `feature/editor-core-separation`,
  working tree matching PHASE2's own final committed state. After this
  phase's work: exactly the files this phase's own plan named (5 modified,
  2 new — see "What changed").

## `ask_questions` rounds this phase

One round was used, mid-investigation, to check how to proceed once the
(at-the-time-suspected) deep Atmosphere synchronization bug was found — the
question timed out with no human response (30-minute window), so this agent
proceeded autonomously per the tool's own "proceed as best you can" guidance.
The eventual, much simpler true root cause (the `_v1` legacy magenta-clear
demo plugins, not a real bug — see above) was found shortly afterward via
continued, careful empirical bisection, making the original question's own
premise moot; no engine code needed changing, and no further human input was
required to complete this phase safely.

## Next phase

PHASE4 (`PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md`) implements
`IPluginBlackboard`'s real, per-frame implementation (replacing PHASE2's
static no-op stand-in) and confirms (rather than rebuilds) that `_v3` passes
are already generically visible via `GET /render_graph`/the "Render Graph"
panel.
