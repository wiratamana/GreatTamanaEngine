# PHASE4 — Completion Report: First Real Consumer, GBuffer Validation Pass

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document:
`PHASE4_GBUFFER_VALIDATION_PASS_AND_SHADER.md`.

## Status: DONE

`PHASE1_COMPLETION_REPORT.md`, `PHASE2_COMPLETION_REPORT.md`, and
`PHASE3_COMPLETION_REPORT.md` were all read in full before starting, per this
phase's own prerequisite. Key handoff facts re-confirmed and relied on:

- PHASE3's new `Renderer::CreatePipeline(std::span<const VkFormat> colorFormats, ...)`
  overload exists and compiles, with its own "live-smoke-instantiation check"
  explicitly deferred to this phase (PHASE3's own Definition of Done
  pre-authorized this) — this phase's own `GBufferValidation` pass IS that
  live smoke test, and it succeeded (see Verification below).
- `kPipelineMaxColorAttachments` (`Pipeline.h`) and `gte::rg::kMaxColorAttachments`
  (`RenderGraphTypes.h`) are two independent, hand-synced constants (both 8) —
  irrelevant here since this phase only ever declares 2 color attachments.
- `PassRecord::colorClearValue` is confirmed fully dead in production code —
  not touched by this phase either.

## Administrative note (branch), re-confirmed per this campaign's own
recursive instruction

