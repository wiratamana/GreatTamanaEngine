# Project Assembly System

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

## What this is, and why it exists

A per-developer, single-project, `.gitignore`d source tree
(`Projects/<Name>/`) holding REAL, user-authored C++ and GLSL shader source
that compiles into two ordinary Windows `.dll`s (`<Name>_Game.dll`,
`<Name>_Editor.dll`) which `GreatTamanaEditor.exe` loads at its own startup
and which then call **real, live, non-ABI-wrapped engine types** (`gte::Core&`,
real ImGui, real `rg::RenderGraphBuilder`) directly — with **zero
recompilation of the engine itself** for a content change, and **zero new
per-feature ABI surface** to design/maintain (unlike the OLD, now fully-removed
`plugins/gte_plugin_abi` system this system never touched, edited, or depended
on while it still existed — see "`better-render-pass-2` ..." note below).

Built by the `editor-core-separation-11` campaign
(`task_manager/editor-core-separation-11/PHASE0_MASTER_STRATEGY.md`, 8
phases, `CAMPAIGN_COMPLETION_REPORT.md`). Two concrete, working, end-to-end
proofs were the campaign's own Definition of Done, both built as one
throwaway-but-permanent test project, `Projects/ProjectAssemblyProbe/`:

1. A real, dockable, genuinely interactive Editor panel ("Probe Panel", a
   click counter) contributed entirely from `ProjectAssemblyProbe_Editor.dll`,
   calling real `ImGui::*` functions directly — no ABI wrapper.
2. A real render-graph compute pass
   (`"ProjectAssemblyProbe.FillTexture"`) contributed entirely from
   `ProjectAssemblyProbe_Game.dll`, using genuine `rg::RenderGraphBuilder`/
   `rg::RenderPipeline` types directly, with its own freshly-compiled `.spv`
   shader — visible in the Editor's real "Render Graph" panel and
   `GET /render_graph`.

## The toolchain prerequisite this whole system depends on

This system is only safe because this repo's ACTIVE toolchain is a
shared-CRT-capable GCC 16.2.0 `mingw-builds-binaries` build
(`scoop/apps/mingw/current`), not the older `scoop/apps/gcc/current` build
(`--disable-shared`) every campaign through `editor-core-separation-9`/
`editor-enchancements-1` ran against. **The switch already happened, quietly,
sometime between `editor-core-separation-9` and 2026-09-28 — nobody
documented a deliberate decision to perform it anywhere under
`task_manager/`.** `editor-core-separation-11`'s own PHASE1 mechanically
re-confirmed (not assumed) that this is still true: `build/CMakeCache.txt`'s
`CMAKE_CXX_COMPILER` points at `scoop/apps/mingw/current/bin/c++.exe`,
`GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED` reads `TRUE`, and the generated
plugin ABI fingerprint's `sharedRuntimeLinkage` field reads `1`. **A Project
Assembly `.dll` links against `GreatTamanaEditor.exe`'s own import library
and freely passes real `std::string`/`std::vector` values across that
boundary — this is only safe on a shared-CRT toolchain.** No separate
`build-shared-crt` tree exists or is needed; the existing default `build/`
tree already IS the shared-CRT tree.

## Locked Design Decisions (do not silently re-litigate these)

- **A Project Assembly links `GreatTamanaEditor` (the executable's own import
  library) ONLY, never `gte_core`/`gte_editor` directly.** This is what keeps
  exactly one physical copy of every `gte_core`/`gte_editor`/`imgui` global in
  the process.
- **`Projects/` is `.gitignore`d, single-developer, same-toolchain,
  same-build-run only.** Never design anything to survive being zipped up and
  handed to a different machine/toolchain version.
- ~~No hot reload, anywhere, ever. A Project Assembly `.dll` is scanned/
  loaded exactly once, at `GreatTamanaEditor.exe` startup. A changed/
  recompiled `.dll` requires a full close+relaunch. No file-watcher, no
  reload button, no `OnBeforeUnload` hook.~~ **SUPERSEDED, 2026-09-28
  onward** - see `task_manager/editor-core-separation-12/` through `-15/`
  (the "Project Assembly Hot Reload" 4-campaign effort) and this file's own
  new `## Hot Reload` section below for the full, current, honest picture.
  The strikethrough text above is the ORIGINAL, now-historical Locked
  Design Decision, kept for the record, not deleted.
- ~~This is a NEW, ADDITIVE, PARALLEL system. It never modifies
  `plugins/gte_plugin_abi/`, `src/Core/Plugins/PluginHost.h/.cpp`, or any
  existing `IRenderFeatureModule_*`/`IEditorPanelModule_v1` ABI type.~~
  **NO LONGER A MEANINGFUL CONSTRAINT, `better-render-pass-2` campaign onward** -
  the OTHER system (`plugins/gte_plugin_abi`) this bullet used to promise never
  to touch was itself fully, deliberately removed by that campaign
  (`task_manager/better-render-pass-2/PHASE0_MASTER_STRATEGY.md`,
  `CAMPAIGN_COMPLETION_REPORT.md`) - Project Assembly is this engine's ONLY
  loadable-module system today. The strikethrough text above is the ORIGINAL,
  now-historical Locked Design Decision, kept for the record, not deleted.

## Folder layout and the CMake mechanism

```
Projects/<Name>/
  Assets/
    *.cpp, *.vert, *.frag, *.comp   (compiled into <Name>_Game.dll)
    Editor/
      *.cpp                        (compiled into <Name>_Editor.dll, only if this folder has any source)
  Libraries/
    CMakeLists.txt                 (one line: gte_add_project(<Name>))
    ProjectAssemblyExports.h        (copy of cmake/templates/ProjectAssemblyExports.h)
```

`gte_add_project(<Name>)` (`cmake/GteProject.cmake`):
- Globs every `.cpp` under `Assets/` (`CONFIGURE_DEPENDS`, `Editor/` handled
  as its own separate glob) — never a hand-maintained source list.
- Builds `<Name>_Game` unconditionally; builds `<Name>_Editor` only if
  `Assets/Editor/` has any source at all.
- Calls `gte_add_project_shaders()` for each target's own `Assets/*.vert/.frag/.comp`
  (a thin wrapper around the engine's own, completely unmodified
  `gte_add_shader()`, `cmake/CompileShaders.cmake`).
- Propagates every header search path `gte_core`/`gte_editor` themselves
  declare `PUBLIC`, via explicit
  `$<TARGET_PROPERTY:<dep>,INTERFACE_INCLUDE_DIRECTORIES>` generator
  expressions — needed because `GreatTamanaEditor` links `gte_editor`
  `PRIVATE` (root `CMakeLists.txt`), so ordinary CMake usage-requirement
  propagation does not flow through to a Project Assembly target linking
  only `GreatTamanaEditor`.
