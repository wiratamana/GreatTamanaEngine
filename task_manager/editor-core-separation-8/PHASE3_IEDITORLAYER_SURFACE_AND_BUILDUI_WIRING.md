# PHASE3 — `IEditorLayer` Surface + `BuildUI()` Wiring

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially Step 2.6
and Locked Architecture Decisions #13/#14). Also read `PHASE1_COMPLETION_REPORT.md`
and `PHASE2_COMPLETION_REPORT.md` first — this phase's new `BuildUI()`
parameters reference the EXACT types those two phases just shipped
(`rg::RenderPassToggleRegistry`, `RenderFeatureCompositor`). Use
`ask_questions` for any genuine ambiguity.

## Step 1: The Goal

Thread `Core`'s two PHASE1/PHASE2 objects (`rg::RenderPassToggleRegistry&`
and `RenderFeatureCompositor*`) all the way from `EditorHost.cpp` (the one
place that owns both `Core` and the concrete `IEditorLayer`) down to
`IEditorLayer::BuildUI()`'s own signature, and add 2 new plain `IEditorLayer`
virtual setters for the Blur/GBuffer `EditorContext` booleans. By the end of
this phase, `RenderGraphPanel::Build()` (PHASE4's job) WILL be able to
receive everything it needs to draw and mutate pass/feature toggle state
directly — but this phase itself does not change `RenderGraphPanel` at all
yet; it only widens the signatures the data flows through. Zero observable
behavior change.

## Step 2: The Situation

Read these exact files in full before writing any code:

- `src/Editor/EditorLayer.h` — the WHOLE file. Confirm:
  - The EXACT current `BuildUI()` signature (7 parameters today, ending in
    `const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries`).
  - How `rg::RenderGraph` is forward-declared today (`namespace rg { class
    RenderGraph; }` or similar) — mirror this EXACT style for the two new
    forward declarations this phase adds.
  - Where `FrameDebuggerSetEnabled(bool enabled)`'s declaration lives (around
    line ~655 per the Investigation, but CONFIRM the real current line) — the
    2 new Blur/GBuffer setters this phase adds go in a similarly-named,
    similarly-documented spot (their own logical group, not jammed into the
    Frame Debugger's own block).
  - `RenderFeatureDebugEntry.h`'s own `#include` here (confirms EditorLayer.h
    ALREADY includes one small `gte_core` header directly — the established
    precedent this phase's own 2 new forward-declares extend, per Locked
    Architecture Decision #13's own justification).
- `src/Editor/ImGuiEditorLayer.cpp` — find the EXACT current `BuildUI()`
  override (search for `void BuildUI(Game& game`) and the 2 existing
  `FrameDebuggerSetEnabled`-shaped setter overrides, to mirror their EXACT
  one-line body style for this phase's own 2 new setters.
- `src/Editor/NullEditorLayer.cpp` — find the EXACT current `BuildUI()`
  override (a no-op body) and `FrameDebuggerSetEnabled(bool /*enabled*/) { }`'s
  own exact stub style, to mirror it for this phase's 2 new setters.
- `src/Editor/EditorHost.cpp` — find the EXACT current call site of
  `m_editorLayer->BuildUI(...)` (inside the
  `{ GTE_PROFILE_SCOPE("IEditorLayer::BuildUI"); ... }` block — this is the
  SAME block `editor-core-separation-7`'s own PHASE4 already extended once
  before, to publish `RenderGraphMetadata` — read that surrounding code
  carefully, since this phase adds its own 2 new trailing ARGUMENTS to the
  SAME existing call, immediately after the already-existing
  `renderFeatureEntries` argument).

## Step 3: The Plan

### Step 3.1 — `EditorLayer.h` changes

1. New forward declarations, near the existing `namespace rg { class
   RenderGraph; }` block:

```cpp
namespace rg {
class RenderGraph;
class RenderPassToggleRegistry; // editor-core-separation-8 campaign, PHASE1/PHASE3.
} // namespace rg

class RenderFeatureCompositor; // editor-core-separation-8 campaign, PHASE2/PHASE3 -
    // forward-declared only, mirrors "class Core;"'s own forward-declare-only
    // precedent elsewhere in this codebase - the REAL header
    // (src/Core/Plugins/RenderFeatureCompositor.h) is heavy (pulls in
    // ComputeDescriptorSet.h/ComputePipeline.h/RenderTexture.h/volk.h) and is
    // only ever #included by the .cpp files that actually CALL a method on
    // this pointer (ImGuiEditorLayer.cpp, RenderGraphPanel.cpp) - this header
    // itself only ever passes the pointer through, never dereferences it.
```

2. `BuildUI()`'s signature gains 2 new TRAILING parameters (never inserted
   in the middle — matches this file's own established "append new
   parameters at the end" convention, confirmed by `renderFeatureEntries`
   itself having been added this exact way by an earlier campaign):

