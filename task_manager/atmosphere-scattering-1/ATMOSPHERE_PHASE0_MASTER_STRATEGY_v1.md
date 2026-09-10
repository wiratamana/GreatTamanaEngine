# Atmosphere Scattering + Aerial Perspective — Master Strategy (Phase 0) — v1

**Orchestrator document.** This file does not implement anything itself. It
defines the goal, the reference material, the locked design decisions, the
full phase breakdown, the cross-cutting engineering rules every child
document must follow, and the workflow every implementing session must
follow. Every other file in this folder (`task_manager/atmosphere-scattering-1/`)
is a child of this one. Read this file FIRST, in full, before opening any
`ATMOSPHERE_PHASEn_*.md` child document.

Branch: `feature/atmosphere-scattering-impl`. Every phase below is implemented
on this SAME branch, incrementally, one phase at a time — never a new branch
per phase.

## Step 1: The Goal (Where are we going?)

Give **GreatTamanaEngine** a physically-based, real-time **atmosphere
scattering** system with **aerial perspective** — the same class of technique
documented in Sébastien Hillaire's *"A Scalable and Production Ready Sky and
Atmosphere Rendering Technique"* (Eurographics 2020) and implemented as a
reference, engine-agnostic sample in
**[hoffstadt/pl-sky](https://github.com/hoffstadt/pl-sky)** ("A real-time sky
and aerial-perspective renderer"). Concretely, when this campaign is done, the
engine must be able to:

1. Render a physically-plausible sky dome (color/gradient that responds to a
   sun direction, time-of-day, and viewing angle) behind all scene geometry,
   in both the Editor's "Game" and "Scene" views.
2. Apply **aerial perspective** to already-rendered opaque geometry — distant
   objects progressively wash out/tint toward the sky's own color
   (extinction + in-scattered light) exactly the way real-world haze/
   atmosphere behaves over distance, instead of the engine's current "no
   atmosphere at all, geometry is tinted only by its own unlit/lambert
   shading" look.
3. Let a scene author control the sun's direction via a real, first-class ECS
   entity (see Locked Design Decision 1 below) and see the sky/aerial
   perspective react live as they rotate it in the Editor.
4. Let a developer visually debug every intermediate LUT (Look-Up Texture)
   this technique produces via the engine's EXISTING `GET /get_texture`/
   `GET /list_textures` HTTP endpoints (`network-impl-4` campaign — see
   `AGENTS.md`'s "Named Texture Capture" section) — this is precisely why that
   campaign's own README entry calls out "useful for debugging off-screen
   intermediate passes (e.g. future atmosphere-scattering LUTs)" by name.

This is a genuinely large feature (new GPU resource kind, four chained
compute passes, two new full-screen passes, a new ECS component, new Editor
UI, new validation tooling) — it is deliberately broken into **9 child
phases**, each one independently compilable and independently useful, so an
LLM-driven implementer can execute one phase, verify a fast compile, commit,
and move to the next without ever holding the entire feature "half-built and
uncompilable" for long.

## Step 2: The Situation (Where are we now?)

A careful, source-grounded audit of the current engine (not memory/assumption
— every claim below was checked directly against real files) found:

- **The engine has zero atmosphere/sky/fog of any kind today.** There is no
  `Light`/`DirectionalLight` ECS component (`src/ECS/Components/` only has
  `Transform.h`, `MeshRenderer.h`, `Camera.h`, `Name.h`, `SkeletalAnimator.h`),
  no sky rendering, and no post-process fog/haze pass. Every mesh is shaded by
  a small, fixed-direction ambient+lambert term baked into
  `Shaders/Mesh.frag`/`TexturedMesh.frag` (see `README.md`, "Rendering").
- **The engine has a mature, real, general-purpose Render Graph**
  (`src/Renderer/RenderGraph/`, `gte::rg::RenderGraph`) with automatic
  dependency resolution, culling, resource pooling, and barrier synthesis —
  this is the correct place to add every new pass this campaign needs. It
  already executes real per-frame passes twice: an "Offscreen Regime" (Game
  View + Scene View, synchronous) and a "Pipelined Regime" (swapchain
  Present) — see `Application::Run()`.
- **The engine has a mature, real compute-shader stack**
  (`ComputePipeline`, `ComputeDescriptorSet`, `DescriptorSetLayoutBuilder`,
  `ComputeDispatch.h`, all under `src/Renderer/`), proven end-to-end by
  `src/Editor/ComputeBlurValidation.h/.cpp` + `src/Shaders/BoxBlur.comp` — a
  real compute pass that reads one texture (`ShaderRead`/`sampler2D`) another
  pass wrote this same frame and writes its own persistent, storage-capable
  output texture (`ComputeShaderWrite`/`image2D`), fully synchronized by the
  render graph's own barrier planner with zero manual `vkCmdPipelineBarrier`
  calls. **This is the single most important existing precedent every LUT
  compute pass in this campaign must be modeled on** — do not invent a
  different shape.
- **The Render Graph's resource vocabulary today only understands 2D
  textures and linear buffers** (`RenderGraphTypes.h`'s `TextureHandle`/
  `TextureDesc` — `width`/`height`/`format`/`hasDepth`, no depth/slice
  dimension at all — and `BufferHandle`/`BufferDesc`). The aerial-perspective
  "camera volume" this technique needs is a genuine **3D texture** (a
  froxel/voxel grid aligned to the camera frustum) — **this does not exist in
  the engine's vocabulary at all today** and is real, net-new engineering,
  not a config change. This is the single riskiest, highest-context piece of
  this whole campaign — see Phase 2.
- **`src/Shaders/*.comp` files have zero shared-GLSL-code mechanism today.**
  Every existing shader is fully self-contained; `cmake/CompileShaders.cmake`'s
  `gte_add_shader()` compiles exactly one file via one `glslc` invocation with
  a `DEPENDS` list containing only that one file. This technique needs the
  SAME density-profile/phase-function/LUT-parameterization math shared across
  at least four `.comp` files and one `.frag` file — duplicating that math by
  hand six times is exactly the "three independently-maintained copies of the
  same logic" anti-pattern `AGENTS.md` already warns against elsewhere (see
  "Skeletal Animation Pose Resolution"). This needs a small, deliberate,
  verified CMake/shader-authoring change — see Phase 1.
- **The engine's `GET /get_texture` endpoint already auto-captures every 2D
  texture any render-graph pass declares, with zero opt-in** (see `AGENTS.md`,
  "Named Texture Capture") — every 2D LUT this campaign adds gets free visual
  debuggability the moment it's declared via `CreateTexture()`/
  `ImportTexture()`. The 3D aerial-perspective volume is the one exception —
  `RenderGraphDebugTextureRegistry` has no 3D concept, so Phase 9 adds a small
  manual 2D "debug slice" mirror instead of extending that registry (see
  Phase 9's own scope note on why this is the right amount of effort).
- **No reference implementation has been read yet.** Nobody has cloned or
  inspected `github.com/hoffstadt/pl-sky` — Phase 1's first concrete step is
  doing exactly that and producing a written, engine-specific translation
  of its passes/formulas before any engine code is written.

## Step 3: The Plan (How will we get there?)

### Locked Design Decisions

These were decided directly with the project owner before this document was
written, and every child phase below is written assuming them — do not
revisit them without a fresh discussion:

1. **Sun direction lives on a brand-new ECS component**, not a bare Editor
   slider. A new `DirectionalLight` component (`src/ECS/Components/
   DirectionalLight.h`) sits on a `Transform`-bearing entity ("Sun"), the same
   way `Camera` already does — selectable/rotatable/parentable in
   "Hierarchy"/"Inspector" exactly like any other entity, and reusable later
   by a genuine future lighting system (this campaign does NOT build general
   lighting — see Step 4). See Phase 8.
2. **Aerial perspective is applied to already-rendered opaque geometry via a
   full-screen POST-PROCESS COMPOSITING PASS**, never by editing the existing
   forward mesh shaders (`Mesh.frag`/`TexturedMesh.frag`/`MeshPreview.frag`).
   This mirrors `ComputeBlurValidation`'s own proven shape exactly (a compute
   pass reading the just-rendered scene color + depth, writing a new output
   texture) and touches zero existing pipeline permutations. This is also how
   Hillaire's own reference technique (and `pl-sky`) actually composite it in
   practice — Phase 1's reference read-through must confirm this before Phase
   7 is implemented, but it is not expected to change this decision.
3. **The `pl-sky` reference repository is cloned to a scratch,
   reference-ONLY folder — never added to CMake, never compiled, never
   linked.** Clone target: `<repo-root>\_reference\pl-sky` (a new top-level
   `_reference/` folder, added to `.gitignore` in Phase 1 — its contents are
   a local study aid, never committed). Every engine-side deliverable is a
   hand-ported, from-scratch GLSL/C++ implementation living under
   `src/Renderer/Atmosphere/`/`src/Shaders/`, written and understood well
   enough to explain, not a vendored copy of someone else's code.
4. **This feature is ALWAYS compiled in — no new `GTE_ENABLE_ATMOSPHERE`
   CMake switch.** It is a core rendering feature, the same tier as the
   Render Graph or GPU Vertex Skinning (neither of which has an on/off
   switch of its own) — see `CMakeLists.txt`'s existing `GTE_ENABLE_*` options
   for the ones that DO exist (`GTE_ENABLE_EDITOR`, `GTE_ENABLE_NETWORK`,
   `GTE_ENABLE_PROJECT_PANEL`, `GTE_ENABLE_PROFILER`, `GTE_ENABLE_JOB_SYSTEM`)
   for contrast — none of them gate a rendering technique itself, only
   Editor/tooling/profiling/threading infrastructure.

### Document Map

| # | File | One-line summary |
|---|---|---|
| 0 | `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` | This file. |
| 1 | `ATMOSPHERE_PHASE1_REFERENCE_ANALYSIS_AND_PHYSICAL_MODEL_FOUNDATIONS_v1.md` | Clone + read `pl-sky`; stand up `src/Renderer/Atmosphere/` with the pure, Tier-1-tested physical-parameter math (the permanent CPU oracle); verify/extend GLSL shared-include support in `CompileShaders.cmake`. |
| 2 | `ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md` | Add a genuine 3D-texture GPU primitive (`VolumeTexture`) and teach `gte::rg::RenderGraph` a THIRD resource kind so a pass can declare/read/write one — the highest-risk phase in this campaign. |
| 3 | `ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md` | First compute pass: the Transmittance LUT (2D), the foundation every later pass samples. |
| 4 | `ATMOSPHERE_PHASE4_MULTISCATTERING_LUT_v1.md` | Second compute pass: the Multi-Scattering LUT (2D), depends on Phase 3's output. |
| 5 | `ATMOSPHERE_PHASE5_SKYVIEW_LUT_v1.md` | Third compute pass: the per-frame Sky-View LUT (2D) plus the new per-frame `AtmosphereFrameUniforms` (camera height, sun direction) every later pass also needs. |
| 6 | `ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md` | Fourth compute pass: the camera-frustum-aligned 3D scattering/transmittance volume — the actual "aerial perspective" data. |
| 7 | `ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md` | Two new full-screen passes: draw the sky background from the Sky-View LUT, and composite aerial perspective onto opaque geometry from the froxel volume — wired into the real Game/Scene View regimes. |
| 8 | `ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md` | The new `DirectionalLight` ECS component + a global `AtmosphereSettings` singleton + Editor "Atmosphere" panel + Hierarchy "Create Sun" entry. |
| 9 | `ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md` | Tier-1 test recap, an Editor-side numeric LUT validation tool (mirroring `GpuSkinningValidation`/`ComputeBlurValidation`), GPU-timing registration, `/get_texture` visual-verification walkthrough, `README.md`/`AGENTS.md`/`TODO.md` updates, final full build+test pass. |

### Data-Flow Summary (what each phase produces, and who consumes it)

```
Phase 1: AtmosphereParametersGpu (physical constants) + AtmosphereMath.h (CPU oracle)
             |
Phase 2: VolumeTexture primitive + RenderGraph 3rd resource kind (infrastructure only)
             |
Phase 3: TransmittanceLut (2D texture)  -------------------------\
             |                                                    |
Phase 4: MultiScatteringLut (2D texture, reads TransmittanceLut)  |
             |                                                    |
Phase 5: SkyViewLut (2D texture, reads both above) + AtmosphereFrameUniforms
             |                                                    |
Phase 6: AerialPerspectiveVolume (3D VolumeTexture, reads TransmittanceLut + MultiScatteringLut + AtmosphereFrameUniforms)
             |
Phase 7: SkyBackground pass (reads SkyViewLut) + AerialPerspectiveComposite pass (reads AerialPerspectiveVolume + SceneColor + SceneDepth)
             |
Phase 8: DirectionalLight ECS component + AtmosphereSettings feed AtmosphereFrameUniforms every frame
             |
Phase 9: Validates every LUT above against a CPU oracle / visual capture, wires profiling, updates docs
```

### Cross-Cutting Engineering Rules (apply to EVERY phase below)

Every rule in `AGENTS.md` still applies in full — this section only calls out
the ones most load-bearing for THIS campaign specifically:

- **RAII + `gte` namespace + `GpuMemoryTracker` registration for every new GPU
  resource type** (see AGENTS.md, "GPU Resource Memory Tracking") — the new
  `VolumeTexture` (Phase 2) must register/unregister exactly like
  `Texture2D`/`RenderTexture` already do, with zero exceptions.
- **Tier 1 vs. Tier 2 split, strictly enforced** (see AGENTS.md, "Testability
  & Regression Safety"): every pure math function this campaign adds
  (density profiles, phase functions, LUT UV parameterization, the CPU
  oracle) lives in a Vulkan-free header/source pair under
  `src/Renderer/Atmosphere/` and gets a real Tier-1 GoogleTest under
  `tests/Renderer/Atmosphere/` in the SAME phase that introduces it — never
  "add the test later".
  - **A genuinely new discipline this campaign is establishing:
    `AtmosphereMath.h`'s density-profile/optical-depth/transmittance
    functions (Phase 1) are the PERMANENT CPU ORACLE** for the Transmittance
    LUT, in the exact same spirit `Animation/VertexSkinning.cpp`'s CPU path
    is the permanent oracle for GPU vertex skinning (see AGENTS.md, "GPU
    Vertex Skinning") — if the GPU `.comp` shader and the CPU oracle ever
    disagree, the CPU oracle is right by definition and the shader is what
    needs fixing, never the reverse. Phase 9's validation tool depends on
    this being true from Phase 1 onward.
- **Every new compute pass follows `ComputeBlurValidation`'s exact shape**
  (`src/Editor/ComputeBlurValidation.h/.cpp` + `src/Shaders/BoxBlur.comp`) —
  lazily-initialized `ComputePipeline`/`ComputeDescriptorSet`/output texture,
  an `AddPass(RenderGraphBuilder&, ...)` method returning the output handle,
  a `FinalizeForSampling(VkCommandBuffer)` method for any pass whose output
  needs to be sampled outside the graph that frame. Do not invent a
  differently-shaped class for any of Phases 3/4/5/6/7's passes without a
  documented, specific reason.
- **Every new 2D LUT texture is created via
  `RenderGraphBuilder::CreateTexture()`/`ImportTexture()` under a clear,
  literal, stable name** (e.g. `"AtmosphereTransmittanceLut"`,
  `"AtmosphereMultiScatteringLut"`, `"AtmosphereSkyViewLut"`) — this is what
  makes it show up in `GET /list_textures`/`GET /get_texture` automatically,
  per AGENTS.md's "Named Texture Capture" rule that the TEXTURE name (not the
  pass name) is what matters for capture.
- **No shader reflection, no shader hot-reload, no shader permutation system**
  — same refusal `COMPUTE_SHADER_MASTER_STRATEGY_v2.md` already established;
  this campaign does not need any of them and must not introduce them.
- **A pass whose output has no reader/root is silently culled by
  `RenderGraphCompiler`** — every phase that adds a pass must remember to add
  its output to that `RenderGraph::Execute()` call's root/final-output set
  (mirrors the existing rule already documented for `ComputeBlurValidation`'s
  own output and every real Game/Scene/Present pass).
- **`Renderer`/`Vulkan/*`/`RenderGraph/*` internals are still main-thread-only,
  unsynchronized** (see AGENTS.md, "Job System" thread-safety table) — nothing
  in this campaign touches the Job System or any worker thread; every new
  pass/resource here is created and recorded exclusively from the main
  thread's existing per-frame `Application::Run()` sequence.

### Workflow Every Implementing Session Must Follow

This mirrors the exact workflow already used by every other campaign in
`task_manager/` (`render_graphs/`, `gpu_skinning/`, `compute_shader/`):

1. Read this file (Phase 0) in full, then read the ONE child phase document
   you are about to implement, in full, plus its own "Read report from
   previous phase" pointer if it names one.
2. Implement that phase's plan — real code changes under `src/`
   (and `tests/` for every Tier-1 addition), never a "we will do X later"
   placeholder phase.
3. **Fast compile check only** — do not run a full build/full regression
   suite unless that specific phase's document explicitly says to (this is
   normally only true for the LAST phase, Phase 9). Use `cmake --build build`
   for a targeted incremental build of `gte_core`/`GreatTamanaEngineTests`.
4. Write a short completion report,
   `task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASEn_COMPLETION_REPORT.md`,
   summarizing what changed, any deviation from the plan (and why), and any
   open question left for the next phase.
5. `git add` + `git commit` the code change and its completion report
   together, on the `feature/atmosphere-scattering-impl` branch.
6. Only Phase 9 runs the full build + full `ctest` regression suite, and only
   Phase 9 updates `README.md`/`AGENTS.md`/`TODO.md`.

## Step 4: What We Will NOT Do (Focus)

- **No general-purpose lighting system.** `DirectionalLight` (Phase 8) exists
  ONLY to drive the atmosphere's sun direction/illuminance — it is not wired
  into `Mesh.frag`/`TexturedMesh.frag`'s existing fixed-direction lambert
  term, and no point/spot light of any kind is added. A future "real
  lighting" campaign may reuse this component, but does not exist yet and is
  out of scope here.
- **No volumetric clouds, no god-rays/light-shafts, no atmospheric
  perspective for the Editor's `AssetPreviewMesh`/`BoneViewerWindow`
  viewports** — this campaign targets the Game View and Scene View only
  (the two views that go through the real, per-frame Game/Scene RenderGraph
  regime — see AGENTS.md's "Editor Module Structure").
  `AssetPreviewMesh`/`BoneViewerWindow` keep their existing plain lambert
  preview shading, unchanged.
- **No multiple simultaneous suns/atmospheres.** Exactly one
  `DirectionalLight` (the first `active == true` one found, mirroring
  `Camera`'s own "first active" convention in `RenderSystem::
  ResolveActiveCameraViewProjection()`) and exactly one global
  `AtmosphereSettings` per scene — never a per-entity/per-region atmosphere.
- **No scene (de)serialization of the new component/settings.** `Scene/
  SceneTextFormat.h`'s existing `PrimitiveSource`/`MeshAssetSource`-only
  format is NOT extended in this campaign to round-trip `DirectionalLight`/
  `AtmosphereSettings` through Save/Load — a `DirectionalLight` entity created
  via the Editor exists only for that running session, exactly like a
  `Camera` entity does today (also not yet round-tripped — see `Scene/
  SceneBuilder.h`'s own documented scope). A future scene-serialization
  follow-up phase can close this gap; it is explicitly out of scope here so
  this campaign stays focused on rendering, not on extending an unrelated
  subsystem.
- **No changes to `Mesh.frag`/`TexturedMesh.frag`/`MeshPreview.frag`/
  `SceneGrid.frag`** — Locked Design Decision 2 makes this unnecessary; if
  Phase 1's reference read-through somehow finds a strong reason this is
  wrong, it must be raised as a flagged, explicit deviation in that phase's
  own completion report, not silently changed.
- **No dynamic time-of-day animation/day-night cycle system** — the sun's
  direction is whatever the `DirectionalLight` entity's `Transform` currently
  is; nothing auto-rotates it over time. A future gameplay system could drive
  it by rotating that Transform every frame, entirely outside this campaign.

## Step 5: Their Role (What does this mean for you?)

- Read phases in numeric order — each one depends on the previous one's
  concrete deliverable (types, textures, or infrastructure), not just its
  ideas. Do not start Phase 6 before Phase 2 has landed a real,
  compiling `VolumeTexture`.
- If a phase's plan turns out to disagree with the ACTUAL current source once
  you're reading it with the tool (not just this document's own paraphrase of
  it), the real source always wins — note the discrepancy in that phase's
  completion report exactly the way `GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md`'s
  own "why v2 exists" audit did, and adjust the implementation accordingly.
- Every phase must leave `gte_core` (and, where it added Tier-1 tests,
  `GreatTamanaEngineTests`) compiling cleanly before its own completion
  report is written — an uncompilable intermediate state is never an
  acceptable phase boundary.
- Phase 9 is the only phase allowed to touch `README.md`/`AGENTS.md`/
  `TODO.md`, and the only phase that runs a full build + full `ctest` pass —
  do not pre-empt that in an earlier phase.