- For an `_Editor` target only, also calls
  `gte_project_assembly_apply_editor_header_paths()` — a headers-only
  `$<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>`/`imguizmo`
  propagation, `PRIVATE`, so `_Editor.dll` gets real ImGui headers WITHOUT
  re-linking the compiled `imgui`/`imguizmo` static libraries a second time
  (which would create a second, independent `GImGui` context pointer in the
  process — the exact hazard this whole system exists to avoid).
- Applies `gte_apply_project_assembly_shared_crt_linkage(target)`
  (`cmake/MingwRuntime.cmake`) — a genuinely SEPARATE function from
  `gte_apply_shared_crt_linkage()` (the unrelated host-executable CRT-linkage
  helper `better-render-pass-2` renamed from the OLD, now fully-removed
  `plugins/gte_plugin_abi` system's own `gte_apply_plugin_shared_crt_linkage()`),
  gated ONLY by `GTE_ENABLE_PROJECT_ASSEMBLIES` +
  `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED` — this historically was ALSO never
  gated by the OLD, now-removed `GTE_ENABLE_PLUGINS` flag, back when that flag
  still existed, because the two systems were always logically independent -
  a build with the ABI plugin system disabled still had to get correct,
  shared-CRT-linked Project Assembly `.dll`s. That flag, and the whole system
  it gated, no longer exist at all (`better-render-pass-2` campaign) - this
  function's own gating is unchanged either way.

**One new CMake option, `GTE_ENABLE_PROJECT_ASSEMBLIES` (default `ON`)**,
gates the entire system end to end: both the root `CMakeLists.txt`
auto-discovery loop (`add_subdirectory()` over every `Projects/*/Libraries/`)
and the runtime `Core::LoadProjectAssemblies()` call site
(`src/Editor/EditorHost.cpp`'s own constructor).

**Known CMake gap, not yet fixed**: an incremental, shader-only, target-scoped
rebuild (`cmake --build build --target <Name>_Game`, editing only a `.comp`/
`.vert`/`.frag` file) does not reliably re-stage the compiled `.spv` next to
the `.dll` — the missing link is `gte_add_shader()`'s own `target_sources()`
call wiring the compiled shader in only as an ORDER-ONLY prerequisite, never a
real staleness-tracked link input, for a `SHARED` library target whose
runtime output directory differs from `CMAKE_BINARY_DIR` (true for a Project
Assembly, never true for `GreatTamanaEditor` itself, which is why this gap
was invisible until this campaign). Work around it by also touching/rebuilding
the target's own `.cpp` (forces a real relink, which reliably re-runs the
`POST_BUILD` staging copy), or by doing a full rebuild after any shader edit.

## The runtime loader

`src/Core/Plugins/ProjectAssemblyHost.h/.cpp` — modeled directly on
`src/Core/Plugins/PluginHost.h/.cpp`'s own shape:

- `LoadProjectAssemblies(outputDirectory, core, editorHost)` enumerates every
  regular file directly inside `<exe dir>/project_assemblies/` (no
  recursion), safe on a non-existent directory.
- Filters by filename suffix (`_Game.dll`/`_Editor.dll`, everything else
  silently ignored — never introspects the export itself).
- `LoadLibraryW()`s each match, resolves the ONE fixed export
  `GTE_RegisterProject` through one of TWO different function-pointer
  signatures depending on which suffix matched
  (`cmake/templates/ProjectAssemblyExports.h`'s
  `GTE_DEFINE_PROJECT_EXPORTS_GAME(fn)` → `void(gte::Core&)`;
  `GTE_DEFINE_PROJECT_EXPORTS_EDITOR(fn)` → `void(gte::Core&, gte::EditorHost&)`),
  calls it exactly once.
- An `_Editor.dll` found while `editorHost == nullptr` (a Player-shaped host
  that never constructs one) is skipped with a loud `GTE_LOG_WARNING`, never
  crashed on.
- Keeps every `HMODULE` alive forever — never `FreeLibrary()`'d except on the
  "declined/invalid" early-return paths where the export was never called,
  ~~(LDD4 — no hot reload, ever)~~ **or during a real, later
  `POST /project_assembly/hot_reload` cycle's own unload phase
  (`ProjectAssemblyHost::UnloadProjectAssembly()`) - SUPERSEDED, 2026-09-28
  onward, see this file's own new `## Hot Reload` section below.** This
  file's own initial `LoadProjectAssemblies()` scan/load pass itself is
  still exactly-once-at-startup, unchanged - only a LATER, explicit,
  user-triggered hot-reload cycle ever unloads/reloads an already-loaded
  assembly.

`Core::LoadProjectAssemblies()` is a thin public pass-through into
`m_projectAssemblyHost`, mirroring `Core::LoadPlugins()`'s own identical
shape (a private member, a public one-line forwarding method).
`EditorHost.cpp`'s constructor calls it once, gated by
`#if GTE_ENABLE_PROJECT_ASSEMBLIES` at that call site only (the method itself
always compiles).

## The "Compile" build trigger

`src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp` —
`TriggerProjectAssemblyCompile(projectName, buildDirectory)` runs a genuine
`cmake --build` child process (`CreateProcessW()`) on its own dedicated
`std::thread`, registered via `JobSystem::RegisterBackgroundThread()` —
**never** `Schedule()`, which would tie up a fixed worker-pool thread for the
build's entire, potentially multi-minute duration. Builds `<Name>_Game`
first; only attempts `<Name>_Editor` if that succeeded, and a missing
`_Editor` target (a project with no `Editor/` sources) is detected via a
best-effort keyword heuristic on Ninja's own error text and treated as
success, not failure. Streams the child's combined stdout/stderr into
`GTE_LOG_INFO`/`WARNING`/`ERROR` (category `"ProjectAssemblyBuild"`) as it
runs. A simple per-project in-flight guard turns a second concurrent trigger
for the SAME project into a harmless, logged no-op. The final log line
(`"Build finished with exit code N — relaunch GreatTamanaEditor.exe to use
the result..."`) is emitted on every run, success or failure — no permanent
UI trigger point (menu item/button) is defined by the campaign that built
this; a future UI surface calls `TriggerProjectAssemblyCompile()` directly.

**Known, permanent limitation, not a bug**: a Project Assembly `.dll` already
`LoadLibraryW()`'d by the CURRENTLY RUNNING instance can never be
successfully recompiled by that same instance — Windows locks a mapped DLL
image against being overwritten by the linker (`ld.exe: cannot open output
file ...: Permission denied`). The realistic workflow is: edit source, close
the Editor (releases the lock), rebuild (externally, or via a future
"Compile" UI), relaunch.

## Capability #1 — a custom Editor panel

An `_Editor.dll`'s own registration function implements
`gte::IEditorPanelModule_v1` directly — originally the SAME ABI interface
`plugins/gte_plugin_abi` panels implemented (deliberately reused, since this
system had no reason to invent a parallel one); today, after `better-render-pass-2`
fully removed that OTHER system, `IEditorPanelModule_v1`/`IPluginPanelDrawContext`
live in a small, `gte_core`-owned header, `src/Core/EditorPanelModule.h` — the
SAME interface, same method shapes, just relocated rather than ABI-versioned,
since Project Assembly's own custom-panel capability is now its one remaining
real consumer. `BuildPanel()` **ignores** the `ctx`
(`IPluginPanelDrawContext&`) parameter entirely and calls real `ImGui::*`
functions directly instead, then registers itself through the existing,
unmodified `gte::EditorPanelRegistry::Instance().RegisterPluginPanel(name, module)`.
No new registry, no new panel-hosting mechanism.

**Finding G, resolved**: `src/Editor/ImGuiEditorLayer.cpp`'s per-frame loop
that calls `entry.module->BuildPanel(drawContext)` for every
`EditorPanelRegistry::PluginPanels()` entry used to sit inside
`#if GTE_ENABLE_PLUGINS` only — the OTHER, now fully-removed system's own flag.
Widened, at the time, to `#if GTE_ENABLE_PLUGINS || GTE_ENABLE_PROJECT_ASSEMBLIES`
so a Project Assembly panel actually drew even with `GTE_ENABLE_PLUGINS=OFF` (it
still got a dock slot either way — `src/Editor/DockLayout.cpp`'s own separate loop
over the same registry was never gated by either flag — but without this fix
it would have been a permanently blank tab, which looks like a bug rather than a
configuration choice). **`better-render-pass-2` then narrowed this guard to
`#if GTE_ENABLE_PROJECT_ASSEMBLIES` alone**, since `GTE_ENABLE_PLUGINS` no
longer exists anywhere in this codebase — this loop's own observable behavior
is completely unchanged for Project Assembly, which is all that is left to gate.

## Capability #2 — a custom render-graph pass

**Finding B, resolved**: `Core::RegisterOffscreenRenderPipelineProviders()`
is `private`, and `Core` exposed no public method to register a new provider
from outside `Core` at all. Fixed with a new, public thin pass-through:

```cpp
// Core.h (public section)
void RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope, rg::RenderPassProvider provider);

// Core.cpp
void Core::RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope, rg::RenderPassProvider provider)
{
    m_offscreenRenderPipeline.Register(debugName, scope, std::move(provider));
}
```

`m_offscreenRenderPipeline` is the correct target — the SAME pipeline every
production Game-View/Scene-View pass (`"AtmosphereSharedLut"`,
`"GpuSkinning"`, `"RenderOpaque"`, ...) registers onto;
`m_presentRenderPipeline` is a separate, narrower pipeline used only for the
one `"Present"` swapchain-blit provider. Safe to call any time after `Core`'s
own constructor has run (a Project Assembly's `GTE_RegisterProject` runs from
`EditorHost.cpp`'s constructor body, strictly after `Core`'s own
construction) — `RenderPipeline::Register()` merely appends to an internal
`std::vector` read fresh, in full, every frame by `DeclareInto()`.

**Finding E, resolved**: `RenderGraphBuilder::PassBuilder` (the type a
deferred `RenderPassDesc::setup` receives) has NO `CreateTexture()`/
`ImportTexture()` method — only `ReadTexture()`/`WriteColorAttachment()`/
`WriteTexture()`/`ReadBuffer()`/`WriteBuffer()`/etc., all operating on an
ALREADY-MINTED handle. `CreateTexture()`/`CreateBuffer()`/`ImportTexture()`
are methods of `RenderGraphBuilder` ITSELF. The correct pattern: call
`frame.builder.CreateTexture(name, desc)` directly inside the provider's own
lambda body (available because `RenderPassFrameContext::builder` is a plain
reference member, freely callable through a `const RenderPassFrameContext&`),
obtain the resulting `TextureHandle` BEFORE constructing the
`RenderPassDesc`, then reference that already-minted handle from
`desc.setup`/`desc.execute` via closure capture. Append any transient handle
nobody else reads to `frame.finalTextureOutputs` so
`RenderGraphCompiler`'s backward-reachability culling scan does not silently
drop the pass that writes it.

**Finding F, resolved**: a Project Assembly's own compiled shader is
referenced as a plain, bare relative path string,
`"project_assemblies/shaders/<Name>.spv"` — the exact same convention every
existing internal `ComputePipeline` construction call site already uses
(`Renderer::CreateComputePipeline()`, never a raw `ComputePipeline`
constructor call, since `Renderer` exposes no public `VkDevice` getter —
`Core::GetRenderer().GetVulkanContextInfo().device` is the one legitimate way
to obtain one).

`ProjectAssemblyProbe_Game.dll`'s own `HelloGame.cpp` is the full, working,
concrete example of all three Findings combined: it mints a transient
256x256 texture, lazily builds a `ComputePipeline`/`ComputeDescriptorSet` on
first use (a process-lifetime `std::optional<gte::ComputePipeline>` — never
`std::unique_ptr`, matching every other real call site in this codebase), and
dispatches `ProbeCompute.comp` (a solid-color fill shader,
`local_size_x/y = 16`, matching `ComputeGroupCount(256, 16) == 16` on both
axes) against it every frame — confirmed, live, visible in both the Editor's
real "Render Graph" panel and `GET /render_graph`'s JSON.

### On-screen Game View compositing — safe today via `Core::RegisterProjectRenderFeature()`

A Project Assembly can register a genuine, visible, on-screen render feature —
composited into the SAME Game View / Scene View image real users see — without
writing any manual `ImportTexture()`/handle-aliasing code of its own. This is
`editor-core-separation-23`'s own campaign (BIG-STEP 1 of a two-part effort;
BIG-STEP 2, an Editor "Create → Screen Post-Process Pass" menu item, is a
separate, future, still-unstarted campaign): rather than inventing a new
compositing mechanism, it plugs a Project Assembly in as a THIRD, additive
"module kind" alongside `gte_plugin_abi`'s existing `IRenderFeatureModule_v2`/
`_v3` plugins, reusing the exact same, already-proven, hazard-free
`RenderFeatureCompositor` blend chain those plugins already use.

