# PHASE1 — Built-In Pass Toggle Registry + `RenderPipeline` Choke Point — COMPLETION REPORT

**Status: DONE.** Implemented exactly what
`PHASE1_BUILTIN_PASS_TOGGLE_REGISTRY_AND_CHOKEPOINT.md` describes, including
Step 3.4b's 2 confirmed early-return guard-line exceptions
(`"AtmosphereComposite"`, `"GpuSkinning"`). No deviations from the plan.

## What changed

### New files

- `src/Renderer/RenderGraph/RenderPassToggleRegistry.h` — `gte::rg::RenderPassToggleState`
  + `gte::rg::RenderPassToggleRegistry` class, exactly matching Step 3.1's
  shown contract verbatim (`NoteDeclaredAndCheckEnabled()`, `SetEnabled()`,
  `IsEnabled()`, `ListAll()`, `static IsDenyListed()`).
- `src/Renderer/RenderGraph/RenderPassToggleRegistry.cpp` — implementation,
  exactly matching Step 3.2's shown body verbatim.
- `tests/Renderer/RenderGraph/RenderPassToggleRegistryTests.cpp` — 8 new
  Tier-1 `TEST(RenderPassToggleRegistryTest, ...)` cases, covering every
  bullet Step 3.6 asks for (brand-new-name auto-discovery, no-duplicate
  across two "frames" + reflecting a `SetEnabled()` change made in between,
  `SetEnabled(false)` then re-declare returns `false`, `"Present"` deny-list
  refusal leaving `IsEnabled("Present")` still `true`, `SetEnabled()` on a
  never-declared name creating an entry with `everDeclaredThisSession ==
  false`, `IsEnabled()` on a totally unknown name returning `true`, and
  `ListAll()`'s lexical sort) — plus one extra defensive case (an empty
  `name` never registers an entry), not explicitly required by Step 3.6 but
  directly protecting the header's own documented "empty name -> true,
  registers nothing" contract.

### `src/Renderer/RenderGraph/RenderPipeline.h`

- `#include "RenderPassToggleRegistry.h"` added at line 53, immediately after
  the existing `#include "RenderGraphTypes.h"`.
- New public `void SetPassToggleRegistry(RenderPassToggleRegistry*) noexcept`
  method added immediately after `SetLegacyViewScopeTranslator()` (lines
  483-491), mirroring its exact "nullptr-by-default, unset = old behavior"
  shape.
- New private member `RenderPassToggleRegistry* m_passToggleRegistry =
  nullptr;` added immediately after `m_legacyViewScopeTranslator` (line 609).
- New choke-point check added inside `DeclareOnePhase()`'s existing flush
  loop (lines 580-591), immediately after `translatedViewScope` is computed
  and immediately before the existing `builder.AddRenderPass(...)` call:
  ```cpp
  if (m_passToggleRegistry != nullptr && desc.debugName != nullptr
      && !m_passToggleRegistry->NoteDeclaredAndCheckEnabled(desc.debugName)) {
      continue; // Disabled - skipped entirely, exactly as if never declared.
  }
  ```

### `src/Core/Core.h`

- New public accessor `rg::RenderPassToggleRegistry&
  GetRenderPassToggleRegistryMutable() noexcept` added immediately after
  `GetGpuDrivenBatchDebugInfo()` (lines 188-199).
- New private member `rg::RenderPassToggleRegistry
  m_renderPassToggleRegistry;` added immediately after
  `m_presentRenderPipeline` (line 459).

### `src/Core/Core.cpp`

- `Core::RegisterOffscreenRenderPipelineProviders()`: added
  `m_offscreenRenderPipeline.SetPassToggleRegistry(&m_renderPassToggleRegistry);`
  at line 385, immediately after the existing
  `SetLegacyViewScopeTranslator(&TranslateLegacyViewScope);` call (line 382).
- `Core::RegisterPresentRenderPipelineProvider()`: added
  `m_presentRenderPipeline.SetPassToggleRegistry(&m_renderPassToggleRegistry);`
  at line 824, immediately after that regime's own
  `SetLegacyViewScopeTranslator(...)` call (line 821).
