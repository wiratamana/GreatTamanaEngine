# PHASE0 — MASTER STRATEGY: "Project Assembly On-Screen Render Feature Compositing" — BIG-STEP 1 (Core Engine Wiring)

Campaign folder: `task_manager/editor-core-separation-23/`
Branch: `feature/editor-core-separation` (stay on it, do not create a new branch)
Project root: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

Source design documents (read-only, in a SEPARATE repo/folder — never modify these):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-4\`
  - `DESIGN_REQUIREMENTS_ONSCREEN_RENDER_PASS_COMPOSITING_2026-09-29.txt` — the
    "why"/goal/FR/TR document (read Section 1, "Headline Finding", first — it
    is load-bearing for everything below).
  - `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`
    — the "how", Steps 1-9. **This campaign (`editor-core-separation-23`) is a
    faithful, phased execution plan for THIS file specifically.** Every phase
    below cites this file's own Step numbers; re-read the cited Step in full
    before starting that phase — this master file summarizes, it does not
    replace, that source document.
  - `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
    — BIG-STEP 2, the Editor "Create -> Screen Post-Process Pass" menu item
    and scaffold template. **OUT OF SCOPE for this entire campaign.** BIG-STEP
    2 is HARD-BLOCKED until every checkbox in Step 9 of the Core Engine Wiring
    document (mirrored in PHASE7 below) is independently, live-confirmed
    green. Do not open that file's own content as a reason to add anything to
    any phase below — if a phase's own implementer thinks something from
    BIG-STEP 2 belongs here, that is a scope-creep signal; use `ask_questions`
    instead of proceeding.

This file is the ORCHESTRATOR. Every child phase file (`PHASE1_*.md` ..
`PHASE7_*.md`) must be read together with this file. Every child phase must,
at its own start, re-read this file plus the completion report of the phase
immediately before it (`PHASEn-1_COMPLETION_REPORT.md`, written by that
phase) — there might be a clue for continuation, a discovered root cause, or
a locked decision that changes how the next phase must be carried out.

---

## Step 1: The Goal (Where are we going?)

Give a Project Assembly's own C++ code (a `_Game.dll`, calling a new
`Core::RegisterProjectRenderFeature()` from its own `RegisterProject()` entry
point) a way to register an on-screen render feature that is:

1. Handed the CURRENT frame's real Game View / Scene View private target and
   extent — through the SAME already-proven, hazard-free
   `RenderFeatureCompositor` blend chain `gte_plugin_abi` plugins already use
   today — with **zero** manual `ImportTexture()`/handle-aliasing code of its
   own.
2. Genuinely visible on screen — confirmed via a real Editor Game View
   screenshot (`GET /get_game_view`), not merely `GET /render_graph`'s debug
   JSON.
3. Safe across an unbounded number of register/rename/unregister/re-register
   cycles over one long, live Editor session (a Project Assembly is edited,
   recompiled, and hot-reloaded repeatedly — this is fundamentally different
   from a `gte_plugin_abi` plugin's "load once, never again" lifecycle), with
   NO unbounded growth of GPU descriptor-set consumption from the shared,
   fixed-256-set compute descriptor pool (`GpuResourceFactory.cpp`).
4. Safe across a hot-reload cycle: no dangling `std::function` ever points
   into an unloaded `.dll`'s own code after teardown.

This is BIG-STEP 1 of 2. When every item in Step 9's checklist (PHASE7 below)
is green, BIG-STEP 2 (Editor "Create -> Screen Post-Process Pass" menu item)
becomes unblocked — a SEPARATE, future campaign, not this one.

## Step 2: The Situation (Where are we now?)

`src/Core/Plugins/RenderFeatureCompositor.cpp` already exists, already works,
already proves the hazard-free on-screen blend chain for `gte_plugin_abi`'s
`IRenderFeatureModule_v2`/`_v3` plugins (`editor-core-separation-6` through
`-9` campaigns). This campaign does NOT re-invent that mechanism — it exposes
a third, additive "module kind" into the exact same `Entry`/
`ContributeRenderGraphPasses()` machinery, sized correctly for a Project
Assembly's genuinely different usage pattern (registered/renamed/deleted/
re-registered many times per session, vs. a plugin's "scanned once, forever"
lifetime).

