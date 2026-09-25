# PHASE7 — "Plugin Render Features" Section in the Editor's "Render Graph" Panel — COMPLETION REPORT

**Status: DONE.** Implemented per `PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md`'s
own Step 3.1–3.3, with two small, documented, precedent-following deviations
from its literal wording (see "Deviations from the plan" below) — the
observable feature itself (the new "Plugin Render Features" section, with
exactly the text the phase file specifies) is implemented exactly as
described.

## What changed

### 1. New file — `src/Core/Plugins/RenderFeatureDebugEntry.h`

A small, dependency-free POD struct:

```cpp
struct RenderFeatureDebugEntry {
    std::string name;
    std::string stage;      // "PostComposite" or "PreUI"
    std::int32_t priority = 0;
    std::string blendMode;  // "Replace"/"AlphaOver"/"Additive"/"Multiply"/"ScreenSpaceMask"
};
```

Placed in its own header, co-located with `RenderFeatureCompositor.h` (the
gte_core subsystem that produces it) rather than defined inline inside that
same header — see "Deviations from the plan", item 1, below.

### 2. `src/Core/Plugins/RenderFeatureCompositor.h`/`.cpp`

- `#include "RenderFeatureDebugEntry.h"` added to the header.
- New public method declared: `std::vector<RenderFeatureDebugEntry> DebugSnapshot() const;`
- `.cpp` gained two file-local (anonymous-namespace) helpers,
  `ToString(RenderFeatureStage)`/`ToString(RenderFeatureBlendMode)`, mirroring
  `RenderGraphTypes.cpp`'s own `ToString(RenderPassEvent)`/
  `ToString(RenderPassDrawKind)` precedent exactly (deliberately no `default:`
  case, "Unknown" fallback).
- `DebugSnapshot()`'s body walks `m_postComposite` then `m_preUi` — the exact
  same combined order `ContributeRenderGraphPasses()` itself uses — converting
  each `Entry`'s descriptor into a `RenderFeatureDebugEntry`.

### 3. `src/Core/Core.h`/`.cpp`

- New forward declaration: `class RenderFeatureCompositor;` (mirrors the
  existing `IPluginCapabilityOrchestrator` forward-declare precedent — no
  `#include` of the real header needed in `Core.h`).
- New private member: `RenderFeatureCompositor* m_renderFeatureCompositorPtr = nullptr;`,
  declared immediately after `m_capabilityOrchestrators`.
- New public accessor: `const RenderFeatureCompositor* GetRenderFeatureCompositor() const noexcept { return m_renderFeatureCompositorPtr; }`
- `Core::RegisterBuiltinCapabilityOrchestrators()` now constructs the
  `RenderFeatureCompositor` into a local `auto` variable first, captures its
  raw address into `m_renderFeatureCompositorPtr`, THEN moves the
  `unique_ptr` into `m_capabilityOrchestrators` — exactly the sequence
  `PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md`'s own Step 3.2 code block
  specifies, so the pointer is guaranteed to stay in sync with the owning
  vector entry. **Confirmed via `search_in_dir` for `dynamic_cast` across the
  whole `src/` tree: zero hits — no RTTI was introduced anywhere.**

### 4. `src/Editor/EditorLayer.h`

- `#include "../Core/Plugins/RenderFeatureDebugEntry.h"` added (mirrors the
  existing `GpuDrivenBatchDebugInfo.h` include immediately above it exactly).
- `IEditorLayer::BuildUI()`'s pure-virtual signature gained one new, LAST
  trailing parameter: `const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries`.

### 5. `src/Editor/NullEditorLayer.cpp` / `src/Editor/ImGuiEditorLayer.cpp`

Both `BuildUI()` overrides updated to match the new signature.
`ImGuiEditorLayer::BuildUI()`'s own body now forwards `renderFeatureEntries`
into `m_renderGraphPanel.Build(...)` as its own new trailing argument.

### 6. `src/Editor/EditorHost.cpp`

The one real call site that constructs the actual `renderFeatureEntries`
value passed into `IEditorLayer::BuildUI()`:

```cpp
const RenderFeatureCompositor* renderFeatureCompositor = m_core.GetRenderFeatureCompositor();
const std::vector<RenderFeatureDebugEntry> renderFeatureEntries =
    renderFeatureCompositor != nullptr ? renderFeatureCompositor->DebugSnapshot()
                                        : std::vector<RenderFeatureDebugEntry>{};
m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph, m_atmosphereSettings, m_atmosphereLutRenderer,
    m_core.GetGpuDrivenBatchDebugInfo(), renderFeatureEntries);
```

mirroring `m_core.GetGpuDrivenBatchDebugInfo()`'s own exact "compute at the
host-level call site, pass as a plain trailing argument" precedent
immediately next to it. A new `#include "../Core/Plugins/RenderFeatureCompositor.h"`
was added (needed for the real `->DebugSnapshot()` call — `Core.h` itself
only forward-declares the type).

