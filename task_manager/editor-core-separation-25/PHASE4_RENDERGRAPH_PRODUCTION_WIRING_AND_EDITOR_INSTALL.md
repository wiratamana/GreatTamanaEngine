# PHASE4 — RenderGraph Production Wiring + Editor Startup Install

Parent: `PHASE0_MASTER_STRATEGY.md` — **MUST READ FIRST**. Also read
PHASE1-3's own `.md` files and ALL THREE of their completion reports
before starting — PHASE3 in particular may have adjusted exact
signatures/line numbers this phase depends on.

## Step 1: The Goal

1. `RenderGraph` (`RenderGraph.h`/`.cpp`) gets two new, optional, nullable
   member pointers — `IPassDebugMetadataSink* m_debugMetadataSink` and
   `IPassDebugMetadataProvider* m_debugMetadataProvider` — plus two public
   setters, and its `Execute()` template forwards the sink into every fresh
   `RenderGraphBuilder` it constructs and calls `BeginFrame()` on it,
   unconditionally, before that call's own `build(builder)` callback runs
   (TR4 of the source document).
2. `ExecuteCompiledGraph()` builds a small `metadataLookup` callable from
   `m_debugMetadataProvider` (when installed) and threads it into
   `BuildRenderGraphSnapshot()` (PHASE3's own new trailing parameter).
3. `EditorHost` — the ONE, real, live `RenderGraph` instance's actual
   owner-adjacent host object — constructs ONE persistent
   `FrameDebuggerPassMetadataRecorder` (PHASE2) and installs it as BOTH the
   sink and the provider, exactly once, at startup.
4. Live, HTTP-driven proof (never just "it compiled") that the whole
   mechanism actually works end to end in a real, running Editor session.

## Step 2: The Situation

- `RenderGraph::Execute()`'s template body (`RenderGraph.h`, ~line 174) is
  currently:
  ```cpp
  template <typename BuildFn>
  void Execute(VkCommandBuffer cmd, ExecuteTimingMode timingMode, BuildFn&& build)
  {
      RenderGraphBuilder builder;
      const std::vector<TextureHandle> finalOutputs = build(builder);
      ExecuteCompiledGraph(cmd, timingMode, builder.Finish(), finalOutputs);
  }
  ```
- `RenderGraph::ExecuteCompiledGraph()`'s tail (`RenderGraph.cpp`,
  ~lines 788-795) currently builds and stores the snapshot as:
  ```cpp
  const bool timingSlotBudgetExhausted = timingSlots.AssignedCount() >= timingSlots.SlotBudget();
  RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(
      compiled, input, [this](const char* name) { return LastKnownStatsFor(name); }, timingSlotBudgetExhausted);
  if (!isPipelined) {
      m_synchronousSnapshot = std::move(snapshot);
  } else {
      m_pipelinedSnapshot = std::move(snapshot);
  }
  ```
- `RenderGraph`'s private member section (`RenderGraph.h`, ~line 460
  onward) is where the two new pointers go — pick a spot near
  `m_resourcePool`/`m_timestampPool` at the top of that section (these are
  the class's other "installed, optional, session-scoped collaborator"
  members, the closest existing precedent for where a new one like this
  belongs).
- `EditorHost` (`src/Editor/EditorHost.h`/`.cpp`) owns `Core m_core;` and
  binds a REFERENCE member `rg::RenderGraph& m_renderGraph = m_core.GetRenderGraph();`
  (`EditorHost.h` ~line 127, bound via member-initializer list,
  `EditorHost.cpp` ~line 178) — this is the ONE real, live `RenderGraph`
  this whole Editor session ever uses, and `EditorHost` is exactly the
  right place to construct a persistent `FrameDebuggerPassMetadataRecorder`
  and install it, mirroring how `EditorGpuMemoryNameOverlay::Install()`
  installs itself as `GpuMemoryTracker`'s one observer.
  **Important, confirmed difference from that precedent**:
  `EditorGpuMemoryNameOverlay::Install()` happens inside `CreateEditorLayer()`
  (`ImGuiEditorLayer.cpp`, ~line 1311) — NOT a viable location for THIS
  install, because `CreateEditorLayer(Window&, Renderer&)` has no access to
  a `RenderGraph&` at all (only `Renderer&`). `EditorHost`'s OWN
  constructor body is the correct location instead — it already has
  `m_renderGraph` bound (via the member-initializer list, which always
  finishes before the constructor body starts executing) by the time its
  body runs, and there is no urgency requirement here the way
  `GpuMemoryTracker`'s install has (`RenderGraph::Execute()` is never
  called during `EditorHost`/`Core`/`Renderer` construction — it is only
  ever called later, from inside `Core::BuildFrame()`, during the main
  loop `Run()` drives — so installing anywhere in `EditorHost`'s
  constructor body is safe).
