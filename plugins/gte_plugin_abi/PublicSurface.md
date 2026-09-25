# `gte_plugin_abi` — Public Surface

A short, explicit, reviewed list (source design doc Section 3.3) of exactly
which types cross the plugin ABI boundary, kept up to date as later phases
add to it. **Zero real `gte_core`/`gte_editor` header is ever included by
anything under `plugins/gte_plugin_abi/`** — confirmed by PHASE1's own
compile check (building this folder's headers with an include path limited
to exactly `plugins/gte_plugin_abi/` plus its CMake-generated-headers folder,
nothing else, succeeds — see `PHASE1_COMPLETION_REPORT.md` for the exact
verification evidence).

## As of PHASE1 (this campaign's foundation phase)

- `GtePluginAbiFingerprint` (`GtePluginAbiFingerprint.h`) — the fixed-size POD
  handshake struct, plus its `operator==`.
- `MakeThisBuildsFingerprint()` / `kGtePluginAbiContractGeneration`
  (`GtePluginAbiFingerprintGenerated.h`, CMake-generated from
  `GtePluginAbiFingerprintGenerated.h.in` at configure time — never
  hand-edited, never committed as a generated artifact).
- `GtePluginModuleInfo` (`GtePluginModuleInfo.h`) — plain fixed-size `char[]`
  identity fields (name/version/description), never `std::string`.
- `IPluginModule` (`IPluginModule.h`) — the one interface every plugin
  implements directly (`QueryCapability()` + `GetModuleInfo()`).
- The three fixed `extern "C"` exports every plugin `.dll` provides
  (`PluginExports.h`): `GTE_GetPluginAbiFingerprint`, `GTE_CreatePluginModule`,
  `GTE_DestroyPluginModule` — documented here as C++ function-pointer
  typedefs (`PFN_GTE_*`), resolved by literal name via `GetProcAddress()` in
  `PluginHost` (PHASE2), never called directly from within this folder.

## Added by later phases (tracked here as each phase lands)

- PHASE3: `IRenderFeatureModule_v1` / `IPluginRenderPassBuilder`.
- PHASE4: `IEditorPanelModule_v1` / `IPluginPanelDrawContext`.
- editor-core-separation-5 campaign, PHASE1: `PluginExportsMacro.h`
  (`GTE_DEFINE_PLUGIN_EXPORTS`/`GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE`) and
  `SingleCapabilityPluginModule.h` (`MakeModuleInfo()`,
  `SingleCapabilityPluginModule<T>`, `ZeroCapabilityPluginModule`) - optional,
  additive authoring sugar around the existing IPluginModule/extern "C" contract
  above; zero ABI change, never required, never used by PluginHost itself
  (PluginHost only ever calls the three fixed extern "C" exports and
  IPluginModule's own two virtual methods, regardless of which flavor a given
  plugin .dll used to produce them).
- editor-core-separation-6 campaign, PHASE1: `RenderFeatureDescriptor.h`
  (`RenderFeatureStage`, `RenderFeatureBlendMode`,
  `GtePluginRenderFeatureDescriptor`, `MakeRenderFeatureDescriptor()`),
  `IPluginRenderPassBuilder_v2.h`, and `IRenderFeatureModule_v2` (appended to
  the existing `IRenderFeatureModule.h`, `IRenderFeatureModule_v1` completely
  untouched) - the additive `_v2` render-feature ABI. Only `PostComposite`
  and `PreUI` stages are actually wired into the live render graph (see
  `task_manager/editor-core-separation-6/PHASE0_MASTER_STRATEGY.md`'s Locked
  Design Decision #1). **Re-confirmed accurate as of PHASE8 (campaign
  closeout)** against the final, real, shipped shape of every one of these 3
  files - field names/method signatures did not drift from this bullet's own
  description during PHASES 4-6's real implementation work. Nothing else
  crossed this ABI boundary in PHASES 2-7: `IPluginCapabilityOrchestrator`,
  `LegacyRenderFeatureOrchestrator`, `EditorPanelCapabilityOrchestrator`,
  `RenderFeatureCompositor`, and `PluginRenderPassBuilderAdapter_v2` all live
  under `src/Core/Plugins/` (`gte_core`-internal), never under
  `plugins/gte_plugin_abi/` - confirmed by direct re-read of every new file
  those phases added before writing this note.

## Rules every type on this list must follow (Locked Design Decision #3)

Every cross-boundary interface method signature uses ONLY plain, built-in
C++ types — `const char*`, `bool`, `float`, `int`, fixed-size POD structs, raw
non-owning pointers/references to `gte_plugin_abi`'s own interface types.
Never `std::string`, `std::vector`, `std::filesystem::path`, or any real
`gte_core`/`gte_editor` class type, by value or by reference, anywhere in
this folder. A `const char*` returned across the boundary is always a
stable, static-duration string literal — never a freshly-heap-allocated
buffer — documented at each such declaration, so there is never an ownership
question to resolve for it.
