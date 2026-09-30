# PHASE4 — COMPLETION REPORT: RenderGraph Production Wiring + Editor Startup Install

Campaign: `editor-core-separation-25` ("Core/Editor Separation: PassRecord Debug
Metadata Sink"), Branch: `feature/editor-core-separation`.

## What was read first

- `readme.md`, `AGENTS.md` (full).
- `task_manager/editor-core-separation-25/PHASE0_MASTER_STRATEGY.md` (full).
- `task_manager/editor-core-separation-25/PHASE1_COMPLETION_REPORT.md`,
  `PHASE2_COMPLETION_REPORT.md`, `PHASE3_COMPLETION_REPORT.md` (full) —
  confirmed: PHASE1 shipped `RenderGraphDebugMetadataSink.h`
  (`PassDebugMetadata`, `IPassDebugMetadataSink` = 2 methods,
  `IPassDebugMetadataProvider` = 1 method); PHASE2 shipped
  `src/Editor/FrameDebuggerPassMetadataRecorder.h` implementing both; PHASE3
  removed `category`/`drawKind`/`tags` from `PassRecord`, widened `ViewScope`
  to `std::uint8_t`, wired `RenderGraphBuilder::AddRenderPass()` to the sink,
  and gave `BuildRenderGraphSnapshot()` a new trailing, DEFAULTED
  `metadataLookup` parameter (`const std::function<bool(std::size_t,
  PassDebugMetadata&)>&`) — no clue for continuation beyond "PHASE4 wires
  `RenderGraph` itself + installs the recorder in `EditorHost`" was found.
- `task_manager/editor-core-separation-25/PHASE4_RENDERGRAPH_PRODUCTION_WIRING_AND_EDITOR_INSTALL.md`
  (full — this phase's own plan, with exact worked-example code for every
  file this phase touches).
- `src/Renderer/RenderGraph/RenderGraph.h`/`.cpp` — re-read directly to
  re-confirm current line numbers (they had drifted slightly from the phase
  file's own `~line N` estimates, exactly as PHASE3's own report warned could
  happen) before making any edit.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h` — re-read directly to
  confirm `BuildRenderGraphSnapshot()`'s exact PHASE3-era signature
  (`metadataLookup` trailing, defaulted `{}`) before wiring
  `ExecuteCompiledGraph()`'s new call site against it.
- `src/Editor/EditorHost.h`/`.cpp` — re-read directly to confirm
  `m_renderGraph`'s exact binding (a reference member, bound via the
  constructor's member-initializer list, `EditorHost.cpp` line ~185) and the
  exact constructor-body ordering around `InstallLogSink()`/
  `SetEditorLayerHook()` this phase's plan depends on.

No ambiguity beyond what PHASE0/PHASE4's own file already resolved was found,
so `ask_questions` was not needed for this phase. No sub-task was delegated —
per PHASE0's Locked Decision 8, `delegate_task` self-double-checks are
reserved for a phase's OWN heaviest, riskiest work (PHASE3 already used one);
this phase's own diff is small (4 files, all additive except one), fully
specified by the phase file's own worked-example code, and was independently
verified three separate ways below (incremental build, full targeted `ctest`
re-run, and a live, HTTP-driven capture showing genuinely non-default
`category`/`draw_kind`/`tag_group_label` values) — the delegation rule's own
bar ("large piece of work", "most likely candidate") does not fit this phase.

## What was done

### 1. `src/Renderer/RenderGraph/RenderGraph.h`

- Added `#include "RenderGraphDebugMetadataSink.h"` to the include list
  (immediately after `RenderGraphCompiler.h`).
- `Execute()`'s template body — now constructs `RenderGraphBuilder builder;`,
  immediately calls `builder.SetDebugMetadataSink(m_debugMetadataSink)`, then
  (only when non-null) `m_debugMetadataSink->BeginFrame()`, BEFORE
  `build(builder)` runs — exactly per the phase file's Step 3.1 worked
  example, byte-for-byte.
- Two new public setters, placed immediately after
  `SetGpuTimingCaptureEnabled()` (the closest existing "session-scoped,
  optional collaborator setter" precedent, per the phase file's own Step 2):
  `void SetDebugMetadataSink(IPassDebugMetadataSink* sink) noexcept` and
  `void SetDebugMetadataProvider(IPassDebugMetadataProvider* provider) noexcept`.
- Two new private members, placed immediately after `m_resourcePool` (per the
  phase file's own Step 2 placement instruction):
  `IPassDebugMetadataSink* m_debugMetadataSink = nullptr;` and
  `IPassDebugMetadataProvider* m_debugMetadataProvider = nullptr;`.
- Doc comments copied verbatim from the phase file's own worked examples.

### 2. `src/Renderer/RenderGraph/RenderGraph.cpp`

- `ExecuteCompiledGraph()`'s tail — inserted a new `metadataLookup`
  construction block immediately after the existing
  `timingSlotBudgetExhausted` computation and immediately before the
  `RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(...)` call: an
  empty (default-constructed) `std::function<bool(std::size_t,
  PassDebugMetadata&)>` when `m_debugMetadataProvider == nullptr`, or a small
  lambda capturing the raw `IPassDebugMetadataProvider*` and forwarding into
  `QueryPassDebugMetadata()` otherwise — mirroring the existing `statsLookup`
  lambda immediately above it, exactly as the phase file specifies.
  `BuildRenderGraphSnapshot()`'s own call site now passes `metadataLookup` as
  its new 5th argument.
- **One transcription slip caught and fixed during this phase's own editing,
  before ever compiling**: the first `edit_line` replacement for this block
  left a stray leftover line (a duplicate, no-longer-referenced continuation
  of the OLD 2-line `BuildRenderGraphSnapshot(...)` call:
  `compiled, input, [this](const char* name) { return
  LastKnownStatsFor(name); }, timingSlotBudgetExhausted);`) immediately after
  the new 2-line call — caught by re-reading the file immediately after the
  edit (this campaign's own standing practice of never trusting an edit tool's
  success message alone), and removed with a second, one-line `edit_line`
  call before ever invoking the compiler. Confirmed, by a direct re-read of
  the final file (quoted in "Verification performed" below), that exactly one
  correct `BuildRenderGraphSnapshot(...)` call remains.

### 3. `src/Editor/EditorHost.h`

- Added `#include "FrameDebuggerPassMetadataRecorder.h"` (grouped with the
  other bridge/service includes, immediately after
  `ProjectLifecycleLoadCommandBridge.h`).
- Added a new private member, `FrameDebuggerPassMetadataRecorder
  m_passMetadataRecorder;`, immediately after `rg::RenderGraph&
  m_renderGraph;` — per the phase file's Step 3.3 placement instruction. This
  class has a trivial default constructor with no dependency on any other
  member, so its exact declaration-order position relative to
  `m_editorLayer`/`m_game`/etc. carries no construction-order hazard (C++
  constructs members strictly in declaration order regardless of
  initializer-list order — confirmed by direct reasoning, not just assumed).

### 4. `src/Editor/EditorHost.cpp`

- Constructor body — immediately after
  `gte::InstallLogSink(&gte::LoggerLogSink::Instance());` and BEFORE the
  `assert(m_editorLayer != nullptr ...); m_core.SetEditorLayerHook(...);`
  block, added:
  ```cpp
  m_renderGraph.SetDebugMetadataSink(&m_passMetadataRecorder);
  m_renderGraph.SetDebugMetadataProvider(&m_passMetadataRecorder);
  ```
  with the doc comment from the phase file's own Step 3.3 worked example.
  `FrameDebuggerPassMetadataRecorder.h` did not need its own new `#include`
  line in this `.cpp` file — it is already pulled in transitively via
  `EditorHost.h`.

### 5. CMake registration

No new files this phase (confirmed, matching the phase file's own Step 3.4)
— `RenderGraph.h`/`.cpp` and `EditorHost.h`/`.cpp` are all pre-existing,
already-registered files; only their CONTENT changed.

## Verification performed

1. **Incremental build**: `cmake --build build` (working directory: project
   root). Result: **zero errors, zero warnings** — `gte_core` (including
   `RenderGraph.cpp.obj`), `gte_editor` (including `EditorHost.cpp.obj`),
   `GreatTamanaEditor.exe`, and `GreatTamanaEngineTests.exe` all rebuilt and
   relinked cleanly (40 build steps total, including the two Project
   Assembly `.dll`s, which also rebuilt/relinked cleanly against the changed
   `gte_core`/`gte_editor` archives — confirming no ABI-visible break for a
   Project Assembly consumer either).
2. **Targeted `ctest` re-run** (this phase introduces no new test file, but
   re-running PHASE3's own filter set proves this phase's `RenderGraph.h`/
   `.cpp` changes did not silently break anything PHASE3 already covered):
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug -R "RenderGraphSnapshotTest|RenderGraphBuilderTest|RenderPassTest|RenderPipelineTest|RenderGraphPassRecordTest|FrameDebuggerPassMetadataRecorderTest" --output-on-failure`
   — **106 tests matched, 100% passing** (0 failures), 6.41s total. Every
   PHASE2 recorder test, every PHASE3 rewritten/new sink-wiring test, and
   every pre-existing untouched test in all 6 suites — all **Passed**.
3. **Live, HTTP-driven verification** — the whole point of this phase, per
   PHASE0's own Situation notes ("no `RenderGraphTests.cpp` precedent exists
   for constructing a real `gte::rg::RenderGraph` object" — this phase's
   correctness can only be proven live):
   a. `run_app_background` launched `build\GreatTamanaEditor.exe`
      (PID **11596**), working directory `build\`.
   b. `GET /render_graph` — confirmed the JSON response's
      `offscreen_regime.passes[]` entries carry real, genuinely
      NON-uniformly-default `category`/`draw_kind`/`tag_group_label` values,
      matching this engine's own known, hand-authored pass declarations
      exactly:
      - **`"DrawSkyBackground"`** (both the `"GameView"` and `"SceneView"`
        instances) reports **`"draw_kind":"DrawQuad"`** — every other pass in
        the same response reports the struct default, `"DrawMesh"` — proving
        this specific, hand-verified fact (a real 3-vertex full-screen-triangle
        draw, per `render-pass-2`'s own campaign history) is reaching the JSON
        through the new sink -> provider -> `metadataLookup` -> snapshot chain,
        not a coincidental default.
      - Every one of the 5 Atmosphere LUT passes
        (`AtmosphereTransmittanceLutPass`/`AtmosphereMultiScatteringLutPass`/
        `AtmosphereSkyViewLutPass`/`AtmosphereAerialPerspectiveVolumePass`/
        `AtmosphereAerialPerspectiveVolumeDebugSlicePass`) reports
        **`"tag_group_label":"Compute LUT"`** — every non-Atmosphere pass in
        the same response reports `"tag_group_label":null` — proving the
        `RenderPassTagMask`-driven tag-group lookup (`render-pass-7`
        campaign's own `RenderPassGroupRegistry`) is likewise flowing
        correctly through this campaign's new mechanism, end to end.
      - `"DemoRenderFeaturePlugin_Clear"`/`"DemoRenderFeatureSecondPlugin_Clear"`
        report **`"category":"Debug"`** — every other pass in the response
        reports the struct default, `"General"` — a third, independent
        confirmation using the third migrated field.
      - A response where every pass silently showed the exact same default
        values (the explicit failure condition this phase's own plan called
        out) was NOT observed — every one of the 3 migrated fields shows real
        variation exactly matching this engine's own documented, pre-existing
        pass metadata.
   c. `GET /get_logs?category=editor-core-separation-25` — **`{"count":0,
      "entries":[]}`** — zero log lines from this campaign's own new
      mechanism (it never logs on the happy path, per design).
      `GET /get_logs?min_level=Error` — **`{"count":0,"entries":[]}`** — zero
      errors/warnings of any kind logged this session. (The full,
      unfiltered `GET /get_logs` output was also inspected and contains only
      pre-existing, unrelated `RenderFeatureCompositor`/`ProjectAssembly`
      startup log lines that exist completely independently of this
      campaign.)
   d. `GET /get_swapchain` — captured a full Editor screenshot: the "Scene"/
      "Game" panels render normally (a pink/tan background with a blue
      radial-gradient render-feature tint — this is pre-existing
      Project-Assembly-demo content from `Projects/ScreenPassAutoWireProbe/`
      and the various `demo_render_feature*` plugins already loaded in this
      dev tree, completely unrelated to and unaffected by this phase's own
      diff), the "Render Graph" panel tab is present, and the "Probe Panel"
      (a Project Assembly Editor panel) shows its normal content — confirming
      the Editor is rendering and running completely normally, zero visual
      regression.
   e. `stop_app_background(pid: 11596)` — confirmed stopped ("Stopped process
      PID 11596 (GreatTamanaEditor) successfully."). Not left running.
4. **`git_status`** after all changes — exactly the 4 files this phase was
   supposed to touch, nothing else:
   ```
   modified:   src/Editor/EditorHost.cpp
   modified:   src/Editor/EditorHost.h
   modified:   src/Renderer/RenderGraph/RenderGraph.cpp
   modified:   src/Renderer/RenderGraph/RenderGraph.h
   ```

## Scope discipline confirmed

- `PassRecord::kind`/`::viewScope` — untouched this phase (were already
  migrated/untouched by PHASE3; this phase touches neither).
- `AddPass()`/`AddComputePass()` — untouched this phase (PHASE3's own
  scope; this phase never touches `RenderGraphBuilder.h` at all).
- `BuildRenderGraphSnapshot()`'s own signature — untouched this phase (its
  new trailing, defaulted `metadataLookup` parameter was PHASE3's addition);
  this phase only supplies a real argument at its ONE production call site
  (`RenderGraph::ExecuteCompiledGraph()`) instead of leaving it at its
  default `{}`.
- No new CMake registration was needed (no new files) — confirmed against
  the phase file's own Step 3.4 note.
- `CreateEditorLayer()` (`ImGuiEditorLayer.cpp`) — confirmed untouched; the
  install call correctly went into `EditorHost`'s own constructor body
  instead (the one documented, deliberate difference from
  `EditorGpuMemoryNameOverlay::Install()`'s own placement precedent, per the
  phase file's own Step 2 reasoning: `CreateEditorLayer(Window&, Renderer&)`
  has no `RenderGraph&` parameter to install onto).
- A Player-style build linking `gte_core` alone still compiles and runs with
  both new `RenderGraph` pointers permanently `nullptr` — confirmed
  structurally: neither setter is called from anywhere in `gte_core`, only
  from `EditorHost.cpp` (`gte_editor`-tier), and every new branch this phase
  adds (`Execute()`'s `if (m_debugMetadataSink != nullptr)`,
  `ExecuteCompiledGraph()`'s `if (m_debugMetadataProvider != nullptr)`) is
  null-guarded exactly as the phase file specifies.

## Next phase

PHASE5 (`PHASE5_FULL_REGRESSION_ACCEPTANCE_AUDIT_AND_CLOSEOUT.md`) re-confirms
every acceptance-criteria checkbox from the source design document (Section 8)
with fresh evidence, runs a full clean build and a full `ctest` regression
pass (the only phase in this campaign allowed to do so, per PHASE0's Locked
Decision 6), and writes `CAMPAIGN_COMPLETION_REPORT.md` closing out
`editor-core-separation-25` for good.