### 7. `src/Editor/Panels/RenderGraphPanel.h`/`.cpp`

- `#include "../../Core/Plugins/RenderFeatureDebugEntry.h"` added to the
  header.
- `Build()`'s signature gained one new, LAST trailing parameter:
  `const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries`.
- New function-local free function `BuildPluginRenderFeaturesSection(...)`,
  mirroring `BuildGpuDrivenBatchesSection()`'s exact shape (per the phase
  file's own Step 3.3 code block, implemented verbatim):

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

- Called from `Build()` right after `BuildGpuDrivenBatchesSection(...)`,
  exactly as the phase file specifies.

## Deviations from the plan

1. **`RenderFeatureDebugEntry` was placed in its own new header,
   `RenderFeatureDebugEntry.h`, instead of being defined inline inside
   `RenderFeatureCompositor.h` as the phase file's Step 3.1 code block
   literally shows.** Reason: `RenderFeatureCompositor.h` pulls in
   `ComputeDescriptorSet.h`/`ComputePipeline.h`/`RenderTexture.h`/
   `RenderGraphBuilder.h`/`volk.h` — genuinely heavier Vulkan-facing
   dependencies than `EditorLayer.h` (the one documented
   gte_core-visible-from-gte_editor exception file) needs just to see one
   small POD type. This codebase already has an established, reviewed
   precedent for EXACTLY this situation:
   `src/Renderer/Culling/GpuDrivenBatchDebugInfo.h`'s own header comment
   documents that its struct was deliberately relocated OUT of `EditorLayer.h`
   into its own small, dependency-free header, co-located with the subsystem
   that produces it, for this exact reason. Mirroring that already-reviewed
   pattern (AGENTS.md's own "never invent a new pattern where an existing file
   already shows the exact shape to copy" rule) was judged the more correct
   engineering decision than a literal copy-paste of the phase file's inline
   code sketch — the OBSERVABLE result (a `RenderFeatureDebugEntry` type with
   the exact 4 fields the phase file specifies, consumed identically by the
   panel) is unchanged. This was not treated as a genuine design ambiguity
   requiring `ask_questions`, since the codebase itself already shows the
   answer via a directly-analogous, previously-reviewed precedent.
2. **The "whichever single call site threads the new argument through" language
   in the phase file's own Step 3.2/Verification-3 undersold how many files
   actually needed a one-line signature change.** Because
   `IEditorLayer::BuildUI()` is a pure-virtual interface method (not a single
   concrete function), adding its new trailing parameter required touching
   FOUR files, not one: `EditorLayer.h` (the interface declaration),
   `ImGuiEditorLayer.cpp` (the real override, plus the real
   `m_renderGraphPanel.Build(...)` call site), `NullEditorLayer.cpp` (the
   release/Player-facing no-op override, which must keep compiling), and
   `EditorHost.cpp` (the actual call site that constructs the real
   `renderFeatureEntries` value via `Core::GetRenderFeatureCompositor()`).
   This is the exact same shape `gpuDrivenBatchDebugInfo` (the immediately
   preceding parameter) already has across these same four files — not a new
   pattern, just a more complete enumeration of what "the call site" already
   meant. `git_status`'s own diff (below) reflects this honestly.

Everything else matches `PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md`'s own plan
exactly: no new panel was added, no existing `RenderFeatureCompositor`
ordering/blending logic was touched, and every literal string this phase's
own Verification section names (`"[PostComposite] DemoRenderFeatureV2 -
priority 0, blend Replace"` and `"[PreUI] DemoRenderFeatureV2Second -
priority 0, blend AlphaOver"`) is byte-for-byte what actually rendered (see
evidence below).

## What this phase does NOT do (confirmed honored)

- Does not add a NEW panel — extends the existing "Render Graph" panel only.
- Does not change `RenderFeatureCompositor`'s own real ordering/blending logic
  in any way — `DebugSnapshot()` is a pure, read-only, `const` method that
  only converts already-resolved state into display strings.
- Introduces no `dynamic_cast`/RTTI anywhere (confirmed via `search_in_dir`).

## Verification evidence

### 1. Incremental build

`cmake --build build` (Ninja/MinGW). **16/16 steps succeeded** on the first
attempt, zero compile errors, zero new warnings:

```
[1/16] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/PluginRenderPassBuilderAdapter_v2.cpp.obj
[2/16] Building CXX object CMakeFiles/gte_core.dir/src/Editor/NullEditorLayer.cpp.obj
[3/16] Building CXX object CMakeFiles/gte_editor.dir/src/Editor/GBufferValidation.cpp.obj
[4/16] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/LegacyRenderFeatureOrchestrator.cpp.obj
[5/16] Building CXX object CMakeFiles/gte_editor.dir/src/Editor/Panels/RenderGraphPanel.cpp.obj
[6/16] Building CXX object CMakeFiles/gte_editor.dir/src/Editor/Panels/FrameDebuggerPanel.cpp.obj
[7/16] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/RenderFeatureCompositor.cpp.obj
[8/16] Building CXX object CMakeFiles/gte_core.dir/src/Core/Core.cpp.obj
[9/16] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Core/CoreHeadlessConstructionTests.cpp.obj
[10/16] Building CXX object CMakeFiles/gte_editor.dir/src/Editor/ImGuiEditorLayer.cpp.obj
[11/16] Building CXX object CMakeFiles/gte_editor.dir/src/Editor/EditorHost.cpp.obj
[12/16] Linking CXX static library libgte_core.a
[13/16] Linking CXX static library libgte_editor.a
[14/16] Building CXX object CMakeFiles/GreatTamanaEditor.dir/src/main.cpp.obj
[15/16] Linking CXX executable GreatTamanaEditor.exe (+ shader staging)
[16/16] Linking CXX executable tests\GreatTamanaEngineTests.exe
```

### 2. Live smoke test — real screenshot evidence (not just the honest-fallback path)

Ran `build\GreatTamanaEditor.exe` (PID 18760).

**`GET /get_logs?limit=200`** — all 6 demo plugins (4 `_v1`-era +
`demo_editor_panel` + `demo_hello_world`, plus the 2 permanent `_v2` demo
plugins from PHASE6) loaded correctly, with the exact same warnings as
PHASE6's own recorded baseline and zero new ones:

```
[Info] PluginHost: "Loaded plugin 'DemoRenderFeatureV2Plugin' v1.0.0 from ...\plugins\demo_render_feature_v2.dll"
[Info] PluginHost: "Loaded plugin 'DemoRenderFeatureV2SecondPlugin' v1.0.0 from ...\plugins\demo_render_feature_v2_second.dll"
[Warning] PluginHost: "2 loaded plugins implement IRenderFeatureModule_v1 - ..." (byte-for-byte unchanged)
[Warning] RenderGraph: 3x "could not be assigned a GPU-timing slot" (the same 3 pass names PHASE6 already documented)
```

**`GET /activate_tab?name=Render Graph`** → `{"activated_tab":"Render Graph","success":true}`.

**`GET /get_swapchain`** — a real screenshot (loaded via `load_image`, not
just reasoned about) confirms the "Render Graph" panel now shows, directly
below "GPU-Driven Batches" and above "Offscreen Regime":

```
Plugin Render Features
[PostComposite] DemoRenderFeatureV2 - priority 0, blend Replace
[PreUI] DemoRenderFeatureV2Second - priority 0, blend AlphaOver
```

— an EXACT, character-for-character match to
`PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md`'s own Verification step 2 expected
text for both lines. This is real, direct visual confirmation via the
`GET /get_swapchain` path the phase file itself anticipated might not exist
yet — it does, so no honest-fallback reasoning-only path was needed.

**`GET /get_logs?limit=300&min_level=Warning`** — exactly the same 5 warning
entries already quoted above (1 CRT-linkage notice, 1 `_v1` multi-plugin
warning, 3 GPU-timing-slot-budget warnings) — zero new warnings or errors
from this phase's own change.

**`GET /list_tabs`** — unchanged: `["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]`.

`stop_app_background` called at the end of the check (PID 18760).

### 3. `git_status`

Before commit, the diff is exactly:

```
modified:   src/Core/Core.cpp
modified:   src/Core/Core.h
modified:   src/Core/Plugins/RenderFeatureCompositor.cpp
modified:   src/Core/Plugins/RenderFeatureCompositor.h
modified:   src/Editor/EditorHost.cpp
modified:   src/Editor/EditorLayer.h
modified:   src/Editor/ImGuiEditorLayer.cpp
modified:   src/Editor/NullEditorLayer.cpp
modified:   src/Editor/Panels/RenderGraphPanel.cpp
modified:   src/Editor/Panels/RenderGraphPanel.h
untracked:  src/Core/Plugins/RenderFeatureDebugEntry.h
```

— matching the phase file's own "touches only `RenderFeatureCompositor.h`/
`.cpp`, `Core.h`, `RenderGraphPanel.h`/`.cpp`, and whichever call site(s)
thread the new argument through" requirement (see "Deviations from the plan",
item 2, for the honest, complete enumeration of those call-site files).

## Summary

The Editor's "Render Graph" panel now has a real, live "Plugin Render
Features" section showing every loaded `IRenderFeatureModule_v2` plugin's own
REAL, resolved ordering decision (stage, name, priority, blend mode) — the
compositor's own internal state, made directly, honestly visible to a
developer or an automated HTTP-driven check, with zero change to
`RenderFeatureCompositor`'s actual rendering/ordering/blending behavior. The
live screenshot evidence confirms the exact text the phase's own plan
specifies. PHASE8 (docs, full regression, campaign closeout) can now proceed.
