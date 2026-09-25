# PHASE2 — `IPluginCapabilityOrchestrator` Registry + Legacy Render-Feature Migration — COMPLETION REPORT

**Status: DONE.** Implemented exactly as written in
`PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md`,
with one confirmed, necessary, additive deviation (see "Deviations" below)
found during the incremental-build verification step itself.

## What changed

1. **New file** `src/Core/Plugins/IPluginCapabilityOrchestrator.h` — the
   generic `IPluginCapabilityOrchestrator` interface, verbatim match against
   the phase file's Step 3.1 code block: `OnPluginsLoaded(const
   std::vector<IPluginModule*>&)` (pure virtual) and
   `ContributeRenderGraphPasses(const rg::RenderPassFrameContext&,
   std::vector<rg::RenderPassDesc>&)` (virtual, default no-op body).

2. **New files** `src/Core/Plugins/LegacyRenderFeatureOrchestrator.h`/`.cpp` —
   `LegacyRenderFeatureOrchestrator final : public IPluginCapabilityOrchestrator`,
   holding a `Core&`. Its two methods are a verbatim relocation of:
   - `OnPluginsLoaded()` — the exact multi-plugin warning body that used to
     live directly inside `Core::LoadPlugins()` (editor-core-separation-4,
     PHASE5) — same trigger condition (`> 1`), same exact warning string,
     byte-for-byte.
   - `ContributeRenderGraphPasses()` — the exact loop body that used to live
     directly inside the `"PluginRenderFeatures"` provider's lambda
     (editor-core-separation-3, PHASE3): resolves the view's plugin-facing
     target via the new `Core::FindPluginRenderFeatureTarget()` accessor
     (instead of re-deriving `isGameView`/`compositedKey`/`pluginTarget`
     inline), then keeps `QueryCapability`/`PluginRenderPassBuilderAdapter`/
     `finalTextureOutputs.push_back` completely unchanged.

3. **`Core.h`/`Core.cpp` wiring**:
   - `Core.h`: added `#include <memory>`, a forward declaration for
     `IPluginCapabilityOrchestrator` (mirroring `IEditorLayer`'s own
     forward-decl-only precedent), a new public nested type
     `Core::PluginRenderFeatureTargetInfo { rg::TextureHandle target;
     VkExtent2D extent{}; }`, a new public accessor
     `std::optional<PluginRenderFeatureTargetInfo>
     FindPluginRenderFeatureTarget(const rg::RenderPassFrameContext&) const`,
     a new private method `void RegisterBuiltinCapabilityOrchestrators();`,
     and a new private member
     `std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>>
     m_capabilityOrchestrators;`.
   - `Core.cpp`: constructor now calls `RegisterBuiltinCapabilityOrchestrators()`
     before `RegisterOffscreenRenderPipelineProviders()`.
     `RegisterBuiltinCapabilityOrchestrators()`'s ENTIRE body is
     `m_capabilityOrchestrators.push_back(std::make_unique<LegacyRenderFeatureOrchestrator>(*this));`
     — exactly the plan's Step 3.4 snippet. `Core::LoadPlugins()` now loops
     `for (auto& orchestrator : m_capabilityOrchestrators) {
     orchestrator->OnPluginsLoaded(m_pluginHost.AllLoadedModules()); }`
     instead of hand-coding the warning inline. `Core::FindPluginRenderFeatureTarget()`
     is the exact 3-line `isGameView`/`compositedKey`/`pluginTarget`
     computation the old provider body used to do inline, plus one new line
     computing `.extent` (needed by a future PHASE4 consumer,
     `RenderFeatureCompositor` — `LegacyRenderFeatureOrchestrator` itself
     never reads `.extent`). The `"PluginRenderFeatures"` provider's body
     collapsed to the plan's Step 3.4 generic loop:
     `for (auto& orchestrator : m_capabilityOrchestrators) {
     orchestrator->ContributeRenderGraphPasses(frame, out); }` — same
     provider name, same `ProviderScope::PerActiveView`, same
     `ProviderTiming::AfterDeferredPasses`, same LAST-`Register(...)`-call
     position in the function (still immediately after `"AtmosphereComposite"`'s
     own call).
   - Removed now-dead includes from `Core.cpp` (confirmed via `search_in_dir`
     before removing — zero remaining real code usage, only stale comments):
     `../../plugins/gte_plugin_abi/IRenderFeatureModule.h`,
     `../../plugins/gte_plugin_abi/IPluginRenderPassBuilder.h`,
     `Plugins/PluginRenderPassBuilderAdapter.h`,
     `Plugins/PluginRenderFeatureDiagnostics.h`, `Logging.h` — that real
     logic (and those includes) now live entirely inside
     `LegacyRenderFeatureOrchestrator.cpp`. Added
     `#include "Plugins/IPluginCapabilityOrchestrator.h"` and
     `#include "Plugins/LegacyRenderFeatureOrchestrator.h"` instead.