This phase's own top-level task instructions again said "Stay on the current
branch: `feature/logger-impl`" — the same stale/mistaken instruction
PHASE2/PHASE3's own reports already flagged and corrected. Re-verified via
`git status`: the repository's actual current branch is
`feature/render-pass-impl`, with a clean working tree before this phase
started (PHASE1/2/3's commits already present). Stayed on
`feature/render-pass-impl`; `feature/logger-impl` was never touched. No fresh
`ask_questions` round-trip was needed — this is now a well-established,
repeatedly-confirmed correction.

## What was done

### 3.1 — `Shaders/GBufferValidation.vert`/`.frag`, `Shaders/GBufferCopy.comp`

- `GBufferValidation.vert` — a plain full-screen-triangle vertex shader
  driven purely by `gl_VertexIndex` (mirrors `Shaders/AtmosphereSkyBackground.vert`'s
  own technique exactly), outputting a UV varying. Declares no `in` vertex
  attributes at all.
- `GBufferValidation.frag` — two outputs: `outAlbedo` (location 0, a plain
  8x8 checkerboard) and `outNormal` (location 1, a synthetic "fake normal"
  derived from a radial UV falloff, packed via the standard `n * 0.5 + 0.5`
  normal-buffer convention) — deliberately simple, clearly-distinguishable
  procedural content, exactly per the phase document's own Step 3.1
  guidance.
- `GBufferCopy.comp` — the second pass's own trivial compute copy shader
  (`texture()` read + `imageStore()` write, no processing at all),
  `local_size_x = 16, local_size_y = 16`, binding 0 = combined image
  sampler (source), binding 1 = storage image (destination) — mirrors
  `Shaders/BoxBlur.comp`'s own binding convention exactly.

### 3.2 — `src/Editor/GBufferValidation.h`/`.cpp`

New `GTE_ENABLE_EDITOR`-only class, mirroring `ComputeBlurValidation`'s shape
almost exactly:

- Three persistent `RenderTexture`s (`m_albedoOutput`/`m_normalOutput`/
  `m_visualizedOutput`), all explicit `VK_FORMAT_R8G8B8A8_UNORM`, lazily
  created in `EnsureInitialized()`.
- A `Pipeline` built via `Renderer::CreatePipeline()`'s PHASE3 span
  overload, with `colorFormats = { albedoFormat, normalFormat }` (both the
  same explicit format).
- A real, throwaway 3-vertex `Mesh` (`m_dummyTriangle`) bound before the
  MRT pass's own draw call — see the "Design decision" section below for
  why this exists.
- A compute copy `ComputePipeline` + `VkDescriptorSetLayout` +
  `ComputeDescriptorSet`, mirroring `ComputeBlurValidation`'s own
  descriptor-set-layout/dispatch shape almost verbatim.
- `AddPass(builder, renderer, sceneExtent) -> GBufferValidationHandles`
  declares TWO passes every time it's called:
  1. `"GBufferValidation"` (`PassKind::Graphics`, `ViewScope::SceneView`,
     `RenderPassCategory::Debug`) — writes `outAlbedo`/`outNormal` via two
     `WriteColorAttachment()` calls (PHASE1's ordered-list mechanism) in
     one draw (`vkCmdDraw(cmd, 3, 1, 0, 0)`), plus a
     `WriteDepthStencilAttachment()` against the SAME handle as the
     albedo color write (reusing `m_albedoOutput`'s own companion
     `DepthBuffer` — every `RenderTexture` already owns one).
  2. `"GBufferValidationCopy"` (`PassKind::Compute`, same `ViewScope`/
     `RenderPassCategory`) — reads `outAlbedo` back (`ShaderRead`) and
     writes `m_visualizedOutput` (`ComputeShaderWrite`) via a trivial
     imageLoad/imageStore copy, proving the "later pass reads one of N
     outputs, cross-pass, barrier-synchronized automatically" half of the
     campaign's goal.
- `FinalizeForSampling(cmd)` — transitions albedo/normal
  (`ColorAttachmentWrite -> ShaderRead`, mirroring
  `FinalizeRenderTextureForExternalSampling()`) and the visualized output
  (`ComputeShaderWrite -> ShaderRead`, mirroring
  `ComputeBlurValidation::FinalizeForSampling()`) — a safe no-op when
  `AddPass()` wasn't called this frame (`m_writtenThisFrame` guard,
  identical convention).
- `OutputTexture()` exposes only the "visualized" RenderTexture for an
  optional ImGui preview (see the Scene-panel-checkbox design decision
  below) — the real albedo/normal outputs are inspected purely via
  `GET /get_texture`/`GET /list_textures`/the Render Graph panel.

### 3.3 — Wiring: `EditorLayer.h`/`NullEditorLayer.cpp`/`ImGuiEditorLayer.cpp`/`EditorContext.h`/`ScenePanel.cpp`/`Application.cpp`

- `EditorLayer.h` gains a new, tiny, dependency-free struct,
  `GBufferValidationHandles` (mirrors `TabActivationResult`'s own
  precedent), plus two new pure-virtual `IEditorLayer` methods:
  `AddGBufferValidationPass()`/`FinalizeGBufferValidationForSampling()`,
  matching `AddBlurValidationPass()`/`FinalizeBlurValidationForSampling()`'s
  shape — minus the `sceneViewHandle`/`sceneViewSampler` parameters, since
  this pass reads no Scene View texture at all.
- **`src/Editor/NullEditorLayer.cpp` was given matching no-op overrides for
  BOTH new methods in this SAME change** (`AddGBufferValidationPass()`
  returns `std::nullopt`; `FinalizeGBufferValidationForSampling()` is an
  empty `{ }` body) — confirmed by successfully compiling the
  `GTE_ENABLE_EDITOR=OFF` configuration (see Verification below) BEFORE
  considering this phase done, exactly as this phase's own corrected
  instructions required.
- `ImGuiEditorLayer` gains an `m_gbufferValidation` member (mirrors
  `m_blurValidation`) and a `m_lastKnownGBufferValidationView` tracking
  field; `BuildUI()` (re)creates an ImGui descriptor for the "visualized"
  output only, the same "recreate whenever the underlying `VkImageView`
  actually changed" discipline `m_blurValidation`'s own descriptor already
  uses. The destructor releases this descriptor BEFORE
  `ImGui_ImplVulkan_Shutdown()`, mirroring `ReleaseBlurredSceneOutputDescriptor()`'s
  own ordering requirement.
- `EditorContext.h` gains its own, separate `showGBufferValidationOutput`
  toggle and `gbufferValidationOutputDescriptor` descriptor field — never
  reusing `showBlurredSceneOutput`/`blurredSceneOutputDescriptor`, so the
  two debug tools stay independently toggleable (explicitly required by
  this task's own corrected instructions).
- `Panels/ScenePanel.cpp` gains a second checkbox, "Show GBuffer Validation
  (debug)", right next to "Show Compute Blur (debug)". **Design decision**:
  this checkbox DOES swap the Scene panel's own displayed image (mirroring
  the Compute Blur checkbox's exact precedent) — when checked (and the
  pass has produced output), "Scene" shows the "visualized" (copy-of-
  albedo) output instead of the normal Scene view. If somehow both debug
  checkboxes are on at once, Compute Blur's output wins (an arbitrary but
  harmless tie-break — neither tool needs to forbid the combination).
