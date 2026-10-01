# Plugin Architecture

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

> **REMOVED, `better-render-pass-2` campaign.** This entire ABI-versioned, runtime-`.dll`
> plugin system (`plugins/gte_plugin_abi`, `PluginHost`, `IRenderFeatureModule_v1/_v2/_v3`,
> `IPluginRenderPassBuilder/_v2/_v3`, `IEditorPanelModule_v1` as an ABI-versioned interface,
> `PluginRenderOperationRegistry`, every `demo_*` plugin folder) was fully removed by the
> `better-render-pass-2` campaign (`task_manager/better-render-pass-2/PHASE0_MASTER_STRATEGY.md`,
> `CAMPAIGN_COMPLETION_REPORT.md`). See `docs/conventions/project-assembly-system.md` for the
> system that replaced it as this engine's one remaining loadable-module mechanism -
> `IEditorPanelModule_v1`/`IPluginPanelDrawContext` were relocated (not deleted) into
> `src/Core/EditorPanelModule.h`, since Project Assembly's own custom Editor panel capability
> still implements them directly; `RenderFeatureCompositor`'s blend-compositing pipeline was
> kept and is reached today only through `Core::RegisterProjectRenderFeature()`/
> `Core::AddScreenPostProcessPass()`. **Everything below this notice is kept, verbatim, as a
> historical record of the removed design - it describes a system that no longer exists in
> this codebase.**

`plugins/gte_plugin_abi/` is the engine's frozen, versioned, minimal ABI
contract for a REAL, runtime-loadable `.dll` plugin system (`editor-core-
separation-3` campaign, `task_manager/editor-core-separation-3/
PHASE0_MASTER_STRATEGY.md`) — a feature can ship as one or more `.dll`s,
dropped into a `plugins/` folder next to the built executable, discovered and
used by the running engine with zero recompilation of the engine itself and
zero per-plugin code inside `gte_core`/`gte_editor`. This document covers the
foundation (`gte_plugin_abi` itself, PHASE1); later phases (`PluginHost`, the
render-feature/editor-panel capabilities, the Player-process isolation probe)
extend it — see the campaign's own `PHASEn_COMPLETION_REPORT.md` files for
the full, evolving picture.

## What `gte_plugin_abi` is, and why it exists

A plugin `.dll` and the host process (`GreatTamanaEditor.exe`, or a future
Player host) are two SEPARATE binaries, potentially built at different times.
Something has to define, once, in one frozen place, exactly what crosses that
boundary — `plugins/gte_plugin_abi/` is that place: a small set of
self-contained, header-only files with **zero dependency on any real
`gte_core`/`gte_editor` header, ever** (confirmed by PHASE1's own compile
check: building this folder's headers with an include path limited to
exactly this folder plus its CMake-generated-headers folder, nothing else,
succeeds — see `plugins/gte_plugin_abi/PublicSurface.md` for the full,
explicit, reviewed list of exactly which types cross the boundary).

## The fingerprint gate — checked FIRST, always, a clean skip on mismatch

`GtePluginAbiFingerprint` (`GtePluginAbiFingerprint.h`) is a fixed-size POD —
`abiContractGeneration`, `compilerId`, `compilerVersionMajor/Minor/Patch`,
`buildConfig`, `pointerSize`, `sharedRuntimeLinkage` — generated fresh at
CMake configure time from THIS EXACT build's own real compiler id/version/
build config (`GtePluginAbiFingerprintGenerated.h.in` → `configure_file()` →
`MakeThisBuildsFingerprint()`), never hand-typed. Two fingerprints are only
ever compared byte-for-byte via `operator==` — **no "close enough" logic,
ever**: a mismatch in ANY field means the plugin came from a different
compiler/version/build-config/architecture/CRT-linkage-mode, any of which is
a real, historically-common source of silent ABI/struct-layout breakage. A
mismatch is always a clean, logged skip of that ONE `.dll` (`PluginHost`,
PHASE2) — never a crash, never a partial load, never a warning-only "load
anyway."

## Never link `gte_core`/`gte_editor` directly — curated wrapper interfaces only

