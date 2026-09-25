# editor-core-separation-6 — PHASE0 MASTER STRATEGY

**Branch:** `feature/editor-core-separation` (stay on it — do not create a new branch).

**Project root:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

**This folder:** `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\task_manager\editor-core-separation-6`

**Source finding/proposal document (read this IN FULL before starting ANY phase):**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\render-feature\RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_2026-09-25.md`
(referred to below as "the Proposal"). This master strategy narrows and corrects
the Proposal's Part 3/4 in several real, evidence-based ways (see "Locked
Design Decisions" below) — where this document and the Proposal disagree,
**this document wins**, because it was written after live inspection of the
actual current source, not just the Proposal's own prose.

This document is the ORCHESTRATOR for this whole campaign. Every child phase
file (`PHASE1_*.md` .. `PHASE8_*.md`) in this same folder must be read together
with this file before starting work on that phase. Every phase produces its
own `PHASEn_COMPLETION_REPORT.md` in this same folder when done, and ends
with its own `git_add` + `git_commit`.

**Use `ask_questions` whenever a real design ambiguity comes up that this
master strategy or the relevant phase file does not already resolve — never
silently guess. This applies transitively: if a phase (or any task it
delegates) itself ever hands off further work, that further work must also be
told to use `ask_questions` for its own genuine ambiguities.**

---

## Step 1: The Goal (Where are we going?)

Today, `plugins/gte_plugin_abi/IPluginRenderPassBuilder.h`'s ENTIRE
plugin-facing drawing API is one method, `AddFullscreenClearPass(name, r, g,
b, a)` — a hard, destructive `VK_ATTACHMENT_LOAD_OP_CLEAR` of the whole
screen. Every loaded `IRenderFeatureModule_v1` plugin is handed the exact
SAME shared texture handle (`Core.cpp`'s `"PluginRenderFeatures"` provider),
so with 2+ render-feature plugins loaded, only the LAST-registered one's
output survives — silently, "last write wins," chosen by filesystem
directory-scan order, not by anything a plugin author declares. This is
proven, file/line-backed, in the Proposal's Part 1.

The goal of this campaign is to ship a real, additive `_v2` render-feature
system that fixes this for real:

1. A new, versioned ABI surface (`IRenderFeatureModule_v2`,
   `IPluginRenderPassBuilder_v2`, `GtePluginRenderFeatureDescriptor`) that
   lets a plugin declare an explicit `stage` + `priority` and draw using a
   small, growable, host-implemented palette of real operations (solid fill,
   radial vignette, color grade) — never raw Vulkan/`rg::` types, never
   plugin-supplied shader bytecode.
2. A real compositor (`RenderFeatureCompositor`) that gives every `_v2`
   plugin its OWN private offscreen render target, composites them in
   explicit, author-declared order, through a real, host-owned GPU blend
   pipeline (Replace / AlphaOver / Additive / Multiply / ScreenSpaceMask) —
   never "last write wins on shared memory" again.
3. A generic host-side extensibility mechanism (`IPluginCapabilityOrchestrator`)
   so that `Core.cpp` no longer hand-codes a bespoke `for` loop per plugin
   capability kind — today's render-feature loop AND today's editor-panel
   wiring both migrate onto ONE shared, reusable registry, proving the
   pattern generalizes to whatever capability kind comes next.
4. Two new, genuinely-different `_v2` demo plugins, and a real, pixel-level,
   live-engine proof that 2 simultaneously-loaded render-feature plugins
   really do composite correctly — replacing the Proposal's own documented
   embarrassment (2 existing demo plugins deliberately clear to the exact
   SAME color, so the underlying bug was invisible in every prior
   screenshot-based smoke test).
5. Editor visibility (the "Render Graph" panel gets a new "Plugin Render
   Features" section) and updated conventions docs.

**`_v1` is never touched, never deprecated, never removed.** Every existing
interface, every existing demo plugin, every existing observable behavior
(log lines, pass names, panel names) from `editor-core-separation-3/4/5`
stays byte-for-byte identical, forever. This is a strictly additive `_v2`
layer sitting next to `_v1`, exactly like this repository's own established
`_v1`/`_v2` interface-versioning convention
(`docs/conventions/plugin-architecture.md`) already requires.

## Step 2: The Situation (Where are we now?)

Confirmed by direct inspection of the real, current source (not guessed):

- `plugins/gte_plugin_abi/IRenderFeatureModule.h` — `IRenderFeatureModule_v1`,
  one method: `AddRenderGraphPasses(IPluginRenderPassBuilder&)`.
- `plugins/gte_plugin_abi/IPluginRenderPassBuilder.h` — `IPluginRenderPassBuilder`,
  one method: `AddFullscreenClearPass(name, r, g, b, a)`.
- `src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp` — implements that one
  method as a single `rg::RenderGraphBuilder::AddRenderPass(...)` call,
  `rg::PassKind::Graphics`, `rg::ViewScope::Shared`,
  `rg::RenderPassCategory::Debug`, tagged
  `rg::RenderPassEvent::AfterEverything` (the LATEST tier this engine's
  render graph compiler understands — confirmed in
  `src/Renderer/RenderGraph/RenderGraphTypes.h`, line ~498: the full
  `RenderPassEvent` enum is `BeforeEverything(0), PreOpaques(1000),
  Opaques(2000), AfterOpaques(2500), Transparents(3000),
  AfterTransparents(4000), AfterEverything(9000)` — **there is no tier AFTER
  `AfterEverything`**; this is load-bearing for a Locked Design Decision
  below).
- `src/Core/Core.cpp`'s `"PluginRenderFeatures"` provider (registered last,
  `rg::ProviderTiming::AfterDeferredPasses`, `rg::ProviderScope::PerActiveView`,
  the very last `Register(...)` call in
  `RegisterOffscreenRenderPipelineProviders()`, immediately after
  `"AtmosphereComposite"`): fetches ONE `pluginTarget` handle (the view's
  composited output, or the raw color target as a fallback) ONCE per view,
  outside the loop, and hands that SAME handle to EVERY loaded
  `IRenderFeatureModule_v1` module via a fresh
  `PluginRenderPassBuilderAdapter` each time.
- `src/Core/Plugins/PluginRenderFeatureDiagnostics.cpp` —
  `CountModulesImplementingRenderFeature()`, used by `Core::LoadPlugins()` to
  log a `GTE_LOG_WARNING` when 2+ modules implement
  `IRenderFeatureModule_v1` (the bug is logged, never fixed).
- `plugins/demo_render_feature/` and `plugins/demo_render_feature_second/` —
  both implement `IRenderFeatureModule_v1`, both clear to the EXACT SAME
  solid magenta `(1,0,1,1)`, by explicit design (the second plugin's own
  header comment admits this was done specifically so the clobbering bug
  stays invisible in every screenshot-based smoke test).
- `plugins/gte_plugin_abi/SingleCapabilityPluginModule.h` +
  `PluginExportsMacro.h` (from `editor-core-separation-5`) — the
  authoring-sugar every demo plugin now uses
  (`MakeModuleInfo()`, `SingleCapabilityPluginModule<T>`,
  `ZeroCapabilityPluginModule`, `GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE`).
  New `_v2` demo plugins (Phase 6) use this exact same sugar.
- `src/Core/EditorPanelRegistry.h` — a Meyers-singleton, **already living in
  `src/Core/` (gte_core tier)**, not `src/Editor/` — confirmed by direct read.
  This matters: it means a new `gte_core`-tier orchestrator CAN legitimately
  own the editor-panel-registration wiring without any layering violation
  (Phase 3 below).
- `src/Editor/EditorHost.cpp` (constructor, confirmed by direct read,
  current line numbers ~199-235): calls `m_core.LoadPlugins(...)` FIRST
  (line 199), THEN registers 10 built-in panel names via
  `EditorPanelRegistry::Instance().RegisterBuiltinPanelName(...)` (lines
  210-222), THEN loops every loaded module querying
  `IEditorPanelModule_v1` and calls `RegisterPluginPanel(...)` (lines
  224-235). **This exact ordering is load-bearing** — see Locked Design
  Decision #6 below; naively moving this loop's logic to run automatically
  INSIDE `Core::LoadPlugins()` (as the generic orchestrator mechanism
  implies) would make it run BEFORE the built-in panel names are
  registered, breaking the documented invariant "`AllNames()` always lists
  every built-in panel first, plugins after."
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`'s
  `AddAerialPerspectiveCompositePass()` (confirmed by direct read, lines
  ~716-854) is the exact real, working precedent this campaign's own blend
  compositor pass mirrors: a `rg::PassKind::Compute` pass, a persistent,
  named `RenderTexture` output created via
  `renderer.CreateRenderTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM,
  name, nullptr, /*allowStorageImageAccess=*/true)`, imported fresh each
  frame via `builder.ImportTexture(name, target, VK_IMAGE_LAYOUT_UNDEFINED)`,
  a `ComputeDescriptorSet` (`renderer.AllocateComputeDescriptorSet(layout)`,
  rewritten via `.Rewrite(device, {ComputeDescriptorWrite::
  CombinedImageSampler(binding, view, sampler), ...,
  ComputeDescriptorWrite::StorageImage(binding, view)})`), a push-constant
  struct, and `renderer.Dispatch(pipeline, descriptorSet.Native(),
  &pushConstants, sizeof(pushConstants), groupX, groupY, groupZ)` with group
  counts from `ComputeGroupCount3D()` (`src/Renderer/ComputeDispatch.h`).
  Shader source lives at `src/Shaders/*.comp`, registered in the root
  `CMakeLists.txt` via `gte_add_shader(GreatTamanaEditor
  src/Shaders/Whatever.comp)` (`cmake/CompileShaders.cmake`), and referenced
  at runtime by its compiled path `"shaders/Whatever.comp.spv"`.