**The API** (`src/Core/Core.h`/`.cpp`, two new public methods, thin
pass-throughs onto `RenderFeatureCompositor::RegisterProjectFeature()`/
`UnregisterProjectFeature()`):

```cpp
bool RegisterProjectRenderFeature(const char* debugName, RenderFeatureStage stage,
    RenderFeatureBlendMode blendMode, std::int32_t priority, ProjectRenderFeatureCallback callback);

void UnregisterProjectRenderFeature(const char* debugName);
```

`ProjectRenderFeatureCallback` (`src/Core/Plugins/ProjectRenderFeatureCallback.h`,
a brand-new, free-standing header — never nested inside `RenderFeatureCompositor`,
so `Core.h` can keep forward-declaring that still-incomplete class) is
`std::function<void(rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D)>` —
handed the current frame's real Game View/Scene View PRIVATE compositing target
and its extent, directly, with no adapter object in between. Call it from your
own `_Game.dll`'s `RegisterProject()` entry point, exactly once per feature.

`RegisterProjectRenderFeature()` REJECTS (never silently truncates) any
`debugName` longer than 63 bytes — the first call site in this engine building
this descriptor's name from free-form, un-length-checked input, deliberately
diverging from `gte_plugin_abi`'s own silent-truncation precedent.

**This is a plain, non-ABI-versioned path, deliberately separate from
`gte_plugin_abi`'s `IRenderFeatureModule_v2`/`_v3`** — a Project Assembly calls
straight into a real, live `gte::Core&`, exactly like every other Project
Assembly capability above; it never touches the ABI-wrapped plugin interfaces
at all, and registering/unregistering a project render feature has zero effect
on any loaded `_v2`/`_v3` plugin's own behavior.