- **Step 3.4b exception #1 — `"GpuSkinning"` provider lambda** (registered at
  line 400): added the early-return guard at lines 404-414, immediately
  after `GpuSkinningPipelines& pipelines = m_game.GetGpuSkinningPipelines();`
  and immediately BEFORE the existing
  `for (std::size_t i = 0; i < m_gpuSkinningRequestsThisFrame.size(); ++i)`
  loop:
  ```cpp
  if (!m_renderPassToggleRegistry.NoteDeclaredAndCheckEnabled("GpuSkinning")) {
      return;
  }
  ```
- **Step 3.4b exception #2 — `"AtmosphereComposite"` provider lambda**
  (registered at line 753): added the early-return guard at lines 760-769,
  immediately after the existing
  `if (viewData == nullptr || viewData->renderTexture == nullptr) { return; }`
  guard:
  ```cpp
  if (!m_renderPassToggleRegistry.NoteDeclaredAndCheckEnabled("AtmosphereComposite")) {
      return;
  }
  ```
  Confirmed by full read of the lambda body: this correctly short-circuits
  BEFORE `AddAtmosphereCompositePass()` is called, before
  `frame.finalTextureOutputs.push_back(composited)`, and before the
  `frame.blackboard.Publish<rg::TextureHandle>(...)` call — exactly the
  "graceful no-op, existing `!resolved.has_value()` guard downstream already
  handles it" behavior the phase doc describes.

No other line in either of these 2 lambda bodies, or in any of the other 8
`Register(...)` lambda bodies in `Core.cpp`, was touched — confirmed by
`git_status`'s own diff scope check below.

### Build system

- Root `CMakeLists.txt`: added `src/Renderer/RenderGraph/RenderPassToggleRegistry.h`
  and `.cpp` to the `gte_core` source list, immediately after
  `RenderPipeline.cpp` (lines 674-675).
- `tests/CMakeLists.txt`: added `Renderer/RenderGraph/RenderPassToggleRegistryTests.cpp`
  to `GTE_TEST_SOURCES`, immediately after `RenderPipelineTests.cpp` (line
  2190), plus a matching doc-comment block (lines 466-482) describing the new
  test file, mirroring this file's own established per-test-file comment
  convention (not explicitly required by the phase doc, but keeps the file's
  existing self-documenting style consistent).

### `tests/Renderer/RenderGraph/RenderPipelineTests.cpp`

Added exactly the 2 tests Step 3.7 asks for, at the end of the file:
- `RenderPipelineTest.DisabledPassInToggleRegistryIsNeverDeclaredToTheBuilder`
  — registers a trivial `"TestPass"` provider, disables it via
  `registry.SetEnabled("TestPass", false)` BEFORE calling `DeclareInto()`,
  and asserts the compiled graph's own pass list is empty.
- `RenderPipelineTest.WithNoToggleRegistryEverySetPassIsDeclaredNormally` —
  the symmetric default-behavior-unchanged proof: `SetPassToggleRegistry()`
  never called at all, and the pass is declared exactly as before this
  phase.

Every pre-existing test in this file (20 of them) still passes unmodified —
confirmed by the ctest run below.

## Deviations from the plan

**None.** Every file changed is exactly the one Step 3.1-3.6 (plus the CMake
registration in Step 3.5) names, every new function's signature matches the
plan's own shown code verbatim, and the 2 Step 3.4b guard insertion points
match the plan's own described locations exactly (confirmed by reading the
real, current `Core.cpp` body of both lambdas before editing, per the phase
doc's own "get the exact insertion point right" instruction).

One small, intentional addition beyond the letter of the plan: the doc
comment added to `tests/CMakeLists.txt`'s existing per-test-file listing
block (see above) — purely descriptive, zero code/behavior effect, added
only to keep that file's own long-standing self-documenting convention
intact for the new test file too.

## Verification evidence