```cpp
virtual void BuildUI(Game& game, Renderer& renderer, const rg::RenderGraph& renderGraph,
    AtmosphereSettings& atmosphereSettings, AtmosphereLutRenderer& atmosphereLutRenderer,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
    // editor-core-separation-8 campaign, PHASE3 - NEVER null (Core owns
    // exactly one instance as a plain value member - see
    // Core::GetRenderPassToggleRegistryMutable()). The "Render Graph" panel
    // (PHASE4) reads/writes THROUGH this exact reference to draw and mutate
    // its own "Enabled" checkbox column - see PHASE0_MASTER_STRATEGY.md's
    // Step 2.6 for why this is passed as a plain mutable reference rather
    // than routed through a bridge: both this call and
    // RenderPipeline::DeclareOnePhase()'s own consult of the SAME registry
    // happen on the main thread only, so there is no data race to guard
    // against here (unlike the genuinely cross-thread HTTP path, PHASE5).
    rg::RenderPassToggleRegistry& renderPassToggleRegistry,
    // editor-core-separation-8 campaign, PHASE3 - NULLABLE, mirroring
    // Core::GetRenderFeatureCompositor()'s own existing nullability exactly
    // (null whenever no loaded _v2 plugin exists this session). The "Render
    // Graph" panel's own per-plugin-feature "Enabled" checkbox + priority
    // DragInt (PHASE4) call SetFeatureEnabled()/SetFeaturePriority()
    // directly through this pointer, always null-checked first.
    RenderFeatureCompositor* renderFeatureCompositor) = 0;
```

3. 2 new plain virtual setters, placed in their own small, clearly-labeled
   group (mirroring `FrameDebuggerSetEnabled`'s own doc-comment density,
   scaled down for how much simpler these two are):

```cpp
// editor-core-separation-8 campaign, PHASE3 - HTTP automation entry point
// (GET /render_graph/set_blur_enabled, PHASE5) for exactly the SAME state
// ScenePanel.cpp's own "Show Compute Blur (debug)" checkbox already flips
// directly (EditorContext::showBlurredSceneOutput) - and, per
// PHASE0_MASTER_STRATEGY.md's Locked Product Decision #10, also what the
// "Render Graph" panel's own NEW matching checkbox (PHASE4) calls. Mirrors
// FrameDebuggerSetEnabled(bool)'s exact shape - a plain, always-succeeding
// setter (no return value; there is no failure mode for flipping a bool).
virtual void SetShowBlurredSceneOutput(bool enabled) = 0;

// editor-core-separation-8 campaign, PHASE3 - the GBuffer Validation
// equivalent of SetShowBlurredSceneOutput() immediately above - same
// contract, same reasoning, mirrors EditorContext::showGBufferValidationOutput.
virtual void SetShowGBufferValidationOutput(bool enabled) = 0;
```

### Step 3.2 — `ImGuiEditorLayer.cpp` changes

