# PHASE3 — `IEditorLayer` Surface + `BuildUI()` Wiring — COMPLETION REPORT

**Status: DONE.** Implemented exactly what
`PHASE3_IEDITORLAYER_SURFACE_AND_BUILDUI_WIRING.md` describes. No deviations
from the plan. Step 3.5's explicit pitfall (the existing `const
RenderFeatureCompositor* renderFeatureCompositor` local inside
`EditorHost.cpp`'s `{ GTE_PROFILE_SCOPE("IEditorLayer::BuildUI"); ... }` block)
was avoided exactly as instructed: `m_core.GetRenderFeatureCompositor()` is
called a SECOND, fresh time for the new trailing `BuildUI()` argument, never
reusing that pre-existing `const`-typed local.

## What changed

### `src/Editor/EditorLayer.h`

- New forward declarations added right after the existing `namespace rg {
  class RenderGraph; class RenderGraphBuilder; }` block:
  `class RenderPassToggleRegistry;` inside `namespace rg { ... }`, and (at
  file scope inside `namespace gte`) `class RenderFeatureCompositor;` —
  exactly matching Step 3.1's shown code, with the same doc-comment
  reasoning (heavy real header, forward-declare-only, mirrors `class Core;`'s
  own existing precedent).
- `IEditorLayer::BuildUI()`'s signature gains 2 new TRAILING parameters,
  appended after the existing `renderFeatureEntries` parameter:
  `rg::RenderPassToggleRegistry& renderPassToggleRegistry` (never null) and
  `RenderFeatureCompositor* renderFeatureCompositor` (nullable) — doc
  comments match Step 3.1's shown text verbatim.
- 2 new plain virtual setters added immediately after
  `FrameDebuggerSetEnabled(bool enabled) = 0;`, in their own small labeled
  group: `virtual void SetShowBlurredSceneOutput(bool enabled) = 0;` and
  `virtual void SetShowGBufferValidationOutput(bool enabled) = 0;`.

### `src/Editor/ImGuiEditorLayer.cpp`

- `BuildUI()`'s override signature gains the same 2 new trailing parameters.
- The existing `m_renderGraphPanel.Build(...)` call inside `BuildUI()`'s body
  now forwards both new arguments verbatim:
  `m_renderGraphPanel.Build(m_ctx, renderGraph, gpuDrivenBatchDebugInfo,
  renderFeatureEntries, renderPassToggleRegistry, renderFeatureCompositor);`
- 2 new setter overrides added immediately after the existing
  `FrameDebuggerSetEnabled` override:
  `void SetShowBlurredSceneOutput(bool enabled) override { m_ctx.showBlurredSceneOutput = enabled; }`
  and
  `void SetShowGBufferValidationOutput(bool enabled) override { m_ctx.showGBufferValidationOutput = enabled; }`.
- No new `#include` was needed: `RenderFeatureCompositor.h` is not referenced
  in this file (the pointer is only forwarded, never dereferenced here), and
  `rg::RenderPassToggleRegistry` only needs the forward declaration already
  brought in via `EditorLayer.h`.

### `src/Editor/NullEditorLayer.cpp`

- `BuildUI()`'s override signature gains the same 2 new trailing parameters,
  both left unused/discarded, exactly like every other parameter this no-op
  override already ignores.
- 2 new no-op setter stubs added immediately after `FrameDebuggerSetEnabled`:
  `void SetShowBlurredSceneOutput(bool /*enabled*/) override { }` and
  `void SetShowGBufferValidationOutput(bool /*enabled*/) override { }`.

### `src/Editor/Panels/RenderGraphPanel.h`

- New `#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"`
  (a small, plain, dependency-free `gte_core` header, direct include is safe
  — mirrors this file's own existing `RenderGraphMetadata.h` direct-include
  precedent), plus a new forward declaration `class RenderFeatureCompositor;`
  (mirroring `EditorLayer.h`'s own forward-declare-only choice — the real,
  heavy header is left for `RenderGraphPanel.cpp` to include once PHASE4
  actually calls a method on it).
- `RenderGraphPanel::Build()`'s declaration gains the same 2 new trailing
  parameters as `BuildUI()`: `rg::RenderPassToggleRegistry&
  renderPassToggleRegistry` and `RenderFeatureCompositor*
  renderFeatureCompositor`.