A plugin `.dll` **never** links or calls a real `gte_core`/`gte_editor`
symbol directly, in EITHER direction (Locked Design Decision #2,
`PHASE0_MASTER_STRATEGY.md`) — confirmed via `ask_questions` before this
campaign's implementation began. Every cross-boundary operation goes through
a small, curated, pure-virtual interface declared in `gte_plugin_abi`
(`IPluginModule` today; `IRenderFeatureModule_v1`/`IPluginRenderPassBuilder`
and `IEditorPanelModule_v1`/`IPluginPanelDrawContext` in later phases),
implemented on the HOST side by a thin adapter class living inside
`gte_core`/`gte_editor` that forwards to the real internal type
(`rg::RenderGraphBuilder`, ImGui, ...). `gte_core.a`/`gte_editor.a` remain
plain CMake `STATIC` libraries forever under this design — they never become
`SHARED`/`.dll` targets.

A second, stricter rule rides along with this: every cross-boundary
interface method signature uses ONLY plain, built-in C++ types — `const
char*`, `bool`, `float`, `int`, fixed-size POD structs, raw non-owning
pointers/references to `gte_plugin_abi`'s own interface types. Never
`std::string`, `std::vector`, `std::filesystem::path`, or any real
`gte_core`/`gte_editor` class type, by value or by reference, anywhere in
`gte_plugin_abi`. A `const char*` returned across the boundary is always a
stable, static-duration string literal — never a freshly-heap-allocated
buffer, so there is never an ownership question to resolve for it. This is a
DELIBERATELY stronger rule than the source design doc's own default, chosen
because — combined with the "never link directly" rule above — it means
`gte_plugin_abi` needs zero real `gte_core` header at all, ever, closing a
whole class of STL-ABI risk for zero real cost.

## `IPluginModule` and the three fixed exports

`IPluginModule` (`IPluginModule.h`) is the one interface every plugin
implements directly: `QueryCapability(const char* capabilityNameAndVersion)`
(a COM/Source-Engine-style string-versioned interface query — returns
`nullptr` for any capability a plugin doesn't implement, never guesses,
never returns a mismatched type through a stale pointer) and
`GetModuleInfo(GtePluginModuleInfo&)` (a short, stable, human-readable
identity for logs/diagnostics — plain fixed-size `char[]` buffers, never
`std::string`).

Every plugin `.dll` exports EXACTLY three `extern "C"` functions, under
exactly these three names (case-sensitive) — the ONLY functions ever
resolved by literal name via `GetProcAddress()` (`PluginExports.h` documents
their C++ function-pointer-typedef shape; `PluginHost`, PHASE2, is where the
real `GetProcAddress()` call sites live):

- `GTE_GetPluginAbiFingerprint` — returns this plugin's own
  `GtePluginAbiFingerprint`.
- `GTE_CreatePluginModule` — returns a new `IPluginModule*`, or `nullptr` as
  a legal "I decline to load" signal (not an error).
- `GTE_DestroyPluginModule` — destroys a previously-returned `IPluginModule*`.

## Authoring sugar — `PluginExportsMacro.h` / `SingleCapabilityPluginModule.h` (optional, additive)

`editor-core-separation-5` campaign added two more header-only files under
`plugins/gte_plugin_abi/`, purely to reduce how much boilerplate a plugin
author has to hand-write — neither changes the ABI contract described above
in any way:

- **`PluginExportsMacro.h`** — `GTE_DEFINE_PLUGIN_EXPORTS(ModuleClass)` and
  `GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(instanceExpr)` each expand to
  the exact same `extern "C" { ... }` block of the three fixed exports above,
  so a plugin author never has to hand-type it.
- **`SingleCapabilityPluginModule.h`** — `MakeModuleInfo(name, version,
  description)` (a bounded, always-null-terminated `GtePluginModuleInfo`
  builder) plus `SingleCapabilityPluginModule<CapabilityInterface>` and
  `ZeroCapabilityPluginModule`, the two ready-made `IPluginModule` glue
  classes for the "one capability" and "zero capability" cases — the two
  shapes every demo plugin in this repo needs.

This is 100% optional, additive, zero-ABI-change sugar: `PluginHost` and
every existing interface (`IPluginModule`, `IRenderFeatureModule_v1`,
`IEditorPanelModule_v1`, `IPluginRenderPassBuilder`,
`IPluginPanelDrawContext`) are completely unaware of which flavor a given
plugin `.dll` used to produce its three exports or its `IPluginModule` — a
plugin implementing 2+ capabilities from one module still hand-writes its
own `IPluginModule`, exactly as before; this sugar only covers the common
single-capability (or zero-capability) case.