- `Application.cpp`'s offscreen `build` lambda gains a new, parallel,
  still-unmigrated call site (mirroring `AddBlurValidationPass()`'s own
  Locked-Design-Decision-4 call site immediately above it) —
  **deliberately reuses `sceneExtentForBlurValidation`/
  `sceneVisibleForBlurValidation`** rather than introducing parallel
  `...ForGBufferValidation` locals, since both already describe exactly
  "is the Scene panel visible this frame, and what size is it" — the
  identical real input this pass needs (it has no use for
  `sceneColorHandleForBlurValidation`, since it reads no Scene View
  texture). All three returned handles are appended to `outputs` (or
  PHASE1-3's culling would silently drop whichever one never reaches a
  root). `FinalizeGBufferValidationForSampling()` is called unconditionally
  every frame (mirroring the "BlurredSceneOutput" unconditional-correction
  precedent), followed by three `NotifyDebugTextureStateOverride()` calls
  (`"GBufferAlbedo"`/`"GBufferNormal"`/`"GBufferVisualized"`) so the debug
  texture registry's own tracked state stays correct for `GET /get_texture`
  even though this transition happens outside the render graph proper.

### 3.4 — CMake

- `src/Editor/GBufferValidation.h`/`.cpp` added to the `GTE_ENABLE_EDITOR`
  `target_sources()` list, right next to `ComputeBlurValidation.h`/`.cpp`.