1. `BuildUI()`'s override signature gains the exact same 2 new trailing
   parameters, and its body's existing call to `m_renderGraphPanel.Build(...)`
   gains the same 2 new trailing ARGUMENTS, forwarded verbatim (this phase
   does NOT change `RenderGraphPanel::Build()`'s own signature yet — that is
   PHASE4's job; if `RenderGraphPanel::Build()` does not yet accept these 2
   extra arguments, this phase's own build will fail to compile against it.
   **Resolve this by having THIS phase (PHASE3) also widen
   `RenderGraphPanel::Build()`'s signature by 2 trailing parameters RIGHT
   NOW, with the panel's own body simply ignoring both (an unused-but-named
   parameter, or a
   `(void)renderPassToggleRegistry; (void)renderFeatureCompositor;`
   discard) — PHASE4 then only needs to add the actual UI code that USES
   them, never touch the signature again.** This mirrors exactly how
   `RenderGraphPanel::Build()`'s own EXISTING `EditorContext& /*ctx*/`
   parameter was already added, unused, by an earlier campaign, for a LATER
   campaign (this one!) to finally use — the same "signature now, behavior
   later" split this codebase already uses.

2. 2 new setter overrides, placed near `FrameDebuggerSetEnabled`'s own
   override, in their own labeled group:

```cpp
void SetShowBlurredSceneOutput(bool enabled) override { m_ctx.showBlurredSceneOutput = enabled; }
void SetShowGBufferValidationOutput(bool enabled) override { m_ctx.showGBufferValidationOutput = enabled; }
```

### Step 3.3 — `NullEditorLayer.cpp` changes

1. `BuildUI()`'s override signature gains the same 2 new trailing
   parameters (both simply unused/discarded in the no-op body, exactly like
   every other parameter this override already ignores).
2. 2 new no-op setter stubs, mirroring `FrameDebuggerSetEnabled`'s own exact
   stub style:

```cpp
void SetShowBlurredSceneOutput(bool /*enabled*/) override { }
void SetShowGBufferValidationOutput(bool /*enabled*/) override { }
```

### Step 3.4 — `RenderGraphPanel.h`/`.cpp` signature-only widening (see Step 3.2's note above)

`RenderGraphPanel::Build()`'s declaration (`.h`) and definition (`.cpp`) both
gain the same 2 new trailing parameters as `BuildUI()`:

```cpp
void Build(EditorContext& ctx, const rg::RenderGraph& renderGraph,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
    rg::RenderPassToggleRegistry& renderPassToggleRegistry,
    RenderFeatureCompositor* renderFeatureCompositor);
```

`RenderGraphPanel.h` needs its own new forward declarations (or a direct
`#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"` — this
header is small/plain, a direct include is fine here since `RenderGraphPanel.h`
already directly includes `RenderGraphMetadata.h`, a comparable-weight
`gte_core` header) plus a forward declaration for `class RenderFeatureCompositor;`
(mirroring `EditorLayer.h`'s own choice — `RenderGraphPanel.cpp`, not `.h`,
is what will eventually `#include` the real, heavy
`RenderFeatureCompositor.h` in PHASE4, once it actually calls a method on
it).

`RenderGraphPanel.cpp`'s `Build()` body gains 2 new, clearly-commented,
UNUSED parameter markers for THIS phase only (PHASE4 removes these markers
when it adds the real UI code that uses them):

```cpp
void RenderGraphPanel::Build(EditorContext& /*ctx*/, const rg::RenderGraph& renderGraph,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
    rg::RenderPassToggleRegistry& /*renderPassToggleRegistry*/, // editor-core-separation-8, PHASE3: signature-only - PHASE4 wires real UI.
    RenderFeatureCompositor* /*renderFeatureCompositor*/) // editor-core-separation-8, PHASE3: signature-only - PHASE4 wires real UI.
{
    ... existing body, completely unchanged ...
}
```

