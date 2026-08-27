# RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md
### (Part 8 of 9 - see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign)

## Step 1: The Goal (Where are we going?)

Give the Editor a **"Render Graph" panel** - the same instinct that already
produced "Memory" (`Panels/MemoryPanel.cpp`) and "Profiler"
(`Panels/ProfilerPanel.h/.cpp`) applied to the engine's own frame
scheduling: a live, honest view of exactly what `RenderGraph::Execute()`
decided THIS frame - which passes ran, which were culled and why, what
order they ran in, how long each one took on the GPU (reusing Phase 6's
generalized per-pass timing), and each resource's computed lifetime. This
turns "what does the render graph actually do" from a question you can
only answer by reading source code into one you can answer by opening a
panel, exactly like "Memory"/"Profiler" already did for GPU memory and CPU/
GPU timing respectively.

## Step 2: The Situation / The Problem (Where are we now?)

Every debugging tool this engine already has for a system this
sophisticated follows the exact same recipe (AGENTS.md, "Editor Module
Structure"): a small, ImGui-free, Tier-1-tested "data shaping" module
(`MemoryPanelData.h/.cpp`, `ProfilerPanelData.h`) plus a thin ImGui panel
that calls it (`Panels/MemoryPanel.cpp`, `Panels/ProfilerPanel.cpp`). A
render graph is, if anything, a HARDER system to reason about by reading
source alone than GPU memory or CPU scope timing ever were - it has actual
graph structure (dependencies, culling, ordering) that is fundamentally
easier to SHOW than to describe in prose. Without this phase, every future
"why did my new pass not run" / "why is this resource allocated twice"
debugging session falls back to print-statement debugging or manually
stepping the compiler in a debugger - exactly the class of friction this
engine's existing Editor tooling philosophy has consistently chosen to
build a real panel to eliminate instead.

## Step 3: The Plan (How will we get there?)

### 3.1 - `RenderGraph` gains a debug-snapshot accessor

`RenderGraph::Execute()` (Phase 6), immediately after compiling, copies the
`CompiledGraph` (Phase 3's `executionOrder` + resource lifetimes) plus a
list of every DECLARED pass's name/culled-flag (including CULLED ones -
crucial: a culled pass must still be VISIBLE in the panel, tagged as
culled, or "why didn't my pass run" becomes just as hard to answer as
before) into a small, plain, ImGui-free `RenderGraphSnapshot` struct
(`src/Renderer/RenderGraph/RenderGraphSnapshot.h` - deliberately its own
header, not embedded in `RenderGraph.h`, mirroring
`MemorySnapshotBuilder.h`'s own "small, dedicated, directly-testable
reshaping module" precedent from the Profiler's Phase 5):

```cpp
struct RenderGraphPassSnapshot {
    std::string name;              // copied, not a raw const char* - this snapshot must outlive the frame it describes, for Pause support (see 3.4)
    bool isCulled = false;
    std::vector<std::string> readNames;  // resource names this pass declared reading
    std::vector<std::string> writeNames; // resource names this pass declared writing
    DrawStats drawStats;
    GpuTimingSample timing;
};
struct RenderGraphResourceSnapshot {
    std::string name;
    bool isImported = false;
    std::int32_t firstUsePassIndex = -1;
    std::int32_t lastUsePassIndex = -1;
};
struct RenderGraphSnapshot {
    std::vector<RenderGraphPassSnapshot> passesInExecutionOrder; // culled passes appended at the end, clearly marked
    std::vector<RenderGraphResourceSnapshot> resources;
};

RenderGraphSnapshot BuildRenderGraphSnapshot(const CompiledGraph&, const CompiledGraphInput&,
    const RenderGraph& graphForStats);
```

`BuildRenderGraphSnapshot()` is a PURE, Tier-1-testable reshaping function
(hand-fabricate a `CompiledGraph`/`CompiledGraphInput` and assert on the
resulting snapshot's shape - `tests/Renderer/RenderGraph/
RenderGraphSnapshotTests.cpp`), exactly mirroring
`BuildMemoryRows()`/`BuildMemorySnapshot()`'s own already-proven shape from
the Memory/Profiler panels.

### 3.2 - New Editor files: `src/Editor/Panels/RenderGraphPanel.h/.cpp`

A thin ImGui wrapper, following `Panels/ProfilerPanel.h`'s own "small
stateful class, not a free function" precedent (AGENTS.md, "Editor Module
Structure" - `ProfilerPanel`/`BoneViewerWindow` are the two pre-approved
examples of this), since this panel needs its own Pause-equivalent
snapshot-freeze state (3.4). Layout, mirroring "Profiler"'s own
established visual vocabulary:

- A top-to-bottom ORDERED LIST of passes exactly as `executionOrder` ran
  them, each row showing: name, draw-call count, triangle count, GPU
  milliseconds (or "N/A"/"Unsupported" exactly per `GpuTimingSample::Status`
  - reuse `ProfilerPanel`'s existing formatting helpers rather than
  reinventing them), and the resource names it reads (as small inline
  "chips"/tags) and writes.
- A CULLED PASSES section underneath, visually de-emphasized (greyed text,
  matching "Profiler"'s own "de-emphasized as context" treatment of
  Scene View/Present relative to Game View), each entry showing its name
  and, on hover, a tooltip explaining WHY it was culled ("no path to this
  frame's final outputs").
- A RESOURCES table: name, imported vs. transient, first-use/last-use pass
  index (shown as "Pass 2 -> Pass 4", i.e. resolved back to pass NAMES via
  `passesInExecutionOrder`, not raw integers - a raw index is meaningless
  to a human reader).
- A **Pause** control (mirrors `ProfilerPanel`'s own Pause exactly - freezes
  which `RenderGraphSnapshot` this ONE panel currently displays, while
  `RenderGraph::Execute()` keeps running/updating its live snapshot
  underneath every frame) - deliberately NOT a **Capture** control this
  time: there is no meaningful "disable render-graph-snapshot-capture"
  runtime toggle the way GPU timestamp queries have one, since building
  this snapshot costs nothing beyond copying already-computed strings/
  small vectors once per frame (no new Vulkan calls, no new GPU cost at
  all - it is pure CPU-side reflection of work the graph was doing anyway).
- A disabled "Export DOT" button (stub, tooltipped, pointing at Phase 9 -
  see Step 4) - the natural next step once a human wants to paste the
  graph into Graphviz/an online renderer for a prettier, zoomable picture
  than an ImGui list can give, explicitly deferred exactly like
  "Profiler"'s own "Export CSV" stub was.

### 3.3 - Docking

Docked alongside "Memory"/"Profiler" along the bottom
(`DockLayout.cpp`, one more tab in that same group) - not a new top-level
layout region; this is one more diagnostic tool in the SAME family, and
should read as such to a user, not as a separate, unrelated feature.

### 3.4 - Pause semantics, precisely

Exactly mirroring `ProfilerPanel`'s own already-established pattern
(AGENTS.md, "Profiling"): Pause freezes ONLY this panel's OWN displayed
snapshot; `RenderGraph`/`FrameProfiler` underneath keep collecting/updating
normally regardless of this panel's Pause state. This is why
`RenderGraphPassSnapshot`/`RenderGraphResourceSnapshot` copy their `name`s
into real `std::string`s rather than holding onto the ORIGINAL
`const char*` string-literal pointers `PassRecord`/`AddPass()` used - a
paused, frozen snapshot must remain valid and displayable indefinitely,
independent of whatever the live graph is doing meanwhile (a purely
defensive, cheap-at-this-scale choice, not a performance-critical path -
this snapshot is built once per frame, not once per draw call, so
`std::string` allocation here carries none of the same "never allocate in
the per-frame hot path" weight `FrameProfiler`'s OWN ring buffer has to
honor for CPU scope timing).

## Step 4: What We Will NOT Do (Focus)

- We will **not** implement the "Export DOT" button's actual functionality
  in this phase - ship it disabled/stubbed, exactly like "Profiler"'s own
  "Export CSV" button was shipped disabled first. A real DOT/Graphviz
  exporter is a small, self-contained follow-up once this panel's data
  model (3.1) has already proven itself useful in its ImGui-list form -
  no reason to gold-plate an export format before anyone has used the
  panel at all.
- We will **not** make this panel interactive/editable (no "click a pass
  to disable it," no live pass reordering from the UI) - it is a READ-ONLY
  observability tool, exactly like "Memory"/"Profiler" are today. Any
  future "toggle a pass on/off from the Editor" feature is gameplay/
  scene-authoring territory, not debug tooling, and is out of scope here.
- We will **not** attempt to render an actual NODE-GRAPH visual (boxes and
  connecting lines, force-directed layout, etc.) inside ImGui - that is a
  meaningfully larger UI investment than this campaign's budget justifies
  right now, and the ordered-list + culled-section + resource-table
  presentation above already answers every debugging question this phase
  exists to answer. A visual graph, if ever wanted, is best served by the
  DOT export above feeding an EXISTING, purpose-built graph-layout tool,
  not a homegrown one.
- We will **not** add any NEW per-pass GPU/CPU measurement this phase
  doesn't already have from Phase 6/existing `FrameProfiler` - this panel
  is purely a new VIEW over data that already exists; it adds zero new
  instrumentation.

## Step 5: Their Role (What does this mean for you?)

- If you are implementing this phase, treat `BuildRenderGraphSnapshot()`
  with the same "pure function, hand-fabricated inputs, Tier-1 tested"
  discipline every other `*PanelData.h`/`*SnapshotBuilder.h` module in this
  engine already gets - resist the temptation to compute any of this data
  directly inside `Panels/RenderGraphPanel.cpp` itself; if it can be pure,
  it MUST be pure and it MUST live outside the ImGui-touching file, per
  this codebase's own established convention, no exceptions.
- When you land this phase, actually USE it immediately afterward to
  answer one real question about the engine's own three existing passes
  (e.g. "confirm Present's only declared read is whatever ImGui itself
  samples, and Present writes only the swapchain image") - if the panel
  can't easily answer that, its data model needs another look before this
  phase is considered done.
- This phase closes out the campaign's MVP scope (Phases 1-8). Once it
  lands, write a short, campaign-level
  `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md` (mirroring how
  `PROFILER_IMPLEMENTATION_STATUS_v7.md` summarizes that whole multi-phase
  effort) tying all eight phases' individual completion reports together,
  and update `README.md`'s own "Status"/"Rendering" section to describe the
  render graph the same way every other landed subsystem is described
  there - this is what makes the work discoverable to the NEXT person, the
  same courtesy every prior phase in this engine's history has extended.