- `Shaders/GBufferValidation.vert`/`.frag`/`GBufferCopy.comp` registered via
  three `gte_add_shader()` calls, **wrapped in `if(GTE_ENABLE_EDITOR) ... endif()`**
  (mirroring `BoxBlur.comp`'s/`SceneGrid.vert`'s own registration — the
  document's explicitly-called-out CORRECT precedent) — confirmed NOT
  registered unconditionally like `Mesh.frag`/`TexturedMesh.frag` (the
  explicitly-called-out WRONG precedent for an Editor-only debug shader).

### 3.5 — What this phase did NOT touch

- The Game View's actual default rendered output — verified unaffected
  (see Verification below).
- No new `RenderPassCategory` — reuses `RenderPassCategory::Debug`.
- No Frame Debugger code changes — the pass's category/view-scope already
  route it correctly through the existing generic machinery.
- `RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp`/`RenderGraph.cpp` —
  untouched (PHASE1/2's own job, already proven sufficient).

## Design decisions made (not fully pinned down verbatim by PHASE0/PHASE4)

1. **Depth-attachment strategy: option (a), the standard `Renderer::CreatePipeline()`/
   `Pipeline` path, paired with a real (scratch/unused) depth attachment**
   — the phase document's own weak recommendation, chosen specifically so
   PHASE3's new N-format `Pipeline` capability is genuinely exercised by
   its own first real consumer (option (b), hand-building a raw
   `VkPipeline` like `AtmosphereSkyBackgroundRenderer`, would have
   sidestepped that mechanism entirely, undercutting PHASE0's stated goal
   of PHASE3+PHASE4 proving it together). The scratch depth attachment
   costs nothing extra to allocate: it simply reuses `m_albedoOutput`'s
   own companion `DepthBuffer`, since every `RenderTexture` already owns
   one (see `RenderTexture.h`) — the exact same "one shared `TextureHandle`
   for both a color write and the depth write" pattern
   `AddRenderOpaquePass()` (`RenderPasses.cpp`) already uses. This did not
   require `ask_questions` — the document's own text already supplied a
   clear default and the reasoning to choose it, and no genuine
   counter-argument was found while implementing.
2. **The mandatory vertex-binding wrinkle: a real, throwaway 3-vertex
   `Mesh` IS bound before the draw call** (rather than leaving the
   binding unfilled and hoping the driver/validation layer tolerates it).
   `Shaders/GBufferValidation.vert` never reads this data (it derives its
   full-screen triangle purely from `gl_VertexIndex`, exactly like
   `Shaders/AtmosphereSkyBackground.vert`) — an unused vertex attribute is
   legal Vulkan; only a shader `in` variable with nothing bound for it
   would be illegal, and this shader declares none. This was the simpler,
   safer of the two options the phase document flagged, requiring no
   `ask_questions` round-trip.
3. **The second ("read one output back") pass is a COMPUTE pass**, not a
   second graphics pass — the phase document's own stated preference
   ("likely simplest"), confirmed correct in practice:
   `ComputeBlurValidation`'s own descriptor-set-layout/dispatch/push-
   constant shape was reused almost verbatim, just swapping a blur kernel
   for a one-line `imageLoad`-via-`texture()`/`imageStore()` copy. No
   `ask_questions` needed — the document already resolved this "if
   genuinely torn" question with its own recommendation, and no reason to
   deviate was found.
4. **`RenderPassEvent` stays at its default (`Opaques`) for BOTH of this
   phase's passes** — unlike `ComputeBlurValidation`, `"GBufferValidation"`
   reads NO Scene-View texture at all (a deliberate simplification, see
   decision 5 below), so there is no real cross-pass ordering hazard a
   future render-pass-4-style effective-order change could ever expose;
   `"GBufferValidationCopy"`'s own dependency on `"GBufferValidation"`
   having already written `GBufferAlbedo` is a structural RAW resource
   dependency the compiler already tracks, not something a
   `RenderPassEvent` tag needs to additionally encode. This exact
   reasoning was already laid out by the phase document itself
   ("if this phase's pass does NOT read the Scene View at all, this
   concern does not apply") — no `ask_questions` needed.
5. **Vertex geometry / content source: a self-contained, purely
   procedural full-screen-triangle pass, reading NO real Scene View
   geometry at all** — the simplest option the phase document itself
   flagged as "likely sufficient", matching `BoxBlur.comp`'s own
   deliberately-simple, non-production precedent. No richer proof (e.g.
   deriving albedo/normal from real scene geometry) was attempted — this
   phase's job is proving the MRT *mechanism*, not building a real
   G-buffer feature (explicitly out of scope per PHASE0's Non-Goals). No
   `ask_questions` needed — the document already framed this as the
   default, asking only to escalate if a richer proof seemed more
   valuable, which it did not.
6. **The Scene-panel checkbox DOES swap the displayed image** (rather than
   being purely an HTTP-query-only toggle with no own preview at all) —
   chosen because it costs almost nothing extra (mirrors
   `ComputeBlurValidation`'s own already-proven checkbox/descriptor
   wiring line-for-line) and gives a human a fast, no-HTTP-required way to
   confirm the pass is running, without building any new custom
   multi-image preview panel (which WOULD have been scope creep beyond
   PHASE0's stated Non-Goals). The pass's own real albedo/normal outputs
   deliberately have NO dedicated ImGui preview of their own — they are
   independently inspectable only via `GET /get_texture`/
   `GET /list_textures`/the Render Graph panel's own texture list, exactly
   matching PHASE0's Non-Goal of "no Frame Debugger per-output picker
   UI"/no scope-creeping into a richer multi-image viewer. No
   `ask_questions` needed — the phase document explicitly said "either is
   acceptable", and this is the smaller-risk, already-proven-pattern
   choice.
7. **Branch re-confirmation** — see the dedicated section above. Stayed on
   `feature/render-pass-impl`. No `ask_questions` needed (already
   established by PHASE2/PHASE3).

No other genuine ambiguity was hit during this phase. No `ask_questions`
call was needed at all this phase — every decision point the phase
document raised already came with enough of its own reasoning/default to
resolve unambiguously.

## Deviations from the plan

None beyond the seven documented decisions above, all of which were already
anticipated/pre-authorized by the phase document itself (either as its own
recommended default, an explicitly offered alternative, or an explicitly
allowed "either is acceptable" choice) rather than being an unplanned
deviation from a locked instruction.

## Verification

- **Fast, targeted incremental compile check, `GTE_ENABLE_EDITOR=ON`**
  (the project's existing `build/` directory): `cmake --build build --target
  GreatTamanaEngine` — **succeeded**, no warnings promoted to errors. This
  also compiled and staged the three new shaders
  (`GBufferValidation.vert.spv`/`.frag.spv`/`GBufferCopy.comp.spv`) next to
  the built executable.
- **Fast, targeted incremental compile check, `GTE_ENABLE_EDITOR=OFF`**
  (a fresh, separate `build_editor_off/` directory, `cmake -S . -B
  build_editor_off -G Ninja -DGTE_ENABLE_EDITOR=OFF` then `cmake --build
  build_editor_off --target GreatTamanaEngine`) — **succeeded**, confirming
  `NullEditorLayer.cpp`'s two new stub overrides compile cleanly and the
  release/non-Editor configuration is not broken by this phase's two new
  `IEditorLayer` pure virtual methods. Confirmed (via the build log) that
  none of the three new shaders were compiled/staged in this configuration,
  as expected (`GBufferValidation.cpp` — the only thing that ever loads
  them — is not compiled into `gte_core` at all under this configuration).
  This scratch directory was deleted after the check (not part of the
  repository; `.gitignore`'s own `/build-editor-off/` entry uses a hyphen,
  not the underscore this throwaway directory used, so it was removed
  manually to avoid any risk of being picked up).
- **Live, running-engine check, BOTH the checkbox off (default) AND on**
  (per this phase's own Definition of Done):
  - **Checkbox OFF (default)**: launched `GreatTamanaEngine.exe`
    (`GTE_ENABLE_EDITOR=ON` build), confirmed via `GET /list_textures` that
    NO `GBufferAlbedo`/`GBufferNormal`/`GBufferVisualized` entries exist at
    all, and `GET /get_swapchain` (after `GET /activate_tab?name=Scene`)
    shows both debug checkboxes unchecked and the Scene panel rendering its
    completely normal sky-background/ground-grid content — zero effect on
    default behavior, confirmed.
  - **Checkbox ON**: temporarily flipped `EditorContext::showGBufferValidationOutput`'s
    default to `true` (a deliberate, temporary, documented test-only edit —
    reverted to `false` immediately afterward, then re-verified compiling
    cleanly again — see below; there is no HTTP endpoint capable of
    clicking an arbitrary ImGui checkbox, so this is the same technique
    used to exercise any other checkbox-gated Editor debug tool without a
    mouse), rebuilt, relaunched, and confirmed:
    - `GET /list_textures` now lists `GBufferAlbedo`/`GBufferNormal`/
      `GBufferVisualized` as three independent 719x447 `R8G8B8A8_UNORM`
      texture2d entries.
    - `GET /get_texture?texture_name=GBufferAlbedo` returned a real
      red/yellow checkerboard image.
    - `GET /get_texture?texture_name=GBufferNormal` returned a real,
      completely different smooth multi-color gradient image (blue/purple/
      pink/orange/green) — **visually, immediately, unambiguously distinct
      from the albedo output**, proving the two-color-attachment MRT write
      is real, not a duplicate/aliased write.
    - `GET /get_texture?texture_name=GBufferVisualized` returned an image
      pixel-identical to `GBufferAlbedo`'s own checkerboard — proving the
      second, compute pass's cross-pass read of one of the two MRT outputs
      round-tripped correctly, entirely barrier-synchronized by the
      existing `RenderGraphBarrierPlanner` with zero new barrier code.
    - `GET /get_swapchain` (Scene tab active) showed the "Show GBuffer
      Validation (debug)" checkbox checked and the Scene panel's own image
      swapped to the checkerboard "visualized" output, exactly as designed.
    - `GET /activate_tab?name=Game` + `GET /get_game_view` returned the
      engine's completely normal sky-background Game View — **entirely
      unaffected** by the GBuffer Validation pass running in the Scene
      View, confirming this campaign's own "no change to the default Game
      View render output" Non-Goal held.
    - Reverted `showGBufferValidationOutput`'s default back to `false`
      immediately afterward and rebuilt again — final compile succeeded
      cleanly, confirming the shipped state matches the intended
      opt-in-only default.
  - The app was cleanly terminated via `stop_app_background` after each
    check.

## Files changed

- `src/Editor/GBufferValidation.h` (new)
- `src/Editor/GBufferValidation.cpp` (new)
- `src/Shaders/GBufferValidation.vert` (new)
- `src/Shaders/GBufferValidation.frag` (new)
- `src/Shaders/GBufferCopy.comp` (new)
- `src/Editor/EditorLayer.h` (new `GBufferValidationHandles` struct, two new
  pure-virtual `IEditorLayer` methods)
- `src/Editor/NullEditorLayer.cpp` (matching no-op overrides for both new
  methods)
- `src/Editor/ImGuiEditorLayer.cpp` (`m_gbufferValidation` member, override
  implementations, ImGui descriptor (re)creation, destructor release call)
- `src/Editor/EditorContext.h` (`showGBufferValidationOutput`/
  `gbufferValidationOutputDescriptor` fields)
- `src/Editor/Panels/ScenePanel.cpp` (second "Show GBuffer Validation
  (debug)" checkbox + image-swap logic)
- `src/Application/Application.cpp` (new `AddGBufferValidationPass()`/
  `FinalizeGBufferValidationForSampling()` call sites, three
  `NotifyDebugTextureStateOverride()` calls)
- `CMakeLists.txt` (new source-file registration, new
  `if(GTE_ENABLE_EDITOR)`-wrapped shader registrations)
- `task_manager/mrt-1/PHASE4_COMPLETION_REPORT.md` (this file)

## Handoff notes for PHASE5

- Every real production `Renderer::CreatePipeline()`/`Pipeline` call site
  (the two pre-existing single-format ones, PLUS this phase's own new
  2-format `GBufferValidation` consumer) is confirmed compiling and
  behaving correctly — PHASE5's own full build + full `ctest` pass is the
  next, and final, verification gate.
- No new Tier-1 tests were added this phase — `GBufferValidation`/
  `Shaders/*` are Tier-2 (GPU-dependent) code with no automated test
  coverage, exactly like `ComputeBlurValidation`/`Pipeline` itself (see
  `AGENTS.md`'s "Testability & Regression Safety" section's documented,
  accepted gap for this class of code). PHASE5's own "final Tier-1 test
  sweep" item refers to re-running the EXISTING Render Graph Tier-1 suites
  (`RenderGraphBuilderTests.cpp`/`RenderGraphCompilerTests.cpp`/
  `RenderGraphSnapshotTests.cpp`/`RenderGraphTypesTests.cpp`), not adding
  new ones for this phase's own debug-only pass.
- `AGENTS.md`/`README.md`/`docs/conventions/` still need their own
  campaign-summary updates describing the new MRT capability — explicitly
  PHASE5's job, not touched here.
- The temporary `showGBufferValidationOutput = true` edit used for this
  phase's own live-check screenshots was fully reverted before this
  report was written; the shipped default is `false`, confirmed by the
  final `cmake --build build --target GreatTamanaEngine` run recorded
  above.