**IMPORTANT:** do NOT touch the existing `EditorContext& /*ctx*/` marker in
this phase — leave it exactly as-is; PHASE4 is what finally removes BOTH
that pre-existing marker AND this phase's own 2 new markers, in one single
later pass, once real UI code exists for all three.

### Step 3.5 — `EditorHost.cpp` changes

The existing `m_editorLayer->BuildUI(...)` call gains 2 new trailing
arguments:

```cpp
m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph, m_atmosphereSettings, m_atmosphereLutRenderer,
    m_core.GetGpuDrivenBatchDebugInfo(), renderFeatureEntries,
    m_core.GetRenderPassToggleRegistryMutable(), m_core.GetRenderFeatureCompositor());
```

**A real, confirmed pitfall to avoid**: this same `{ GTE_PROFILE_SCOPE(...) }`
block already declares a LOCAL named `renderFeatureCompositor`
(`const RenderFeatureCompositor* renderFeatureCompositor = m_core.GetRenderFeatureCompositor();`,
used a few lines above to build `renderFeatureEntries` via `DebugSnapshot()`)
— that local is typed `const RenderFeatureCompositor*` (its declared type
never changes; only `Core::GetRenderFeatureCompositor()`'s OWN return type
widened in PHASE2). **Do NOT reuse that existing local as the new trailing
`BuildUI()` argument** — passing a `const RenderFeatureCompositor*` where
`BuildUI()`'s new parameter expects a plain (non-`const`)
`RenderFeatureCompositor*` will not compile. Call
`m_core.GetRenderFeatureCompositor()` a SECOND time, fresh, for this new
trailing argument instead (exactly as shown above) — this is cheap (it just
returns an already-computed, cached pointer, `Core.h`'s own `noexcept`
accessor), so calling it twice per frame has no meaningful cost.

Confirm this call sits in the EXACT SAME `{ GTE_PROFILE_SCOPE("IEditorLayer::BuildUI"); ... }`
block `editor-core-separation-7`'s own PHASE4 already documented as the
publish point for `RenderGraphMetadata` — do not move or duplicate this
call; only widen its own existing argument list.

### Verification

1. Incremental build: `cmake --build build`. This phase's ENTIRE point is
   getting every call site (both `IEditorLayer` implementations, and
   `RenderGraphPanel`'s own signature) to compile cleanly against the new,
   wider `BuildUI()`/`Build()` contract — a clean compile IS the primary
   proof of correctness for this phase.
2. No new automated test is needed for this phase (pure signature/plumbing
   widening, zero new logic) — but do a quick live sanity check anyway:
   `run_app_background` the real `GreatTamanaEditor.exe`,
   `gte_send_request("/get_swapchain")` + `load_image` to confirm the Editor
   still starts up and renders normally (a broken forward-declare/include
   chain here would typically fail to COMPILE, not merely misbehave at
   runtime — but a quick visual confirmation costs little and catches any
   surprise). `stop_app_background` afterward.
3. `git_status` — confirm the diff touches EXACTLY: `EditorLayer.h`,
   `ImGuiEditorLayer.cpp`, `NullEditorLayer.cpp`, `EditorHost.cpp`,
   `RenderGraphPanel.h`, `RenderGraphPanel.cpp`. Nothing else.

### What this phase does NOT do

- Does not add any real UI behavior to `RenderGraphPanel` — the 2 new
  parameters are received and immediately discarded this phase (PHASE4's
  job to actually use them).
- Does not add any HTTP surface (PHASE5's job).
- Does not run a full build or full `ctest` pass (Workflow Rule 1 — Phase 6
  only).

### Completion

Write `PHASE3_COMPLETION_REPORT.md` (confirm the exact final `BuildUI()`/
`Build()` signatures, and that every one of the 4 call sites — `EditorLayer.h`
declaration, `ImGuiEditorLayer.cpp` override, `NullEditorLayer.cpp` override,
`EditorHost.cpp`'s one real call — agree byte-for-byte), then `git_add` +
`git_commit`.