- `plugins/gte_plugin_abi/GtePluginAbiFingerprint.h`'s own doc comment
  confirms: **adding a brand-new interface/capability version string is NOT
  a reason to bump `abiContractGeneration`** — this campaign never touches
  the fingerprint.
- `plugins/gte_plugin_abi/CMakeLists.txt` defines `gte_plugin_abi` as a
  plain `INTERFACE` library with only include-directory declarations — no
  per-file source list to maintain, so adding new headers under
  `plugins/gte_plugin_abi/` needs ZERO `CMakeLists.txt` edits.
- This repo's own testability discipline (confirmed via
  `tests/Network/CaptureEndpointsEndToEndTests.cpp`'s own header comment)
  explicitly documents real GPU-rendered-pixel content as "Tier 2, no
  automated coverage yet" — there is no existing automated `ctest` that
  reads back and checks actual rendered pixel colors. This campaign does
  not invent one (see the Locked Design Decisions below) — the real
  pixel-level proof (Phase 6) is a manual, live-engine, HTTP-driven check.

## Step 3: The Plan (How do we get there?)

### Locked Design Decisions (resolved via `ask_questions` with the user before writing any phase file — do not re-litigate these; challenge via `ask_questions` only if real code contradicts one)

1. **Only 2 of the Proposal's 5 `RenderFeatureStage` values get real engine
   wiring in this campaign: `PostComposite` (today's existing single hook
   point) and `PreUI`.** `PreOpaque`, `PostOpaque`, `PostTransparent` are
   declared in the enum (for future-proofing the ABI shape) but a plugin
   that declares one of them is REFUSED at `OnPluginsLoaded()` time with a
   loud `GTE_LOG_WARNING` naming the plugin and the unwired stage, and is
   simply never invoked — fail loud, never a silent mis-render. Wiring
   those 3 remaining stages means inserting new hook points into the LIVE
   opaque/transparent production render passes — a materially larger,
   riskier change explicitly deferred to a future follow-up campaign.