- `EditorHost.cpp`'s constructor body currently starts with
  `gte::InstallLogSink(&gte::LoggerLogSink::Instance());` (~line 222),
  followed by the `m_core.SetEditorLayerHook(m_editorLayer.get());` assert
  block (~lines 224-232) — this new install call goes immediately after
  the log-sink install, before the `SetEditorLayerHook()` block (both are
  "install this session's one X" calls; grouping them together at the top
  of the constructor body is the clearest place for a future reader to
  find every "session-wide singleton install" call in one glance).

## Step 3: The Plan

### 3.1 — `RenderGraph.h`: new members + setters + `Execute()` wiring

Add `#include "RenderGraphDebugMetadataSink.h"` to this file's own include
list.

New public setters (place near `SetGpuTimingCaptureEnabled()`, ~line 297 —
the closest existing "session-scoped, optional collaborator setter" on
this class):

```cpp
    // editor-core-separation-25 campaign - installed EXACTLY ONCE per
    // session, by Editor-tier startup code (EditorHost's own constructor,
    // PHASE4) - mirrors GpuMemoryTracker::SetDebugNameObserver()'s own
    // "install once, on the one persistent owning object" placement
    // (docs/conventions/gpu-resource-memory-tracking.md), extended with a
    // SECOND, separate read-only pointer (see RenderGraphDebugMetadataSink.h's
    // own header comment for why one interface could not serve both
    // directions). A Player build that never calls either setter pays one
    // null-pointer branch per pass declaration (via the builder this
    // sink is forwarded into, every Execute() call) plus one more per
    // Execute() call (the BeginFrame() call below) plus one more per
    // ExecuteCompiledGraph() call (the metadataLookup construction below)
    // - and stores nothing.
    void SetDebugMetadataSink(IPassDebugMetadataSink* sink) noexcept { m_debugMetadataSink = sink; }
    void SetDebugMetadataProvider(IPassDebugMetadataProvider* provider) noexcept { m_debugMetadataProvider = provider; }
```

New private members (near `m_resourcePool`, ~line 460):

```cpp
    // editor-core-separation-25 campaign - see SetDebugMetadataSink()/
    // SetDebugMetadataProvider() above. Both nullptr forever in a
    // Player-style build that links `gte_core` alone (never `gte_editor` -
    // this codebase has no `GTE_ENABLE_EDITOR` preprocessor macro anymore,
    // see AGENTS.md's "`gte_core` / `gte_editor` Library Separation"
    // section) - neither setter is ever called from Core, only from
    // Editor-tier startup code.
    IPassDebugMetadataSink* m_debugMetadataSink = nullptr;
    IPassDebugMetadataProvider* m_debugMetadataProvider = nullptr;
```

`Execute()`'s template body — change from the 3-line body shown in Step 2
to:

```cpp
    template <typename BuildFn>
    void Execute(VkCommandBuffer cmd, ExecuteTimingMode timingMode, BuildFn&& build)
    {
        RenderGraphBuilder builder;
        // editor-core-separation-25 campaign - forwarded into THIS call's
        // own fresh builder, before build(builder) ever runs, so every
        // pass this call declares (via AddRenderPass()) reaches the
        // installed sink, if any. BeginFrame() marks the start of a fresh
        // declaration sequence - see IPassDebugMetadataSink::BeginFrame()'s
        // own doc comment (RenderGraphDebugMetadataSink.h) for why this
        // must happen exactly here, exactly once, before any pass this
        // call declares.
        builder.SetDebugMetadataSink(m_debugMetadataSink);
        if (m_debugMetadataSink != nullptr) {
            m_debugMetadataSink->BeginFrame();
        }
        const std::vector<TextureHandle> finalOutputs = build(builder);
        ExecuteCompiledGraph(cmd, timingMode, builder.Finish(), finalOutputs);
    }
```

### 3.2 — `RenderGraph.cpp`: `ExecuteCompiledGraph()` metadata-lookup wiring

Change the tail shown in Step 2 from:

```cpp
    const bool timingSlotBudgetExhausted = timingSlots.AssignedCount() >= timingSlots.SlotBudget();
    RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(
        compiled, input, [this](const char* name) { return LastKnownStatsFor(name); }, timingSlotBudgetExhausted);
```

to:

```cpp
    const bool timingSlotBudgetExhausted = timingSlots.AssignedCount() >= timingSlots.SlotBudget();

    // editor-core-separation-25 campaign - built fresh every call, exactly
    // like the statsLookup lambda immediately above it (which this
    // mirrors precisely) - std::function is cheap here, this runs once
    // per Execute() call, not once per pass. Left as a default-constructed
    // (empty) std::function when no provider is installed - BuildPassSnapshot()
    // (RenderGraphSnapshot.cpp) already treats an empty metadataLookup
    // exactly like "no entry found for this index" (PHASE3), so a headless
    // build takes this exact same code path with zero extra cost beyond
    // the one null-pointer check below.
    std::function<bool(std::size_t, PassDebugMetadata&)> metadataLookup;
    if (m_debugMetadataProvider != nullptr) {
        IPassDebugMetadataProvider* provider = m_debugMetadataProvider;
        metadataLookup = [provider](std::size_t declarationIndex, PassDebugMetadata& outMetadata) {
            return provider->QueryPassDebugMetadata(declarationIndex, outMetadata);
        };
    }

    RenderGraphSnapshot snapshot = BuildRenderGraphSnapshot(compiled, input,
        [this](const char* name) { return LastKnownStatsFor(name); }, timingSlotBudgetExhausted, metadataLookup);
```

### 3.3 — `EditorHost.h`/`.cpp`: persistent recorder + install

`EditorHost.h` — add `#include "FrameDebuggerPassMetadataRecorder.h"`, and
a new private member immediately after `rg::RenderGraph& m_renderGraph;`
(~line 127):

```cpp
    // editor-core-separation-25 campaign - the ONE, persistent, whole-
    // session Editor-owned implementation of IPassDebugMetadataSink AND
    // IPassDebugMetadataProvider, installed onto m_renderGraph once, below
    // (EditorHost.cpp's own constructor body) - mirrors
    // EditorGpuMemoryNameOverlay's own role for GpuMemoryTracker, kept as
    // a plain, non-global member here (rather than all-static/global like
    // that overlay) since nothing OUTSIDE this exact install call ever
    // needs to reach this object directly - every real consumer
    // (FrameDebuggerData.cpp, RenderGraphMetadata.cpp, etc.) already reads
    // the resolved category/drawKind/tags values straight off
    // RenderGraphPassSnapshot, which m_renderGraph itself populates using
    // this object internally (PHASE3/PHASE4's own metadataLookup wiring).
    FrameDebuggerPassMetadataRecorder m_passMetadataRecorder;
```

`EditorHost.cpp`'s constructor body — immediately after
`gte::InstallLogSink(&gte::LoggerLogSink::Instance());` (~line 222) and
BEFORE the `m_core.SetEditorLayerHook(...)` assert block (~line 224):

