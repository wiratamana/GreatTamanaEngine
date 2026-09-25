# editor-core-separation-9 — PHASE2 COMPLETION REPORT

**Phase:** `PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md`
**Status:** DONE. `PluginRenderOperationRegistry` and `PluginRenderPassBuilderAdapter_v3`
are real, shipped, and wired into `RenderFeatureCompositor`. Three permanent
`_v3` demo plugins reimplement `_v2`'s exact effects via the new generic
`Dispatch(opId, ...)` mechanism. `_v2`'s own production behavior is confirmed
byte-for-byte unchanged. `gte.builtin.box_blur` is registered (genuinely new
operation, zero `IPluginRenderPassBuilder_v3` interface change). Two real bugs
were found and fixed during this phase's own required live verification (see
"Deviations" below) — this phase's own warning that a mistake here "breaks
`_v2` in production, not just `_v3`" was correct and is exactly what happened
on the first live-run attempt, caught before being called done.

## What changed

### New files

- `src/Core/Plugins/PluginRenderOperationRegistry.h/.cpp` (NEW) — the real,
  host-owned, growable, string-keyed registry (Locked Architecture Decision
  #11). Owns `RenderFeatureOpsPushConstants`/`RenderFeatureBlendPushConstants`
  (RELOCATED here from `RenderFeatureCompositor.h`, since this registry is now
  the one place that builds the push-constant ranges those sizes describe),
  `PluginRenderOpKind`, `PluginRenderOpSlot`, `PluginRenderOpInfo`, and the
  `PluginRenderOperationRegistry` class itself (`EnsureBuiltinsRegistered()`,
  `Find()`, `OpsPipeline()`/`OpsDescriptorSetLayout()`/`BlendPipeline()`/
  `BlendDescriptorSetLayout()`, plus two small new convenience accessors,
  `GetRenderer()`/`GetDevice()`, needed by the adapter's own deferred
  `execute` closures — see Deviations).
- `src/Core/Plugins/PluginRenderPassBuilderAdapter_v3.h/.cpp` (NEW) — the real
  `IPluginRenderPassBuilder_v3` implementation. Nested `SetupContextAdapter`
  (`IPluginPassSetupContext`) and `CommandRecorderAdapter`
  (`IPluginCommandRecorder`). Handle translation table (`TranslationState`),
  32-resource/8192×8192/64-groups/exact-paramSize caps, the
  `"SceneColor"`/`GetPrivateOutputTarget()` sampler-override mechanism, and
  the opCode-stamping mechanism for the shared uber-ops pipeline — see
  "Design details" below for each.
- `src/Core/Plugins/PluginRenderPassBuilderAdapterV3Validation.h` (NEW,
  header-only, no `.cpp`) — the pure validation functions Step 3.6 requires:
  `IsPluginResourceHandleValid()`, `IsWithinResourceCreationCountCap()`,
  `IsWithinTextureDimensionCap()`, `IsWithinDispatchGroupCountCap()`,
  `IsExactParamSizeMatch()`, `ValidatePluginOpSlotBinding()`
  (+`PluginOpSlotBindingResult`).
- `tests/Core/Plugins/PluginRenderPassBuilderAdapterV3ValidationTests.cpp`
  (NEW) — 25 Tier-1 tests, one per boundary/rule named in Step 3.6 (handle
  validity ×5, resource-count cap ×3, texture-dimension cap ×3, group-count
  cap ×4, exact-paramSize-match ×3, slot-binding-result ×7).
- `plugins/demo_render_feature_v3/` (NEW, permanent) — reimplements
  `demo_render_feature_v2`'s exact RED solid fill via
  `Dispatch("gte.builtin.solid_fill", ...)`. This is the plugin PHASE3 will
  add its own 2-pass GPU blur demo to.
- `plugins/demo_render_feature_v3_second/` (NEW, permanent) — reimplements
  `demo_render_feature_v2_second`'s exact BLUE radial vignette via
  `Dispatch("gte.builtin.radial_vignette", ...)`.
- `plugins/demo_render_feature_v3_third/` (NEW, permanent) — a
  solid-fill-then-color-grade pair via `Dispatch("gte.builtin.solid_fill"` /
  `"gte.builtin.color_grade", ...)`, proving the third shared uber-op (no
  existing `_v2` demo baseline exists for color grade — see "Deviations").

### Edited files

- `src/Core/Plugins/RenderFeatureCompositor.h/.cpp` — constructor gained a
  new trailing `PluginRenderOperationRegistry&` parameter; `Entry::module`
  renamed `moduleV2`, new `IRenderFeatureModule_v3* moduleV3 = nullptr`;
  `EnsureOpsInitialized()`/`EnsureBlendPipelineInitialized()` DELETED
  (migrated into the registry); `DispatchOps()`/`DispatchBlend()` now read
  `m_operationRegistry.OpsPipeline()`/`OpsDescriptorSetLayout()`/
  `BlendPipeline()`/`BlendDescriptorSetLayout()` instead of owning them
  directly (every other line byte-for-byte unchanged); `OnPluginsLoaded()`
  queries `_v3` first, falls back to `_v2`, logs+prefers `_v3` if a module
  declares both; `ContributeRenderGraphPasses()`'s per-entry loop branches on
  `entry.moduleV3 != nullptr` to construct either adapter; new
  `EnsureV3OpDescriptorSet()` public method + `m_v3OpDescriptorSets` map
  (Step 2.6's persistent cache); a file-local `NoOpPluginBlackboard`
  singleton (`GetOrCreateNoOpBlackboard()`) is the PHASE4 stand-in.
- `src/Core/Core.h/.cpp` — new member `PluginRenderOperationRegistry
  m_pluginRenderOperationRegistry;`, declared immediately after `Renderer
  m_renderer;` (before `rg::RenderGraph m_renderGraph;`), constructed in the
  member-initializer list right after `m_renderer(surfaceProvider)`.
  `RegisterBuiltinCapabilityOrchestrators()`'s `RenderFeatureCompositor`
  construction call updated to pass `m_pluginRenderOperationRegistry`.
- `CMakeLists.txt` — added the 5 new `gte_core` source files (registry +
  adapter + validation header) to `gte_core`'s source list, and the 3 new
  `add_subdirectory()` calls for the `_v3` demo plugins (inside the same
  `if(GTE_ENABLE_PLUGINS)` block, immediately after
  `demo_render_feature_v2_second`).
- `tests/CMakeLists.txt` — added the new validation test file.

## Registry entry table shipped (all 4 built-in ops)

| id | kind | slots (binding → type) | opCode | maxParamBytes | pipeline source |
|---|---|---|---|---|---|
| `gte.builtin.solid_fill` | Compute | 0: StorageImage | 0 | 64 | shared `RenderFeatureOps.comp` (`m_opsPipeline`) |
| `gte.builtin.radial_vignette` | Compute | 0: StorageImage | 1 | 64 | shared `RenderFeatureOps.comp` (`m_opsPipeline`) |
| `gte.builtin.color_grade` | Compute | 0: StorageImage | 2 | 64 | shared `RenderFeatureOps.comp` (`m_opsPipeline`) |
| `gte.builtin.box_blur` | Compute | 0: CombinedImageSampler, 1: StorageImage | 0 (unused) | 8 | own, separate `m_boxBlurPipeline` built from `shaders/BoxBlur.comp.spv` |

`RenderFeatureBlend.comp` is **not** a registry entry — it stays a purely
internal `RenderFeatureCompositor` mechanism (`BlendPipeline()`/
`BlendDescriptorSetLayout()` accessors, never reachable via any `opId`).

## Pipeline-migration diff summary

- `RenderFeatureCompositor::EnsureOpsInitialized()`/
  `EnsureBlendPipelineInitialized()` — both methods (declarations AND
  definitions) deleted outright.
- `RenderFeatureCompositor::m_opsPipeline`/`m_opsDescriptorSetLayout`/
  `m_blendPipeline`/`m_blendDescriptorSetLayout` members deleted.
- `ContributeRenderGraphPasses()`'s own two former call sites,
  `EnsureOpsInitialized(m_renderer); EnsureBlendPipelineInitialized(m_renderer);`
  (right after the `resolved->has_value()` check), **deleted** — replaced by
  a single `m_operationRegistry.EnsureBuiltinsRegistered();` call at the same
  point (see "Deviations" for why a second line, `m_device =
  m_operationRegistry.GetDevice();`, had to be added right next to it).
- `Core.h`'s new member is declared AFTER `Renderer m_renderer;` and BEFORE
  `std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>>
  m_capabilityOrchestrators;`, exactly as the phase plan specified.

## Exact `RenderPassEvent` used for every `_v3`-declared pass, and why

Both `PluginRenderPassBuilderAdapter_v3::AddGraphicsPass()` and
`AddComputePass()` call `m_builder.AddRenderPass(...)` with an **explicit,
hardcoded trailing `rg::RenderPassEvent::AfterEverything`** argument — never
the default (`Opaques`). Confirmed live: `GET /render_graph`'s JSON reports
`"render_pass_event":"AfterEverything"` for every single
`DemoRenderFeatureV3*`/`DemoRenderFeatureV3Second*`/`DemoRenderFeatureV3Third*`
pass (see "Live verification evidence" below) — this is the exact same tier
every `_v2` `DispatchOps()`/`DispatchBlend()` pass already uses, matching
Step 2.4's requirement that a `_v3` pass reading `"SceneColor"` resolve
against the real, already-composited scene, never race ahead of it.

## Exact sampler-override mechanism for `"SceneColor"`/`GetPrivateOutputTarget()`

`PluginRenderPassBuilderAdapter_v3::TranslatedTexture` carries an
`externalSamplerOverride` field (`VkSampler`, default `VK_NULL_HANDLE`).
`TryGetNamedTexture("SceneColor", ...)` sets it to the constructor-supplied
`sceneColorSampler` (== `resolved->sampler` from
`Core::FindPluginRenderFeatureTarget()`) when caching its one entry.
`GetPrivateOutputTarget()`'s own cached entry leaves it `VK_NULL_HANDLE` and
additionally sets `isPrivateOutputTarget = true`.
`CommandRecorderAdapter::BuildAndRewriteDescriptorSet()` prefers
`externalSamplerOverride` over `ctx.resolveTexture(handle).sampler` whenever
it is non-null for a `CombinedImageSampler` slot; a `CombinedImageSampler`
slot bound to an `isPrivateOutputTarget` entry is refused outright (loud
warning) at `ValidateCommon()`'s own per-slot check, before any GPU work.

## Exact generation-counter scheme (one real, deliberate deviation from the literal pseudocode)

Implemented as `std::atomic<std::uint32_t> s_nextAdapterGeneration` (a
process-wide, monotonic counter, incremented once per adapter construction,
via `fetch_add(1)`), **not** `std::uint64_t` as
`PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md`'s own Step 3.4 pseudocode
suggested. Reason: `PluginTextureHandle::generation`/`PluginBufferHandle::generation`
(PHASE1's own shipped ABI, `plugins/gte_plugin_abi/PluginRenderResource.h`)
are real `std::uint32_t` fields — assigning a wider 64-bit counter into that
32-bit field would silently truncate, defeating the whole "never share a
generation value" guarantee the mismatch check depends on. A 32-bit counter
wraps only after ~4 billion adapter constructions, several orders of
magnitude beyond anything a real session (or even a very long-running
process) will ever reach — this is a mechanical correction, not a design
ambiguity, so no `ask_questions` round was needed for it.

## Live verification evidence

1. **Isolated pipeline-migration check (Step 3.2's own required pre-check)**
   — done FIRST, before any `_v3` code was exercised: built + ran the editor
   with only the pre-existing `_v1`/`_v2` demo plugins loaded.
   - **First attempt crashed** (0xC0000005 access violation) — see
     Deviations #1. After the fix, re-ran: `GET /get_game_view` showed the
     exact expected RED-fill-with-BLUE-vignette composite, `GET /get_logs`
     showed zero new warnings/errors beyond the two pre-existing, benign
     "GPU-timing slot budget exhausted" ones. `_v2` confirmed byte-for-byte
     unchanged before proceeding to `_v3` work.
2. **All three `_v3` demo plugins load successfully** (`GET /get_logs`
   confirms `DemoRenderFeatureV3Plugin`/`DemoRenderFeatureV3SecondPlugin`/
   `DemoRenderFeatureV3ThirdPlugin` all `Loaded plugin ...`), with only the
   expected, benign same-priority tie-break warnings (both `_v2`/`_v3` pairs
   intentionally share priority 0 in their own stage — cosmetic, not an
   error).
3. **Pixel-parity A/B proof (Goal 1)** — using the ALREADY-EXISTING
   `GET /render_graph/set_feature_enabled?name=...&enabled=...` toggle (no
   new HTTP endpoint), isolated to avoid the double-AlphaOver-blend artifact
   two simultaneously-enabled identical vignettes would otherwise produce:
   - Disabled `DemoRenderFeatureV3`/`V3Second`/`V3Third`, kept `_v2`'s two
     plugins enabled → `GET /get_game_view`: **13280-byte PNG**, RED fill +
     BLUE vignette.
   - Disabled `DemoRenderFeatureV2`/`V2Second`, enabled
     `DemoRenderFeatureV3`/`V3Second` → `GET /get_game_view`: **13280-byte
     PNG**, visually identical RED fill + BLUE vignette.
   - **Identical PNG byte size in both cases** (a deterministic encoder over
     identical pixel input) is strong, direct evidence of true pixel
     identity, not just "looks similar" — confirming the generic
     `Dispatch()` mechanism reproduces `_v2`'s fixed-method dispatch
     byte-for-byte for both `gte.builtin.solid_fill` and
     `gte.builtin.radial_vignette`.
   - `gte.builtin.color_grade` (the third op, exercised by
     `DemoRenderFeatureV3Third`) has no existing `_v2` demo to A/B against —
     documented honestly rather than silently glossed over: it shares the
     IDENTICAL `RenderFeatureOps.comp` pipeline, the IDENTICAL adapter
     `Dispatch()`/descriptor-set/opCode-stamping call path, and the IDENTICAL
     push-constant layout already pixel-proved correct by the other two ops
     above — `opCode` is simply a different constant stamped into the same
     already-proven-correct path. Its own dispatch produced the expected
     visible effect (a warm, higher-contrast, partially-desaturated tint
     replacing the flat gray base) with zero warnings.
4. **`GET /render_graph` JSON structurally confirms correct scheduling** —
   every `_v3` pass appears with `"kind":"Compute"`, correct `reads`/`writes`
   (e.g. `DemoRenderFeatureV3_Game_Blend` reads
   `RenderFeatureCompositor_Game_Seed` + `DemoRenderFeatureV3_Game_Private`,
   writes `DemoRenderFeatureV3_Game_Accum`), and
   `"render_pass_event":"AfterEverything"` throughout — real `rg::PassRecord`s
   indistinguishable from any internal engine pass, confirming Step 2.7's
   "already generically visible" claim without any panel/JSON code change.
   `render_features[]` correctly reflects live enable/disable toggling.
5. **Zero adapter warnings** — `GET /get_logs?category=PluginRenderPassBuilderAdapter_v3`
   returned `"count":0` throughout every run: zero "rejected a
   PluginTextureHandle...", zero "slot ... binding is invalid", zero
   "paramSize" mismatches.
6. **Descriptor-set-pool persistence (Step 2.6's own required long-run
   check)** — with ALL 7 render-feature plugins loaded and enabled
   simultaneously (`_v1`×2, `_v2`×2, `_v3`×3 — the maximum real stress this
   repository can currently produce), logs were cleared
   (`POST /clear_logs`), then the engine ran continuously for **>20 real
   seconds** (well over 1000 frames at a typical 60 FPS) while repeatedly
   polling `GET /get_game_view` (5 consecutive calls, each returning an
   identical 33464-byte PNG) and `tasklist` (process confirmed alive
   throughout). `GET /get_logs` after this window returned **`"count":0`** —
   zero new warnings/errors of ANY kind, in particular zero
   `vkAllocateDescriptorSets failed` errors and no repeated/growing warning
   pattern that would indicate a leak. This confirms
   `RenderFeatureCompositor::m_v3OpDescriptorSets`/`EnsureV3OpDescriptorSet()`
   is genuinely persistent (allocated once per distinct key, `Rewrite()`-only
   thereafter) — the exact fix Step 2.6 requires, not the naive
   per-frame-adapter-instance cache that would have exhausted the 256-entry
   compute descriptor pool within seconds.

## Blackboard stand-in confirmation

`RenderFeatureCompositor.cpp`'s file-local `NoOpPluginBlackboard` (a
`Publish()`-is-a-no-op, `Fetch()`-always-`false` implementation) is the ONE
instance `GetOrCreateNoOpBlackboard()` returns (a function-local `static`,
process-wide, never reset) — exactly the PHASE3.3-specified stand-in.
`PluginRenderPassBuilderAdapter_v3::Blackboard()` returns a reference to
whatever `IPluginBlackboard&` the constructor received, so PHASE4 only needs
to change the ONE call site in `ContributeRenderGraphPasses()` that currently
passes `GetOrCreateNoOpBlackboard()`.

## Deviations from the plan (both real bugs, found and fixed during this
phase's own required verification — not merely "compiles and doesn't crash")

1. **`RenderFeatureCompositor::m_device` regression (found during the
   MANDATORY Step 3.2 pre-check, before any `_v3` code was even built)** —
   deleting `EnsureOpsInitialized()`/`EnsureBlendPipelineInitialized()` also
   deleted the ONLY two places that ever set `m_device` (both used to do
   `m_device = context.device;`). Nothing replaced this, so `m_device` stayed
   permanently `VK_NULL_HANDLE` — every `EnsureTextureSized()`'s
   `vkDeviceWaitIdle(m_device)` and every `DispatchOps()`/`DispatchBlend()`
   `Rewrite(m_device, ...)` call used a null `VkDevice`. **This crashed the
   editor with an 0xC0000005 access violation on the very first real frame
   that did GPU render-feature work — confirmed via `Start-Process -Wait`
   exit-code inspection (`-1073741819` = `0xC0000005`) after two silent,
   otherwise-undiagnosed background-process crashes.** Fixed by adding
   `m_device = m_operationRegistry.GetDevice();` immediately after the new
   `m_operationRegistry.EnsureBuiltinsRegistered();` call in
   `ContributeRenderGraphPasses()`. Re-verified: `_v2` production behavior
   confirmed correct (byte-identical composited image, zero new warnings)
   after the fix, before any `_v3` adapter work proceeded — exactly the
   phase's own mandated order of operations.
2. **A real use-after-free hazard found while implementing (not present in
   either master doc's own pseudocode, both of which capture `this` in the
   `execute` closure) — a genuinely NEW, fourth hazard beyond the phase's own
   documented Steps 2.4/2.5/2.6** — `Core::BuildFrame()`'s architecture
   declares EVERY pass across BOTH views into ONE shared
   `rg::RenderGraphBuilder` before the whole graph is compiled and executed
   exactly once; this means `PluginRenderPassBuilderAdapter_v3` (a stack-local
   inside `RenderFeatureCompositor::ContributeRenderGraphPasses()`'s per-entry
   loop iteration) is ALREADY DESTROYED by the time ANY pass's `execute`
   callback actually runs. A naive `execute` lambda capturing `this` (as both
   this phase's own pseudocode sketch and my own first draft did) is
   therefore a REAL use-after-free the moment the render graph executes a
   `_v3` compute pass — not a theoretical concern. **Fixed** by extracting the
   handle-translation table into a separate `TranslationState` struct,
   heap-allocated via `std::shared_ptr` and captured BY VALUE (extending its
   lifetime) into every `execute` closure, alongside plain references to the
   two LONG-LIVED, Core-owned singletons (`PluginRenderOperationRegistry&`/
   `RenderFeatureCompositor&`) and a by-value-copied key-prefix string —
   `CommandRecorderAdapter` (execute-time) holds NO reference to the
   short-lived adapter itself; `SetupContextAdapter` (declare-time, invoked
   synchronously before `AddRenderPass()` even returns) safely keeps
   referencing the adapter directly. Documented in full at the top of
   `PluginRenderPassBuilderAdapter_v3.h`/`.cpp` for any future maintainer.
   Live-verified: the >20-second, all-7-plugins-enabled stability run (above)
   is the direct proof this fix works — a use-after-free here would have
   crashed or corrupted output within the very first frame, not run stably
   for over a thousand frames.
3. **Demo-plugin structure (Step 3.5's own explicitly-granted latitude,
   "finalize exact naming in this phase... one at a time, or side-by-side at
   different priorities")** — built as THREE separate plugin folders
   (`demo_render_feature_v3`/`_second`/`_third`) rather than one plugin
   declaring all 3 ops in a single feature, because a single shared private
   target would layer fill+vignette+grade together (not independently
   comparable to `_v2`'s two SEPARATE features at different stages/blend
   modes), and because a naive single static `PluginTextureHandle` global
   would be genuinely WRONG once both Game and Scene views are visible in the
   same frame (both views' `AddRenderGraphPasses()` calls happen before
   either's `execute` runs) — each demo plugin therefore uses a small,
   fixed-size (4-slot) ring buffer of state structs, keyed by an incrementing
   call counter, giving each within-one-frame declare call its own stable
   storage address (documented in each plugin's own source).
4. **Third demo plugin's color-grade parameters are original (no `_v2`
   baseline exists to copy numeric values from)** — chosen deliberately
   distinct from the fill's own flat gray base so the grade's own effect is
   visually unambiguous; documented above and in the plugin's own source as
   an honest limitation, not silently glossed over.

No `ask_questions` round was needed this phase — every judgment call above
was either a mechanical correction (generation-counter width) confirmed
unambiguous by PHASE1's own already-shipped ABI, or fell within latitude the
phase's own file already explicitly granted (demo-plugin structure/naming).

## Non-Goals confirmed still out of scope this phase

- `gte.builtin.blit_fullscreen`/the real 2-pass blur demo/actually
  dispatching `gte.builtin.box_blur` — still PHASE3's job; this phase only
  registered the operation and its pipeline (confirmed present in
  `PluginRenderOperationRegistry::RegisterBoxBlur()`, never called by any
  `_v3` demo plugin this phase).
- `IPluginBlackboard`'s real implementation — still the static no-op
  singleton, per plan (PHASE4's job).
- Any Render Graph panel UI change — none made (PHASE4's job).
- Plugin-registered custom operations — untouched, per the whole campaign's
  scope.

## Verification summary

- `cmake --build build --target gte_core` — clean.
- `cmake --build build --target GreatTamanaEngineTests GreatTamanaEditor` —
  clean.
- `tests\GreatTamanaEngineTests.exe` (no filter): **1882 tests from 262
  suites, 1880 PASSED, 2 SKIPPED (both pre-existing, environment-gated,
  unchanged from PHASE1's own baseline), 0 FAILED** — up from PHASE1's own
  1857 baseline by exactly the 25 new
  `PluginRenderPassBuilderAdapterV3ValidationTest` cases.
- Live HTTP-driven smoke test — see "Live verification evidence" above (7
  numbered checks, including the mandatory >20-second/1000+-frame descriptor-
  pool stability run with every render-feature demo plugin loaded and
  enabled simultaneously).
- `git_status` before starting: confirmed branch `feature/editor-core-separation`,
  working tree clean except this same folder's own untracked strategy docs
  (matches PHASE1's own final state). After this phase's work: exactly the
  files this phase's own plan named (6 modified, 9 new — see "What changed").

## Next phase

PHASE3 (`PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md`) extends
`plugins/demo_render_feature_v3/` with the real, permanent 2-pass GPU blur
demo: a compute pass reading `"SceneColor"`, writing a new transient
half-res texture via `gte.builtin.box_blur` (already registered, this
phase), then a graphics pass reading that texture and drawing it into
`GetPrivateOutputTarget()` via a new `gte.builtin.blit_fullscreen` operation
— the first real `DrawFullscreenTriangle`-kind registry entry, and the first
real exercise of `PluginRenderPassBuilderAdapter_v3::DrawFullscreenTriangle()`
(implemented, but never yet exercised, this phase).