### `src/Editor/Panels/RenderGraphPanel.cpp`

- `RenderGraphPanel::Build()`'s definition signature gains the same 2 new
  trailing parameters, both left as clearly-commented UNUSED markers for THIS
  phase only (`/*renderPassToggleRegistry*/`, `/*renderFeatureCompositor*/`,
  each with an `// editor-core-separation-8, PHASE3: signature-only - PHASE4
  wires real UI.` comment) — the existing, pre-existing `EditorContext&
  /*ctx*/` marker was left completely untouched, exactly as Step 3.4
  instructs (PHASE4 removes all three markers together, in one pass).
- The function body itself is completely unchanged.

### `src/Editor/EditorHost.cpp`

- The one real `m_editorLayer->BuildUI(...)` call site, inside the SAME
  `{ GTE_PROFILE_SCOPE("IEditorLayer::BuildUI"); ... }` block
  `editor-core-separation-7`'s own PHASE4 already used to publish
  `RenderGraphMetadata` (confirmed: this block was neither moved nor
  duplicated — only its own existing call's argument list was widened), now
  reads:
  ```cpp
  m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph, m_atmosphereSettings, m_atmosphereLutRenderer,
      m_core.GetGpuDrivenBatchDebugInfo(), renderFeatureEntries,
      m_core.GetRenderPassToggleRegistryMutable(), m_core.GetRenderFeatureCompositor());
  ```
  The new trailing `m_core.GetRenderFeatureCompositor()` call is a genuinely
  SECOND, fresh call — confirmed the pre-existing `const
  RenderFeatureCompositor* renderFeatureCompositor` local (declared a few
  lines above, used to build `renderFeatureEntries` via `DebugSnapshot()`)
  was left completely untouched and is NOT reused as this new argument,
  exactly per Step 3.5's explicit pitfall warning.

## Final signatures (confirmed byte-for-byte identical across all 4 call sites)

```cpp
virtual void BuildUI(Game& game, Renderer& renderer, const rg::RenderGraph& renderGraph,
    AtmosphereSettings& atmosphereSettings, AtmosphereLutRenderer& atmosphereLutRenderer,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
    rg::RenderPassToggleRegistry& renderPassToggleRegistry,
    RenderFeatureCompositor* renderFeatureCompositor) = 0; // EditorLayer.h

void BuildUI(Game& game, Renderer& renderer, const rg::RenderGraph& renderGraph,
    AtmosphereSettings& atmosphereSettings, AtmosphereLutRenderer& atmosphereLutRenderer,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
    rg::RenderPassToggleRegistry& renderPassToggleRegistry,
    RenderFeatureCompositor* renderFeatureCompositor) override; // ImGuiEditorLayer.cpp

void BuildUI(Game& /*game*/, Renderer& /*renderer*/, const rg::RenderGraph& /*renderGraph*/,
    AtmosphereSettings& /*atmosphereSettings*/, AtmosphereLutRenderer& /*atmosphereLutRenderer*/,
    const std::vector<GpuDrivenBatchDebugInfo>& /*gpuDrivenBatchDebugInfo*/,
    const std::vector<RenderFeatureDebugEntry>& /*renderFeatureEntries*/,
    rg::RenderPassToggleRegistry& /*renderPassToggleRegistry*/,
    RenderFeatureCompositor* /*renderFeatureCompositor*/) override; // NullEditorLayer.cpp
```

```cpp
void Build(EditorContext& ctx, const rg::RenderGraph& renderGraph,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
    rg::RenderPassToggleRegistry& renderPassToggleRegistry,
    RenderFeatureCompositor* renderFeatureCompositor); // RenderGraphPanel.h / .cpp
```

## Deviations from the plan

**None.** Every file touched is exactly the one Step 3.1-3.5 names, every new
parameter/method signature matches the plan's own shown code verbatim, the
insertion points (near `FrameDebuggerSetEnabled`, near the existing
`m_renderGraphPanel.Build(...)` call, inside the exact
`GTE_PROFILE_SCOPE("IEditorLayer::BuildUI")` block) match the plan's own
described locations exactly, and Step 3.5's pitfall was correctly avoided
(confirmed by a clean compile — reusing the wrong local would have failed to
compile with a `const`-qualification mismatch, and it did not).

## Verification evidence

1. **`git_status` at start**: branch was `feature/editor-core-separation`,
   working tree clean (PHASE1 + PHASE2's diffs were already committed,
   nothing outstanding) — confirmed before touching any file.

2. **Incremental build**:
   - `cmake --build build --target GreatTamanaEditor -j 8` — succeeded, 11
     build steps: `NullEditorLayer.cpp`, `GBufferValidation.cpp`,
     `FrameDebuggerPanel.cpp`, `RenderGraphPanel.cpp`, `Core.cpp`,
     `ImGuiEditorLayer.cpp`, `EditorHost.cpp` (the genuinely touched/
     transitively-affected translation units), plus the 2 static-library
     relinks, `main.cpp`, and the final executable relink — a real
     incremental build, not a full clean one.
   - `cmake --build build --target GreatTamanaEngineTests -j 8` — succeeded
     (1 step: relink only — no test `.cpp` file references any of these
     changed signatures, so nothing needed recompiling; the test binary
     still links cleanly against the widened `libgte_core.a`/`libgte_editor.a`).

3. **Live sanity check**: `run_app_background` on the real
   `GreatTamanaEditor.exe` (PID 13864), then
   `gte_send_request("/get_swapchain")` — HTTP 200, a real, correctly
   rendered frame (Hierarchy/Scene/Game/Inspector panels, the "Show Compute
   Blur (debug)"/"Show GBuffer Validation (debug)" checkboxes in the Scene
   panel, the demo plugin panel all visible and rendering exactly as
   before this phase). `gte_send_request("/get_logs?limit=20")` showed only
   the expected pre-existing plugin-load/GPU-timing-slot-budget warnings,
   nothing new/unexpected. `stop_app_background(pid: 13864)` afterward —
   confirmed stopped successfully.

4. **`git_status` immediately before this commit**: diff touches EXACTLY —
   `src/Editor/EditorHost.cpp`, `src/Editor/EditorLayer.h`,
   `src/Editor/ImGuiEditorLayer.cpp`, `src/Editor/NullEditorLayer.cpp`,
   `src/Editor/Panels/RenderGraphPanel.cpp`,
   `src/Editor/Panels/RenderGraphPanel.h` — all modified, nothing else, no
   untracked files. Exactly the file set this phase's own "Verification" §3
   names.

## Honest notes for future phases

- This phase adds ZERO observable behavior change, confirmed both by design
  (a clean compile of pure signature widening, with every new parameter
  either forwarded verbatim or explicitly discarded) and by the live
  screenshot above showing an identical-looking Editor to before this phase.
- `RenderGraphPanel::Build()`'s body now carries THREE unused-parameter
  markers total (the pre-existing `EditorContext& /*ctx*/` from an earlier
  campaign, plus this phase's own 2 new ones) — PHASE4 is what finally wires
  real UI code for all three and removes every marker in one single pass, per
  Step 3.4's explicit instruction not to touch the pre-existing marker this
  phase.
- `m_core.GetRenderFeatureCompositor()` is now called TWICE per frame inside
  `EditorHost.cpp`'s `BuildUI()` block (once for the pre-existing
  `const`-typed local used to build `renderFeatureEntries`, once fresh for
  the new trailing `BuildUI()` argument) — both calls are cheap, cached
  `noexcept` pointer accessors (`Core.h`), so this has no meaningful runtime
  cost, exactly as the phase doc anticipated.
- `Core::GetRenderPassToggleRegistryMutable()` returns a genuinely mutable
  `rg::RenderPassToggleRegistry&` — the SAME single instance
  `RenderPipeline::DeclareOnePhase()` already consults every frame (PHASE1),
  now also reachable, unsynchronized, from the main-thread-only
  `BuildUI()`/`RenderGraphPanel::Build()` path this phase wires through —
  exactly the "no data race, same thread" design PHASE0's Step 2.6 describes.
  PHASE4 is the first phase that will actually call a mutating method on it
  from inside `RenderGraphPanel::Build()`.