1. **`git_status` at start**: branch was `feature/editor-core-separation`,
   working tree had only the untracked `task_manager/editor-core-separation-8/`
   folder (this campaign's own strategy files) — clean otherwise, as
   expected for the first implementation phase.

2. **Incremental build** (`cmake -S . -B build` reconfigure to pick up the 2
   new source files, then `cmake --build build --target
   GreatTamanaEngineTests -j 8`): succeeded, only 12 objects rebuilt
   (`RenderPassToggleRegistry.cpp`, `RenderPipeline.cpp`,
   `LegacyRenderFeatureOrchestrator.cpp`, `RenderFeatureCompositor.cpp`,
   `Core.cpp`, `EditorHost.cpp` — all `gte_core`/`gte_editor` translation
   units that transitively include the touched headers — plus the 3 touched/
   new test `.cpp` files) — a genuine incremental build, not a full clean
   one. Also built `GreatTamanaEditor` itself (2 objects: `main.cpp` +
   re-link) to confirm the real production executable — not just the test
   binary — compiles and links cleanly against the changed `Core.h`/`Core.cpp`/
   `RenderPipeline.h`.

3. **Targeted `ctest` run** (`ctest -C Debug -R
   "RenderPassToggleRegistry|RenderPipeline" --output-on-failure`, from
   `build/`): **22/22 tests passed** — the 8 new
   `RenderPassToggleRegistryTest.*` cases, all 20 pre-existing
   `RenderPipelineTest.*`/`RenderPassIdTest.*`/`RenderViewIdTest.*`/
   `RenderPassBlackboardTest.*` cases (unmodified, still green), and the 2
   new `RenderPipelineTest.*` cases from Step 3.7.

4. **`git_status` immediately before this commit**: diff touches exactly —
   `CMakeLists.txt`, `src/Core/Core.cpp`, `src/Core/Core.h`,
   `src/Renderer/RenderGraph/RenderPipeline.h`, `tests/CMakeLists.txt`,
   `tests/Renderer/RenderGraph/RenderPipelineTests.cpp` (all modified), plus
   `src/Renderer/RenderGraph/RenderPassToggleRegistry.h`,
   `src/Renderer/RenderGraph/RenderPassToggleRegistry.cpp`,
   `tests/Renderer/RenderGraph/RenderPassToggleRegistryTests.cpp` (all new/
   untracked) and this campaign's own `task_manager/editor-core-separation-8/`
   folder (untracked, includes this very report) — exactly the file set
   Step 3.5/the phase's own "Verification" section §3 names, nothing else.

No live/HTTP/screenshot-based verification was performed for this phase —
correctly out of scope: this phase adds zero UI or HTTP surface
("What this phase does NOT do"), and nothing in production code calls
`SetEnabled()` yet, so there is no observable behavior to visually confirm
(every built-in pass stays enabled by default, exactly as documented).

## Honest notes for future phases

- The shared-name-across-views consequence PHASE0 documents for
  `"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"` also applies,
  as designed, to `"AtmosphereComposite"`'s own new Step 3.4b guard: since it
  runs once per active view (`ProviderScope::PerActiveView`) and always
  checks the exact same literal string `"AtmosphereComposite"` regardless of
  which view is current, disabling it disables it for BOTH Game View and
  Scene View simultaneously. This is the exact, accepted, intentional
  consequence PHASE0's Step 2.1 already calls out — not a new surprise this
  phase introduces.
- `"GpuSkinning"`'s new guard is a genuinely different kind of switch from
  every other pass this registry manages: it is a single whole-stage on/off
  toggle keyed under the literal name `"GpuSkinning"`, entirely separate
  from the individual per-dispatch `RenderPassDesc` entries this SAME
  provider also pushes (under `request.name`, a different, dynamic string)
  which are independently toggleable one-by-one through the ordinary
  choke-point mechanism (Step 3.3) once PHASE4/PHASE5 exposes a way to call
  `SetEnabled()` on them. Both mechanisms now coexist on the exact same
  provider lambda, confirmed compiling and passing correctly together.
