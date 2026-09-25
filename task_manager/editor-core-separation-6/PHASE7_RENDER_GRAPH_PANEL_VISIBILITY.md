# PHASE7 — "Plugin Render Features" Section in the Editor's "Render Graph" Panel

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST). Also read
`PHASE6_COMPLETION_REPORT.md` before starting.

## Step 1: The Goal

Extend the Editor's EXISTING "Render Graph" panel
(`RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_2026-09-25.md` Section
3.6) with a new "Plugin Render Features" section, listing, per wired stage
(`PostComposite`, `PreUI`): plugin name, priority, blend mode — the
compositor's own real, RESOLVED ordering decision, made directly visible,
so a developer (or a future automated HTTP check) can confirm ordering
without reverse-engineering it from pixels. Never invent a new panel — this
is an addition to the panel that already exists.

## Step 2: The Situation

Confirmed by direct read, `src/Editor/Panels/RenderGraphPanel.cpp`'s
`Build()` method (lines ~248-301): `ImGui::Begin("Render Graph")` ... a
Pause checkbox ... `BuildGpuDrivenBatchesSection(...)` ... 2 regime
sections (`BuildRegimeSection(...)`) ... an "Export" section (disabled
button) ... `ImGui::End()`. `BuildGpuDrivenBatchesSection()` (lines
~222-244) is the most directly relevant existing precedent to mirror: a
free function taking a small, already-CPU-side-collected
`std::vector<SomeDebugInfo>` and rendering it via
`ImGui::SeparatorText(...)` + a loop of `ImGui::Text(...)` calls — no new
ImGui widget kind, no new panel, no per-frame GPU readback.