Every file this campaign touches was RE-READ FRESH, live, on 2026-09-29,
immediately before these phase files were written, and matches the design
document's own citations exactly — **no drift found**:

  - `src/Core/Plugins/RenderFeatureCompositor.h` — `Entry` has exactly 4
    fields today (`moduleV2`, `moduleV3`, `descriptor`, `enabledOverride`);
    `OnPluginsLoaded()`'s per-module branch and `ContributeRenderGraphPasses()`'s
    per-entry loop (`if (entry.moduleV3 != nullptr) { ... } else { ... }`,
    `RenderFeatureCompositor.cpp` lines 641-649) are still a plain two-way
    branch; `EnsurePrivateTargetState()`/`EnsureBlendStageDescriptorOnly()`/
    `EnsureBlendStageState()` are still keyed purely by an interned
    `const char*` with no other identity check (lines 408-446).
  - `plugins/gte_plugin_abi/RenderFeatureDescriptor.h` — `RenderFeatureStage`/
    `RenderFeatureBlendMode`/`GtePluginRenderFeatureDescriptor` are declared
    directly in `namespace gte` (not `gte::rg`); `name` is a fixed `char[64]`,
    filled only through `MakeRenderFeatureDescriptor()`'s bounded, silently-
    truncating copy helper; this header's own includes are still exactly
    `<cstdint>`/`<cstddef>`.
  - `src/Core/Core.h` — `RegisterProjectRenderPassProvider()`/
    `UnregisterProjectRenderPassProvider()` sit at lines 357/363, and `Core.h`
    still only forward-declares `class RenderFeatureCompositor;` (line ~60 of
    that header's own `RenderFeatureCompositor.h`, confirmed from the OTHER
    side of the same discipline) — never a full `#include`.
    `GetRenderFeatureCompositor()` (line 423) is set unconditionally, at
    construction time, by `RegisterBuiltinCapabilityOrchestrators()` — it is
    **never null** after a real `Core` object exists (confirmed: no
    `#if`/feature-flag gates this orchestrator's construction). This matters
    directly for PHASE2/PHASE3's own Tier-1 tests below.
  - `src/Core/Plugins/ProjectAssemblyRegistrationLedger.h/.cpp` — `Entry` has
    exactly 3 vectors today (`renderPassNames`, `panelNames`,
    `componentTypeNames`); `UnregisterEverythingFor()`'s real teardown order
    is component types, then panels, then render-pass providers (all reverse-
    registration-order), confirmed from the real `.cpp` body.
  - `src/Core/EditorCapabilities.h` — `IHotReloadDebugCapability::LedgerEntry`
    (line 197) mirrors those same 3 field names exactly; its real
    implementation, `EditorHotReloadDebugCapability::GetLedgerEntry()`
    (`src/Editor/EditorHotReloadDebugCapability.cpp` line 29), is a trivial
    1-to-1 field copy off `ProjectAssemblyRegistrationLedger::PeekEntry()`.
  - `src/Renderer/GpuResourceFactory.cpp` — the compute descriptor pool's
    `maxSets` is `kMaxComputeDescriptorSets = 256` (line 84), created WITHOUT
    `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` — confirmed, an
    individual `VkDescriptorSet` from it is never returned piecemeal.
    `src/Renderer/ComputeDescriptorSet.h` documents this explicitly and
    exposes `Rewrite()` as the one safe way to repoint an already-allocated
    set at a different physical resource.
  - **A proven, reusable Tier-1 test fixture already exists** for exactly
    this shape of problem: `tests/Core/Plugins/ProjectAssemblyRegistrationLedgerTests.cpp`
    constructs a REAL `gte::Core` headlessly (`tests/Fakes/HeadlessSurfaceProvider.h`
    + a trivial `NoopHostServices`, `GTEST_SKIP()`-ing if this machine's
    Vulkan driver doesn't support `VK_EXT_headless_surface`) and exercises
    real ledger/registry round-trips against it. Since
    `Core::GetRenderFeatureCompositor()` is unconditionally non-null on any
    such real `Core`, **every phase below that needs to test
    `RenderFeatureCompositor::RegisterProjectFeature()`/
    `Core::RegisterProjectRenderFeature()` reuses this EXACT fixture** — no
    stub/fake `Renderer` needs to be invented; a real, live, headless GPU
    device backs these tests, exactly like the ledger's own tests already do.

## Step 3: The Plan (detailed strategy)

This campaign is split into 7 implementation phases, each its own `.md` file
in this same folder, plus this PHASE0 orchestrator.

| Phase | File | Design doc Step(s) | One-line summary |
|---|---|---|---|
| 1 | `PHASE1_PROJECT_RENDER_FEATURE_CALLBACK_HEADER_AND_ENTRY_THIRD_KIND.md` | Step 2 | New `ProjectRenderFeatureCallback.h`; `RenderFeatureCompositor::Entry` gains `projectCallback`/`projectFeatureSlot`; the two `moduleV3`-branch call sites become real three-way branches. |
| 2 | `PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md` | Step 3 | `RenderFeatureCompositor::RegisterProjectFeature()`/`UnregisterProjectFeature()`; the bounded, reusable `kMaxConcurrentProjectRenderFeatures`-sized GPU-state slot free-list; `gpuStateKey` resolution in `ContributeRenderGraphPasses()`; a new `RenderFeatureDebugEntry::isProjectFeature` field (mirroring `isV3`) so a Project Assembly feature is structurally distinguishable from a plugin feature in `GET /render_graph`/the "Render Graph" panel. |
| 3 | `PHASE3_CORE_REGISTER_PROJECT_RENDER_FEATURE_API.md` | Step 4 | `Core::RegisterProjectRenderFeature()`/`UnregisterProjectRenderFeature()`; the 63-byte `debugName` length-guard; `Core.h`'s two new includes. |
| 4 | `PHASE4_HOT_RELOAD_LEDGER_TEARDOWN_WIRING.md` | Step 5 | `ProjectAssemblyRegistrationLedger` gains `renderFeatureNames`/`RecordRenderFeature()`; `UnregisterEverythingFor()` gains the new teardown loop, correctly ORDERED before `renderPassNames`; `IHotReloadDebugCapability::LedgerEntry` + `EditorHotReloadDebugCapability::GetLedgerEntry()` mirror it. |
| 5 | `PHASE5_ORDERING_SAFETY_NET_AND_LIFETIME_CONFIRMATION.md` | Steps 6-7 | Main-thread-only debug-build assertions on the two new mutators; a Tier-1 regression test proving a `projectCallback`-declared pass tagged `RenderPassEvent::AfterEverything` reports zero `DetectRenderPassEventContradictions()` findings. |
| 6 | `PHASE6_HAND_WIRED_DEMO_PROJECT_AND_LIVE_VERIFICATION.md` | Step 8 (live items) | A real, throwaway, hand-wired Project Assembly `.cpp` that calls `Core::RegisterProjectRenderFeature()` with a bare clear-color pass; full HTTP-driven live proof (visible on screen, survives hot-reload, survives `kMaxConcurrentProjectRenderFeatures + 4` rename cycles with bounded descriptor-set consumption). |
| 7 | `PHASE7_FULL_REGRESSION_DOCS_AND_ENTRY_GATE_CLOSEOUT.md` | Step 8 (Tier-1 suite) + Step 9 | Full clean incremental build + full `ctest` regression; `docs/conventions/` write-up; final, explicit tick-through of every Step 9 checkbox — the literal Entry Gate for BIG-STEP 2. |

### 3.1 — Locked Decisions (apply to every phase, do not re-litigate)

1. **Never use `printf`/`std::cout`/`fprintf`/`OutputDebugString` for any NEW
   diagnostic or permanent code in this campaign.** Always
   `GTE_LOG_DEBUG`/`_INFO`/`_WARNING`/`_ERROR` (`src/Core/Logging.h`),
   retrieved via `GET /get_logs`.
2. **Never run a full clean build or full `ctest` regression pass except in
   PHASE7.** Every other phase uses an INCREMENTAL build (`cmake --build
   build`) as its compile-check gate, plus a TARGETED `ctest -R <filter>` for
   whatever Tier-1 test(s) that phase itself added, plus live, HTTP-driven
   checks (`run_app_background`/`gte_send_request`/`stop_app_background`) for
   PHASE6's own behavior verification.
3. **A `debugName`/human-typed name is NEVER silently truncated anywhere this
   campaign introduces.** `GtePluginRenderFeatureDescriptor::name`'s existing
   `MakeRenderFeatureDescriptor()` helper still silently truncates (unchanged,
   out of scope) — but `Core::RegisterProjectRenderFeature()` (PHASE3) must
   reject (not truncate) anything that would not fit, loudly, before ever
   calling that helper. This is the FIRST call site in this engine building
   that string from free-form, un-length-checked input — see PHASE3 for the
   full reasoning.
4. **A Project Assembly's own render-feature GPU state is NEVER keyed by its
   human-typed `descriptor.name`.** It is keyed by a small, fixed-size,
   reusable slot index (`kMaxConcurrentProjectRenderFeatures = 16`, a named
   constant, never an inlined literal) — see PHASE2 for the full "why" (an
   unbounded rename-cycle session would otherwise permanently starve the
   shared 256-slot compute descriptor pool). `descriptor.name` itself is still
   used, unchanged, everywhere a human needs to recognize the feature
   (`FindEntryByName()`, `DebugSnapshot()`, every log line).
5. **The new callback type (`ProjectRenderFeatureCallback`) lives in its own,
   brand-new, free-standing header
   (`src/Core/Plugins/ProjectRenderFeatureCallback.h`) — never nested inside
   `RenderFeatureCompositor`.** `Core.h` only ever forward-declares
   `RenderFeatureCompositor` and must keep doing so; a type nested inside an
   incomplete class cannot be named from `Core.h`. PHASE1 builds this.
6. **Every phase that changes `gte_core`-tier logic must add or extend a
   Tier-1 test**, reusing the `HeadlessSurfaceProvider` + real-`Core` fixture
   pattern already proven in `ProjectAssemblyRegistrationLedgerTests.cpp`
   (Step 2 above) wherever a real `RenderFeatureCompositor`/`Core` instance is
   needed — never invent a second, parallel fixture style.
7. **Every phase must end with**: an incremental compile check succeeding, a
   targeted `ctest` pass for that phase's own new/changed tests, a `.md`
   completion report (`PHASEn_COMPLETION_REPORT.md`) written into this same
   folder, and a git commit (`git_add` + `git_commit`) covering both the code
   change and the report.
8. **Whenever a phase discovers a genuine design ambiguity or a decision only
   a human can make, it MUST use `ask_questions` before proceeding** — every
   phase file below calls out its own likely decision points explicitly, but
   an implementer must use `ask_questions` for ANY other genuine ambiguity it
   personally discovers too. Every task an implementation phase itself
   delegates (see point 9) must ALSO be instructed to use `ask_questions` for
   its own ambiguities.
9. **Implementation-phase agents may use `delegate_task` ONLY with
   `position: "next"`, and ONLY to double-check their OWN just-finished, large
   piece of work before writing that phase's completion report** (e.g. PHASE2
   and PHASE6 are the two heaviest phases in this campaign and are the most
   likely candidates). Such a sub-task double-check must NEVER create its own
   separate report file — it reports back inline, and the ORIGINAL phase
   still writes the one `PHASEn_COMPLETION_REPORT.md`. No phase may delegate
   an ENTIRELY DIFFERENT phase's work ahead of schedule, and no phase may use
   `position: "end"`.
10. **`RenderFeatureCompositor`'s existing `_v2`/`_v3` plugin path is NEVER
    modified in behavior** — every change in this campaign is a strictly
    ADDITIVE third module-kind. Any phase whose diff touches existing
    `moduleV2`/`moduleV3` behavior (beyond widening a two-way branch into a
    three-way one with the third arm a true no-op for existing entries) is
    out of scope for that phase — stop and `ask_questions`.
11. **`RenderPassEvent::AfterEverything` is the ONLY tag a `projectCallback`
    entry's own declared pass(es) may use** (Step 7 of the design doc, PHASE5
    below) — this is what keeps this whole on-screen compositing chain
    provably last, unconditionally, matching every other pass this
    compositor itself declares.