**Bounded GPU-state slot design.** Unlike a `gte_plugin_abi` plugin (loaded
once, for the life of the process), a Project Assembly is registered, renamed,
and re-registered repeatedly over one long, live Editor session (edit source,
hot-reload, repeat) — keying its GPU state (private render target, blend-stage
descriptor set) by its own human-typed `descriptor.name`, the way a plugin
feature's name already is, would let an unbounded rename-cycle session
permanently starve the shared, fixed-256-set compute descriptor pool
(`GpuResourceFactory.cpp`'s `kMaxComputeDescriptorSets`). Instead, a Project
Assembly feature's GPU state is keyed by a small, fixed-size, reusable slot
index — `kMaxConcurrentProjectRenderFeatures = 16`, a named constant — drawn
from a free-list that a live 20-cycle rename test confirmed reuses the exact
same slot every time, indefinitely, with zero unbounded growth. `descriptor.name`
itself is unaffected by this — it is still what a human sees everywhere
(`GET /render_graph`, the "Render Graph" panel, every log line).

**Hot-reload teardown guarantee.** `ProjectAssemblyRegistrationLedger` records
every render feature a Project Assembly registers, and
`UnregisterEverythingFor()` tears them all down — releasing their GPU-state
slots back to the free list — BEFORE tearing down that same project's
render-pass providers, on every hot-reload/unload cycle. This closes the one
hard requirement this whole capability depends on: a `projectCallback`
`std::function` whose captured state lives inside a Project Assembly's own
`.dll` image would otherwise dangle the instant `FreeLibrary()` runs on it after
an unload — the ledger's teardown always removes it first, on the GPU-idle main
thread, strictly before that `.dll` image is ever unmapped.

**`RenderPassEvent::AfterEverything` is the ONLY tag any pass this callback
declares may use** — this is what keeps the whole on-screen compositing chain
provably last, unconditionally, matching every other pass `RenderFeatureCompositor`
itself declares. `DetectRenderPassEventContradictions()` confirms zero
contradictions for this shape, live.

**A registered feature is visible, and structurally distinguishable from a
`gte_plugin_abi` plugin feature**, via `GET /render_graph`'s `render_features[]`
array (a `"is_project_feature": true` field, sibling to the existing `"is_v3"`)
and the Editor's "Render Graph" panel (a light-green `"[Project]"` tag next to
the feature's row, mirroring `"[v3]"`'s own precedent).

**Working example**, from `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`'s
own permanent `"ProjectAssemblyProbe.ScreenTint"` feature (a translucent red
tint over the whole Game View — `priority=100` places it after every existing
demo plugin's own `PostComposite` entries, including any `Replace`-blend one,
so it survives to the final composited image):

```cpp
core.RegisterProjectRenderFeature(
    "ProjectAssemblyProbe.ScreenTint", gte::RenderFeatureStage::PostComposite,
    gte::RenderFeatureBlendMode::AlphaOver,
    /*priority=*/100,
    [](gte::rg::RenderGraphBuilder& builder, gte::rg::TextureHandle privateTarget, VkExtent2D /*extent*/) {
        builder.AddRenderPass(
            "ProjectAssemblyProbe.ScreenTint.Clear", gte::rg::PassKind::Graphics,
            [privateTarget](gte::rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(privateTarget, std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 0.15f });
            },
            [](gte::rg::PassContext&) {},
            gte::rg::RenderPassDrawKind::DrawQuad, gte::rg::RenderPassEvent::AfterEverything);
    });
```

**BIG-STEP 1 (`editor-core-separation-23`) shipped this on-screen compositing
mechanism itself; BIG-STEP 2 (`editor-core-separation-24`) then added the
Editor "Create → Screen Post-Process Pass" menu item and its own auto-wire
mechanism on top of it — the entire 2-BIG-STEP effort is now closed. See
`task_manager/editor-core-separation-23/PHASE0_MASTER_STRATEGY.md`/
`CAMPAIGN_COMPLETION_REPORT.md` for the full seven-phase BIG-STEP 1 writeup,
and this file's own new
`### Screen Post-Process Pass scaffolding` section immediately below for
BIG-STEP 2's own full picture.

### Screen Post-Process Pass scaffolding — Editor "Create" menu + auto-wire (BIG-STEP 2)