4. **`CMakeLists.txt`** — added
   `src/Core/Plugins/IPluginCapabilityOrchestrator.h`,
   `src/Core/Plugins/LegacyRenderFeatureOrchestrator.h`,
   `src/Core/Plugins/LegacyRenderFeatureOrchestrator.cpp` to `gte_core`'s
   source-file list, inserted immediately after
   `PluginRenderFeatureDiagnostics.h`/`.cpp`'s own existing entries, same
   relative style/comment convention as every neighboring entry.

## Deviations from the plan

**One confirmed, necessary, additive deviation — NOT anticipated by the
phase file's own Step 3.1–3.5 text**, found by the incremental-build
verification step itself (Workflow Rule per `AGENTS.md`'s testability
discipline: "a change can compile cleanly while still silently breaking..." —
here it did NOT compile cleanly at all, caught immediately):

`Core` gained an explicit destructor, declared in `Core.h` and defined
`= default` out-of-line in `Core.cpp`:

```cpp
// Core.h
~Core();
// Core.cpp
Core::~Core() = default;
```

**Why:** `m_capabilityOrchestrators` is a
`std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>>`, and
`IPluginCapabilityOrchestrator` is (correctly, per the phase file's own Step
3.1/3.4 design) only ever FORWARD-declared in `Core.h` — never `#include`d
there. Before this fix, `Core` had no explicit destructor, so the compiler
implicitly generates one wherever a `Core` object is destroyed — which
happens in OTHER translation units too (`tests/Core/CoreHeadlessConstructionTests.cpp`'s
own `std::unique_ptr<Core> core;`, and `EditorHost`'s own `std::unique_ptr<Core>
m_core`), not just inside `Core.cpp` itself. `std::unique_ptr<T>`'s destructor
needs `T` to be a COMPLETE type at the point the surrounding destructor is
generated — this is the classic "incomplete type behind `unique_ptr`,
forward-declared in a header, implicit destructor generated in a different TU"
pitfall (the exact same reason `pimpl`-style classes always declare an
explicit, out-of-line destructor). This surfaced as two real, confirmed
compile errors during this phase's own mandatory incremental-build
verification (`tests/Core/CoreHeadlessConstructionTests.cpp.obj` and
`src/Editor/EditorHost.cpp.obj`, both:
`error: invalid application of 'sizeof' to incomplete type
'gte::IPluginCapabilityOrchestrator'`), not a hypothetical — see the raw
compiler output captured during this phase's own build step.

**Fix, and why it is the correct minimal one**: declare `~Core();` in
`Core.h` (no body), define `Core::~Core() = default;` in `Core.cpp` — the
one translation unit that DOES `#include` the real
`Plugins/IPluginCapabilityOrchestrator.h`/`LegacyRenderFeatureOrchestrator.h`
headers, so `IPluginCapabilityOrchestrator` IS a complete type at the exact
point the destructor's body is actually emitted. This changes zero
observable runtime behavior (the generated destructor still does exactly
what the implicit one would have — destroy every member in reverse
declaration order — `= default` guarantees this), and required zero other
design change: the phase file's own Locked Design Decision to keep
`IPluginCapabilityOrchestrator` forward-declared-only in `Core.h` (never
`#include`d there) is preserved exactly as written; only `Core`'s own
destructor visibility needed to change to make that decision compile
correctly everywhere `Core` itself is destroyed, not just inside `Core.cpp`.

No other deviation. Every string literal, warning message, provider name,
`ProviderScope`/`ProviderTiming` value, and control-flow branch from the
original code is preserved byte-for-byte, confirmed both by direct
before/after diff reading during editing and by the live verification below.

## Verification evidence

### 1. Incremental build

`cmake --build build` (Ninja/MinGW). First attempt failed with the two
`incomplete type 'gte::IPluginCapabilityOrchestrator'` errors described above
(caught immediately, fixed per the Deviations section, re-built). Final
result: **9/9 steps succeeded** — `gte_core` (including
`LegacyRenderFeatureOrchestrator.cpp` and `Core.cpp`), `GreatTamanaEngineTests.exe`,
`gte_editor` (`EditorHost.cpp`), and `GreatTamanaEditor.exe` all compiled and
linked with zero errors.

### 2. Live smoke test

`run_app_background` on `build\GreatTamanaEditor.exe`, waited ~3 seconds for
startup, then `GET /get_logs?limit=100` (no `/clear_logs` first, specifically
to catch the plugin-load-time warning that fires before any HTTP client could
possibly connect and clear it) — response (trimmed to the two load-bearing
entries):

```json
{"category":"PluginHost","frame":0,"id":4,"level":"Info","message":"Loaded plugin 'DemoRenderFeaturePlugin' v1.0.0 from ...\\plugins\\demo_render_feature.dll", ...}
{"category":"PluginHost","frame":0,"id":5,"level":"Info","message":"Loaded plugin 'DemoRenderFeaturePluginSecond' v1.0.0 from ...\\plugins\\demo_render_feature_second.dll", ...}
{"category":"PluginHost","frame":0,"id":6,"level":"Warning","message":"2 loaded plugins implement IRenderFeatureModule_v1 - only the LAST-registered one's render output will be visible this frame (render-graph compositing for multiple render-feature plugins is not implemented - see docs/conventions/plugin-architecture.md).", ...}
```

— **byte-for-byte identical** to the phase file's own Step 2 quoted warning
text, confirming `LegacyRenderFeatureOrchestrator::OnPluginsLoaded()`'s
relocation changed nothing observable. Both `demo_render_feature`/
`demo_render_feature_second` are still the only 2 `_v1` plugins loaded (this
phase added no new plugin, per its own "What this phase does NOT do"
section).

`GET /get_game_view` — returned a 2368-byte PNG, visually confirmed **solid
magenta**, the exact same pre-existing visual baseline (both `_v1` demo
plugins still clear to the same magenta; the "last write wins" bug's outcome
is unchanged, exactly as this phase's own goal requires — a pure internal
refactor, zero new user-visible capability).

`GET /list_tabs` — returned
`["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]`
— unaffected by this phase (PHASE3's own future concern), included here as
an extra confirmation that plugin-panel wiring is untouched.

`stop_app_background` called at the end of the check.

### 3. `git_status`

Before commit, the diff is exactly:

```
modified:   CMakeLists.txt
modified:   src/Core/Core.cpp
modified:   src/Core/Core.h
new file:   src/Core/Plugins/IPluginCapabilityOrchestrator.h
new file:   src/Core/Plugins/LegacyRenderFeatureOrchestrator.cpp
new file:   src/Core/Plugins/LegacyRenderFeatureOrchestrator.h
```

— matching the phase file's own "Verification" step 3 exactly (`Core.h`,
`Core.cpp`, `CMakeLists.txt`, and the 3 new `src/Core/Plugins/` files, no
other file touched).

## What this phase does NOT do (confirmed honored)

- Did not migrate `IEditorPanelModule_v1` (left for PHASE3 — `EditorHost.cpp`
  itself was not touched by this phase at all).
- Did not add `RenderFeatureCompositor` or anything `_v2`-related (PHASE4).
- Did not change `CountModulesImplementingRenderFeature()`'s own warning
  text, wording, or trigger condition (`> 1` modules) in any way — its
  header/source files (`PluginRenderFeatureDiagnostics.h`/`.cpp`) were not
  edited at all, only relocated-to-call-site (`LegacyRenderFeatureOrchestrator.cpp`)
  gained a new `#include` of the unchanged header.