`RenderGraphPanel::Build()`'s own signature currently is `Build(EditorContext&
/*ctx*/, const rg::RenderGraph& renderGraph, const
std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo)` — confirm
the exact current call site (`ImGuiEditorLayer.cpp`, most likely) that
invokes `RenderGraphPanel::Build(...)` with its own real arguments, via
`search_in_dir` for `RenderGraphPanel` before editing anything, to know
exactly what new argument needs threading through and from where.

`RenderFeatureCompositor` (PHASE4/PHASE5) already holds, as private members,
`m_postComposite`/`m_preUi` — the exact resolved, ordered, collision-
corrected lists this panel needs to display. This phase needs a small,
read-only, `_v2`-ABI-free (never crosses the plugin boundary — this is
pure `gte_core`-internal debug data) snapshot type and a public accessor.

## Step 3: The Plan

### Step 3.1 — New, small debug-snapshot type

`src/Core/Plugins/RenderFeatureCompositor.h` gains a small nested (or
sibling) POD-ish struct, e.g.:

```cpp
struct RenderFeatureDebugEntry {
    std::string name;          // copied from descriptor.name (bounded char[64])
    std::string stage;         // "PostComposite" or "PreUI" (human-readable)
    std::int32_t priority = 0;
    std::string blendMode;     // "Replace"/"AlphaOver"/"Additive"/"Multiply"/"ScreenSpaceMask"
};
```

and a public, `noexcept`, read-only accessor:

```cpp
std::vector<RenderFeatureDebugEntry> DebugSnapshot() const;
```

`DebugSnapshot()`'s body walks `m_postComposite` then `m_preUi` (same
combined order `ContributeRenderGraphPasses()` itself uses), converting
each `Entry`'s `descriptor` fields into the human-readable strings above
(a small `ToString(RenderFeatureStage)`/`ToString(RenderFeatureBlendMode)`
pair of free functions, `src/Core/Plugins/RenderFeatureCompositor.cpp`-local,
mirrors `RenderPassEvent`'s own existing `ToString()` free-function
precedent, `RenderGraphTypes.h`). This is intentionally a plain
`std::vector<std::string>`-based snapshot — safe, simple, called at most
once per Editor frame from `gte_editor`-tier code, never on a hot path.

### Step 3.2 — Thread it from `Core` to the panel

`Core.h` gains a small public accessor,
`const RenderFeatureCompositor* GetRenderFeatureCompositor() const noexcept;`,
returning a raw, non-owning pointer into `m_capabilityOrchestrators`.
**Confirmed via `search_in_dir` for `dynamic_cast` across the whole `src/`
tree during this campaign's own review: zero hits, anywhere — this codebase
uses no RTTI at all today.** Do not introduce the first one here. Use a
second, explicitly-typed `RenderFeatureCompositor* m_renderFeatureCompositorPtr`
member, set at the same time (same statement/line) the owning `unique_ptr` is
pushed into `m_capabilityOrchestrators` inside
`RegisterBuiltinCapabilityOrchestrators()` (e.g. `auto compositor =
std::make_unique<RenderFeatureCompositor>(*this, m_renderer);
m_renderFeatureCompositorPtr = compositor.get();
m_capabilityOrchestrators.push_back(std::move(compositor));`), so
`GetRenderFeatureCompositor()` is a plain, zero-cost pointer return with no
`dynamic_cast`/RTTI dependency introduced anywhere in this codebase.

Whatever code currently calls `RenderGraphPanel::Build(ctx, renderGraph,
gpuDrivenBatchDebugInfo)` (located in Step 2) gains one more argument,
`core.GetRenderFeatureCompositor() != nullptr ?
core.GetRenderFeatureCompositor()->DebugSnapshot() :
std::vector<RenderFeatureDebugEntry>{}` (or an empty vector when
`GTE_ENABLE_PLUGINS` is off / the compositor genuinely has zero loaded `_v2`
plugins — the panel section below already handles an empty vector
gracefully, mirroring `BuildGpuDrivenBatchesSection()`'s own existing
"No ... this frame" empty-state message).

### Step 3.3 — The new panel section

`RenderGraphPanel.h`/`.cpp`: `Build()`'s signature gains one new parameter,
`const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries` (placed
as the LAST parameter, so every existing call site only needs ONE new
trailing argument, not a full argument-order rewrite). A new function-local
free function, `BuildPluginRenderFeaturesSection(...)`, mirroring
`BuildGpuDrivenBatchesSection()`'s own exact shape:

```cpp
void BuildPluginRenderFeaturesSection(const std::vector<RenderFeatureDebugEntry>& entries)
{
    ImGui::SeparatorText("Plugin Render Features");
    if (entries.empty()) {
        ImGui::TextDisabled("No loaded plugin implements IRenderFeatureModule_v2 this session.");
        return;
    }
    for (const RenderFeatureDebugEntry& entry : entries) {
        ImGui::Text("[%s] %s - priority %d, blend %s", entry.stage.c_str(), entry.name.c_str(),
            entry.priority, entry.blendMode.c_str());
    }
}
```

Called from `Build()` right after `BuildGpuDrivenBatchesSection(...)`
(same relative placement logic: a live, actionable ordering signal, shown
early, before the two much-longer regime pass/resource tables).

### Verification

1. Incremental build: `cmake --build build`.
2. Live smoke test: `run_app_background` with both PHASE6 `_v2` demo
   plugins loaded, confirm via a live screenshot of the "Render Graph"
   panel (there is no direct "read ImGui panel text" HTTP endpoint
   confirmed yet — use `GET /get_swapchain` with the Editor UI visible and
   the "Render Graph" panel open/docked, then visually read the new
   section's text via `load_image`; if no such capture path reliably shows
   panel text, note this honestly in the completion report and fall back
   to reasoning about the code path plus the compile succeeding, same
   "Tier 2, no automated coverage yet" honesty this whole campaign already
   applies to GPU-rendered pixels) that BOTH `"DemoRenderFeatureV2Plugin"`/
   `"[PostComposite] DemoRenderFeatureV2 - priority 0, blend Replace"` and
   `"[PreUI] DemoRenderFeatureV2Second - priority 0, blend AlphaOver"`
   appear.
3. `git_status` — confirm the diff touches only
   `RenderFeatureCompositor.h`/`.cpp`, `Core.h`, `RenderGraphPanel.h`/`.cpp`,
   and whichever single call site threads the new argument through.

### What this phase does NOT do

- Does not add a NEW panel — extends the existing "Render Graph" panel only.
- Does not change `RenderFeatureCompositor`'s own real ordering/blending
  logic in any way — read-only visibility, zero behavior change to
  rendering itself.

### Completion

Write `PHASE7_COMPLETION_REPORT.md` (screenshot evidence or the honest
fallback reasoning described above), then `git_add` + `git_commit`.