All four of this repository's own demo plugins — `demo_hello_world`,
`demo_render_feature`, `demo_render_feature_second`, `demo_editor_panel` —
now use this sugar, as living proof. Their current file contents under
`plugins/demo_*/` are the canonical, up-to-date example to copy for a new
plugin — see each header's own doc comments,
`plugins/gte_plugin_abi/PublicSurface.md`, and the source proposal document
(`PLUGIN_AUTHORING_ERGONOMICS_PROPOSAL_2026-09-24.md`, referenced by
`task_manager/editor-core-separation-5/PHASE0_MASTER_STRATEGY.md`) for the
full rationale and before/after code — not duplicated here.

## The shared/DLL CRT requirement, and this repository's own real, discovered limitation

The moment ANY memory could conceivably be allocated on one side of the
plugin boundary and freed on the other — even indirectly — a statically-
linked host and a statically-linked plugin `.dll` (each with their OWN
private copy of libstdc++/libgcc's heap) is undefined behavior. Flipping to
shared (DLL) libgcc/libstdc++ linkage gives every participating binary ONE
process-wide heap instead. `cmake/MingwRuntime.cmake`'s
`gte_apply_plugin_shared_crt_linkage(<target>)` is the ONE reusable helper
every target on either side of the plugin ABI boundary must call (the host
executable, every plugin `.dll`, every standalone probe that loads a real
plugin `.dll`) — never apply a raw link-options flip by hand to just one
target and assume that is enough.

**Honest, load-bearing caveat, discovered during PHASE1's own
implementation (not silently smoothed over)**: `-shared-libstdc++` is **not
a real GCC/G++ command-line option** (unlike its real, valid sibling
`-shared-libgcc`) — shared libstdc++ linkage is simply that toolchain's own
DEFAULT whenever its own libstdc++ was itself built with `--enable-shared`;
explicitly NOT passing `-static-libgcc -static-libstdc++` is what actually
produces a shared-linked binary. Furthermore, **the only MinGW toolchain
installed on this development machine as of PHASE1 (scoop's `gcc` package,
GCC 15.2.0) was itself built with `--disable-shared`** — it has NO shared
libstdc++/libgcc/libwinpthread variant at all, under any flag combination.
`cmake/MingwRuntime.cmake` therefore PROBES, at configure time, whether the
active `CMAKE_CXX_COMPILER` has a real `libstdc++-6.dll` sitting next to it
(`GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`) — when it does not (true for
this repository's default configure today),
`gte_apply_plugin_shared_crt_linkage()` is a clean, clearly-warned, HONEST
no-op: the target stays statically linked, and the fingerprint's
`sharedRuntimeLinkage` field correctly reads `0`, never `1`. A second,
shared-runtime-CAPABLE toolchain (`scoop install mingw` — mingw-builds-
binaries, GCC 16.2.0, x86_64-posix-seh-ucrt) was installed alongside the
original one specifically so this mechanism is real and ready, but PHASE1
deliberately does NOT switch this repository's own `CMAKE_CXX_COMPILER` to
it — doing so would force a de facto full rebuild of the entire existing
build tree, which conflicts with this campaign's own "no full build except
its own final regression phase" rule. Actually flipping the active toolchain
(and confirming the flip end-to-end, including the real `-shared-libgcc`
flag plus runtime-DLL staging working against a genuinely shared-linked
build) is an explicitly deferred decision for a dedicated later step.

**Honest correction (`editor-core-separation-4` campaign, PHASE1)**: an
earlier version of `GtePluginAbiFingerprint.h`'s own doc comment claimed the
host additionally refuses to load ANY plugin outright whenever its own
fingerprint has `sharedRuntimeLinkage` read as `0` — that standalone hard
refusal never existed in the real code, and is still not implemented (doing so
today would disable plugin loading entirely on this development machine, since
its only usable toolchain cannot produce a shared-CRT binary at all). What
exists instead, as of this phase: `PluginHost::LoadPlugins()` logs one loud,
one-time `GTE_LOG_WARNING` naming this exact risk whenever the host's own
`sharedRuntimeLinkage` reads `0`, so it is visible (via `GET /get_logs`) rather
than silently, permanently true.

**Multiple `IRenderFeatureModule_v1` plugins — the LEGACY, still-supported
`_v1` path (`editor-core-separation-4` campaign, PHASE5)**: multiple plugins
may implement `IRenderFeatureModule_v1`; today, they all render into the same
shared target, and only the last-registered plugin's output ends up visible —
`Core::LoadPlugins()` logs a `GTE_LOG_WARNING` when more than one is detected.
This "last write wins" behavior is a genuine, permanent, still-real limitation
of `_v1` specifically — it is never fixed retroactively, since `_v1` itself is
never touched, deprecated, or removed. **See "`_v2` Render-Feature System"
immediately below for the real, additive fix** — a plugin that actually wants
correct multi-plugin compositing implements `IRenderFeatureModule_v2` instead.

## `_v2` Render-Feature System — Real Multi-Plugin Compositing

`editor-core-separation-6` campaign (`task_manager/editor-core-separation-6/
PHASE0_MASTER_STRATEGY.md`, `CAMPAIGN_COMPLETION_REPORT.md`) shipped a second,
strictly ADDITIVE render-feature ABI surface — `_v1` above is never touched,
deprecated, or removed; every existing `_v1` plugin/observable behavior stays
byte-for-byte identical forever. A plugin implementing the new
`IRenderFeatureModule_v2` (`plugins/gte_plugin_abi/IRenderFeatureModule.h`,
queried via `IPluginModule::QueryCapability("IRenderFeatureModule_v2")`)
declares a fixed-size POD descriptor exactly once, at load time —
`GtePluginRenderFeatureDescriptor` (`RenderFeatureDescriptor.h`): a
`stage` (`RenderFeatureStage`), a `priority` (author-declared, lower runs
first within the same stage — never auto-assigned by the host), and a
`blendMode` (`RenderFeatureBlendMode`) — and then draws every frame through
`IPluginRenderPassBuilder_v2`'s exactly 3 fixed operations (`AddSolidFillPass`,
`AddRadialVignettePass`, `AddColorGradePass`) — never raw Vulkan/`rg::` types,
never plugin-supplied shader bytecode, mirroring `_v1`'s own
`AddFullscreenClearPass`-style "small curated palette" discipline, just with
more than one operation.

The real fix this system delivers: `RenderFeatureCompositor`
(`src/Core/Plugins/RenderFeatureCompositor.h/.cpp`, `gte_core`-internal, never
plugin-ABI-facing) gives every loaded `_v2` plugin its OWN PRIVATE offscreen
render target — never the shared handle `_v1` plugins all clobber — then
composites them, in the author-declared priority order, through a real,
host-owned GPU blend compute shader (`RenderFeatureBlend.comp`, one "uber"
shader, blend mode selected via a push-constant integer): `Replace` (the
`_v1`-equivalent hard overwrite, still legal for a `_v2` plugin that wants it),
`AlphaOver`, `Additive`, `Multiply`, `ScreenSpaceMask`. Two (or more) `_v2`
plugins therefore genuinely, correctly composite into one final image — proven
with a real, live, HTTP-driven, mathematically-verified per-pixel comparison
(two differently-colored, differently-shaped, differently-blended permanent
demo plugins, `plugins/demo_render_feature_v2/` and
`plugins/demo_render_feature_v2_second/`), not just "looks about right."

**Only 2 of `RenderFeatureStage`'s 5 declared values are actually wired into
the live render graph this campaign: `PostComposite` (today's existing single
hook point) and `PreUI`.** `PreOpaque`/`PostOpaque`/`PostTransparent` are
declared in the enum for ABI future-proofing only — a plugin that declares one
of them is REFUSED at `RenderFeatureCompositor::OnPluginsLoaded()` time, with a
loud `GTE_LOG_WARNING` naming the plugin and the unwired stage, and is simply
never invoked (fail loud, never a silent mis-render). Wiring those 3 remaining
stages would mean inserting new hook points into the LIVE opaque/transparent
production render passes — a materially larger, riskier change explicitly
deferred to a future follow-up campaign. `PostComposite` and `PreUI` are both
realized at the SAME existing per-view offscreen hook point
(`"PluginRenderFeatures"`), as two ORDERED, back-to-back sub-stages processed
in sequence — every `PostComposite` entry composites first, then every `PreUI`
entry composites on top of that result, all still inside the one existing,
already-working hook (a deliberate, documented choice — see
`task_manager/editor-core-separation-6/PHASE0_MASTER_STRATEGY.md`'s Locked
Design Decision #2 for exactly why `PreUI` is NOT a new `RenderPassEvent` tier
in this engine today).

**`_v1` and `_v2` render-feature plugins are NOT unified into one
deterministic composited order** — if a build has both an `_v1` and a `_v2`
render-feature plugin loaded simultaneously (this repository's own permanent
demo-plugin set does, deliberately, to exercise exactly this coexistence),
whichever orchestrator's pass happens to execute later in the shared
`RenderPassEvent::AfterEverything` tier wins the final pixel for that view — an
accepted, explicitly out-of-scope edge case, never treated as a bug.

Full convention: `task_manager/editor-core-separation-6/PHASE0_MASTER_STRATEGY.md`
and each `PHASEn_COMPLETION_REPORT.md`/`CAMPAIGN_COMPLETION_REPORT.md` in that
same folder.

## `_v3` Generic Render-Feature System — Feature-Agnostic Resource Graph + Operation Registry

`editor-core-separation-9` campaign (`task_manager/editor-core-separation-9/
PHASE0_MASTER_STRATEGY.md`, `CAMPAIGN_COMPLETION_REPORT.md`) shipped a third,
strictly ADDITIVE render-feature ABI surface — `_v1`/`_v2` above are never
touched, deprecated, or removed. `IPluginRenderPassBuilder_v2`'s exactly 3
fixed C++ methods are a CLOSED enumeration: each hardcoded to one `opCode`
inside one host-owned uber compute shader (`Shaders/RenderFeatureOps.comp`) —
adding a 4th effect would require an ABI header edit + an adapter edit + a
shader edit + an engine rebuild, and "a compute pass writes a texture/buffer,
a later pass reads it" (ordinary render-graph plumbing) is completely
impossible for any `_v2` plugin. `_v3` fixes both problems at once, and is now
the RECOMMENDED path for new plugin authors going forward (`_v2` remains fully
supported, forever, for backward compatibility).

### The resource vocabulary and two-phase pass builder

`plugins/gte_plugin_abi/PluginRenderResource.h` declares `PluginTextureHandle`/
`PluginBufferHandle` (cheap POD index+generation structs, deliberately TWO
distinct types so a texture handle can never be passed where a buffer handle
is expected), a curated 4-value `PluginResourceAccess`
(`ColorAttachmentWrite`/`ShaderRead`/`ComputeShaderRead`/`ComputeShaderWrite` —
a SUBSET of the internal `rg::ResourceAccess`, never a raw 1:1 mirror), and
physical-shape-only `PluginTextureDesc`/`PluginBufferDesc` (no `debugName`
field, mirroring `rg::TextureDesc`/`rg::BufferDesc`'s own documented
"pointer-identity `debugName` silently breaks resource pooling" lesson).
`plugins/gte_plugin_abi/IPluginRenderPassBuilder_v3.h` declares the rest of the
surface: `IPluginPassSetupContext` (`ReadTexture`/`WriteTexture`/`ReadBuffer`/
`WriteBuffer`/`WriteColorAttachment` — the curated equivalent of
`rg::RenderGraphBuilder::PassBuilder`, declare-time only, never records GPU
work), `IPluginCommandRecorder` (`BindTexture`/`BindBuffer`/`Dispatch`/
`DrawFullscreenTriangle` — execute-time only), and the top-level
`IPluginRenderPassBuilder_v3` itself (`CreateTexture`/`CreateBuffer`/
`TryGetNamedTexture`/`GetPrivateOutputTarget`/`AddGraphicsPass`/
`AddComputePass`/`Blackboard`). A plugin declares a pass via
`AddGraphicsPass(debugName, setupFn, executeFn, userData)`/`AddComputePass(...)`
— plain function pointers + `void* userData`, never `std::function`, matching
`PublicSurface.md`'s ABI-boundary rule. `CreateTexture()`/`CreateBuffer()` mint
a NEW, transient, pooled resource for the current frame, realized host-side
through the EXACT SAME transient resource pool
`rg::RenderGraphBuilder::CreateTexture()`/`CreateBuffer()` already uses (no new
pooling mechanism was needed). `TryGetNamedTexture()` exposes exactly ONE
semantic name this campaign, `"SceneColor"` — the already-composited scene
color for this view, read-only, resolved BEFORE this plugin's own stage runs.
`GetPrivateOutputTarget()` returns this plugin's own already-allocated,
per-(plugin, view) private compositing target for this frame — the plugin's
own last pass(es) must `WriteColorAttachment()`/`WriteTexture()` into this
handle themselves; there is no implicit/auto-detected "last write wins".

**Handle translation, never a raw internal type crossing the ABI**: a
`gte_core`-internal adapter, `PluginRenderPassBuilderAdapter_v3`
(`src/Core/Plugins/PluginRenderPassBuilderAdapter_v3.h/.cpp`), owns a small,
per-(plugin, view, frame)-scoped translation table — a
`PluginTextureHandle::index`/`PluginBufferHandle::index` is simply that
table's own index, never numerically identical to a real
`rg::TextureHandle`/`rg::BufferHandle`, so a plugin can never fabricate a
handle to an arbitrary host resource by guessing an index. A real,
load-bearing use-after-free hazard was found and fixed during this campaign's
own implementation: `Core::BuildFrame()` declares every pass across BOTH views
into one shared `rg::RenderGraphBuilder` before the whole graph is compiled
and executed once, meaning the adapter itself (a per-entry stack local) is
ALREADY DESTROYED by the time any pass's `execute` callback actually runs —
fixed by extracting the translation table into a `std::shared_ptr`-held
`TranslationState` captured BY VALUE into every `execute` closure.

### The operation registry — the mechanism that makes a new operation a content addition, not an ABI change

`src/Core/Plugins/PluginRenderOperationRegistry.h/.cpp` (`gte_core`-internal) is
the real, host-owned, growable, string-keyed registry
`IPluginCommandRecorder::Dispatch(opId, ...)`/`DrawFullscreenTriangle(opId, ...)`
looks operations up in. Each registered operation carries its OWN pipeline
(a `ComputePipeline*` for a `Dispatch`-family op, a graphics `Pipeline*` for a
`DrawFullscreenTriangle`-family op), its OWN `VkDescriptorSetLayout`, an
ordered slot table (`{ VkDescriptorType, isBuffer }` per slot — `BindTexture`/
`BindBuffer` validate the caller passed the right kind, refusing a mismatch
with a loud warning), an `opCode` (meaningful only for the shared uber-ops
pipeline), and a `maxParamBytes` (capped at 128 bytes, this engine's own
graphics-push-constant convention). The registry OWNS every pipeline it
registers, INCLUDING the migrated `RenderFeatureOps.comp`/
`RenderFeatureBlend.comp` pipelines `_v2`'s own `DispatchOps()`/
`DispatchBlend()` now source through a small registry accessor instead of
owning directly — a pure refactor, zero `_v2` observable behavior change
(re-verified via pixel-parity A/B against the existing `_v2` demo plugins).

Four built-in operations shipped this campaign:

| id | kind | slots (binding → type) | opCode | maxParamBytes | pipeline source |
|---|---|---|---|---|---|
| `gte.builtin.solid_fill` | Compute | 0: StorageImage | 0 | 64 | shared `RenderFeatureOps.comp` |
| `gte.builtin.radial_vignette` | Compute | 0: StorageImage | 1 | 64 | shared `RenderFeatureOps.comp` |
| `gte.builtin.color_grade` | Compute | 0: StorageImage | 2 | 64 | shared `RenderFeatureOps.comp` |
| `gte.builtin.box_blur` | Compute | 0: CombinedImageSampler, 1: StorageImage | 0 (unused) | 8 | own, separate pipeline, `Shaders/BoxBlur.comp` (already-shipped/tested) |
| `gte.builtin.blit_fullscreen` | DrawFullscreenTriangle | 0: CombinedImageSampler (fragment stage) | 0 (unused) | 0 | own, separate graphics pipeline, `Shaders/PluginBlitFullscreen.vert/.frag` |

`gte.builtin.solid_fill`/`_radial_vignette`/`_color_grade` reproduce `_v2`'s
exact 3 fixed effects, proven byte-for-byte pixel-identical to `_v2`'s own
fixed-method dispatch (identical PNG byte size in an A/B toggle test — a
deterministic encoder over identical pixel input). `gte.builtin.box_blur` and
`gte.builtin.blit_fullscreen` are genuinely NEW operations, proving R13's
central claim twice: a new operation is a host-side content addition, never an
`IPluginRenderPassBuilder_v3` interface change.

### The 2-pass GPU blur demo — the real, permanent proof

`plugins/demo_render_feature_v3/` is a real, permanent, committed 2-pass GPU
downsample-blur, using ONLY generic `_v3` primitives: `"DemoRenderFeatureV3_Downsample"`
(compute, reads `"SceneColor"`, writes a NEW transient half-res texture minted
via `CreateTexture()`, through `Dispatch("gte.builtin.box_blur", ...)`) then
`"DemoRenderFeatureV3_UpsamplePresent"` (graphics, reads that half-res texture,
writes `GetPrivateOutputTarget()` via
`DrawFullscreenTriangle("gte.builtin.blit_fullscreen", nullptr, 0)`). Verified
live with a real, mathematically-checked pixel proof: a sharp baseline capture
vs. a blurred-output capture show a soft transition band around the cube
silhouette/horizon line whose width matches `Shaders/BoxBlur.comp`'s own
`kBlurRadius = 3` (7x7 box average) hand-computed against the real
source/destination resolutions — not merely "some blur happened".

### The blackboard — generic cross-plugin data hand-off

`IPluginBlackboard::Publish(key, value)`/`Fetch(key, expectedKind, outValue)`
(`plugins/gte_plugin_abi/IPluginRenderPassBuilder_v3.h`) mirrors
`rg::RenderPassBlackboard`'s own "last-publish-wins", per-frame-cleared
contract, but ABI-safe — `PluginBlackboardValue` is a plain tagged struct
(`PluginBlackboardValueKind`: `Texture`/`Buffer`/`Float`/`Int32`/`Float4`),
never `std::any`/`std::variant` crossing the boundary. Owned by
`RenderFeatureCompositor` (ONE instance, cleared at the start of every
`ContributeRenderGraphPasses()` call), reachable only via
`IPluginRenderPassBuilder_v3::Blackboard()` (`_v2`'s own 3-method interface
has no `Blackboard()` accessor — an additive-only campaign). Since a plugin
`.dll` has zero logging capability of its own, `BlackboardAdapter::Publish()`/
`Fetch()` (host-side) log on the plugin's behalf — a success path logs once
per distinct key for the whole process lifetime (never spamming the log ring
buffer for a value published every frame forever); a genuine failure (a
never-published key, or a key published under a different
`PluginBlackboardValueKind`) logs every time. Proven with a real, VISIBLE
2-plugin demo: `plugins/demo_render_feature_v3/` publishes
`"DemoV3.BlurStrength"` (a `Float`), `plugins/demo_render_feature_v3_second/`
fetches it and widens its own radial vignette's outer radius by the fetched
value — a genuinely stronger proof than a log-only check, since the live
`GET /get_swapchain` screenshot directly shows the value reaching and
influencing an independently-loaded plugin's own rendering.

### Diagnostics — already generic, confirmed rather than rebuilt

A `_v3` plugin's pass is, under the hood, a REAL `rg::PassRecord` produced by
the SAME `RenderGraphBuilder::AddRenderPass()` chokepoint every internal
engine pass already uses (declared with an explicit, hardcoded
`rg::RenderPassEvent::AfterEverything`, the same tier every `_v2`
`DispatchOps()`/`DispatchBlend()` pass already uses) — it is therefore ALREADY
generically visible in `GET /render_graph`'s `offscreen_regime.passes` array
and the Editor's "Render Graph" panel, with ZERO panel/JSON code change
required (mirrors the `mrt-1` campaign's own identical finding for a different
resource-write shape). The ONE genuine, confirmed gap this campaign found and
fixed: the SEPARATE `render_features[]`/"Plugin Render Features" section
(one row per LOADED PLUGIN, not per pass) had no way to say "this row is a
`_v3` plugin" other than eyeballing the plugin's own chosen name string — fixed
by one small, additive `bool isV3` field on `RenderFeatureDebugEntry`, threaded
through the existing snapshot/JSON/panel plumbing (`"is_v3"` in the JSON, a
light-blue `[v3]` label in the panel) — no new data-collection path.

### Caps and limits (never silently unbounded)

`kPluginComputeDispatchMaxGroupsPerDimension = 64` (each of `Dispatch()`'s
`groupsX`/`groupsY`/`groupsZ`, independently) and
`kPluginMaxOperationParamBytes = 128` are named ABI constants
(`IPluginRenderPassBuilder_v3.h`); at most 32 `CreateTexture()`/`CreateBuffer()`
calls per `AddRenderGraphPasses()` invocation and a 8192x8192 per-texture cap
are enforced adapter-side. Every cap violation is a loud `GTE_LOG_WARNING`
naming the plugin + the offending call + the limit, then a clean skip (the
offending resource/pass/dispatch is simply not created/recorded) — never a
crash. **Honest, load-bearing caveat**: there is no device-lost recovery path
of any kind for a `Dispatch()` call that somehow still manages to drive the
GPU past a reasonable workload despite this cap — this engine has no
device-lost recovery path anywhere today, for any workload, so this is a
restated pre-existing limitation, not a `_v3`-specific regression.

Full convention: `task_manager/editor-core-separation-9/PHASE0_MASTER_STRATEGY.md`
and each `PHASEn_COMPLETION_REPORT.md`/`CAMPAIGN_COMPLETION_REPORT.md` in that
same folder.

## `IPluginCapabilityOrchestrator` — Generic Plugin Capability Registry

`src/Core/Plugins/IPluginCapabilityOrchestrator.h` (`editor-core-separation-6`
campaign, PHASE2/PHASE3) is the general, reusable mechanism for "`Core` reacts
to a newly-loaded plugin capability" — `Core::LoadPlugins()` no longer
hand-codes a bespoke `for` loop per plugin capability kind; instead it loops a
single `std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>>`
(`Core::RegisterBuiltinCapabilityOrchestrators()`), calling each
orchestrator's `OnPluginsLoaded(const std::vector<IPluginModule*>&)` once, and
(every frame, per active view) each orchestrator's `ContributeRenderGraphPasses(...)`
(a virtual with a default no-op body — an orchestrator with nothing to
contribute to the render graph, like the editor-panel one below, simply never
overrides it). Three built-in implementations exist today:

- **`LegacyRenderFeatureOrchestrator`** — the EXISTING `_v1` render-feature
  loop (multi-plugin warning + shared-target rendering), migrated here
  verbatim from its old home directly inside `Core::LoadPlugins()`/the
  `"PluginRenderFeatures"` provider lambda, with zero observable behavior
  change.
- **`EditorPanelCapabilityOrchestrator`** — the EXISTING `IEditorPanelModule_v1`
  discovery loop, migrated here verbatim from its old home directly inside
  `EditorHost.cpp`'s constructor, again with zero observable behavior change
  (this required a small, deliberate reordering of `EditorHost.cpp`'s own
  constructor — the 10/11 `RegisterBuiltinPanelName(...)` calls now run BEFORE
  `Core::LoadPlugins()`, not after, so `EditorPanelRegistry`'s own documented
  "built-ins always list first" invariant still holds now that plugin-panel
  registration happens automatically INSIDE `Core::LoadPlugins()`).
- **`RenderFeatureCompositor`** — the new `_v2` compositor described above.

A future capability kind (a third render-graph-contributing capability, or any
other "Core reacts once a plugin loads" need) should add a FOURTH orchestrator
here, never hand-edit `Core::LoadPlugins()`'s or
`Core::RegisterOffscreenRenderPipelineProviders()`'s own bodies again — this
registry is the whole point.

**Failure-path regression coverage (`editor-core-separation-4` campaign,
PHASE8)**: `PluginHost`'s 4 documented failure/skip paths (missing export,
fingerprint mismatch, decline-to-load, reverse-order destroy) are covered by
`tests/Core/Plugins/PluginHostFailurePathTests.cpp`, using deliberately-broken
fixture `.dll`s under `tests/Fixtures/FakePlugins/` — never the real,
production demo plugins.