### 3.2 — Why this shape (seven phases, not fewer/more)

- PHASE1 and PHASE2 are split because PHASE1 is a small, mechanical,
  low-risk "shape" change (new header, new struct fields, a stubbed third
  branch arm) that must compile and pass a standalone-header Tier-1 test
  BEFORE PHASE2's much heavier, genuinely new logic (the slot free-list, the
  `gpuStateKey` resolution rewrite inside `ContributeRenderGraphPasses()`) is
  attempted — debugging a slot-pool bug is much harder if it is tangled
  together with a fresh compile error in the surrounding scaffolding.
- PHASE3 is separate from PHASE2 because it lives in a DIFFERENT file
  (`Core.h`/`Core.cpp`) with its own, independent hazard (the 63-byte
  truncation-vs-rejection decision) that has nothing to do with
  `RenderFeatureCompositor`'s own internals.
- PHASE4 is separate because it touches a THIRD, independent class
  (`ProjectAssemblyRegistrationLedger`) plus a fourth
  (`EditorHotReloadDebugCapability`) — genuinely different files, and this is
  the single HARD REQUIREMENT the design doc calls out as gating correctness
  (a dangling `std::function` into an unloaded `.dll` crashes the engine) —
  it deserves its own focused phase and its own focused live hot-reload
  proof.
- PHASE5 is its own phase because it is a different KIND of work — safety-net
  confirmation and a regression test for an EXISTING, separate detector
  (`DetectRenderPassEventContradictions()`), not new registration logic.
- PHASE6 is the single most important LIVE phase (the actual on-screen proof,
  the hot-reload proof, the bounded-slot-reuse proof) and is kept separate so
  it can run AFTER every piece of code it needs already compiles and has its
  own Tier-1 coverage — a live smoke test is the wrong place to also be
  debugging a fresh compile error.
- PHASE7 is always last, matching every prior campaign in this codebase's own
  history — full regression + docs + final Step-9 checklist tick-through,
  once, at the end, never spread across phases.

### 3.3 — Definition of Done for the whole campaign

Identical to Step 9 of `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`
— PHASE7 restates the full checklist verbatim and ticks every box with its
own fresh evidence. A full clean incremental build and a full `ctest`
regression pass (100%, test count only ever growing) both succeed, and the
ENTRY GATE FOR BIG-STEP 2 is explicitly, individually confirmed true for
every one of its checkboxes before this campaign is called finished.