2. **`PreUI`, in THIS engine's actual pipeline shape, is NOT a new
   `RenderPassEvent` tier.** Per Step 2's evidence, `AfterEverything` (9000)
   is already the LAST tier the compiler understands, and Dear ImGui itself
   is recorded separately, later, inside the swapchain-level "Present"
   `RenderPipeline` regime (`Core::RegisterPresentRenderPipelineProvider()`,
   `m_recordImGuiThisFrame` callback) — a fundamentally different
   `RenderPipeline`/regime than the offscreen Game/Scene View regime the
   existing `"PluginRenderFeatures"` provider runs in. Reaching genuinely
   INTO the Present regime (to sit strictly between the swapchain blit and
   `AddPresentPass()`'s own ImGui recording) means editing
   `RenderPasses.cpp`'s `AddPresentPass()` itself — explicitly the kind of
   "touch core Present path" risk this campaign avoids for the same reason
   it defers `PreOpaque`/`PostOpaque`/`PostTransparent`. **Resolution:**
   `PostComposite` and `PreUI` are both realized at the SAME existing
   per-view offscreen hook point (`"PluginRenderFeatures"`,
   `AfterDeferredPasses`/`AfterEverything`), as two ORDERED, back-to-back
   sub-stages processed in sequence — `PostComposite` entries composite
   first, then `PreUI` entries composite on top of that result, all still
   inside the one existing, already-working hook. This is architecturally
   honest (a real ordering guarantee between the two stages exists) without
   inventing a new, riskier wiring point. Documented plainly in Phase 4/5 so
   no implementer is confused later; an implementer is free to challenge
   this via `ask_questions` if new evidence contradicts it.
3. **The "read the current scene color/depth" capability
   (`TryReadSceneColorThisFrame`/`TryReadSceneDepthThisFrame`,
   `wantsSceneColorRead`/`wantsSceneDepthRead`) is DROPPED ENTIRELY from
   this campaign's `_v2` ABI.** None of the 3 fixed drawing operations this
   campaign ships (solid fill, radial vignette, color grade) need to read
   the scene first — they only draw on top. Adding an unused read-capability
   API now would be speculative surface with zero real consumer. A future
   `_v3` (or a later addition to `_v2`, since it is purely additive) is the
   right place to add this, once a real effect (e.g. a depth-aware outline)
   actually needs it.
4. **The existing editor-panel wiring (`IEditorPanelModule_v1`) DOES migrate
   onto the new `IPluginCapabilityOrchestrator` registry** (Phase 3), as a
   second, real proof the mechanism generalizes — with the EditorHost.cpp
   reordering fix from Step 2 applied first, so the built-in-panels-first
   invariant is preserved byte-for-byte.
5. **All 5 blend modes (`Replace`/`AlphaOver`/`Additive`/`Multiply`/
   `ScreenSpaceMask`) are implemented as ONE single "uber" GPU compute
   shader**, with the blend mode selected at dispatch time via a
   push-constant integer — not 5 separate shader files. The SAME
   "uber-shader, mode selected via push constant" philosophy is also
   applied to the 3 fixed drawing operations (solid fill / radial vignette
   / color grade) — one shader, `RenderFeatureOps.comp`, an `opCode`
   push-constant selects the formula — for engineering consistency and to
   minimize the number of new shader files/pipelines this campaign
   maintains.
6. **`EditorHost.cpp`'s constructor is reordered**: the 10
   `RegisterBuiltinPanelName(...)` calls move to run BEFORE
   `m_core.LoadPlugins(...)`, not after (Phase 3) — a pure, safe reordering
   (nothing else touches `EditorPanelRegistry` in between), required so
   that once the editor-panel wiring becomes fully automatic (driven by
   `Core::LoadPlugins()` calling every registered orchestrator internally,
   with zero capability-specific special-casing left at the `EditorHost.cpp`
   call site), the built-in-panels-first invariant still holds.
7. **No new automated `ctest` for real rendered-pixel content.** Phase 6's
   pixel-level proof (Proposal Section 3.7) is a manual, live-engine
   procedure: run the real `GreatTamanaEditor.exe`
   (`run_app_background`), use `gte_send_request` against
   `GET /get_swapchain` or `GET /get_game_view`, decode the returned image,
   and confirm the blend math by hand/script — matching this repo's own
   existing, documented "GPU-rendered pixels = no ctest coverage yet"
   convention (`tests/Network/CaptureEndpointsEndToEndTests.cpp`'s own
   header comment).
8. **Two brand-new plugin folders** (`plugins/demo_render_feature_v2/`,
   `plugins/demo_render_feature_v2_second/`) prove the `_v2` system (Phase
   6). **All 4 existing demo plugins stay 100% untouched** — this campaign
   never edits `plugins/demo_render_feature/`,
   `plugins/demo_render_feature_second/`, `plugins/demo_editor_panel/`, or
   `plugins/demo_hello_world/`.
9. **`_v1` and `_v2` render-feature plugins are NOT unified into one
   deterministic composited order in this campaign** (Proposal Part 5,
   restated, non-goal). If a build somehow has both an `_v1` and a `_v2`
   render-feature plugin loaded simultaneously, whichever orchestrator's
   pass happens to execute later in the shared `RenderPassEvent::
   AfterEverything` tier wins the final pixel for that view — an accepted,
   explicitly out-of-scope edge case, exactly mirroring (at one level
   higher) the original `_v1`-vs-`_v1` bug this whole campaign exists to
   fix for `_v2`-vs-`_v2`. Every phase's own demo-plugin verification keeps
   `_v1` and `_v2` demo plugins loaded together in the SAME `plugins/`
   folder (never removing the old ones) specifically so this coexistence
   is exercised for real, even though its outcome is unspecified.
10. **`RenderFeatureCompositor`'s final blend write, for the LAST plugin in
    the LAST wired stage that ran this frame, writes directly back into the
    SAME `pluginTarget` handle** the legacy `_v1` path already writes into
    (`viewData->colorTarget` or the composited handle, whichever
    `"PluginRenderFeatures"` already resolves today) — never a brand-new,
    separately-tracked "final image" handle. This is what makes
    `GET /get_game_view`, the swapchain Present blit, and every other
    existing downstream consumer of that handle keep working with ZERO
    changes anywhere else in the engine. Every OTHER intermediate texture
    (each plugin's own private target, and any accumulator between two
    plugins/stages) is a fresh, transient `RenderGraphBuilder::CreateTexture()`
    handle, safely reused frame-to-frame by the render graph's own existing
    desc-matching transient pool (`RenderGraphBuilder.h`'s own documented
    behavior) — no new pooling code needed.

### Phase map

- **`PHASE1_RENDER_FEATURE_ABI_V2_FOUNDATION.md`** — new, additive-only ABI
  headers under `plugins/gte_plugin_abi/`: `GtePluginRenderFeatureDescriptor`,
  `RenderFeatureStage`, `RenderFeatureBlendMode`, `IRenderFeatureModule_v2`
  (appended to the existing `IRenderFeatureModule.h`), and a new
  `IPluginRenderPassBuilder_v2.h` with exactly 3 methods (`AddSolidFillPass`,
  `AddRadialVignettePass`, `AddColorGradePass`). Zero `gte_core` change.
  Verified by a standalone compile check only.
- **`PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md`**
  — the new `IPluginCapabilityOrchestrator` interface
  (`src/Core/Plugins/`), `Core`'s new orchestrator vector +
  `RegisterBuiltinCapabilityOrchestrators()`, and migration of the EXISTING
  `_v1` render-feature loop into a new `LegacyRenderFeatureOrchestrator`,
  with **zero observable behavior change** (same warning text, same pass
  names). Verified via a live Editor smoke test (same magenta baseline,
  same `GET /get_logs` warning text).
- **`PHASE3_EDITOR_PANEL_ORCHESTRATOR_MIGRATION.md`** — the `EditorHost.cpp`
  reordering fix (Locked Design Decision #6), then migration of the
  existing `IEditorPanelModule_v1` wiring into a new
  `EditorPanelCapabilityOrchestrator`, with **zero observable behavior
  change**. Verified via live `GET /list_tabs` + `GET /get_logs`.
- **`PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md`** — the real
  `PluginRenderPassBuilderAdapter_v2` (implements the 3 fixed drawing
  operations via the new `RenderFeatureOps.comp` uber compute shader), and
  `RenderFeatureCompositor` (a NEW `IPluginCapabilityOrchestrator`
  implementation): descriptor collection, per-stage priority sorting,
  loud collision detection/warning, per-plugin private offscreen targets,
  chained composition — with blending TEMPORARILY hardcoded to a
  Replace-equivalent single mode, to prove the whole ordering/private-target
  pipeline end-to-end with exactly one loaded `_v2` demo plugin before the
  real multi-mode blend shader exists. Verified via incremental build +
  live Editor smoke test with ONE new throwaway `_v2` test plugin.
- **`PHASE5_BLEND_MODE_COMPUTE_SHADER_AND_PREUI_STAGE.md`** — the real
  `RenderFeatureBlend.comp` uber blend shader (5 modes, one push-constant
  int), replacing Phase 4's Replace-only stub inside
  `RenderFeatureCompositor`, plus wiring the second, back-to-back `PreUI`
  sub-stage (Locked Design Decision #2). Verified via incremental build +
  live Editor smoke test with 2 differently-configured throwaway `_v2` test
  plugins.
- **`PHASE6_V2_DEMO_PLUGINS_AND_PIXEL_PROOF.md`** — the two new,
  genuinely-different demo plugins (`demo_render_feature_v2`,
  `demo_render_feature_v2_second`), root `CMakeLists.txt` registration, and
  the real, manual, live-engine pixel-level regression proof (Proposal
  3.7 / Locked Design Decision #7) — this is the phase that actually PROVES
  the fix with real evidence, not log text.
- **`PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md`** — extends the Editor's
  existing "Render Graph" panel with a new "Plugin Render Features" section
  (per active stage: plugin name, priority, blend mode — the compositor's
  own real, resolved ordering decision, made visible).
- **`PHASE8_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`** — updates
  `docs/conventions/plugin-architecture.md` and
  `plugins/gte_plugin_abi/PublicSurface.md`, runs the ONE full clean build +
  full `ctest` pass + live HTTP smoke test this whole campaign is allowed to
  run, confirms every pre-existing consumer of the old behavior still works
  identically, and writes `CAMPAIGN_COMPLETION_REPORT.md`.

### Campaign-wide rules every phase must follow ("Workflow Rule 1" – "Workflow Rule 10" below — later phase files cite these exact numbers)

1. **No full build, no full regression test, for Phases 1–7.** Only a fast,
   targeted, incremental build/compile check (see each phase's own
   "Verification" section) — this machine's full clean build and full
   `ctest` pass are slow and reserved for Phase 8 alone.
2. **Use the engine's own internal logging + `GET /get_logs`** for anything
   inside `GreatTamanaEditor.exe`'s own run — never
   `printf`/`std::cout`/`OutputDebugString`/raw C++ logging for anything new
   added under `src/` or `plugins/`. Use `GTE_LOG_INFO`/`GTE_LOG_WARNING`
   (`src/Core/Logging.h`), exactly like every existing call site this
   campaign touches already does.
3. **Use the engine's own network debugging endpoints for visual/behavioral
   verification** — `run_app_background` the real `GreatTamanaEditor.exe`,
   then `gte_send_request` against `http://127.0.0.1:8080`:
   `GET /get_logs?limit=N`, `GET /list_tabs`, `GET /get_swapchain`,
   `GET /get_game_view`, `GET /get_texture?name=...`, `GET /list_textures`,
   `POST /clear_logs`. Always `stop_app_background` the PID when finished
   with a check.
4. **Every phase writes its own `PHASEn_COMPLETION_REPORT.md`** in this same
   folder once its own verification passes, documenting exactly what
   changed, any real deviation from this phase's own plan, and the exact
   verification evidence gathered.
5. **`git_add` + `git_commit` at the end of every phase** — one commit per
   phase, message referencing the phase number and its one-line summary.
6. **If a phase hits a genuine design ambiguity not already resolved by
   this master strategy or its own phase file, use `ask_questions` before
   guessing.** This applies transitively to anything a phase itself
   delegates further.
7. **Implementation phases (1–8) must NOT call `delegate_task` themselves.**
   Only the double-check/orchestration step of this campaign (run
   separately, after all phase files are scaffolded and reviewed) is
   allowed to use `delegate_task` to hand off each phase's real
   implementation. The one exception: Phase 8's own mandatory final full
   regression pass may invoke `delegate_task` ONLY if that regression pass
   surfaces a real, newly-broken, unexplained test failure that needs a
   dedicated fix — never for any other reason, and never by any phase other
   than Phase 8.
8. **Every phase's code must follow `AGENTS.md`'s existing coding
   guidelines** (Clean Architecture, RAII, `namespace gte`) and this repo's
   own established plugin-architecture conventions
   (`docs/conventions/plugin-architecture.md`,
   `plugins/gte_plugin_abi/PublicSurface.md`) — never invent a new pattern
   where an existing file (`AtmosphereLutRenderer.cpp`'s
   `AddAerialPerspectiveCompositePass()`, `GpuDrivenBatchNamePool` in
   `Core.cpp`, `SingleCapabilityPluginModule.h`) already shows the exact
   shape to copy.
9. **Never change any EXISTING (`_v1`-era) observable behavior.** Every
   `_v1`-related log line, pass name, panel name, and warning string stays
   byte-for-byte identical before and after every phase in this campaign.
   Diff against the original file (read it first, before editing) to
   confirm this by hand before moving on, exactly like
   `editor-core-separation-5`'s own established discipline.
10. **Run `git_status` at the very START of every phase** — confirm the
    branch still reads `feature/editor-core-separation` and the working
    tree is either clean or contains only the exact diff the immediately-
    prior phase already committed — **and run it AGAIN immediately before
    that phase's own final commit**, to confirm the about-to-be-staged diff
    touches ONLY the files this phase's own plan says it may touch.

### What this campaign explicitly does NOT do (Non-Goals)

- No arbitrary plugin-supplied shader/bytecode execution (Proposal Part 5).
- No hot reload, no cross-process sandboxing, no per-project plugin
  manifest/UI, no cross-compiler third-party plugin SDK — unchanged,
  permanent non-goals carried over from every prior
  `editor-core-separation-*` campaign.
- No wiring of `PreOpaque`/`PostOpaque`/`PostTransparent` stages (Locked
  Design Decision #1) — declared in the ABI, refused loudly at runtime if a
  plugin uses one.
- No "read the current scene color/depth" plugin capability (Locked Design
  Decision #3).
- No new automated `ctest` for real rendered-pixel content (Locked Design
  Decision #7).
- No unification of `_v1` and `_v2` render-feature ordering (Locked Design
  Decision #9).
- No change to `GtePluginAbiFingerprint`/`abiContractGeneration`.
- No edits to any of the 4 existing demo plugins.

### Reference commands

- Main build tree (existing, already configured): `cmake --build build`
  (working directory `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`,
  Ninja/MinGW) — use this for every phase's own incremental build/compile
  check; a Ninja incremental build only recompiles what actually changed.
- Full regression test (Phase 8 ONLY): `cd /d
  C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug
  --output-on-failure`.
- Run the live Editor for HTTP-driven smoke checks:
  `run_app_background` on `build\GreatTamanaEditor.exe`, then
  `gte_send_request` against `http://127.0.0.1:8080` (default port). Always
  `stop_app_background` the PID when finished.

### Every child phase file in this folder

1. `PHASE1_RENDER_FEATURE_ABI_V2_FOUNDATION.md`
2. `PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md`
3. `PHASE3_EDITOR_PANEL_ORCHESTRATOR_MIGRATION.md`
4. `PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md`
5. `PHASE5_BLEND_MODE_COMPUTE_SHADER_AND_PREUI_STAGE.md`
6. `PHASE6_V2_DEMO_PLUGINS_AND_PIXEL_PROOF.md`
7. `PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md`
8. `PHASE8_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`

Read this master file FIRST, then the source Proposal document
(`RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_2026-09-25.md`), then the
one phase file you are working on, then (if it exists yet) the previous
phase's own `PHASEn_COMPLETION_REPORT.md` for continuity clues, before
writing any code.