An eight-phase campaign, `editor-core-separation-24`
(`task_manager/editor-core-separation-24/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), closes the gap the paragraph above used to
describe as still-open: a Project Assembly author no longer has to hand-write
a call to `Core::RegisterProjectRenderFeature()` at all to get a genuine,
visible, on-screen render feature working end to end.

**A new, fourth `AssetScaffoldKind`** (`src/Core/EditorCapabilities.h`):
`enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair,
ScreenPostProcessPass };`. Reachable two ways, exactly like the three
pre-existing scaffold kinds:

- Right-click the "[Active Project] `<Name>`" row in the Project panel →
  "Create" → **"Screen Post-Process Pass..."** (a fourth `ImGui::MenuItem`,
  `src/Editor/Panels/ProjectPanel.cpp`) → type a name → "Create" (the window's
  own title bar correctly reads "Create New Screen Post-Process Pass",
  `src/Editor/CreateAssetWindow.cpp`'s `TitleForKind()`).
- `POST /project_assembly/create_asset?kind=screen_post_process_pass&name=<X>`
  (the SAME existing route every other scaffold kind already uses —
  `src/Network/NetworkServer.cpp`'s `kindParam` chain gained one more
  `else if` branch; its 400 error message now lists all four valid kinds).

**The generated template** (`BuildScreenPostProcessPassCppContent()`,
`src/Editor/EditorProjectLifecycleCapability.cpp`) writes exactly ONE new
file, `Assets/<Name>ScreenPass.cpp` — a real, immediately-compileable,
working translucent red tint (`RenderFeatureStage::PostComposite` +
`RenderFeatureBlendMode::AlphaOver`, `RenderPassEvent::AfterEverything`,
`RenderPassDrawKind::DrawQuad`), calling `Core::RegisterProjectRenderFeature()`
correctly — the exact same API BIG-STEP 1 above already proved, never a new
mechanism. A generated debug name (`"<Name>.ScreenTint"`) that would exceed
`GtePluginRenderFeatureDescriptor::name`'s 63-usable-byte limit is REJECTED at
scaffold time, before any file is written — never silently truncated, and
never merely deferred to compile-and-run time (mirroring BIG-STEP 1's own
`RegisterProjectRenderFeature()` reject-not-truncate precedent).

**Priority auto-assignment** (`ComputeNextScreenPassPriority()`,
`src/Editor/ScreenPassPriorityAssignment.h/.cpp`) keeps scaffolding the SAME
kind of pass more than once into the SAME project collision-free, with zero
manual editing: it scans every sibling `*ScreenPass.cpp` file
(case-insensitive) already in the project's `Assets/` folder, reads back each
one's own real, still-present `/*priority=*/<N>` integer literal, and assigns
one more than the HIGHEST SURVIVING value found — never a plain file count,
which would silently collide the moment any sibling file is deleted outside
the Editor and a new one is then created. The first Screen Post-Process Pass
ever scaffolded into a project gets priority `0`; deleting a middle-priority
sibling and scaffolding a new one afterward correctly skips straight past the
gap (confirmed live: three passes at priorities `0, 1, 2`, the priority-`1`
one deleted from disk, a fourth scaffold correctly reads priority `3`, never
`1` or `2` again).

**The auto-wire mechanism** (`TryAutoWireRegisterCall()`,
`src/Editor/ScreenPassAutoWire.h/.cpp`) tries to automatically insert the one
required call, `Register<Name>ScreenPass(core);`, into the active project's
own `RegisterProject()` function (`Assets/<ProjectName>Game.cpp`) — zero
manual C++ editing required to see a fresh scaffold live, end to end: compile,
reload, done. This depends on a small, one-time template change
(`BuildGameStubCppContent()`, the "New Project" scaffold): every project
created from this point forward has its `RegisterProject(gte::Core& core)`
parameter named (not commented out) and carries two exact, stable,
machine-readable anchor comments (`GTE_AUTO_REGISTER_FORWARD_DECLARATIONS`/
`GTE_AUTO_REGISTER_ANCHOR`) the auto-wire helper searches for.

**Honest, permanent, two-sided boundary — the single most important thing to
understand about this mechanism**:

- **A project created AFTER this campaign shipped** (this file's own
  permanent proof fixture: `Projects/ScreenPassAutoWireProbe/`, created fresh
  via `POST /project_assembly/create_project` specifically to prove this
  path) has both anchors, so auto-wiring genuinely succeeds — confirmed live,
  repeatedly, across several scaffold-then-compile-then-reload cycles in the
  same running Editor session.
- **A project created BEFORE this campaign shipped** (this system's own other
  permanent fixture, `Projects/ProjectAssemblyProbe/` — BIG-STEP 1's proof
  artifact, predating this campaign) has NEITHER anchor, and its
  `RegisterProject` parameter is still `gte::Core& /*core*/` (commented out).
  Scaffolding a Screen Post-Process Pass into it is still 100% safe — the
  `.cpp` file is written either way — but auto-wiring correctly, safely
  refuses, and the reminder message falls back to the original, fully-manual
  "remember to add this line yourself" wording, forever, unless a human
  manually adds the two anchors and un-comments `core` themselves. **A naming
  quirk worth stating explicitly, since it is easy to get wrong**:
  `ProjectAssemblyProbe`'s own Game-half source file is named
  `Assets/HelloGame.cpp`, NOT `Assets/ProjectAssemblyProbeGame.cpp` (which has
  never existed — this project predates the `<Name>Game.cpp` naming
  convention `CreateNewProjectAssembly()` later established) — so
  `TryAutoWireRegisterCall()` actually fails via its own "the target file
  cannot be opened at all" branch for this specific fixture, not the "anchors
  are missing" branch — the exact same safe, harmless, zero-file-touched
  observable outcome, just a different, confirmed real reason. Confirmed
  live: scaffolding into `ProjectAssemblyProbe/` leaves `HelloGame.cpp`
  byte-for-byte, 252-lines identical, before and after.
- This campaign does NOT retroactively rewrite any project created before the
  template change shipped, and does NOT extend auto-wiring to the two OLDER
  scaffold kinds (`RenderPass`/`ComputeShader`) — both keep their fully
  manual, reminder-only behavior, unchanged, forever, unless some future,
  separate campaign decides otherwise.
- The idempotency guard correctly treats a PREVIOUSLY auto-wired call line a
  human later comments out (e.g. to temporarily disable one effect) as NOT
  currently wired — re-scaffolding under that same name inserts a fresh,
  active call line alongside the untouched, still-commented old one, rather
  than mistaking the comment for still-active wiring (confirmed live, not
  merely by the dedicated `tests/Editor/ScreenPassAutoWireTests.cpp` Tier-1
  suite this behavior also has).

**Live-proven realistic churn, not just a one-shot demo**: on
`Projects/ScreenPassAutoWireProbe/`, five full cycles of "scaffold, decide
against the name, delete the `.cpp` file, remove the now-dangling forward
declaration/call by hand, scaffold a differently-named replacement, compile,
hot-reload" ran back to back in the same live Editor session with zero
failures, zero stale/duplicate `GET /render_graph` entries, and — checked via
`GET /get_logs?category=RenderFeatureCompositor` — every single one of the
five replacement passes claimed the EXACT SAME bounded GPU-state slot each
time (mirroring BIG-STEP 1's own 20-rename-cycle proof methodology), never
growing without bound.

Full campaign writeup:
`task_manager/editor-core-separation-24/PHASE0_MASTER_STRATEGY.md`, each
`PHASEn_COMPLETION_REPORT.md` in that same folder, and
`CAMPAIGN_COMPLETION_REPORT.md`.

### PreOpaque passes — safe today via `Core::AddPreOpaquePass()`

A Project Assembly can register a pass that is PROVABLY GUARANTEED to
finish before the engine's own `"RenderOpaque"` pass runs, for every
active view — the mechanism shadow maps, GI, and any other
"must-finish-before-the-main-scene-draw" technique needs.
`better-render-pass-5`'s BLOCK 3 campaign
(`task_manager/better-render-pass-5/PHASE0_MASTER_STRATEGY.md`) wired
this; `RenderFeatureStage::PreOpaque` is no longer refused.

**This is a genuinely different, SIMPLER mechanism than the
PostComposite/PreUI compositing chain described above — no private
target, no blend mode, no GPU-state slot pool.** A PreOpaque pass has
nothing to blend onto (nothing has been drawn yet this point in the
frame) — it draws into its own, self-managed render view
(`Core::CreateRenderView()`) and `Publish()`es whatever it produces
directly onto the `RenderPassBlackboard` itself. Nothing reads a
PreOpaque pass's output except by Blackboard key — never by a
hardcoded cross-pass texture reference.

**The API** (`src/Core/Core.h`/`.cpp`):

```cpp
bool AddPreOpaquePass(const char* debugName, ProjectPreOpaqueCallback callback, std::int32_t priority = 0);
void RemovePreOpaquePass(const char* debugName);
```

`ProjectPreOpaqueCallback` (`src/Core/Plugins/ProjectPreOpaqueCallback.h`) is
`std::function<void(rg::RenderGraphBuilder& builder, rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView)>` —
deliberately NOT the same type as `ProjectRenderFeatureCallback` above:
there is no `privateTarget`/`extent` parameter at all, and - unlike
every OTHER immediate provider's callback shape in this engine - it is
handed the frame's own `RenderPassBlackboard&` DIRECTLY, since a
Project Assembly callback has no other way to reach it (it is not a
singleton, not reachable through `Core`). The callback is expected to
call `core.CreateRenderView(...)` and `gte::DrawScene(...)`
(`src/Game/SceneQuery.h`) itself, entirely self-contained.

**Rule 1 (bolded, not a suggestion): `rg::RenderPassEvent::PreOpaques` is
the ONLY tag any pass this callback declares may use.** Tagging a pass
with the implicit `AddRenderPass()` default (`RenderPassEvent::Opaques`
— the SAME tier `"RenderOpaque"` itself uses) means your pass is no
longer provably guaranteed to run first — and this exact mistake is
silently uncaught by `DetectRenderPassEventContradictions()`, which only
fires on a STRICTLY LATER tag, never an EQUAL one. The engine's own
`"PreOpaqueFeatures"` provider (`Core.cpp`) runs a second, independent,
debug-asserted runtime check for exactly this reason — a violation logs
a `GTE_LOG_WARNING` (category `"RenderFeatureCompositor"`) naming your
feature and the offending pass, and fails an `assert()` in debug
builds.

**Rule 2 (bolded, not a suggestion): every Blackboard key you
`Publish()` under MUST incorporate your callback's own `currentView`
parameter.** The provider this callback is reached through is
`ProviderScope::PerActiveView` — your SAME callback runs once per
active view, in the SAME frame, against the SAME
`RenderPassBlackboard`. `Publish()`ing one shared, view-agnostic key
means the SECOND view processed this frame silently overwrites the
FIRST view's result (`RenderPassBlackboard::Publish()`'s own documented
"last-publish-wins" contract) — and the later per-view reader for the
FIRST view then `Fetch()`es the SECOND view's data instead. Wrong,
silent, and uncaught by any existing diagnostic. Mirror this engine's
own already-shipped `"AtmosphereViewLut"` precedent
(`kAtmosphereViewLutGameKey`/`kAtmosphereViewLutSceneKey`, `Core.cpp`):
maintain two separate `rg::RenderPassId` constants (or hash/derive a
distinct one per view name) and select between them based on
`currentView`.

**A PreOpaque feature IS tracked by `ProjectAssemblyRegistrationLedger`
and IS torn down automatically on hot reload**, exactly like
PostComposite/PreUI features above — nothing extra required from you
for this guarantee to hold.

**A registered PreOpaque feature is visible** via `GET /render_graph`'s
`render_features[]` array (`"stage": "PreOpaque"`, `"blend_mode":
"None"`) and the Editor's "Render Graph" panel, exactly like every
other wired stage — enable/disable and live priority re-ordering both
work for it too.

**A PreOpaque feature's name shares ONE GLOBAL namespace with every
PostComposite/PreUI feature's name** — not its own, separate one.
Reusing a name already claimed by a PostComposite/PreUI feature (or
vice versa) is refused, loudly, exactly like reusing a name within the
same stage already is.

**Working example** (mirrors `"ProjectAssemblyProbe.ScreenTint"`'s own
precedent above, but for PreOpaque — writes a distinct per-view marker
texture and Publish()es it under a view-qualified key):

```cpp
constexpr gte::rg::RenderPassId kMyShadowMapGameKey = "MyProject.ShadowMap.Game"_passId;
constexpr gte::rg::RenderPassId kMyShadowMapSceneKey = "MyProject.ShadowMap.Scene"_passId;