## Where things live, physically

- **`plugins/gte_plugin_abi/`** (source, repo root) — this ABI's own
  headers, plus `PublicSurface.md`'s explicit boundary-type list.
- **`plugins/<plugin_name>/`** (source, repo root, from PHASE2 onward) —
  each demo/real plugin's own small, independent CMake sub-project.
- **`<build-dir>/plugins/`** (RUNTIME output folder, distinct from the
  SOURCE `plugins/` folder above) — where every built plugin `.dll` is
  copied to, next to the built host executable; the folder `PluginHost`
  actually scans at engine startup (PHASE2 onward).

## What is deliberately deferred, and why (real scope, not silently dropped)

- **Hot reload** (swapping a plugin `.dll` for a rebuilt one while the
  engine keeps running) — Milestone 4 of the source design doc, explicitly
  out of scope for this whole campaign, confirmed via `ask_questions`.
  `OnBeforeUnload()`/`OnAfterReload()` lifecycle hooks are not designed here.
- **An owned-handle-with-bundled-deleter mechanism / debug-build allocation
  tagging** — this campaign's entire curated ABI surface (Milestones 0-3)
  has zero cross-boundary calls that transfer heap ownership in either
  direction (every method borrows, fills a caller-owned buffer, or returns a
  stable string literal/plain value) — building a generic ownership
  mechanism with no real call site that needs it yet is exactly the
  "pre-paying for a problem you don't have" this codebase's own established
  philosophy argues against. The moment a future capability needs to hand
  over real ownership, THAT is the point this must be designed for real.
- **Cross-process sandboxing, a per-project plugin selection UI/manifest, a
  cross-compiler/cross-vendor third-party plugin SDK** — all explicitly out
  of scope; see `PHASE0_MASTER_STRATEGY.md`'s own "Non-Goals" section for
  the complete, restated list.
- **Actually switching this repository's own build to a shared-runtime-
  capable toolchain** — see the CRT-linkage caveat above; a real, deliberate,
  deferred decision, not an oversight.

Full campaign writeup: `task_manager/editor-core-separation-3/
PHASE0_MASTER_STRATEGY.md` and each `PHASEn_COMPLETION_REPORT.md` in that
same folder.