```cpp
    // editor-core-separation-25 campaign - installs this session's ONE
    // pass-debug-metadata sink/provider onto the ONE real, live
    // RenderGraph this EditorHost owns (m_renderGraph, bound above via
    // this constructor's own member-initializer list, already fully
    // constructed by the time this body runs) - mirrors
    // EditorGpuMemoryNameOverlay::Install()'s own "install this session's
    // one X" placement precedent, grouped here rather than inside
    // CreateEditorLayer() because CreateEditorLayer(Window&, Renderer&)
    // has no RenderGraph& to install onto (see this phase's own strategy
    // file, task_manager/editor-core-separation-25/
    // PHASE4_RENDERGRAPH_PRODUCTION_WIRING_AND_EDITOR_INSTALL.md, for the
    // full reasoning). Safe here regardless of ordering relative to
    // SetEditorLayerHook() below - RenderGraph::Execute() is never called
    // during construction of anything this constructor builds, only much
    // later, from inside Core::BuildFrame() once Run()'s own main loop
    // starts.
    m_renderGraph.SetDebugMetadataSink(&m_passMetadataRecorder);
    m_renderGraph.SetDebugMetadataProvider(&m_passMetadataRecorder);
```

### 3.4 — CMake registration

No NEW files in this phase. Confirm `EditorHost.cpp`'s own existing
CMakeLists.txt entries already cover it (they do — `EditorHost.h`/`.cpp`
are pre-existing, already-registered files; only their CONTENT changes
here).

### 3.5 — Verification (this phase)

1. **Incremental compile check**: `cmake --build build` succeeds.
2. There is no `RenderGraphTests.cpp` / Tier-1 precedent for constructing
   a real `gte::rg::RenderGraph` object (`PHASE0`'s own Situation notes) —
   do NOT attempt to invent one here; this phase's own correctness is
   proven LIVE instead:
   a. `run_app_background` the Editor executable (find its actual built
      path/name under `build/` — confirm via `browse_dir` if unsure,
      never guess).
   b. `gte_send_request` to `GET /render_graph` — confirm the JSON
      response's `offscreen_regime.passes[]`/`present_regime.passes[]`
      entries still carry sensible, NON-uniformly-default `category`/
      `draw_kind`/`tag_group_label` values (e.g. at least one pass with
      `category != "General"` or `draw_kind != "DrawMesh"`, matching what
      this exact engine's own real, hand-authored passes are known to
      declare — e.g. `"DrawSkyBackground"` should show `draw_kind:
      "DrawQuad"`). A response where EVERY pass silently shows the exact
      same default values would mean the sink/provider wiring is not
      actually reaching production code — treat that as a real failure,
      not a pass.
   c. `gte_send_request` to `GET /get_logs` — confirm no new
      warning/error was logged by this session that wasn't there before
      (the sink/provider mechanism itself never logs anything on the
      happy path — any log line mentioning this campaign's own new types
      is a red flag, not a status update).
   d. `gte_send_request` to `GET /get_swapchain` or `GET /get_game_view` —
      a plain sanity screenshot proving the Editor is still rendering
      normally (this campaign changes zero visual behavior — a broken/
      black frame here would mean something in this phase's wiring is
      genuinely wrong, e.g. a bad barrier from an accidentally-reordered
      call, even though nothing in this phase's OWN diff should be
      capable of causing that — investigate immediately if seen).
   e. `stop_app_background` the Editor process when done — never leave it
      running.
3. `git_status` to confirm exactly the expected files changed:
   `RenderGraph.h`, `RenderGraph.cpp`, `EditorHost.h`, `EditorHost.cpp`.

### 3.6 — This phase's own completion report

Write `PHASE4_COMPLETION_REPORT.md` into this same folder covering: every
file changed and a summary of the diff, the exact live-verification steps
performed (3.5.2 above) with their actual captured evidence (paste the
relevant `GET /render_graph` JSON excerpt showing real, non-default
category/draw_kind values; note the executable path/PID used). Commit
both the code change and the report together (`git_add` + `git_commit`).

If you discover ANY ambiguity not already resolved by this file or
`PHASE0_MASTER_STRATEGY.md` — use `ask_questions` before proceeding. If you
delegate any part of this phase to a sub-task, that sub-task MUST also be
instructed to use `ask_questions` for its own ambiguities.