core.AddPreOpaquePass(
    "MyProject.ShadowMap",
    [&core](gte::rg::RenderGraphBuilder& builder, gte::rg::RenderPassBlackboard& blackboard,
        gte::rg::RenderViewId currentView) {
        const bool isGameView = (currentView == gte::rg::RenderViewId::Named("Game"));
        const gte::rg::RenderViewId shadowView = core.CreateRenderView(
            isGameView ? "MyProject.ShadowMap.GameView" : "MyProject.ShadowMap.SceneView",
            /*width=*/1024, /*height=*/1024, /*depthOnly=*/true);
        gte::RenderTexture* shadowTarget = core.FindRenderViewTarget(shadowView);
        if (shadowTarget == nullptr) {
            return;
        }
        const gte::rg::TextureHandle shadowHandle = builder.ImportTexture(
            isGameView ? "MyProject.ShadowMap.GameView" : "MyProject.ShadowMap.SceneView",
            shadowTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

        builder.AddRenderPass(
            "MyProject.ShadowMap.Draw", gte::rg::PassKind::Graphics, gte::rg::ViewScope::Shared,
            gte::rg::RenderPassCategory::General,
            [shadowHandle](gte::rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteDepthStencilAttachment(shadowHandle, /*clearDepth=*/1.0f);
            },
            [&core](gte::rg::PassContext& ctx) {
                // gte::DrawScene(core.GetGame().GetRenderSystem(), core.GetRegistry(),
                //     core.GetRenderer(), request) - see src/Game/SceneQuery.h.
            },
            gte::rg::RenderPassDrawKind::DrawMesh,
            gte::rg::RenderPassEvent::PreOpaques); // <- THE one legal tag, Rule 1 above.

        // Rule 2 above - the key MUST incorporate currentView.
        const gte::rg::RenderPassId key = isGameView ? kMyShadowMapGameKey : kMyShadowMapSceneKey;
        blackboard.Publish<gte::rg::TextureHandle>(key, shadowHandle);
    },
    /*priority=*/0);
```

`PostOpaque`/`PostTransparent` remain fully unwired — declaring either
one, through any entry point, is still refused loudly.

## Hot Reload

A four-campaign effort, `editor-core-separation-12` through `-15` (each own
`PHASE0_MASTER_STRATEGY.md`/`CAMPAIGN_COMPLETION_REPORT.md` under
`task_manager/`), gave this whole system a real, working hot reload -
`POST /project_assembly/hot_reload?name=<X>` genuinely recompiles and swaps
in ONE named, already-loaded Project Assembly's `<Name>_Game.dll`/
`_Editor.dll` pair, in place, while `GreatTamanaEditor.exe` keeps running -
`gte_core`/`gte_editor` themselves are NEVER recompiled or reloaded, only
the targeted project's own two `.dll`s ever are.

**What one cycle actually does, in order**: freeze the whole engine main
loop (synchronous, on the main thread, for the cycle's entire duration) ->
capture the live ECS world's full state -> back up the current `.dll` pair
-> cleanly unload it (`ProjectAssemblyHost::UnloadProjectAssembly()`,
BIG-STEP 2) -> recompile synchronously (`cmake --build`) -> on success,
load the fresh binaries; on ANY failure (a bad compile, or a rejected
in-flight-guard race), restore the backed-up binaries and reload the OLD,
still-good pair instead (LDD-HR3 - a failed reload is indistinguishable
from the button never having been pressed, other than the one proof this
section exists to make honest: the ECS state below still reflects whatever
was live immediately before the button was pressed, not a cold start) ->
restore the captured ECS world state -> unfreeze. `GET
/project_assembly/hot_reload/status`, polled from a second connection
while the first request blocks, reports live phase progress
(`CapturingState` -> `BackingUpBinaries` -> `Unloading` -> `Compiling` ->
`ReloadingNewCode`/`RollingBack` -> `RestoringState` -> `Idle`), ending
`lastOutcome`: `"Success"`, `"RolledBack"`, or (rare) `"CriticalFailure"`.

**The honest boundary, stated as plainly as this whole effort's own
external master plan states it**: everything living in the ECS `Registry`
survives a reload cycle, faithfully - every entity, every built-in
reflected component (Transform, Name, Camera, ...), AND every
Project-Assembly-defined CUSTOM reflected component type (proven live by
this system's own permanent `Projects/ProjectAssemblyProbe/` fixture's
`ProbeHotReloadMarker` component, whose runtime-MUTATED value survives both
a successful reload AND a rolled-back one). Anything a Project Assembly's
own code keeps OUTSIDE the ECS Registry - a bare C++ global, a non-ECS
manager object, GPU resources a render pass owns opaquely (like this same
probe fixture's own lazily-built `ComputePipeline`) - does NOT survive: it
is destroyed and rebuilt from scratch, exactly like a fresh process start,
on every single reload. The concrete, already-existing example: a
hypothetical `ProbeEditorPanel::m_clickCount` (a plain `int` tracking how
many times a button was clicked) resets to `0` on every reload, by
construction - there is no mechanism, and none is planned, to preserve
arbitrary non-ECS C++ state across a reload. A future, explicitly
NOT-YET-BUILT option for a Project Assembly author who needs this anyway:
an opt-in `GTE_SerializeProjectState()`/`GTE_RestoreProjectState()` export
pair the reload orchestrator would call if present - named here as real,
deliberately deferred future work, not built by this 4-campaign effort.

**Two further, explicitly out-of-scope limitations, stated honestly rather
than silently smoothed over**:
- The engine's own persistent `AssetDatabase` (refreshed once per reload
  cycle, `Core::GetAssetDatabase()`) is a SEPARATE instance from the one
  `src/Editor/Panels/ProjectPanel.h` already owns for the Project Browser
  panel, and separate again from `Editor/SceneIO.cpp`'s own throwaway
  per-call instances - this effort does not unify all three into one
  single engine-wide instance.
- A reload cycle's own restore step briefly clears and rebuilds the
  ENTIRE live world (every entity, regardless of which Project Assembly -
  if more than one is loaded - originally created it), so every entity's
  numeric `Entity` ID/handle changes across a cycle, for every
  currently-loaded project, not just the one actually being reloaded. This
  is an accepted, currently-uncommon limitation (in practice only one
  Project Assembly is ever loaded at a time), not a scoped, per-project-only
  restore - building that would need an entity-to-owning-project
  attribution system that does not exist today.

See `docs/conventions/scene-serialization.md` for the shared reconstruction
machinery (`Scene/SceneBuilder.cpp`'s `BuildSceneDocumentFromRegistry()`/
`ReconstructSceneFromDocument()`) this whole feature's own capture/restore
hook points reuse verbatim - the SAME functions `Editor::SaveScene()`/
`LoadScene()` (Ctrl+S/Ctrl+O) already use, unmodified.

Full history: `task_manager/editor-core-separation-12/PHASE0_MASTER_STRATEGY.md`
through `task_manager/editor-core-separation-15/PHASE0_MASTER_STRATEGY.md`,
and each campaign's own `CAMPAIGN_COMPLETION_REPORT.md`.

## Creating a New Project (On-Engine Project Workflow, BIG-STEP 2)

A five-phase campaign, `editor-core-separation-16`
(`task_manager/editor-core-separation-16/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), implements "BIG-STEP 2" ("Create New
Project") of a larger, five-part external "On-Engine Project Workflow"
master plan - BIG-STEP 1 (a shared `ActiveProjectAssemblyState` primitive)
shipped as part of the same campaign; BIG-STEP 3 ("Open Project"), BIG-STEP
4 ("Create Script Asset"), and BIG-STEP 5 (a real "Compile" menu action)
remain separate, later campaigns, explicitly out of scope here.

A brand-new, empty `Projects/<Name>/` folder - the exact 3-file scaffold
this file's own "Folder layout" section above describes (`Libraries/
CMakeLists.txt`, `Libraries/ProjectAssemblyExports.h` copied byte-for-byte
from `cmake/templates/ProjectAssemblyExports.h`, `Assets/<Name>Game.cpp`, a
real, immediately-compileable stub) - can now be created two ways, both
calling the exact same underlying method,
`gte::EditorProjectLifecycleCapability::CreateNewProjectAssembly()`
(`src/Editor/EditorProjectLifecycleCapability.h/.cpp`):

- Clicking **Project > New Project...** in the running Editor's menu bar
  (a new top-level menu, alongside "File"/"Window"), typing a name, and
  clicking "Create" (`src/Editor/NewProjectWindow.h/.cpp`) - an inline red
  error message keeps the window open on failure.
- `POST http://127.0.0.1:8080/project_assembly/create_project?name=<X>`
  (`src/Network/NetworkServer.cpp`'s `RegisterRoutes()`).

A name is validated by a new, shared, dependency-free validator,
`gte::IsValidProjectAssemblyIdentifierName()`
(`src/Core/Plugins/ProjectAssemblyNameValidation.h/.cpp` - matches
`^[A-Za-z_][A-Za-z0-9_]*$` and rejects Windows' reserved device names),
**before any filesystem write happens** - an empty/illegal/colliding name is
rejected with a clear, human-readable error, in both the ImGui window and
the HTTP response (`400` + a JSON error body). The exact same validator also
hardened the pre-existing `NetworkRoutes.cpp`'s `ParseProjectNameQuery()`
(previously only checked for an empty string) - every other
`/project_assembly/*` route this repo already shipped benefits from the
same stricter rule, backward-compatibly. A name colliding with an existing
folder or file under `Projects/` is rejected the same way - "already
exists" - creating or overwriting nothing.

"Create" always runs one synchronous, unconditional `cmake -S <repoRoot> -B
<buildDirectory>` reconfigure step immediately after writing the new
scaffold (`RunPlainCMakeReconfigureAndWait()`,
`src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp`) - a deliberate,
permanent simplification over branching on which of two possible real CMake
behaviors is true on a given machine (this campaign's own live verification
found BOTH mechanisms work correctly and independently on the reference
development machine - the plain `file(GLOB CONFIGURE_DEPENDS ...)`
auto-pickup this whole system's own "Folder layout" section above already
describes would, on its own, have been enough; the explicit reconfigure
step is a safe, redundant belt-and-suspenders guarantee, not the thing that
actually made the difference there - but is kept unconditional, always,
since a different machine's real CMake behavior is not something this
system gambles on). The result: the EXISTING, unmodified
`POST /project_assembly/debug/compile_only?name=<X>` route (or a future
"Compile" menu item, BIG-STEP 5) can build a brand-new project's
`<Name>_Game.dll` successfully, on the very first try, with zero manual
`cmake` reconfigure step ever required from a human.

The engine now has exactly one, single, shared, always-fresh concept of
"the currently active Project Assembly",
`gte::ActiveProjectAssemblyState` (`src/Editor/ActiveProjectAssemblyState.h/.cpp`,
a Meyers singleton) - `GetActive()` re-derives `isCompiled`
(`std::filesystem::exists()` against the resolved output `.dll` path) and
`isLoaded` (a scan of `ProjectAssemblyHost::GetLoadedAssemblyFileNames()`,
guarded by the same `GetHotReloadEngineStateMutex()` the Hot Reload feature
above already uses) fresh, on every single call - never cached. A
successful "Create" calls `SetActive()`; this is the ONE, single, new
authority the "Open Project"/"Create Script Asset"/"Compile menu" campaigns
that come after this one are expected to read and extend, never a second,
competing concept.

**Honest, permanent scope boundary, stated plainly**: this capability only
scaffolds a brand-new project and marks it active - it does NOT
auto-compile it (a human, or a later "Compile" UI/an HTTP call to the
existing `compile_only` route, still triggers the actual build), does NOT
scaffold an `Assets/Editor/` folder (Game-only by default), does NOT open
any in-engine code editor (none exists), and does NOT support renaming or
deleting a project.

Full campaign writeup:
`task_manager/editor-core-separation-16/PHASE0_MASTER_STRATEGY.md`, each
`PHASEn_COMPLETION_REPORT.md` in that same folder, and
`CAMPAIGN_COMPLETION_REPORT.md`.

## What this system does NOT do (explicit Non-Goals)

- No gameplay/"MonoBehaviour"-style scripting bridge (per-entity
  `Update()`/`Start()`, ECS component authoring, Input access).
- ~~No hot reload, ever.~~ **SUPERSEDED, 2026-09-28 onward** - this WAS a
  real, permanent non-goal of THIS campaign (`editor-core-separation-11`),
  and remained true through `editor-core-separation-14`'s own PHASE4/PHASE5
  reports right up until the `editor-core-separation-12` through `-15`
  4-campaign "Project Assembly Hot Reload" effort shipped it for real - see
  this file's own new `## Hot Reload` section below for the current, full,
  honest picture, including its own remaining boundaries/limitations.
- No cross-machine/cross-checkout portability — `Projects/` is
  `.gitignore`d, single-developer, same-build-run only.
- No separate Player executable — both `_Game.dll`/`_Editor.dll` load into
  the one existing `GreatTamanaEditor.exe` process.
- ~~No change of any kind to `plugins/gte_plugin_abi`, `PluginHost`, or any
  existing ABI-versioned interface.~~ **NO LONGER A MEANINGFUL NON-GOAL,
  `better-render-pass-2` campaign onward** - the OTHER system this bullet
  promised never to touch was itself fully, deliberately removed by that
  campaign (`task_manager/better-render-pass-2/PHASE0_MASTER_STRATEGY.md`,
  `CAMPAIGN_COMPLETION_REPORT.md`); Project Assembly is this engine's ONLY
  loadable-module system today. The strikethrough text above is the ORIGINAL,
  now-historical Non-Goal, kept for the record, not deleted.
- ~~No scaffolding/"New Project" wizard tool — a human creates
  `Projects/<Name>/{Assets,Libraries}` by hand today.~~ **SUPERSEDED,
  2026-09-28 onward** - see `task_manager/editor-core-separation-16/` (the
  "On-Engine Project Workflow" plan's BIG-STEP 2, "Create New Project") and
  this file's own new `## Creating a New Project` section below for the
  full, current, honest picture. The strikethrough text above is the
  ORIGINAL, now-historical Non-Goal, kept for the record, not deleted.
- No UI-design decision for where a permanent "Compile" button/menu item
  lives — `TriggerProjectAssemblyCompile()` is the mechanism; a future
  campaign decides the UI.
- No resolution of every possible future render-graph capability (reading
  `SceneDepth`, multiple render targets, cross-Project-Assembly-pass
  chaining, a safe way to alias an imported handle onto an already-tracked
  physical resource — this generic handle-aliasing gap remains real and
  unresolved). **On-screen Game View compositing itself is NO LONGER an open
  gap** — `editor-core-separation-23` solved it via a dedicated compositor
  path, `Core::RegisterProjectRenderFeature()`, that never needs handle
  aliasing at all; see this file's own rewritten
  `### On-screen Game View compositing` section above for the full, current
  picture.

Full campaign writeup: `task_manager/editor-core-separation-11/PHASE0_MASTER_STRATEGY.md`,
each `PHASEn_COMPLETION_REPORT.md` in that same folder, and
`CAMPAIGN_COMPLETION_REPORT.md`.
